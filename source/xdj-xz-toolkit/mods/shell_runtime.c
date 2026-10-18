/* SPDX-License-Identifier: MIT */
/* FLAGSHIP shell device glue. See shell_runtime.h for the contract and
   VJToolsLibrary docs/research/2026-10-18-xz-shell-abi.md for every address.

   Threads and locks. `mutex` serialises the shell, the model and its storage
   (setters, refresh, input, render), the action queue and the published
   library results. ui_runtime.c already holds its own UI mutex when it calls
   in, so the order is always UI mutex -> mutex. The worker and the native
   owner tick take only `mutex`, and never call native code while holding it. */
#define _GNU_SOURCE
#include "shell_runtime.h"
#include "shell_model.h"
#include "shell_library.h"
#include "shell/shell_browse.h"
#include "shell/shell_info.h"
#include "shell/shell_settings.h"
#include "shell/shell_source.h"
#include "runtime.h"
#include "audio/native_reader.h"
#include "arcade/native_clock.h"
#include "key/native_browse.h"
#include "ui/layered_wave_source.h"
#include "ui/native_skin_runtime.h"
#include "ui/themes.h"
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>

#define K(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
#define TOTAL_WK 0x2247cacu          /* stTotalWk: deck load records at +0x1610 + deck*0x60 */
#define PLAY_LIFECYCLE 0x1b31f4cu    /* st_ui_playmode_wave_data + 0x7c (mods/ui/NATIVE_LAYOUT.md) */
#define MENU_VISIBLE 0x224e608u      /* uiBrowse + 0x14, read by getBrowseMenuDispOn */
#define GRID_CAP 4096u
#define WAVE_SCROLL_CAP 32768u       /* shell_model.c keeps this many; bin before the hand-over */
#define WAVE_OVERVIEW 1024u
#define PWV7_PER_SECOND 150u         /* rekordbox detail waveform rate (inferred, as layered_wave) */
/* Reader arena per open USB. It holds the reader (two page buffers, three page
   bitmaps, 64 lookup slots) and one list build at a time: an index costs about
   24 bytes per track plus its copied names, so 3 MiB covers a 20,000-track
   USB with room for a search on top. Results are copied out and the arena is
   reset after every build; a build that still does not fit reports the list
   as unknown instead of truncating it. Two USBs cost 6 MiB of arena. */
#define LIBRARY_ARENA ((size_t)3u<<20)

struct xz_shell_proof {
 uint32_t version, size, sequence;
 uint32_t enabled, load_enabled, owns, handed_off, caution_yield, caution, screen, capabilities;
 /* The last whole second. */
 uint32_t second_frames, second_render_calls, second_stock_flips, second_skipped, second_busy, second_pump, second_ticks;
 uint32_t second_render_avg_us, second_render_max_us, second_refresh_avg_us, second_refresh_max_us;
 /* Since start. */
 uint32_t frames, render_calls, stock_flips, keys_swallowed, keys_passed, key_hand_offs, touches, encoder_events, actions;
 uint32_t hand_offs, resumes, back_hold_resumes, commands_run, commands_refused, commands_dropped, command_latency_max_ms;
 uint32_t last_command, last_command_result;
 uint32_t guards;                     /* bit per verified contract, enum guard */
 uint32_t model_guard_failures[4];    /* R3, R4, R5, R6 */
 uint32_t wave_loads, wave_failures, grid_loads, library_tracks[2], library_errors, list_builds, list_errors, list_build_ms;
};
__attribute__((visibility("default"),used)) struct xz_shell_proof xz_mods_shell_proof_v1={.version=1,.size=sizeof(struct xz_shell_proof)};
enum guard { G_TIME_MODE, G_QUANTIZE, G_MENU, G_LOAD, G_PLAY_FLAG, G_MENU_VISIBLE, G_CLOCK, G_SETTINGS };

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static int worker_created, running, enabled, load_enabled, debug;
static unsigned guards;
static int owns_flag, dirty_flag;
static uint32_t last_drawn_ms, period_ms=100;
static struct xz_shell shell;
static struct xz_shell_model model;
static struct xz_shell_model_storage *storage;
static int refreshed;
static struct xz_shrt_handoff handoff;
static struct xz_shrt_contacts contacts;
static struct xz_shrt_touch touch_state;
static struct xz_shrt_push push_state;
static struct xz_shrt_queue queue;
static struct xz_shell_runtime_frame_input last_input;
static uint32_t last_call_ms, last_refresh_ms, last_pump_ms;
static struct { uint32_t start, frames, calls, flips, skipped, busy, pump, ticks, render_max, refresh_max, refreshes; uint64_t render_us, refresh_us; } sec;

/* ---- clocks and reads ---- */
static uint64_t now_us(void) { struct timeval t;gettimeofday(&t,NULL);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_usec; }
static uint32_t now_ms(void) { return (uint32_t)(now_us()/1000u); }
static int model_read(uintptr_t a, void *out, size_t n) { return a>UINT32_MAX?-1:xz_read_memory((uint32_t)a,out,n); }
static int plain_read(void *c, uint32_t a, void *out, size_t n) { (void)c;return xz_read_memory(a,out,n); }
static int word(uint32_t a, uint32_t *v) { return !xz_read_memory(a,v,4); }
static int pointer(uint32_t p, size_t n) { return p>=0x10000u&&!(p&3u)&&(uint64_t)p+n<=UINT32_MAX; }
static int code_is(uint32_t a, const unsigned char *expected, size_t n) {
 unsigned char b[16];return n<=16&&!xz_read_memory(a,b,n)&&!memcmp(b,expected,n);
}
/* Prologues from docs/research/2026-10-18-xz-shell-abi/q7_prologues.py on the verified rbp. */
static const unsigned char set_time_mode_code[16]={0x01,0x20,0xa0,0xe1,0x30,0x10,0x9f,0xe5,0x00,0x00,0x50,0xe3,0x00,0x10,0x91,0xe5};
static const unsigned char set_quantize_code[16]={0x1c,0x30,0x9f,0xe5,0x00,0x10,0xa0,0xe1,0x00,0x30,0x93,0xe5,0x00,0x00,0x53,0xe3};
static const unsigned char menu_code[16]={0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1,0xa4,0xe8,0xff,0xeb,0x00,0x40,0xa0,0xe1};
static const unsigned char load_code[16]={0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1,0x48,0xd0,0x4d,0xe2,0x01,0x00,0xa0,0xe1};
static const unsigned char music_id_code[8]={0x04,0x30,0x90,0xe5,0x00,0x00,0x53,0xe3};
static const unsigned char menu_getter_code[16]={0xf4,0x35,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x14,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
static const unsigned char settings_code[16]={0x08,0x31,0x9f,0xe5,0x01,0x00,0x51,0xe3,0xf0,0x05,0x2d,0xe9,0x41,0x12,0xa0,0x13};
static void check_guards(void) {
 unsigned g=0;uint32_t literal;
 if(code_is(0xca308,set_time_mode_code,16))g|=1u<<G_TIME_MODE;
 if(code_is(0xc9304,set_quantize_code,16))g|=1u<<G_QUANTIZE;
 if(code_is(0xdf1e8,menu_code,16))g|=1u<<G_MENU;
 if(code_is(0x135728,load_code,16)&&code_is(0x13ba38,music_id_code,8))g|=1u<<G_LOAD;
 /* ui_PLAYMODE_Clear's literal pool names the play-mode object whose +0x7c is the lifecycle flag. */
 if(word(0x1fdc98,&literal)&&literal==PLAY_LIFECYCLE-0x7c)g|=1u<<G_PLAY_FLAG;
 if(code_is(0xd94a8,menu_getter_code,16))g|=1u<<G_MENU_VISIBLE;
 if(xz_arcade_native_clock_valid(plain_read,NULL))g|=1u<<G_CLOCK;
 if(code_is(0x2508dc,settings_code,16))g|=1u<<G_SETTINGS;
 guards=g;xz_mods_shell_proof_v1.guards=g;
}
static int has(enum guard g) { return (guards>>g)&1u; }

struct loaded { int known; uint32_t state, id; uint8_t device_kind; };
static struct loaded loaded[XZ_SHELL_DECKS];
/* The deck's load record: state at +0, music_id at +8 {node, device kind, browse kind, 0, id}. */
static struct loaded read_loaded(int deck) {
 struct loaded l={0,0,0,0};unsigned char a[16],b[16];
 uint32_t at=TOTAL_WK+0x1610u+(uint32_t)deck*0x60u;
 if(xz_read_memory(at,a,16)||xz_read_memory(at,b,16)||memcmp(a,b,16))return l;
 memcpy(&l.state,a,4);memcpy(&l.id,a+12,4);l.device_kind=a[9];l.known=1;
 return l;
}
static int settled(const struct loaded *l) { return l->known&&l->state!=7&&l->state!=9&&l->id&&l->id!=UINT32_MAX; }
/* Stock is on its play screen: lifecycle flag set, browse mode 1 and no menu popup. -1 when unreadable. */
static int stock_play(void) {
 uint32_t flag,menu;
 if(!has(G_PLAY_FLAG)||!word(PLAY_LIFECYCLE,&flag))return -1;
 if(!flag)return 0;
 unsigned mode=xz_native_browse_mode();
 if(mode&&mode!=1)return 0;
 if(has(G_MENU_VISIBLE)&&word(MENU_VISIBLE,&menu)&&menu)return 0;
 return 1;
}
/* R4 walk for one id (UiGetUtil* wrappers are PC-relative and are not called). */
static int setting_value(uint32_t id, uint32_t *value) {
 uint32_t settings,list,count,entry,v,again;
 if(!has(G_SETTINGS)||!word(0x1c8a404,&settings)||!pointer(settings,0x170)||
    !word(settings+0x164,&list)||!word(settings+0x16c,&count)||count>512||!pointer(list,(size_t)count*0x8c))return 0;
 int found=0;
 for(uint32_t i=0;i<count;i++){
  if(!word(list+i*0x8c,&entry))return 0;
  if(entry!=id)continue;
  if(found||!word(list+i*0x8c+0x88,&v)||!word(list+i*0x8c+0x88,&again)||v!=again)return 0;
  found=1;*value=v;
 }
 return found;
}

/* ---- libraries ---- */
enum { USB_NONE, USB_LOADING, USB_READY, USB_ERROR };
static struct { struct xz_shlib *lib; int status, space_known; uint64_t free_bytes, total_bytes; uint32_t tracks; } usb[2];
static int browse_source=-1;
enum { L_BROWSE, L_PLAYLIST, L_SEARCH, L_COUNT };
static struct list_slot {
 struct xz_shlib_request req;
 uint32_t wanted, built;
 int source, requested;
 struct xz_shlib_list *result;
 int32_t window_start;
} lists[L_COUNT];
static const enum xz_shell_screen list_screen[L_COUNT]={XZ_SHELL_BROWSE,XZ_SHELL_PLAYLIST,XZ_SHELL_SEARCH};
static struct xz_shell_browse_row rows[XZ_SHELL_BROWSE_WINDOW];
static struct xz_shell_browse_model browse;
static struct xz_shell_source_model source_model;
static struct xz_shell_info_model info_model;
static struct xz_shell_setting_item setting_items[3];
static struct xz_shell_settings_model settings_model;
static const char *const time_choices[2]={"REMAIN","TIME"};
static const char *const off_on[2]={"OFF","ON"};
/* Freed after the lock is released, once the model no longer points into them. */
static struct { struct xz_shlib *lib[4]; struct xz_shlib_list *list[8]; unsigned libs, lists; } retired;
static void retire_list(struct xz_shlib_list *l) {
 if(!l)return;
 if(retired.lists<8)retired.list[retired.lists++]=l;
 else xz_shlib_list_free(l); /* unreachable in practice: callers drain after every swap */
}
static void drain_retired(void) {
 struct xz_shlib *lib[4];struct xz_shlib_list *list[8];unsigned nl,nk;
 pthread_mutex_lock(&mutex);
 nl=retired.libs;nk=retired.lists;memcpy(lib,retired.lib,sizeof lib);memcpy(list,retired.list,sizeof list);
 retired.libs=retired.lists=0;
 pthread_mutex_unlock(&mutex);
 for(unsigned i=0;i<nk;i++)xz_shlib_list_free(list[i]);
 for(unsigned i=0;i<nl;i++)xz_shlib_free(lib[i]);
}
static int list_index(enum xz_shell_screen s) { return s==XZ_SHELL_BROWSE?L_BROWSE:s==XZ_SHELL_PLAYLIST?L_PLAYLIST:s==XZ_SHELL_SEARCH?L_SEARCH:-1; }
/* The list the browse slot describes: the topmost list on the shell's stack, else BROWSE. */
static int shown_list(int *view) {
 for(int i=shell.depth-1;i>=0;i--){int k=list_index(shell.stack[i].screen);if(k>=0){if(view)*view=i;return k;}}
 if(view)*view=-1;
 return L_BROWSE;
}
static void list_reset(struct list_slot *ls, int k) {
 retire_list(ls->result);
 memset(ls,0,sizeof *ls);
 ls->req.screen=list_screen[k];
 ls->req.category=k==L_PLAYLIST?XZ_SHELL_CAT_PLAYLIST:XZ_SHELL_CAT_TRACK;
 ls->source=-1;
}
static void list_rebuild(struct list_slot *ls) { ls->wanted++;ls->window_start=0;pthread_cond_signal(&wake); }
static void choose_source(void) {
 if(browse_source>=0&&usb[browse_source].lib)return;
 browse_source=usb[0].lib?0:usb[1].lib?1:-1;
}

/* ---- model slots ---- */
#define BF(f) (1u<<XZ_SHELL_BF_##f)
static void fill_browse(struct xz_shlib *lib) {
 int view,k=shown_list(&view);
 struct list_slot *ls=&lists[k];
 if(ls->source!=browse_source){
  /* Another USB: keep the category and query, start at its top level. */
  retire_list(ls->result);ls->result=NULL;ls->source=browse_source;ls->req.depth=0;ls->requested=0;
 }
 if(!ls->requested){ls->requested=1;list_rebuild(ls);}
 memset(&browse,0,sizeof browse);
 browse.screen=list_screen[k];
 browse.source_name=browse_source?"USB2":"USB1";
 browse.known=BF(SOURCE)|BF(CATEGORY);
 browse.category_known=(1u<<XZ_SHELL_CAT_COUNT)-1u;
 browse.category_available=xz_shlib_categories();
 browse.category=ls->req.category;
 const struct xz_shlib_list *l=ls->built==ls->wanted?ls->result:NULL;
 if(!l)return; /* still building: rows draw blank under the right title */
 browse.known|=BF(LEVEL)|BF(LIST)|BF(TOTAL)|BF(COLUMNS)|BF(SORT);
 browse.category=l->category;
 browse.level_name=l->names+l->level_name;browse.depth=l->depth;
 if(l->has_parent){browse.parent_name=l->names+l->parent_name;browse.known|=BF(PARENT);}
 browse.list_id=l->list_id;browse.total=l->total;
 if(l->folders){browse.columns[0]=XZ_SHELL_COL_TITLE;browse.column_count=1;}
 else{
  static const enum xz_shell_browse_column track_columns[4]={XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_TITLE,XZ_SHELL_COL_BPM,XZ_SHELL_COL_KEY};
  memcpy(browse.columns,track_columns,sizeof track_columns);browse.column_count=4;
 }
 browse.sort_column=(enum xz_shell_browse_column)l->sort_column;browse.sort_descending=l->sort_descending;
 if(l->letter_count){browse.letters=l->letters;browse.letter_count=l->letter_count;}
 int32_t start=ls->window_start;
 if(start>l->total-XZ_SHELL_BROWSE_WINDOW)start=l->total-XZ_SHELL_BROWSE_WINDOW;
 if(start<0)start=0;
 uint32_t on_deck[2]={0,0};unsigned on_known=0;
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(loaded[d].known){
  on_known|=1u<<d;
  if(settled(&loaded[d])&&loaded[d].device_kind==2+browse_source)on_deck[d]=loaded[d].id;
 }
 browse.window_start=start;
 browse.window_count=xz_shlib_window(lib,l,start,on_deck,on_known,rows);
 browse.rows=rows;
}
static void fill_info(void) {
 memset(&info_model,0,sizeof info_model);
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  struct xz_shell_info_track *t=&info_model.deck[d];
  if(!loaded[d].known)continue;
  if(!settled(&loaded[d])){
   /* Empty (id 0) is known; a load in progress is not. */
   if(loaded[d].state!=7&&loaded[d].state!=9&&!loaded[d].id){t->known=XZ_SHELL_KNOWN(XZ_SHELL_I_LOADED);}
   continue;
  }
  int slot=loaded[d].device_kind==2?0:loaded[d].device_kind==3?1:-1;
  if(slot>=0&&usb[slot].lib&&xz_shlib_info(usb[slot].lib,loaded[d].id,slot?XZ_SHELL_SRC_USB2:XZ_SHELL_SRC_USB1,t)){
   t->loaded=1;t->known|=XZ_SHELL_KNOWN(XZ_SHELL_I_LOADED);
   if(!(model.capabilities&XZ_SHELL_CAP_BROWSE))t->links=0;
  }
 }
 /* The highlighted row of the list under INFO. */
 int view,k=shown_list(&view);
 const struct list_slot *ls=&lists[k];
 const struct xz_shlib_list *l=ls->built==ls->wanted?ls->result:NULL;
 if(view>=0&&l&&browse_source>=0&&usb[browse_source].lib){
  int cursor=shell.stack[view].cursor;
  if(cursor>=0&&cursor<l->total&&l->kind[cursor]==XZ_SHELL_ROW_TRACK&&
     xz_shlib_info(usb[browse_source].lib,l->id[cursor],browse_source?XZ_SHELL_SRC_USB2:XZ_SHELL_SRC_USB1,&info_model.highlight))
   info_model.has_highlight=1;
 }
}
static void fill_source(void) {
 memset(&source_model,0,sizeof source_model);
 for(int i=0;i<2;i++){
  struct xz_shell_device *d=&source_model.device[i];
  d->known=1u<<XZ_SHELL_D_STATUS;
  d->status=usb[i].lib?XZ_SHELL_DEVICE_READY:usb[i].status==USB_LOADING?XZ_SHELL_DEVICE_LOADING:XZ_SHELL_DEVICE_NOT_CONNECTED;
  if(usb[i].lib){d->tracks=(int32_t)usb[i].tracks;d->known|=1u<<XZ_SHELL_D_TRACKS;}
  if(usb[i].space_known){d->free_bytes=usb[i].free_bytes;d->total_bytes=usb[i].total_bytes;d->known|=(1u<<XZ_SHELL_D_FREE)|(1u<<XZ_SHELL_D_TOTAL);}
 }
 /* LINK and COMPUTER are not read: their tiles stay unknown. */
 if(browse_source>=0){source_model.current_known=1;source_model.current=browse_source?XZ_SHELL_SRC_USB2:XZ_SHELL_SRC_USB1;}
}
static void fill_settings(void) {
 const struct xz_shell_settings *s=&model.settings;
 const struct xz_shell_deck *d=&model.deck[0];
 int quantize=d->known&K(QUANTIZE)?xz_shrt_quantize_index(d->quantize_den):-1;
 setting_items[0]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"TIME MODE",time_choices,2,
  s->time_mode==XZ_SHELL_TIME_ELAPSED,(s->known>>XZ_SHELL_SET_TIME_MODE)&1,has(G_TIME_MODE)};
 setting_items[1]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_SETTING_CAT_DJ_SETTING,"QUANTIZE BEAT VALUE",xz_shrt_quantize_choices,4,
  quantize<0?0:quantize,quantize>=0,has(G_QUANTIZE)};
 setting_items[2]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_AUTO_CUE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"AUTO CUE",off_on,2,
  !!s->auto_cue,(s->known>>XZ_SHELL_SET_AUTO_CUE)&1,0};
 settings_model=(struct xz_shell_settings_model){3,setting_items};
}
/* Rebuilds every slot the runtime owns. Called after each refresh and after any swap, so the model
   never points at retired memory. */
static void fill_slots_locked(void) {
 choose_source();
 struct xz_shlib *lib=browse_source>=0?usb[browse_source].lib:NULL;
 uint32_t caps=XZ_SHELL_CAP_SOURCE;
 if(has(G_TIME_MODE)||has(G_QUANTIZE))caps|=XZ_SHELL_CAP_SET_SETTING;
 if(lib)caps|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH;
 if(lib&&load_enabled&&has(G_LOAD)&&has(G_SETTINGS))caps|=XZ_SHELL_CAP_LOAD;
 model.capabilities=caps;
 if(lib){fill_browse(lib);model.browse=&browse;}else model.browse=NULL;
 fill_source();model.source=&source_model;
 fill_settings();model.settings_screen=&settings_model;
 fill_info();model.info=&info_model;
 __atomic_store_n(&xz_mods_shell_proof_v1.capabilities,caps,__ATOMIC_RELAXED);
}

/* ---- waves (worker -> model thread) ---- */
static struct { uint32_t wanted, done, retry_at, attempts; } wave_job[XZ_SHELL_DECKS];
static struct { uint32_t gen; struct xz_shell_column *scroll, *overview; uint32_t scroll_n, overview_n, span; struct xz_shell_beat *grid; uint32_t grid_n; } wave_ready[XZ_SHELL_DECKS];
static struct xz_shell_beat grid_cache[XZ_SHELL_DECKS][GRID_CAP];
static uint32_t grid_count[XZ_SHELL_DECKS];
static char wave_path[XZ_SHELL_DECKS][XZ126_READER_PATH_MAX]; /* worker only */
static void free_wave(int d) {
 free(wave_ready[d].scroll);free(wave_ready[d].overview);free(wave_ready[d].grid);
 memset(&wave_ready[d],0,sizeof wave_ready[d]);
}
static void apply_waves_locked(void) {
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  if(!wave_ready[d].gen)continue;
  if(wave_ready[d].gen==wave_job[d].wanted){
   xz_shell_model_set_wave(storage,d,wave_ready[d].scroll,wave_ready[d].scroll_n,wave_ready[d].span,
                           wave_ready[d].overview,wave_ready[d].overview_n,wave_ready[d].span);
   grid_count[d]=0;
   if(wave_ready[d].grid&&wave_ready[d].grid_n<=GRID_CAP){
    memcpy(grid_cache[d],wave_ready[d].grid,wave_ready[d].grid_n*sizeof *wave_ready[d].grid);grid_count[d]=wave_ready[d].grid_n;
   }
  }
  free_wave(d);
 }
}
/* Peak of each band over each bin, after scaling the 99.5th percentile of the
   loudest band to full height (the same percentile layered_wave uses). */
static struct xz_shell_column *bin_columns(const uint8_t *bands, uint32_t count, uint32_t bins, uint32_t peak, uint32_t *out_n) {
 uint32_t n=count<bins?count:bins;
 struct xz_shell_column *out=malloc((size_t)(n?n:1)*sizeof *out);
 if(!out)return NULL;
 for(uint32_t i=0;i<n;i++){
  uint32_t first=(uint32_t)((uint64_t)i*count/n),end=(uint32_t)((uint64_t)(i+1)*count/n);
  unsigned v[3]={0,0,0};
  for(uint32_t j=first;j<end;j++)for(int b=0;b<3;b++)if(bands[j*3+b]>v[b])v[b]=bands[j*3+b];
  unsigned s[3];
  for(int b=0;b<3;b++){s[b]=v[b]*255u/peak;if(s[b]>255)s[b]=255;}
  out[i]=(struct xz_shell_column){(uint8_t)s[0],(uint8_t)s[1],(uint8_t)s[2]};
 }
 *out_n=n;return out;
}
static uint32_t band_peak(const uint8_t *bands, uint32_t count) {
 uint32_t histogram[256]={0};
 for(uint32_t i=0;i<count;i++){unsigned m=bands[i*3];if(bands[i*3+1]>m)m=bands[i*3+1];if(bands[i*3+2]>m)m=bands[i*3+2];histogram[m]++;}
 uint64_t target=((uint64_t)count*995+999)/1000,seen=0;
 for(unsigned v=0;v<256;v++){seen+=histogram[v];if(seen>=target)return v?v:1;}
 return 255;
}
static int deck_source(unsigned deck, struct xz126_deck_source *s) {
 uint32_t engine;
 return word(XZ126_PLAY_ENGINE_CELL,&engine)&&!xz126_deck_source_read(plain_read,NULL,engine,deck,s);
}
/* The native beat grid of the loaded track, as native_clock.c reaches it: 8-byte
   records {u16 beat in bar, u16 bpm x100, u32 ms}, plus the grid offset at +0x28.
   Beat time = ms + offset (inferred from native_clock.c's phase formula). */
static int read_grid(unsigned deck, struct xz_shell_beat **out, uint32_t *count) {
 uint32_t engine,range[2],player,reader,impl,info,again;unsigned char grid[0x3c];
 *out=NULL;*count=0;
 if(!has(G_CLOCK)||!word(XZ126_PLAY_ENGINE_CELL,&engine)||!pointer(engine,20)||xz_read_memory(engine+12,range,8)||
    range[1]<range[0]||range[1]-range[0]>16||deck>=(range[1]-range[0])/4||!word(range[0]+deck*4,&player)||
    !pointer(player,0xcc)||!word(player+0xc8,&reader)||!pointer(reader,8)||!word(reader+4,&impl)||
    !pointer(impl,0x6a4)||!word(impl+0x6a0,&info)||!pointer(info,sizeof grid)||xz_read_memory(info,grid,sizeof grid))return 0;
 uint32_t present,begin,end,offset_raw;
 memcpy(&present,grid+0x24,4);memcpy(&offset_raw,grid+0x28,4);memcpy(&begin,grid+0x34,4);memcpy(&end,grid+0x38,4);
 int32_t offset=(int32_t)offset_raw;
 if(!present||end<=begin||(end-begin)%8||(end-begin)/8>GRID_CAP||offset<-3600000||offset>3600000||!pointer(begin,end-begin))return 0;
 uint32_t n=(end-begin)/8;
 unsigned char *raw=malloc((size_t)n*8);struct xz_shell_beat *beats=malloc((size_t)n*sizeof *beats);
 if(!raw||!beats||xz_read_memory(begin,raw,(size_t)n*8)){free(raw);free(beats);return 0;}
 int ok=1;
 for(uint32_t i=0;ok&&i<n;i++){
  uint16_t bar;uint32_t ms;memcpy(&bar,raw+i*8,2);memcpy(&ms,raw+i*8+4,4);
  int64_t at=(int64_t)ms+offset;
  if(bar<1||bar>4||at<0||at>INT32_MAX||(i&&at<beats[i-1].ms))ok=0;
  else beats[i]=(struct xz_shell_beat){(int32_t)at,(uint8_t)bar};
 }
 free(raw);
 if(!ok||!word(impl+0x6a0,&again)||again!=info){free(beats);return 0;}
 *out=beats;*count=n;return 1;
}
/* Worker. Returns 1 when the job finished (loaded or given up), 0 to retry later. */
static int load_wave(int d, uint32_t gen, uint32_t requested_at) {
 struct xz126_deck_source s,after;
 uint32_t now=now_ms();
 if(!deck_source((unsigned)d,&s)||s.path[0]!='/')return now-requested_at>10000u;
 /* The reader can still hold the previous file for a moment after the model sees the new track. */
 if(!strcmp(s.path,wave_path[d])&&now-requested_at<5000u)return 0;
 struct xz_wave_load w;
 if(xz_wave_load_three_band(s.path,&w)){__atomic_add_fetch(&xz_mods_shell_proof_v1.wave_failures,1,__ATOMIC_RELAXED);return 0;}
 uint32_t peak=band_peak(w.bands,w.count),scroll_n=0,overview_n=0,grid_n=0;
 struct xz_shell_column *scroll=bin_columns(w.bands,w.count,WAVE_SCROLL_CAP,peak,&scroll_n);
 struct xz_shell_column *overview=bin_columns(w.bands,w.count,WAVE_OVERVIEW,peak,&overview_n);
 uint32_t span=(uint32_t)((uint64_t)w.count*1000u/PWV7_PER_SECOND);
 xz_wave_load_free(&w);
 struct xz_shell_beat *grid=NULL;
 if(read_grid((unsigned)d,&grid,&grid_n))__atomic_add_fetch(&xz_mods_shell_proof_v1.grid_loads,1,__ATOMIC_RELAXED);
 int current=deck_source((unsigned)d,&after)&&!strcmp(after.path,s.path);
 if(!scroll||!overview||!current){free(scroll);free(overview);free(grid);return !current?0:1;}
 memcpy(wave_path[d],s.path,sizeof wave_path[d]);
 pthread_mutex_lock(&mutex);
 if(gen==wave_job[d].wanted){
  free_wave(d);
  wave_ready[d].gen=gen;wave_ready[d].scroll=scroll;wave_ready[d].overview=overview;
  wave_ready[d].scroll_n=scroll_n;wave_ready[d].overview_n=overview_n;wave_ready[d].span=span;
  wave_ready[d].grid=grid;wave_ready[d].grid_n=grid_n;
  scroll=overview=NULL;grid=NULL;dirty_flag=1;
 }
 pthread_mutex_unlock(&mutex);
 free(scroll);free(overview);free(grid);
 __atomic_add_fetch(&xz_mods_shell_proof_v1.wave_loads,1,__ATOMIC_RELAXED);
 if(debug){char line[200];snprintf(line,sizeof line,"SHELL_WAVE: deck=%d columns=%u span_ms=%u grid=%u",d+1,scroll_n,span,grid_n);xz_log(line);}
 return 1;
}

/* ---- USB scan (worker) ---- */
static int safe_file(const char *path, struct stat *out) { return !lstat(path,out)&&S_ISREG(out->st_mode); }
/* Same search as mods/key/browser.c: the mount root, then one folder below it. */
static int find_database(unsigned slot, char out[1024], struct stat *st) {
 char root[32];snprintf(root,sizeof root,"/media/usb%u",slot+1);
 snprintf(out,1024,"%s/PIONEER/rekordbox/export.pdb",root);
 if(safe_file(out,st))return 1;
 DIR *dir=opendir(root);if(!dir)return 0;
 struct dirent *e;int found=0;
 while(!found&&(e=readdir(dir))){
  if(e->d_name[0]=='.')continue;
  char folder[1024];struct stat fs;int n=snprintf(folder,sizeof folder,"%s/%s",root,e->d_name);
  if(n<0||(size_t)n>=sizeof folder||lstat(folder,&fs)||!S_ISDIR(fs.st_mode))continue;
  n=snprintf(out,1024,"%s/PIONEER/rekordbox/export.pdb",folder);
  if(n>0&&n<1024&&safe_file(out,st))found=1;
 }
 closedir(dir);return found;
}
static int same_file(const struct stat *a, const struct stat *b) { return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtime==b->st_mtime; }
static void scan_usb(void) {
 static char seen_path[2][1024];static struct stat seen[2];
 for(unsigned slot=0;slot<2;slot++){
  char path[1024]={0};struct stat st;memset(&st,0,sizeof st);
  int found=find_database(slot,path,&st);
  if(found&&!strcmp(path,seen_path[slot])&&same_file(&st,&seen[slot]))continue;
  if(!found&&!seen_path[slot][0])continue;
  struct xz_shlib *next=NULL;uint64_t free_bytes=0,total_bytes=0;int space=0;
  if(found){
   pthread_mutex_lock(&mutex);usb[slot].status=USB_LOADING;dirty_flag=1;pthread_mutex_unlock(&mutex);
   struct stat after;
   if(xz_shlib_open(path,LIBRARY_ARENA,&next)||!safe_file(path,&after)||!same_file(&st,&after)){
    xz_shlib_free(next);next=NULL;__atomic_add_fetch(&xz_mods_shell_proof_v1.library_errors,1,__ATOMIC_RELAXED);
   }
   char root[1024];snprintf(root,sizeof root,"%s",path);
   char *cut=strstr(root,"/PIONEER/rekordbox/export.pdb");if(cut)*cut=0;
   struct statvfs vfs;
   if(!statvfs(root,&vfs)){free_bytes=(uint64_t)vfs.f_bavail*vfs.f_frsize;total_bytes=(uint64_t)vfs.f_blocks*vfs.f_frsize;space=1;}
  }
  pthread_mutex_lock(&mutex);
  if(usb[slot].lib&&retired.libs<4)retired.lib[retired.libs++]=usb[slot].lib;
  usb[slot].lib=next;usb[slot].status=next?USB_READY:found?USB_ERROR:USB_NONE;
  usb[slot].tracks=xz_shlib_track_count(next);usb[slot].space_known=space;
  usb[slot].free_bytes=free_bytes;usb[slot].total_bytes=total_bytes;
  xz_mods_shell_proof_v1.library_tracks[slot]=usb[slot].tracks;
  for(int k=0;k<L_COUNT;k++)if(lists[k].source==(int)slot){retire_list(lists[k].result);lists[k].result=NULL;lists[k].requested=0;}
  if(browse_source==(int)slot&&!next)browse_source=-1;
  if(refreshed)fill_slots_locked();
  dirty_flag=1;
  pthread_mutex_unlock(&mutex);
  drain_retired();
  /* A failed open is retried when the file changes, not every second. */
  snprintf(seen_path[slot],sizeof seen_path[slot],"%s",found?path:"");seen[slot]=st;
  if(debug){char line[1200];snprintf(line,sizeof line,"SHELL_LIBRARY: usb%u %s tracks=%u",slot+1,next?path:"none",usb[slot].tracks);xz_log(line);}
 }
}

/* ---- worker ---- */
static void *worker(void *unused) {
 (void)unused;
 uint32_t last_scan=0;int first=1;
 pthread_mutex_lock(&mutex);
 while(running){
  uint32_t now=now_ms();
  if(first||now-last_scan>=1000u){
   pthread_mutex_unlock(&mutex);scan_usb();pthread_mutex_lock(&mutex);
   last_scan=now;first=0;continue;
  }
  int worked=0;
  for(int k=0;k<L_COUNT&&!worked;k++){
   struct list_slot *ls=&lists[k];
   if(!ls->requested||ls->built==ls->wanted||ls->source<0||!usb[ls->source].lib)continue;
   struct xz_shlib_request req=ls->req;uint32_t gen=ls->wanted;struct xz_shlib *lib=usb[ls->source].lib;
   pthread_mutex_unlock(&mutex);
   /* Only this thread frees libraries, so lib stays valid without the lock. */
   uint32_t began=now_ms();struct xz_shlib_list *result=NULL;
   int rc=xz_shlib_build(lib,&req,&result);
   uint32_t took=now_ms()-began;
   pthread_mutex_lock(&mutex);
   xz_mods_shell_proof_v1.list_build_ms=took;
   if(rc)xz_mods_shell_proof_v1.list_errors++;else xz_mods_shell_proof_v1.list_builds++;
   if(gen==ls->wanted&&usb[ls->source].lib==lib){
    retire_list(ls->result);ls->result=result;ls->built=gen;result=NULL;
    if(refreshed)fill_slots_locked();
    dirty_flag=1;
   }
   retire_list(result);
   pthread_mutex_unlock(&mutex);drain_retired();pthread_mutex_lock(&mutex);
   worked=1;
  }
  for(int d=0;d<XZ_SHELL_DECKS&&!worked;d++){
   if(wave_job[d].done==wave_job[d].wanted||now<wave_job[d].retry_at)continue;
   uint32_t gen=wave_job[d].wanted;
   static uint32_t requested_at[XZ_SHELL_DECKS],requested_gen[XZ_SHELL_DECKS];
   if(requested_gen[d]!=gen){requested_gen[d]=gen;requested_at[d]=now;wave_job[d].attempts=0;}
   pthread_mutex_unlock(&mutex);
   int finished=load_wave(d,gen,requested_at[d]);
   pthread_mutex_lock(&mutex);
   /* 30 tries at 500 ms outlast the 5 s reader-rebind wait in load_wave. */
   if(finished||++wave_job[d].attempts>=30){if(wave_job[d].wanted==gen)wave_job[d].done=gen;}
   else wave_job[d].retry_at=now_ms()+500u;
   worked=1;
  }
  if(!worked){
   struct timeval tv;gettimeofday(&tv,NULL);
   long ns=tv.tv_usec*1000L+250000000L;
   struct timespec until={tv.tv_sec+ns/1000000000L,ns%1000000000L};
   pthread_cond_timedwait(&wake,&mutex,&until);
  }
 }
 pthread_mutex_unlock(&mutex);
 return NULL;
}

/* ---- ownership, proof ---- */
static int candidate(int eligible, int theme) { return enabled&&eligible&&theme==XZ_THEME_FLAGSHIP; }
static int owns_now(int eligible, int theme) { return candidate(eligible,theme)&&refreshed&&!xz_shrt_handoff_yields(&handoff); }
static void set_owns(int owns) {
 int was=__atomic_exchange_n(&owns_flag,owns,__ATOMIC_ACQ_REL);
 /* Stock only redraws its dirty regions; make it repaint the whole screen it gets back. */
 if(was&&!owns)xz_native_skin_request_repaint();
 if(!was&&owns)dirty_flag=1;
 xz_mods_shell_proof_v1.owns=(uint32_t)owns;
}
static void roll_second(uint32_t now) {
 if(!sec.start){sec.start=now;return;}
 if(now-sec.start<1000u)return;
 struct xz_shell_proof *p=&xz_mods_shell_proof_v1;
 p->second_frames=sec.frames;p->second_render_calls=sec.calls;p->second_stock_flips=sec.flips;p->second_skipped=sec.skipped;
 p->second_busy=sec.busy;p->second_pump=sec.pump;p->second_ticks=sec.ticks;
 p->second_render_avg_us=sec.frames?(uint32_t)(sec.render_us/sec.frames):0;p->second_render_max_us=sec.render_max;
 p->second_refresh_avg_us=sec.refreshes?(uint32_t)(sec.refresh_us/sec.refreshes):0;p->second_refresh_max_us=sec.refresh_max;
 p->handed_off=(uint32_t)handoff.handed;p->caution_yield=(uint32_t)handoff.caution;p->screen=(uint32_t)xz_shell_screen(&shell);
 p->hand_offs=handoff.count;p->resumes=handoff.resumes;p->commands_dropped=queue.dropped;
 if(storage)xz_shell_model_diagnostics(storage,p->model_guard_failures);
 __atomic_add_fetch(&p->sequence,1,__ATOMIC_RELEASE);
 if(debug){
  char line[320];
  snprintf(line,sizeof line,"SHELL: frames=%u calls=%u stock_flips=%u skipped=%u busy=%u render_avg_us=%u render_max_us=%u "
   "refresh_avg_us=%u refresh_max_us=%u pump=%u ticks=%u owns=%u handed=%u caution=%u screen=%s caps=0x%x",
   sec.frames,sec.calls,sec.flips,sec.skipped,sec.busy,p->second_render_avg_us,sec.render_max,p->second_refresh_avg_us,sec.refresh_max,
   sec.pump,sec.ticks,(unsigned)owns_flag,(unsigned)handoff.handed,p->caution,xz_shell_screen_name(xz_shell_screen(&shell)),model.capabilities);
  xz_log(line);
 }
 memset(&sec,0,sizeof sec);sec.start=now;
}

/* ---- actions ---- */
static void queue_command(const struct xz_shrt_command *c) {
 if(!xz_shrt_queue_push(&queue,c))xz_mods_shell_proof_v1.commands_dropped=queue.dropped;
}
static void refuse(void) { xz_mods_shell_proof_v1.commands_refused++; }
static void hand_off_locked(uint32_t now) {
 xz_shrt_handoff_begin(&handoff,now);set_owns(0);
 xz_mods_shell_proof_v1.handed_off=1;
}
static void browse_action(int32_t value, int op) {
 int k=list_index(xz_shell_screen(&shell));
 if(k<0)k=shown_list(NULL);
 struct list_slot *ls=&lists[k];
 struct xz_shlib_request *r=&ls->req;
 switch(op){
 case XZ_SHELL_BROWSE_REQUEST_WINDOW:ls->window_start=value<0?0:value;return;
 case XZ_SHELL_BROWSE_OPEN:if(r->depth>=XZ_SHLIB_PATH_DEPTH)return;r->path[r->depth++]=(uint32_t)value;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_UP:if(r->depth>0)r->depth--;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_TOP:r->depth=0;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_CATEGORY:
  if(value<0||value>=XZ_SHELL_CAT_COUNT||!((xz_shlib_categories()>>value)&1u)){refuse();return;}
  r->category=(enum xz_shell_browse_category)value;r->depth=0;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_SORT:r->sort_set=1;r->sort_column=value&255;r->sort_descending=(value>>8)&1;break;
 case XZ_SHELL_BROWSE_SEARCH_QUERY:snprintf(r->query,sizeof r->query,"%s",xz_shell_browse_query());break;
 default:refuse();return;
 }
 list_rebuild(ls);
}
static void info_jump(const struct xz_shell_action *a) {
 static const struct { int field; enum xz_shell_browse_category category; } map[]={
  {XZ_SHELL_I_ARTIST,XZ_SHELL_CAT_ARTIST},{XZ_SHELL_I_ALBUM,XZ_SHELL_CAT_ALBUM},{XZ_SHELL_I_GENRE,XZ_SHELL_CAT_GENRE},
  {XZ_SHELL_I_KEY,XZ_SHELL_CAT_KEY},{XZ_SHELL_I_BPM,XZ_SHELL_CAT_BPM}};
 int slot=browse_source;
 if(a->deck>=0&&a->deck<XZ_SHELL_DECKS){int kind=loaded[a->deck].device_kind;slot=kind==2?0:kind==3?1:-1;}
 if(slot<0||!usb[slot].lib){refuse();return;}
 for(size_t i=0;i<sizeof map/sizeof *map;i++)if(map[i].field==a->id){
  browse_source=slot;
  struct list_slot *ls=&lists[L_BROWSE];
  if(ls->source!=slot){retire_list(ls->result);ls->result=NULL;ls->source=slot;}
  ls->req.category=map[i].category;ls->req.depth=1;ls->req.path[0]=(uint32_t)a->value;ls->req.sort_set=0;
  ls->requested=1;list_rebuild(ls);return;
 }
 refuse();
}
static void execute_locked(const struct xz_shell_action *out, size_t n, uint32_t now) {
 for(size_t i=0;i<n;i++){
  const struct xz_shell_action *a=&out[i];struct xz_shrt_command c;
  xz_mods_shell_proof_v1.actions++;
  switch(a->kind){
  case XZ_SHELL_ACT_SET_SETTING:
   if(!xz_shrt_setting_command(a,now,&c)||(c.kind==XZ_SHRT_CMD_TIME_MODE&&!has(G_TIME_MODE))||
      (c.kind==XZ_SHRT_CMD_QUANTIZE_BEAT&&!has(G_QUANTIZE))){refuse();break;}
   queue_command(&c);break;
  case XZ_SHELL_ACT_LOAD:
   if(!(model.capabilities&XZ_SHELL_CAP_LOAD)||a->deck<0||a->deck>=XZ_SHELL_DECKS||a->id<=0||browse_source<0){refuse();break;}
   c=(struct xz_shrt_command){XZ_SHRT_CMD_LOAD,a->deck,0,(uint32_t)a->id,(uint8_t)(2+browse_source),now};
   queue_command(&c);break;
  case XZ_SHELL_ACT_HAND_OFF:
   if(a->id==XZ_SHELL_HAND_OFF_MENU&&has(G_MENU)){
    c=(struct xz_shrt_command){XZ_SHRT_CMD_STOCK_MENU,-1,0,0,0,now};queue_command(&c);
   }
   hand_off_locked(now);break;
  case XZ_SHELL_ACT_BROWSE:browse_action(a->value,a->id);break;
  case XZ_SHELL_ACT_INFO_JUMP:info_jump(a);break;
  case XZ_SHELL_ACT_SET_SOURCE:
   if(a->id==XZ_SHELL_SRC_USB1||a->id==XZ_SHELL_SRC_USB2){
    int slot=a->id==XZ_SHELL_SRC_USB1?0:1;
    if(usb[slot].lib)browse_source=slot;else refuse();
   }else refuse();
   break;
  case XZ_SHELL_ACT_ZOOM:case XZ_SHELL_ACT_ACTIVE_DECK:case XZ_SHELL_ACT_NONE:break; /* shell state */
  default:refuse();break; /* SEEK, GRID_ADJUST, TAG_TRACK, SET_RATING, CLOSE_SHELL: capability off */
  }
 }
 if(n)fill_slots_locked();
 dirty_flag=1;
}
static void deliver_locked(const struct xz_shell_event *e, uint32_t now) {
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 size_t n=xz_shell_input(&shell,&model,e,out);
 execute_locked(out,n,now);
}
static void deliver_key_locked(int key, uint32_t now) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,(enum xz_shell_key)key,-1};
 deliver_locked(&e,now);
}

/* ---- model refresh (model thread) ---- */
static void refresh_locked(uint32_t now, const struct xz_shell_runtime_frame_input *in) {
 uint64_t began=now_us();
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  loaded[d]=read_loaded(d);
  struct xz_shell_deck input;memset(&input,0,sizeof input);
  if(in->key[d].valid){
   input.key=in->key[d].text;input.key_matches_master=in->key[d].matches_master;input.key_shift=in->key[d].shift;
   input.known|=K(KEY)|K(KEY_SHIFT);
  }
  struct xz_arcade_track_clock clock;
  if(has(G_CLOCK)&&xz_arcade_native_clock_read(plain_read,NULL,(unsigned)d,&clock)){
   input.playing=!!clock.playing;
   int beat=(int)floorf(clock.bar_phase)+1;
   input.bar_phase=beat<1?1:beat>4?4:beat;
   input.known|=K(PLAYING)|K(BAR_PHASE);
  }
  if(grid_count[d]){input.grid=grid_cache[d];input.grid_count=grid_count[d];input.known|=K(GRID);}
  unsigned kind=loaded[d].known?loaded[d].device_kind:0;
  if(kind==2||kind==3)input.known|=K(SOURCE);
  xz_shell_model_set_input(storage,d,&input,kind);
 }
 apply_waves_locked();
 unsigned changed=xz_shell_model_refresh(&model,storage,model_read,now);
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(changed>>d&1u){
  /* A new track: drop the old grid now, load the new wave and grid on the worker. */
  wave_job[d].wanted++;wave_job[d].retry_at=0;grid_count[d]=0;pthread_cond_signal(&wake);
 }
 uint32_t caution=xz_shell_model_caution(storage);
 xz_shrt_handoff_caution(&handoff,caution);
 xz_mods_shell_proof_v1.caution=caution;
 if(handoff.handed&&xz_shrt_handoff_step(&handoff,now,stock_play()))dirty_flag=1;
 int playing=0;
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(model.deck[d].known&K(PLAYING)&&model.deck[d].playing)playing=1;
 __atomic_store_n(&period_ms,xz_shrt_frame_period_ms(xz_shell_screen(&shell),playing),__ATOMIC_RELAXED);
 refreshed=1;
 fill_slots_locked();
 last_refresh_ms=now;
 uint32_t took=(uint32_t)(now_us()-began);
 sec.refresh_us+=took;sec.refreshes++;if(took>sec.refresh_max)sec.refresh_max=took;
}

/* ---- entry points ---- */
int xz_shell_runtime_owns(void) { return __atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE); }
int xz_shell_runtime_idle(void) {
 if(!__atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE)||__atomic_load_n(&dirty_flag,__ATOMIC_ACQUIRE))return 0;
 return now_ms()-__atomic_load_n(&last_drawn_ms,__ATOMIC_ACQUIRE)<__atomic_load_n(&period_ms,__ATOMIC_RELAXED);
}
int xz_shell_runtime_frame(uint16_t *pixels, size_t stride, const struct xz_shell_runtime_frame_input *in) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!in||!pixels||stride<XZ_SHELL_WIDTH)return 0;
 uint32_t now=now_ms();
 if(pthread_mutex_trylock(&mutex)){
  __atomic_add_fetch(&sec.busy,1,__ATOMIC_RELAXED);
  return __atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE)?-1:0;
 }
 sec.calls++;xz_mods_shell_proof_v1.render_calls++;
 if(xz_native_skin_on_owner_thread()){sec.flips++;xz_mods_shell_proof_v1.stock_flips++;}
 last_call_ms=now;last_input=*in;
 int result=0;
 if(!enabled||!candidate(in->eligible,in->theme)){set_owns(0);goto done;}
 int owned=owns_flag,due=dirty_flag||now-last_drawn_ms>=period_ms;
 if(owned&&!due){sec.skipped++;result=-1;goto done;}
 /* Off screen (handed off or yielding to a caution) the model still refreshes, at 10 Hz, to see the way back. */
 if(!owned&&refreshed&&now-last_refresh_ms<100u)goto done;
 refresh_locked(now,in);
 if(!owns_now(in->eligible,in->theme)){set_owns(0);goto done;}
 set_owns(1);
 uint64_t began=now_us();
 result=xz_shell_render(&shell,&model,pixels,stride)?1:-1;
 uint32_t took=(uint32_t)(now_us()-began);
 sec.frames++;sec.render_us+=took;if(took>sec.render_max)sec.render_max=took;
 xz_mods_shell_proof_v1.frames++;
 __atomic_store_n(&last_drawn_ms,now,__ATOMIC_RELEASE);__atomic_store_n(&dirty_flag,0,__ATOMIC_RELEASE);
done:
 roll_second(now);
 pthread_mutex_unlock(&mutex);
 return result;
}
int xz_shell_runtime_key(int eligible, int theme, uint16_t code, unsigned channel, unsigned op) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 const struct xz_shrt_key_map *map=xz_shrt_key_lookup(code,channel);
 enum xz_shrt_phase phase=xz_shrt_phase_of(op);
 if(!map||map->rule==XZ_SHRT_STOCK)return 0;
 uint32_t now=now_ms();int swallow=0;
 pthread_mutex_lock(&mutex);
 int owns=owns_now(eligible,theme);
 if(phase==XZ_SHRT_PRESS){
  enum xz_shrt_verdict v=owns?xz_shrt_key_verdict(&shell,&model,map):XZ_SHRT_PASS;
  swallow=v==XZ_SHRT_SWALLOW;
  xz_shrt_contact_press(&contacts,code,channel,swallow);
  if(v==XZ_SHRT_PASS_HAND_OFF){hand_off_locked(now);xz_mods_shell_proof_v1.key_hand_offs++;}
  if(swallow){int key=xz_shrt_key_for_phase(map,phase,0);if(key>=0)deliver_key_locked(key,now);}
 }else{
  int held=0,decision=xz_shrt_contact_follow(&contacts,code,channel,phase,&held);
  if(decision>0){
   swallow=1;
   int key=xz_shrt_key_for_phase(map,phase,held);
   if(key>=0&&owns)deliver_key_locked(key,now);
  }else if(decision==0&&map->key==XZ_SHELL_KEY_BACK&&phase==XZ_SHRT_HOLD&&!held&&handoff.handed&&candidate(eligible,theme)){
   /* BACK held over stock brings the shell back; the key itself still belongs to stock. */
   xz_shrt_handoff_end(&handoff);xz_mods_shell_proof_v1.back_hold_resumes++;dirty_flag=1;
  }
 }
 if(swallow)xz_mods_shell_proof_v1.keys_swallowed++;else xz_mods_shell_proof_v1.keys_passed++;
 pthread_mutex_unlock(&mutex);
 return swallow;
}
int xz_shell_runtime_touch(int eligible, int theme, int x, int y, int down, int stock_down) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return XZ_SHELL_TOUCH_TO_STOCK;
 uint32_t now=now_ms();
 x=x<0?0:x>=XZ_SHELL_WIDTH?XZ_SHELL_WIDTH-1:x;y=y<0?0:y>=XZ_SHELL_HEIGHT?XZ_SHELL_HEIGHT-1:y;
 pthread_mutex_lock(&mutex);
 struct xz_shrt_touch_step s=xz_shrt_touch_route(&touch_state,owns_now(eligible,theme),x,y,down,stock_down);
 if(s.event>=0){
  struct xz_shell_event e={(enum xz_shell_event_kind)s.event,s.x,s.y,0,XZ_SHELL_KEY_COUNT,-1};
  deliver_locked(&e,now);xz_mods_shell_proof_v1.touches++;
 }
 pthread_mutex_unlock(&mutex);
 return s.to_stock?XZ_SHELL_TOUCH_TO_STOCK:s.release_stock?XZ_SHELL_TOUCH_TAKEN_RELEASE_STOCK:XZ_SHELL_TOUCH_TAKEN;
}
int xz_shell_runtime_encoder_turn(int eligible, int theme, int delta) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!delta)return 0;
 pthread_mutex_lock(&mutex);
 int owns=owns_now(eligible,theme);
 if(owns){
  struct xz_shell_event e={XZ_SHELL_ENCODER_TURN,0,0,delta,XZ_SHELL_KEY_COUNT,-1};
  deliver_locked(&e,now_ms());xz_mods_shell_proof_v1.encoder_events++;
 }
 pthread_mutex_unlock(&mutex);
 return owns;
}
int xz_shell_runtime_encoder_push(int eligible, int theme, uint32_t flags, uint32_t state) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 pthread_mutex_lock(&mutex);
 int owns=owns_now(eligible,theme);
 if(owns){
  int kind=xz_shrt_push_event(&push_state,flags,state);
  if(kind>=0){
   struct xz_shell_event e={(enum xz_shell_event_kind)kind,0,0,0,XZ_SHELL_KEY_COUNT,-1};
   deliver_locked(&e,now_ms());xz_mods_shell_proof_v1.encoder_events++;
  }
 }
 pthread_mutex_unlock(&mutex);
 return owns;
}

/* ---- native UI thread ---- */
static int load_refused(const struct xz_shrt_command *c) {
 uint32_t lock,state;
 /* UiGetUtilLoadLock (settings id 0x60): stored as 0/1 or 0x30/0x31; anything else is unknown and refuses. */
 if(!setting_value(0x60,&lock)||(lock!=0&&lock!=0x30))return 1;
 if(!word(TOTAL_WK+0x1610u+(uint32_t)c->deck*0x60u,&state)||state==7||state==9)return 1;
 return 0;
}
static int run_command(const struct xz_shrt_command *c) {
 switch(c->kind){
 case XZ_SHRT_CMD_TIME_MODE:
  if(!code_is(0xca308,set_time_mode_code,16))return -1;
  for(int ch=1;ch<=2;ch++)if(c->deck<0||c->deck==ch-1)((void(*)(int,int))(uintptr_t)0xca308)(ch,c->value?1:0);
  return 0;
 case XZ_SHRT_CMD_QUANTIZE_BEAT:
  if(c->value<0||c->value>3||!code_is(0xc9304,set_quantize_code,16))return -1;
  ((void(*)(int))(uintptr_t)0xc9304)(c->value);
  return 0;
 case XZ_SHRT_CMD_STOCK_MENU:{
  /* The release-phase entry the mod already passes for MENU (mods/key/native_browse.c). */
  if(!code_is(0xdf1e8,menu_code,16))return -1;
  uint32_t entry[4]={2,0,2,0};
  return ((int(*)(uint32_t *))(uintptr_t)0xdf1e8)(entry)>0?0:-2;
 }
 case XZ_SHRT_CMD_LOAD:{
  if(c->deck<0||c->deck>=XZ_SHELL_DECKS||!code_is(0x135728,load_code,16)||!code_is(0x13ba38,music_id_code,8))return -1;
  if(load_refused(c))return -3;
  /* music_id {node 0 = this player, device kind, browse kind 1 = track list (inferred), 0, id}. */
  unsigned char id[8]={0,c->device_kind,1,0};memcpy(id+4,&c->track_id,4);
  return ((int(*)(int,void *,int))(uintptr_t)0x135728)(c->deck,id,3)?0:-2;
 }
 default:return -1;
 }
}
void xz_shell_runtime_owner_tick(void) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
 uint32_t now=now_ms();
 struct xz_shrt_command commands[XZ_SHRT_QUEUE];unsigned n=0;int pump=0;
 if(pthread_mutex_trylock(&mutex))return;
 sec.ticks++;
 while(n<XZ_SHRT_QUEUE&&xz_shrt_queue_pop(&queue,&commands[n]))n++;
 /* Off screen, stock may not flip at all; keep checking the way back from here. */
 if(refreshed&&xz_shrt_handoff_yields(&handoff)&&candidate(last_input.eligible,last_input.theme)&&now-last_refresh_ms>=100u){
  refresh_locked(now,&last_input);
  if(owns_now(last_input.eligible,last_input.theme))dirty_flag=1;
 }
 /* The receiver pumps the shell while it owns the screen. If no frame was asked
    for in 500 ms, ask stock for one repaint, at most once a second. */
 if(owns_flag&&now-last_call_ms>500u&&now-last_pump_ms>=1000u){pump=1;last_pump_ms=now;sec.pump++;}
 roll_second(now);
 pthread_mutex_unlock(&mutex);
 for(unsigned i=0;i<n;i++){
  int rc=run_command(&commands[i]);
  uint32_t latency=now-commands[i].at_ms;
  pthread_mutex_lock(&mutex);
  struct xz_shell_proof *p=&xz_mods_shell_proof_v1;
  p->last_command=(uint32_t)commands[i].kind;p->last_command_result=(uint32_t)rc;
  if(rc)p->commands_refused++;else p->commands_run++;
  if(latency>p->command_latency_max_ms&&latency<60000u)p->command_latency_max_ms=latency;
  dirty_flag=1;
  pthread_mutex_unlock(&mutex);
  if(debug){char line[120];snprintf(line,sizeof line,"SHELL_COMMAND: kind=%d deck=%d value=%d result=%d latency_ms=%u",
   (int)commands[i].kind,commands[i].deck,(int)commands[i].value,rc,latency);xz_log(line);}
 }
 if(pump)xz_native_skin_request_repaint();
}

static int flag(const char *name) { const char *v=getenv(name);return v&&!strcmp(v,"1"); }
int xz_shell_runtime_start(void) {
 if(__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!flag("XZ_MODS_SHELL"))return 0;
 load_enabled=flag("XZ_MODS_SHELL_LOAD_BY_ID");debug=flag("XZ_MODS_SHELL_DEBUG");
 /* calloc: shell_model.c requires zeroed storage. */
 storage=calloc(1,xz_shell_model_storage_size());
 if(!storage){xz_log("FLAGSHIP shell unavailable: no memory for the model");return -1;}
 xz_shell_init(&shell);
 memset(&model,0,sizeof model);memset(&handoff,0,sizeof handoff);memset(&queue,0,sizeof queue);
 for(int k=0;k<L_COUNT;k++)list_reset(&lists[k],k);
 check_guards();
 running=1;
 if(pthread_create(&worker_thread,NULL,worker,NULL)){running=0;free(storage);storage=NULL;xz_log("FLAGSHIP shell unavailable: worker thread");return -1;}
 worker_created=1;
 xz_mods_shell_proof_v1.enabled=1;xz_mods_shell_proof_v1.load_enabled=(uint32_t)load_enabled;
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);
 char line[200];
 snprintf(line,sizeof line,"FLAGSHIP shell runtime armed: guards=0x%x load_by_id=%d debug=%d (theme 24 shows it)",guards,load_enabled,debug);
 xz_log(line);
 return 0;
}
void xz_shell_runtime_stop(void) {
 if(!__atomic_exchange_n(&enabled,0,__ATOMIC_ACQ_REL))return;
 pthread_mutex_lock(&mutex);running=0;pthread_cond_signal(&wake);pthread_mutex_unlock(&mutex);
 if(worker_created){pthread_join(worker_thread,NULL);worker_created=0;}
 pthread_mutex_lock(&mutex);
 set_owns(0);
 model.browse=NULL;model.source=NULL;model.info=NULL;model.settings_screen=NULL;
 for(int k=0;k<L_COUNT;k++){retire_list(lists[k].result);lists[k].result=NULL;}
 for(int i=0;i<2;i++){if(usb[i].lib&&retired.libs<4)retired.lib[retired.libs++]=usb[i].lib;usb[i].lib=NULL;}
 for(int d=0;d<XZ_SHELL_DECKS;d++)free_wave(d);
 free(storage);storage=NULL;refreshed=0;
 xz_mods_shell_proof_v1.enabled=0;
 pthread_mutex_unlock(&mutex);
 drain_retired();
}
