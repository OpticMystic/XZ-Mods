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
#include "shell_analysis.h"
#include "shell_playlists.h"
#include "shell_utility_table.h"
#include "shell_mysettings.h"
#include "shell/shell_browse.h"
#include "shell/shell_info.h"
#include "shell/shell_settings.h"
#include "shell/shell_source.h"
#include "runtime.h"
#include "settings.h"
#include "audio/native_reader.h"
#include "arcade/native_clock.h"
#include "key/native_browse.h"
#include "ui/layered_wave_source.h"
#include "ui/native_skin_runtime.h"
#include "ui/themes.h"
#include "ui/ui.h"
#include <dirent.h>
#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

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
 /* Added for the 60 Hz pipeline (the struct reports its own size). Last whole second: */
 uint32_t second_refreshes, second_gap_max_us, second_late, second_tick_busy, second_clock_reads, second_moving;
 /* Since start: */
 uint32_t model_holds, fx_guard_failures, caution_filtered, ui_requests, menu_passes;
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
static struct { uint32_t start, frames, calls, flips, skipped, busy, pump, ticks, tick_busy, render_max, refresh_max, refreshes, clock_reads, moving, following, lock_waits, ui_cached,
 jitter_us_max, jitter_frames;
 uint64_t render_us, refresh_us; struct xz_shrt_frames shown; } sec;
/* Playhead and motion per deck (model thread), the caution debounce, header-button requests for
   ui_runtime.c, the MENU key passed to stock, and the LOAD confirmation read a second later. */
static struct xz_shrt_position position[XZ_SHELL_DECKS];
static struct xz_shrt_motion motion[XZ_SHELL_DECKS];
static struct xz_shrt_caution caution_state;
/* The audio engine's position and speed per deck, read in this refresh (model thread). */
#define ENGINE_AGREE_MS 1500
static struct { int ok; uint32_t position_ms; float speed; } engine[XZ_SHELL_DECKS];
static uint32_t sync_logged[XZ_SHELL_DECKS];
static unsigned follow_bits; /* decks whose playhead follows the engine readings (jog hold, scratch) */
#define FRAME_LOCK_US 4000u
/* The receiver's next vblank in gettimeofday microseconds (xz_directfb_hook.c), found at start. */
static uint64_t (*next_present)(void);
/* About half a frame from the vblank to the beam reaching the lanes (middle rows of 525 lines). */
#define SCANOUT_MID_US 8000u
/* Per-frame playhead step against the ideal (render clock delta x tempo), for the SHELL line. */
static struct { int32_t last[XZ_SHELL_DECKS]; uint32_t last_clock; int valid; } step;
static const int zoom_pps[XZ_SHELL_ZOOM_MAX+1]={32,48,64,96,128,192,256}; /* shell_play.c lane pixels per second */
static uint32_t last_caution_logged;
static unsigned ui_requests;
static int menu_passed;
static int quick_return; /* a key passed to stock that leaves stock on PLAY (TAG TRACK) */
static struct { int pending, deck, tries; uint32_t id, before, at; uint8_t device_kind; } load_check;
/* One-line feedback for actions the shell could not carry out (LOAD refused, not loaded, off). It is
   published to the renderer once shell.h has the notice fields (XZ_SHELL_NOTICE_MS), and logged. */
static char notice_text[48];
static uint32_t notice_at;
static int notice_pending;     /* raised since the last frame: stamped with that frame's clock */
static int menu_inert;         /* a short MENU asked for stock's menu popup and was kept inert (SHELL_KEY note) */
static uint32_t ui_requests_at;/* when header requests were raised; untaken after 500 ms they report a notice */
static uint32_t load_actions; /* LOAD actions the shell input produced (queued or refused) */
/* REKORDBOX: the action ui_runtime.c decided for the press now in the key path (enum xz_rekordbox_action). */
static int rekordbox_action=XZ_REKORDBOX_STOCK;
/* SOURCE LINK / EXPORT MODE: the stock key a select queued (0 LINK, 1 REKORDBOX, -1 none; under mutex), and
   whether the input being delivered is an encoder push (the native key task, where it can run). */
static int stock_key_pending=-1;
static int push_input;
/* ui_runtime.c: the stock LINK (0) or REKORDBOX (1) key handler. run 0: 1 when it can be called; run 1: calls
   it as a press and returns 0, or -1 when its hook is not armed. Weak: host fixtures have no ui_runtime.c. */
extern int xz_ui_runtime_stock_source_key(int which, int run) __attribute__((weak));
static struct { uint32_t second, lines, dropped; } key_log;

/* ---- clocks and reads ---- */
static uint64_t now_us(void) { struct timeval t;gettimeofday(&t,NULL);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_usec; }
static uint32_t now_ms(void) { return (uint32_t)(now_us()/1000u); }
/* Monotonic clock through the raw syscall: clock_gettime is GLIBC_2.17 in libc and librt is not a
   dependency the deck build allows. Falls back to the wall clock if the call fails. */
static uint64_t mono_us(void) {
 struct timespec t;
 if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return now_us();
 return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;
}
/* The stock UTILITY items (shell_utility.h). raw/known are written on the owner tick under the mutex.
   trial: a --ram-settings run; stock saves every UTILITY set to flash within 20 ms, so there the items
   are read only unless XZ_MODS_SHELL_UTILITY_WRITES=1 says the owner accepts that. */
static struct {
 int32_t raw[XZ_SHUT_DEFS];uint8_t known[XZ_SHUT_DEFS],get_ok[XZ_SHUT_DEFS],set_ok[XZ_SHUT_DEFS],apply_ok[XZ_SHUT_DEFS];
 int trial,version_ok,poll_now;uint32_t polled_ms;char version[24];const char *version_choice[1];
 uint32_t polls,sets,set_fails,changes;
} util;
/* MY SETTINGS per USB slot (shell_mysettings.h): read on the worker at mount, under the mutex here. A write
   is one pending job the worker runs (the newest replaces an older one). readonly: --ram-settings, where
   nothing is written to a stick. */
static struct xz_shms_files mys[2];
static int mys_readonly;
static struct { int pending, slot; unsigned kinds; struct xz_shms_files f; char done[48]; } mys_job;
static struct xz_shms_rows mys_rows;
static struct xz_shell_settings_model mys_stored;
static struct xz_shell_my_settings_model mys_model;
static struct xz_shell_dj_profile mys_profiles[XZ_SHELL_RUNTIME_DJ];
static int mys_profile_entry[XZ_SHELL_RUNTIME_DJ]; /* view entry of each published profile */
static int dj_pick=-1; /* under mutex */
/* The last LOAD per stick, for the panel: DONE or FAILED and its line. */
static enum xz_shell_my_state mys_state[2];
static char mys_result[2][48];
/* Who took the lock last (debug: the worst-frame line names what a frame waited for). */
static const char *lock_holder="none";
static void lock_as(const char *who) { pthread_mutex_lock(&mutex);lock_holder=who; }
static int model_read(uintptr_t a, void *out, size_t n) { return a>UINT32_MAX?-1:xz_read_memory((uint32_t)a,out,n); }
static int plain_read(void *c, uint32_t a, void *out, size_t n) { (void)c;return xz_read_memory(a,out,n); }
static int word(uint32_t a, uint32_t *v) { return !xz_read_memory(a,v,4); }
static int pointer(uint32_t p, size_t n) { return p>=0x10000u&&!(p&3u)&&(uint64_t)p+n<=UINT32_MAX; }
static int code_is(uint32_t a, const unsigned char *expected, size_t n) {
 unsigned char b[32];return n<=32&&!xz_read_memory(a,b,n)&&!memcmp(b,expected,n);
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
 for(int i=0;i<XZ_SHUT_DEFS;i++){
  const struct xz_shut_def *d=&xz_shut_defs[i];
  util.get_ok[i]=d->get&&code_is(d->get,d->get_code,32);
  util.set_ok[i]=d->set&&code_is(d->set,d->set_code,32);
  util.apply_ok[i]=!d->apply||code_is(d->apply,d->apply_code,32);
  if(!util.apply_ok[i])util.set_ok[i]=0; /* stock applies before it stores: never store without the apply */
 }
 util.version_ok=code_is(XZ_SHUT_VERSION_GET,xz_shut_version_code,32);
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
/* Stock is on its play screen: lifecycle flag set, browse mode 1 and no menu popup. -1 when neither the
   flag nor the browse mode can be read. Either one alone decides when the other is unreadable, so a
   missing guard cannot keep the shell handed off for good. parts (debug): flag, mode, menu, or ~0u. */
struct stock_parts { uint32_t flag, mode, menu; };
static int stock_play_parts(struct stock_parts *p) {
 uint32_t flag=UINT32_MAX,menu=UINT32_MAX;
 int flag_known=has(G_PLAY_FLAG)&&word(PLAY_LIFECYCLE,&flag);
 if(!flag_known)flag=UINT32_MAX;
 unsigned mode=xz_native_browse_mode();
 if(!(has(G_MENU_VISIBLE)&&word(MENU_VISIBLE,&menu)))menu=UINT32_MAX;
 if(p){p->flag=flag;p->mode=mode;p->menu=menu;}
 if(!flag_known&&!mode)return -1;
 if(flag_known&&!flag)return 0;
 if(mode&&mode!=1)return 0;
 if(menu!=UINT32_MAX&&menu)return 0;
 return 1;
}
static int stock_play(void) { return stock_play_parts(NULL); }

/* ---- libraries ---- */
enum { USB_NONE, USB_LOADING, USB_READY, USB_ERROR };
static struct { struct xz_shlib *lib; int status, space_known, rdonly; uint64_t free_bytes, total_bytes; uint32_t tracks; char root[1024]; } usb[2];
/* Under mutex. The browse column choice (0 = the default columns), the active track filter, each stick's
   playlist edits (authoritative; the worker builds from copies), queued edits, the merged playlists for
   ADD TO PLAYLIST, and stem mutes waiting for ui_runtime.c. */
static uint32_t column_mask;
static struct xz_shlib_filter active_filter;
static struct xz_shpl_set edits[2];
#define PL_QUEUE 16
/* TAG LIST and HISTORY edits, run on the worker into the stick's sidecar like playlist edits. ADD_LIST
   carries the request that lists the folder's tracks. */
enum list_edit_kind { LE_TAG, LE_HISTORY };
static struct list_edit { int slot; enum list_edit_kind kind; int op; uint32_t value; struct xz_shlib_request req; } le_queue[8];
static unsigned le_count;
static struct { int slot; struct xz_shell_playlist_edit e; } pl_queue[PL_QUEUE];
static unsigned pl_head, pl_count;
#define PL_REFS 128
static struct xz_shlib_playlist_ref refs[2][PL_REFS];
static int ref_count[2];
static struct xz_shell_playlist_ref playlist_refs[PL_REFS];
static struct { int deck, stem, value; } stem_queue[8];
static unsigned stem_count;
static void publish_refs(int slot);
static void notice_locked(uint32_t now, const char *text);
static void matching_filter(struct xz_shlib_filter *f);
static int browse_source=-1;
enum { L_BROWSE, L_PLAYLIST, L_SEARCH, L_TAG, L_COUNT };
static struct list_slot {
 struct xz_shlib_request req;
 uint32_t wanted, built;
 int source, requested;
 struct xz_shlib_list *result;
 int32_t window_start;
} lists[L_COUNT];
static const enum xz_shell_screen list_screen[L_COUNT]={XZ_SHELL_BROWSE,XZ_SHELL_PLAYLIST,XZ_SHELL_SEARCH,XZ_SHELL_TAG_LIST};
static struct xz_shell_browse_row rows[XZ_SHELL_BROWSE_WINDOW];
static struct xz_shell_browse_model browse;
static struct xz_shell_source_model source_model;
static struct xz_shell_info_model info_model;
/* The MODS screen's mod tabs (shell_settings.h): the MODS panel's own top pages, in its order (ui/ui.c
   menu_pages). APPEARANCE is an item tab drawn by the shell (its sets run the panel's own actions); the
   others are TAB_PAGEs whose button opens that page in the panel (OPEN_MODS, value = page). */
static void refuse(void);
static void save_display(int id, int value);
/* MODS > APPEARANCE in the shell's own style: an item tab whose sets run the MODS panel's own actions. */
enum { LOOK_TAB=3, LOOK_INTERFACE=1, LOOK_THEME, LOOK_WAVE, LOOK_KEY_FORMAT, LOOK_KEY_FEEDBACK, LOOK_TRAFFIC,
       LOOK_DRUMS, LOOK_HARMONICS, LOOK_VOCALS, LOOK_STEM_LAYOUT, LOOK_MODE_LAMPS, LOOK_QUANTIZE_LAMP };
static const char *const look_sections[4]={"INTERFACE","THEME","WAVEFORM","KEYS AND STEMS"};
static const char *const look_interface[2]={"MODERN","CLASSIC"};
_Static_assert(XZ_UI_MODERN_THEMES==1,"one modern theme name below");
static const char *const look_modern_theme[1]={"STANDARD"};
static const char *const look_wave[3]={"STOCK","3-BAND","STEMS"};
static const char *const look_key_format[3]={"USB PREFERENCE","CLASSIC","WHEEL"};
static const char *const look_off_on[2]={"OFF","ON"};
static const char *const look_stem_color[9]={"THEME DEFAULT","BLUE","RED","GREEN","AMBER","PURPLE","CYAN","PINK","WHITE"};
static const char *const look_show_hide[2]={"SHOW","HIDE"};
static struct xz_shell_setting_item look_items[11];
static struct xz_shell_settings_model look_model;
/* MY SETTINGS > WAVEFORM: the stem row layout, set through SET_MOD on the APPEARANCE tab (waveform_tab). */
static const char *const look_stem_layout[5]={"HIDDEN","BELOW / FULL","BELOW / HALF","OUTER / FULL","OUTER / HALF"};
static struct xz_shell_setting_item wave_items[1];
static struct xz_shell_settings_model wave_model;
/* Descriptions (shell_settings.h help): APPEARANCE is scope LOOK_TAB + 1; the WAVEFORM items share it. */
static const char *const help_interface[2]={"This full-screen interface.","The player's own layout, restyled by the classic themes."};
static const char *const help_wave[3]={"The player's own waveform.","Lows, mids and highs layered in their own colours.",
 "Drums, harmonics and vocals in the stem colours; tracks without stems show 3-BAND."};
static const char *const help_key_format[3]={"As the stick's library sets it.","Note names, like Am.","Wheel numbers, like 8A."};
static const char *const help_mode_lamps[2]={"Each lamp lights while its mode is on.","The lamps leave the screen; the modes keep working."};
static const char *const help_quantize_lamp[2]={"Red while QUANTIZE is on, grey while it is off.","The QUANTIZE button's own light still shows it."};
static const char *const help_stem_color[9]={"Follows the theme.",NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
static const char *const help_stem_layout[5]={"Normal waveforms, no stem rows.","Stem rows under the waveforms, full height.",
 "Stem rows under the waveforms, half height.","Stem rows outside the waveforms, full height.","Stem rows outside the waveforms, half height."};
#define HELP(id,text,count,choices) {LOOK_TAB+1,id,text,count,choices}
static const struct xz_shell_item_help help_items[]={
 HELP(LOOK_INTERFACE,"Who draws the player screen.",2,help_interface),
 HELP(LOOK_MODE_LAMPS,"The top bar's PLAY MODE, A. HOT CUE and A. CUE lamps: whether the next track plays on, and whether hot cues and the cue point load with a track.",2,help_mode_lamps),
 HELP(LOOK_QUANTIZE_LAMP,"Each deck's QUANTIZE lamp and beat value, above its time.",2,help_quantize_lamp),
 HELP(LOOK_THEME,"The look of the modern interface. More modern themes are coming.",0,NULL),
 HELP(LOOK_WAVE,"How the scrolling waveform is drawn.",3,help_wave),
 HELP(LOOK_KEY_FORMAT,"How keys are written.",3,help_key_format),
 HELP(LOOK_KEY_FEEDBACK,"Shows the key a track plays in after tempo and key shift.",0,NULL),
 HELP(LOOK_TRAFFIC,"Colours keys in the browser green or amber by how well they mix with the master deck.",0,NULL),
 HELP(LOOK_DRUMS,"The colour for this stem on the pads, the stem rows and the STEMS waveform.",9,help_stem_color),
 HELP(LOOK_HARMONICS,"The colour for this stem on the pads, the stem rows and the STEMS waveform.",9,help_stem_color),
 HELP(LOOK_VOCALS,"The colour for this stem on the pads, the stem rows and the STEMS waveform.",9,help_stem_color),
 HELP(LOOK_STEM_LAYOUT,"Where the stem rows sit next to the waveforms, and how tall they are.",5,help_stem_layout),
};
#undef HELP
/* MODS > BUTTONS: the REKORDBOX button's two slots, the same rows as the panel's CONTROLS > BUTTONS. Item id =
   slot + 1; a set runs the panel's own XZ_UI_REKORDBOX_ASSIGN. */
enum { BUTTONS_TAB=2, BUTTONS_PRESS=1, BUTTONS_SHIFT };
static const char *const buttons_sections[1]={"REKORDBOX BUTTON"};
static const char *buttons_choices[XZ_UI_RB_CHOICES],*buttons_help[XZ_UI_RB_CHOICES];
static struct xz_shell_setting_item buttons_items[2];
static struct xz_shell_settings_model buttons_model;
static const struct xz_shell_item_help help_all[]={
 {BUTTONS_TAB+1,BUTTONS_PRESS,"What the REKORDBOX button does.",XZ_UI_RB_CHOICES,buttons_help},
 {BUTTONS_TAB+1,BUTTONS_SHIFT,"What the REKORDBOX button does while SHIFT is held on either deck.",XZ_UI_RB_CHOICES,buttons_help},
};
static struct xz_shell_item_help help_list[sizeof help_items/sizeof *help_items+sizeof help_all/sizeof *help_all];
static struct xz_shell_help_model help_model;
static void fill_buttons(void) {
 const struct xz_shell_runtime_frame_input *in=&last_input;int k=!!in->rekordbox_known;
 if(!help_model.count){
  for(int i=0;i<XZ_UI_RB_CHOICES;i++){buttons_choices[i]=xz_ui_rekordbox_choice_name(i);buttons_help[i]=xz_ui_rekordbox_choice_help(i);}
  size_t n=0;
  for(size_t i=0;i<sizeof help_items/sizeof *help_items;i++)help_list[n++]=help_items[i];
  for(size_t i=0;i<sizeof help_all/sizeof *help_all;i++)help_list[n++]=help_all[i];
  help_model=(struct xz_shell_help_model){(uint32_t)n,help_list};
 }
 for(int slot=0;slot<2;slot++){
  int v=k?in->rekordbox_assign[slot]:0;if(v<0||v>=XZ_UI_RB_CHOICES){v=0;k=0;}
  buttons_items[slot]=(struct xz_shell_setting_item){BUTTONS_PRESS+slot,(enum xz_shell_setting_category)0,
   slot?"SHIFT + REKORDBOX":"REKORDBOX BUTTON",buttons_choices,XZ_UI_RB_CHOICES,v,k,k};
 }
 buttons_model=(struct xz_shell_settings_model){2,buttons_items,NULL};
}
static uint32_t look_classic_at; /* the first push of INTERFACE > CLASSIC: a second one within 4 s switches */
static struct { int kind, deck, index; float value; } ui_actions[8]; static int ui_action_count; /* under mutex */
static void fill_look(void) {
 const struct xz_shell_runtime_frame_input *in=&last_input;int k=!!in->look_known;
#define LOOK(n,id,cat,label,choices,count,current) look_items[n]=(struct xz_shell_setting_item){id,(enum xz_shell_setting_category)(cat),label,choices,count,k?(current):0,k,k}
 LOOK(0,LOOK_INTERFACE,0,"INTERFACE",look_interface,2,in->ui_interface==XZ_UI_INTERFACE_MODERN?0:1);
 /* Stock-screen indicators the DJ may hide: shell-owned, always known. */
 look_items[1]=(struct xz_shell_setting_item){LOOK_MODE_LAMPS,(enum xz_shell_setting_category)0,"MODE LAMPS",look_show_hide,2,
  (int)((shell.display.hidden>>XZ_SHELL_IND_MODE_LAMPS)&1),1,1};
 look_items[2]=(struct xz_shell_setting_item){LOOK_QUANTIZE_LAMP,(enum xz_shell_setting_category)0,"QUANTIZE LAMP",look_show_hide,2,
  (int)((shell.display.hidden>>XZ_SHELL_IND_QUANTIZE)&1),1,1};
 LOOK(3,LOOK_THEME,1,"THEME",look_modern_theme,1,in->modern_theme>=0&&in->modern_theme<1?in->modern_theme:0);
 LOOK(4,LOOK_WAVE,2,"SCROLLING WAVEFORM",look_wave,3,in->wave_mode>=0&&in->wave_mode<3?in->wave_mode:0);
 LOOK(5,LOOK_KEY_FORMAT,3,"KEY NOTATION",look_key_format,3,in->key_format>=0&&in->key_format<3?in->key_format:0);
 LOOK(6,LOOK_KEY_FEEDBACK,3,"EFFECTIVE KEY DISPLAY",look_off_on,2,!!in->key_feedback);
 LOOK(7,LOOK_TRAFFIC,3,"COLOR COMPATIBLE KEYS",look_off_on,2,!!in->browse_traffic);
 LOOK(8,LOOK_DRUMS,3,"DRUMS COLOR",look_stem_color,9,in->stem_colors[0]>=0&&in->stem_colors[0]<9?in->stem_colors[0]:0);
 LOOK(9,LOOK_HARMONICS,3,"HARMONICS COLOR",look_stem_color,9,in->stem_colors[1]>=0&&in->stem_colors[1]<9?in->stem_colors[1]:0);
 LOOK(10,LOOK_VOCALS,3,"VOCALS COLOR",look_stem_color,9,in->stem_colors[2]>=0&&in->stem_colors[2]<9?in->stem_colors[2]:0);
#undef LOOK
 look_model=(struct xz_shell_settings_model){11,look_items,NULL};
 wave_items[0]=(struct xz_shell_setting_item){LOOK_STEM_LAYOUT,(enum xz_shell_setting_category)2,"STEM ROW LAYOUT",look_stem_layout,5,
  k&&in->stems_overlay>=0&&in->stems_overlay<5?in->stems_overlay:0,k,k};
 wave_model=(struct xz_shell_settings_model){1,wave_items,NULL};
}
static void ui_action_locked(int kind, int deck, int index, float value, uint32_t now) {
 /* A stem level slide replaces the same stem's unsent level instead of queueing every step. */
 if(kind==XZ_UI_LEVEL)for(int i=0;i<ui_action_count;i++)if(ui_actions[i].kind==kind&&ui_actions[i].deck==deck&&ui_actions[i].index==index){ui_actions[i].value=value;return;}
 if(ui_action_count>=8){refuse();return;}
 ui_actions[ui_action_count].kind=kind;ui_actions[ui_action_count].deck=deck;ui_actions[ui_action_count].index=index;ui_actions[ui_action_count].value=value;ui_action_count++;
 __atomic_or_fetch(&ui_requests,XZ_SHELL_UI_ACTION,__ATOMIC_ACQ_REL);ui_requests_at=now?now:1;
}
/* SET_MOD on APPEARANCE: the panel action that item stands for. */
static void look_set_locked(int id, int choice, uint32_t now) {
 switch(id){
 case LOOK_INTERFACE:
  if(choice==1){
   /* CLASSIC replaces the whole screen and stops the shell: ask for a second push. */
   if(!look_classic_at||now-look_classic_at>4000u){look_classic_at=now?now:1;notice_locked(now,"PUSH AGAIN TO SWITCH TO CLASSIC");return;}
   look_classic_at=0;ui_action_locked(XZ_UI_SET_INTERFACE,-1,XZ_UI_INTERFACE_CLASSIC,0,now);
  }else if(choice==0)ui_action_locked(XZ_UI_SET_INTERFACE,-1,XZ_UI_INTERFACE_MODERN,0,now);
  else refuse();
  return;
 case LOOK_MODE_LAMPS:case LOOK_QUANTIZE_LAMP:{
  int item=id==LOOK_MODE_LAMPS?XZ_SHELL_ITEM_MODE_LAMPS:XZ_SHELL_ITEM_QUANTIZE_LAMP;
  if(choice==0||choice==1){xz_shell_restore_setting(&shell,item,choice);save_display(item,choice);}else refuse();
  return;}
 case LOOK_THEME:if(choice==0)ui_action_locked(XZ_UI_SET_MODERN_THEME,-1,choice,0,now);else refuse();return;
 case LOOK_WAVE:if(choice>=0&&choice<3)ui_action_locked(XZ_UI_WAVE_MODE,-1,choice,0,now);else refuse();return;
 case LOOK_KEY_FORMAT:if(choice>=0&&choice<3)ui_action_locked(XZ_UI_KEY_FORMAT,-1,choice,0,now);else refuse();return;
 case LOOK_KEY_FEEDBACK:ui_action_locked(XZ_UI_KEY_FEEDBACK,-1,0,choice?1.f:0.f,now);return;
 case LOOK_TRAFFIC:ui_action_locked(XZ_UI_BROWSE_TRAFFIC,-1,0,choice?1.f:0.f,now);return;
 case LOOK_STEM_LAYOUT:{
  /* The panel's layout control steps to the next layout: step from the current one to the choice. */
  int from=last_input.stems_overlay;
  if(choice<0||choice>=5||from<0||from>=5){refuse();return;}
  for(int steps=(choice-from+5)%5;steps>0;steps--)ui_action_locked(XZ_UI_STEMS_OVERLAY,-1,0,0,now);
  return;
 }
 case LOOK_DRUMS:case LOOK_HARMONICS:case LOOK_VOCALS:
  if(choice>=0&&choice<9)ui_action_locked(XZ_UI_STEM_COLOR,-1,id-LOOK_DRUMS,(float)choice,now);else refuse();
  return;
 default:refuse();return;
 }
}
static const struct xz_shell_mods_tab mod_tabs[6]={
 {"GENERAL",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_GENERAL,"TURN MODS ON AND OFF AND SET THEM UP"},
 {"CONTROLS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_CONTROLS,"STEM PAD SHORTCUTS AND CUSTOM BEAT JUMPS"},
 {"BUTTONS",XZ_SHELL_TAB_ITEMS,&buttons_model,buttons_sections,1,XZ_UI_BUTTONS,NULL},
 {"APPEARANCE",XZ_SHELL_TAB_ITEMS,&look_model,look_sections,4,XZ_UI_THEMES,NULL},
 {"ADVANCED",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_SETTINGS,"KEY SHIFT, KEY SYNC, GROOVE PADS, X-PAD AND RAM TRACKS"},
 {"EXTRAS",XZ_SHELL_TAB_PAGE,NULL,NULL,0,XZ_UI_EXTRAS,"VJ.TOOLS LINK, GAMES, USB APPS AND UPDATES"}};
_Static_assert(BUTTONS_TAB==2&&LOOK_TAB==3,"tab indexes follow mod_tabs");
static const struct xz_shell_mods_model mods_model={6,mod_tabs};
static int mods_page=-1; /* under mutex: the page OPEN_MODS asked for */
/* TIME MODE, AUTO CUE, the stock UTILITY items and VERSION. */
#define SETTING_ROWS (2+XZ_SHUT_DEFS+1)
static struct xz_shell_setting_item setting_items[SETTING_ROWS];
static const char *setting_notes[SETTING_ROWS];
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
static int list_index(enum xz_shell_screen s) { return s==XZ_SHELL_BROWSE?L_BROWSE:s==XZ_SHELL_PLAYLIST?L_PLAYLIST:s==XZ_SHELL_SEARCH?L_SEARCH:s==XZ_SHELL_TAG_LIST?L_TAG:-1; }
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
/* The playlist folder the shown PLAYLIST list is in: the open level when it is a folder, else the level
   above it (an open playlist's folder); 0 = the top. */
static uint32_t open_folder(int slot) {
 int k=shown_list(NULL);const struct xz_shlib_request *r=&lists[k].req;
 if(slot<0||!usb[slot].lib||lists[k].source!=slot||r->category!=XZ_SHELL_CAT_PLAYLIST)return 0;
 for(int d=r->depth;d>0;d--){
  uint32_t id=r->path[d-1];
  if(xz_shlib_playlist_is_folder(usb[slot].lib,id)==1)return id;
  /* A shell-made playlist folder does not exist (NEW makes playlists): keep walking up. */
 }
 return 0;
}
static void list_rebuild(struct list_slot *ls) { ls->wanted++;ls->window_start=0;pthread_cond_signal(&wake); }
static void choose_source(void) {
 if(browse_source>=0&&usb[browse_source].lib)return;
 browse_source=usb[0].lib?0:usb[1].lib?1:-1;
}

/* Tracks loaded on either deck since the runtime started, per stick: the browse rows' PLAYED marks. */
#define PLAYED_CAP 1024
static struct { uint8_t kind; uint32_t id; } played[PLAYED_CAP];
static unsigned played_n;
static void played_add(uint8_t kind, uint32_t id) {
 for(unsigned i=0;i<played_n;i++)if(played[i].kind==kind&&played[i].id==id)return;
 if(played_n<PLAYED_CAP){played[played_n].kind=kind;played[played_n].id=id;played_n++;}
}
static int played_has(uint8_t kind, uint32_t id) {
 for(unsigned i=0;i<played_n;i++)if(played[i].kind==kind&&played[i].id==id)return 1;
 return 0;
}

/* ---- model slots ---- */
#define BF(f) (1u<<XZ_SHELL_BF_##f)
/* Row previews (BROWSE PREVIEW column): each track's PWV6 binned to PREVIEW_COLS, kept for the last
   PREVIEW_CACHE tracks shown. fill_browse lists the window's tracks that have none; the worker reads them one
   per pass. Entries are fixed storage under the mutex, so a row never points at freed memory. */
#define PREVIEW_COLS 96
#define PREVIEW_CACHE 96
static struct preview { int slot, state; uint32_t id, used, n; struct xz_shell_column col[PREVIEW_COLS]; } previews[PREVIEW_CACHE];
static struct { int slot; uint32_t id[XZ_SHELL_BROWSE_WINDOW]; int n; } preview_want; /* under mutex */
static uint32_t preview_clock;
static uint32_t preview_reads;
/* CUE/LOOP LIST of the highlighted track (BF_CUES): the analysis file's memory points with comments, kept
   for the last CUE_CACHE tracks; the worker reads a missing one. Fixed storage under the mutex. */
#define CUE_CACHE 4
#define CUE_POINTS 32
static struct cue_cache { int slot, state; uint32_t id, used; int n; struct xz_shana_cue c[CUE_POINTS]; } cue_cache[CUE_CACHE];
static struct xz_shell_cue_entry cue_entries[CUE_POINTS];
static struct { int pending, slot; uint32_t id; } cue_want;
static struct cue_cache *cue_find(int slot, uint32_t id) {
 for(int i=0;i<CUE_CACHE;i++)if(cue_cache[i].state&&cue_cache[i].slot==slot&&cue_cache[i].id==id)return &cue_cache[i];
 return NULL;
}
static struct preview *preview_find(int slot, uint32_t id) {
 for(int i=0;i<PREVIEW_CACHE;i++)if(previews[i].state&&previews[i].slot==slot&&previews[i].id==id)return &previews[i];
 return NULL;
}
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
  static const enum xz_shell_browse_column track_columns[5]={XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_TITLE,XZ_SHELL_COL_ARTIST,XZ_SHELL_COL_BPM,XZ_SHELL_COL_KEY};
  memcpy(browse.columns,track_columns,sizeof track_columns);browse.column_count=5;
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
 for(int i=0;i<browse.window_count;i++)if(rows[i].kind==XZ_SHELL_ROW_TRACK){
  /* PLAYED: loaded this session or marked PLAYED here, unless marked UNPLAYED here (sidecar). */
  const struct xz_shpl_set *e=&edits[browse_source];
  rows[i].played=(played_has((uint8_t)(2+browse_source),rows[i].id)||xz_shpl_list_has(e,XZ_SHPL_PLAYED,rows[i].id))&&
   !xz_shpl_list_has(e,XZ_SHPL_UNPLAYED,rows[i].id);
  rows[i].known|=1u<<XZ_SHELL_RF_PLAYED;
  rows[i].tagged=xz_shpl_list_has(e,XZ_SHPL_TAG_LIST,rows[i].id);rows[i].known|=1u<<XZ_SHELL_RF_TAGGED;
 }
 preview_want.slot=browse_source;preview_want.n=0;
 for(int i=0;i<browse.window_count;i++)if(rows[i].kind==XZ_SHELL_ROW_TRACK){
  struct preview *p=preview_find(browse_source,rows[i].id);
  if(!p){if(preview_want.n<XZ_SHELL_BROWSE_WINDOW)preview_want.id[preview_want.n++]=rows[i].id;continue;}
  p->used=++preview_clock;
  if(p->state>0){rows[i].preview=p->col;rows[i].preview_count=p->n;rows[i].known|=1u<<XZ_SHELL_RF_PREVIEW;}
 }
 if(preview_want.n)pthread_cond_signal(&wake);
 /* The highlighted track's memory points. */
 {int cursor=view>=0?shell.stack[view].cursor:-1;
  if(cursor>=0&&cursor<l->total&&l->kind[cursor]==XZ_SHELL_ROW_TRACK){
   uint32_t id=l->id[cursor];struct cue_cache *c=cue_find(browse_source,id);
   if(!c){if(!cue_want.pending||cue_want.id!=id||cue_want.slot!=browse_source){cue_want.pending=1;cue_want.slot=browse_source;cue_want.id=id;pthread_cond_signal(&wake);}}
   else{
    c->used=++preview_clock;
    if(c->state>0){
     for(int i=0;i<c->n;i++)cue_entries[i]=(struct xz_shell_cue_entry){c->c[i].loop,c->c[i].ms,c->c[i].end_ms,c->c[i].rgb,c->c[i].comment[0]?c->c[i].comment:NULL};
     browse.cue_track=id;browse.cues=cue_entries;browse.cue_count=c->n;browse.known|=BF(CUES);
    }
   }
  }}
 browse.rows=rows;
 /* Sort keys and column choices: every column the rows carry. Columns: the chosen mask in enum order. */
 browse.sort_keys=browse.column_choices=xz_shlib_sort_keys();browse.known|=BF(SORT_KEYS)|BF(COLUMN_CHOICES);
 if(column_mask&&!l->folders){
  browse.column_count=0;
  for(int c=0;c<XZ_SHELL_COL_COUNT&&browse.column_count<XZ_SHELL_BROWSE_COLUMNS;c++)
   if(((column_mask|(1u<<XZ_SHELL_COL_TITLE))>>c)&1u)browse.columns[browse.column_count++]=(enum xz_shell_browse_column)c;
 }
 /* Playlist editing: what this list allows, the edited marks and the ADD TO PLAYLIST targets. */
 if(model.capabilities&XZ_SHELL_CAP_PLAYLIST_EDIT&&l->category==XZ_SHELL_CAT_PLAYLIST&&ls->req.screen!=XZ_SHELL_SEARCH){
  if(l->folders)browse.edit=XZ_SHELL_EDIT_PLAYLISTS;
  else if(ls->req.depth>0){browse.edit=XZ_SHELL_EDIT_TRACKS;browse.edit_playlist=ls->req.path[ls->req.depth-1];}
  if(browse.edit)browse.known|=BF(EDIT);
 }
 for(int i=0;i<browse.window_count;i++)if(rows[i].kind==XZ_SHELL_ROW_FOLDER&&l->category==XZ_SHELL_CAT_PLAYLIST){
  const struct xz_shpl *e=xz_shpl_find(&edits[browse_source],rows[i].id);
  if(e&&(e->created||e->renamed||e->has_tracks))rows[i].known|=1u<<XZ_SHELL_RF_EDITED;
 }
 if(model.capabilities&XZ_SHELL_CAP_PLAYLIST_EDIT){
  int n=ref_count[browse_source];
  for(int i=0;i<n;i++)playlist_refs[i]=(struct xz_shell_playlist_ref){refs[browse_source][i].id,refs[browse_source][i].name,refs[browse_source][i].tracks};
  browse.playlists=playlist_refs;browse.playlist_count=n;browse.known|=BF(PLAYLISTS);
 }
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
 /* LINK and COMPUTER are not browsed here: they open in the deck's own screen through the stock key, when
    ui_runtime.c holds its handler. */
 for(int which=0;which<2;which++)if(xz_ui_runtime_stock_source_key&&xz_ui_runtime_stock_source_key(which,0)==1){
  struct xz_shell_device *d=&source_model.device[which?XZ_SHELL_SRC_COMPUTER-1:XZ_SHELL_SRC_LINK-1];
  d->known=1u<<XZ_SHELL_D_STATUS;d->status=XZ_SHELL_DEVICE_STOCK;
 }
 if(browse_source>=0){source_model.current_known=1;source_model.current=browse_source?XZ_SHELL_SRC_USB2:XZ_SHELL_SRC_USB1;}
}
static void fill_settings(void) {
 const struct xz_shell_settings *s=&model.settings;
 setting_items[0]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"TIME MODE",time_choices,2,
  s->time_mode==XZ_SHELL_TIME_ELAPSED,(s->known>>XZ_SHELL_SET_TIME_MODE)&1,xz_shrt_setting_writable(XZ_SHELL_SET_TIME_MODE,has(G_TIME_MODE),0)};
 setting_items[1]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_AUTO_CUE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"AUTO CUE",off_on,2,
  !!s->auto_cue,(s->known>>XZ_SHELL_SET_AUTO_CUE)&1,0};
 setting_notes[0]=NULL;setting_notes[1]=NULL;
 /* The stock UTILITY items in stock's order. QUANTIZE BEAT VALUE is one of them now (same id as before). */
 xz_shut_items(xz_shut_defs,XZ_SHUT_DEFS,util.raw,util.known,util.get_ok,util.set_ok,util.trial,setting_items+2,setting_notes+2);
 int n=2+XZ_SHUT_DEFS;
 if(util.version[0]){
  util.version_choice[0]=util.version;
  setting_items[n]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_VERSION,XZ_SHELL_SETTING_CAT_INFO,"VERSION No.",util.version_choice,1,0,1,0};
  setting_notes[n++]=NULL;
 }
 settings_model=(struct xz_shell_settings_model){(uint32_t)n,setting_items,setting_notes};
 /* NEEDLE LOCK for the play screen (an overview touch does not seek while it is set and the deck plays). */
 int nl=xz_shut_find(xz_shut_defs,XZ_SHUT_DEFS,XZ_SHELL_ITEM_NEEDLE_LOCK);
 model.settings.needle_lock_known=nl>=0&&util.known[nl];
 model.settings.needle_lock=nl>=0&&util.known[nl]&&util.raw[nl]==1;
}
static int util_writable_any(void) {
 for(int i=0;i<XZ_SHUT_DEFS;i++)if(!xz_shut_reason(&xz_shut_defs[i],util.get_ok[i],util.set_ok[i],util.trial))return 1;
 return 0;
}
/* Rebuilds every slot the runtime owns. Called after each refresh and after any swap, so the model
   never points at retired memory. */
static void fill_slots_locked(void) {
 choose_source();
 struct xz_shlib *lib=browse_source>=0?usb[browse_source].lib:NULL;
 uint32_t caps=XZ_SHELL_CAP_SOURCE;
 if(has(G_TIME_MODE)||has(G_QUANTIZE)||util_writable_any())caps|=XZ_SHELL_CAP_SET_SETTING;
 if(lib)caps|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH;
 if(lib&&load_enabled&&has(G_LOAD))caps|=XZ_SHELL_CAP_LOAD;
 if(lib)caps|=XZ_SHELL_CAP_FILTER;
 if(lib&&!usb[browse_source].rdonly&&usb[browse_source].root[0])caps|=XZ_SHELL_CAP_PLAYLIST_EDIT|XZ_SHELL_CAP_TAG_EDIT|XZ_SHELL_CAP_HISTORY_EDIT;
 if(last_input.stem_muted_known[0]||last_input.stem_muted_known[1])caps|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;
 model.capabilities=caps;
 model.settings.writable=0;
 for(int id=0;id<XZ_SHELL_SETTING_COUNT;id++)
  if(xz_shrt_setting_writable(id,has(G_TIME_MODE),has(G_QUANTIZE)))model.settings.writable|=(uint64_t)1<<id;
 if(lib){fill_browse(lib);model.browse=&browse;}else model.browse=NULL;
 fill_source();model.source=&source_model;
 fill_settings();model.settings_screen=&settings_model;
 fill_look();fill_buttons();model.mods=&mods_model;model.help=&help_model;
 model.my_settings=NULL;
 if(lib&&browse_source>=0){
  const struct xz_shms_files *f=&mys[browse_source];
  int writable=!mys_readonly&&!usb[browse_source].rdonly&&xz_shms_any(f);
  xz_shms_rows(f,writable,&mys_rows);
  mys_stored=(struct xz_shell_settings_model){(uint32_t)mys_rows.count,mys_rows.item,NULL};
  mys_model=(struct xz_shell_my_settings_model){.source=browse_source?XZ_SHELL_SRC_USB2:XZ_SHELL_SRC_USB1,
   .source_name=browse_source?"USB 2":"USB 1",.on_stick=xz_shms_any(f),.writable=writable,.stored=&mys_stored,
   .profile_active=-1,.waveform=&wave_model,.waveform_tab=LOOK_TAB,.state=mys_state[browse_source],.result=mys_result[browse_source][0]?mys_result[browse_source]:NULL};
  /* DJ profiles of this stick, from dj_runtime's view (last_input). */
  int pn=0;
  if(last_input.dj_known)for(int i=0;i<last_input.dj_count&&i<XZ_SHELL_RUNTIME_DJ;i++){
   const struct xz_shell_runtime_dj *e=&last_input.dj[i];
   if(e->usb!=browse_source)continue;
   mys_profiles[pn]=(struct xz_shell_dj_profile){e->name,e->has_accent,e->accent,e->damaged};
   if(e->stick_active)mys_model.profile_active=pn;
   if(e->locked)mys_model.profiles_switchable=0;else mys_model.profiles_switchable=1;
   mys_profile_entry[pn++]=i;
  }
  mys_model.profile_count=pn;mys_model.profiles=pn?mys_profiles:NULL;
  if(!pn)mys_model.profile_active=-1;
  model.my_settings=&mys_model;
 }
 fill_info();model.info=&info_model;
 __atomic_store_n(&xz_mods_shell_proof_v1.capabilities,caps,__ATOMIC_RELAXED);
}

/* ---- waves (worker -> model thread) ---- */
/* A deck's wave job runs in two stages on the worker: the 3-band wave (published at once), then the
   beat grid (retried while the engine still loads it) and the stem layer. Each stage posts its parts to
   wave_ready; the model thread applies them on its next refresh. Times are monotonic. */
#define WAVE_RETRY_MS 50u      /* reader still on the previous file, or the analysis not readable yet */
#define WAVE_GIVE_UP_MS 8000u
#define GRID_RETRY_MS 250u
#define GRID_GIVE_UP_MS 6000u
enum { PART_WAVE=1u, PART_GRID=2u, PART_STEMS=4u, PART_CUES=8u };
static struct {
 uint32_t wanted, done, retry_at;   /* wanted is bumped by the model thread on a new track */
 uint64_t requested_us;             /* when wanted was bumped (the model saw the new track) */
 /* worker only */
 uint32_t running_gen; int stage, grid_done, stems_done, cues_done; uint64_t ready_us;
} wave_job[XZ_SHELL_DECKS];
static struct {
 uint32_t gen; unsigned parts;
 struct xz_shell_column *scroll, *overview, *stems; uint32_t scroll_n, overview_n, span, stems_n, stems_span;
 struct xz_shell_beat *grid; uint32_t grid_n;
 struct xz_shell_cue cues[XZ_SHELL_MEMORY_CUES]; int cues_n;
} wave_ready[XZ_SHELL_DECKS];
/* The deck's memory cues as published to the model (model thread); -1 = not read. */
static struct xz_shell_cue memory_cache[XZ_SHELL_DECKS][XZ_SHELL_MEMORY_CUES];
static int memory_count_cache[XZ_SHELL_DECKS]={-1,-1};
static struct xz_shell_beat grid_cache[XZ_SHELL_DECKS][GRID_CAP];
static uint32_t grid_count[XZ_SHELL_DECKS];
static char wave_path[XZ_SHELL_DECKS][XZ126_READER_PATH_MAX]; /* worker only: the file whose wave is published */
static void free_wave(int d) {
 free(wave_ready[d].scroll);free(wave_ready[d].overview);free(wave_ready[d].stems);free(wave_ready[d].grid);
 memset(&wave_ready[d],0,sizeof wave_ready[d]);
}
static void apply_waves_locked(void) {
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  if(!wave_ready[d].gen)continue;
  if(wave_ready[d].gen==wave_job[d].wanted){
   unsigned p=wave_ready[d].parts;
   if(p&PART_WAVE)xz_shell_model_set_wave(storage,d,wave_ready[d].scroll,wave_ready[d].scroll_n,wave_ready[d].span,
                                          wave_ready[d].overview,wave_ready[d].overview_n,wave_ready[d].span);
   if(p&PART_GRID){
    grid_count[d]=0;
    if(wave_ready[d].grid&&wave_ready[d].grid_n<=GRID_CAP){
     memcpy(grid_cache[d],wave_ready[d].grid,wave_ready[d].grid_n*sizeof *wave_ready[d].grid);grid_count[d]=wave_ready[d].grid_n;
    }
   }
   if(p&PART_STEMS)xz_shell_model_set_stems(storage,d,wave_ready[d].stems,wave_ready[d].stems_n,wave_ready[d].stems_span);
   if(p&PART_CUES){memcpy(memory_cache[d],wave_ready[d].cues,sizeof memory_cache[d]);memory_count_cache[d]=wave_ready[d].cues_n;}
  }
  free_wave(d);
 }
}
static int post_cues(int d, uint32_t gen, const struct xz_shell_cue *cues, int count) {
 pthread_mutex_lock(&mutex);
 int current=gen==wave_job[d].wanted;
 if(current){
  if(wave_ready[d].gen!=gen)free_wave(d);
  wave_ready[d].gen=gen;memcpy(wave_ready[d].cues,cues,sizeof wave_ready[d].cues);wave_ready[d].cues_n=count;
  wave_ready[d].parts|=PART_CUES;dirty_flag=1;
 }
 pthread_mutex_unlock(&mutex);
 return current;
}
/* Worker: hands parts to the model thread, merging with parts it has not applied yet. Takes ownership
   of the arrays; returns 0 when the job is stale (a newer track) and the arrays were freed. */
static int post_parts(int d, uint32_t gen, unsigned parts, struct xz_shell_column *scroll, struct xz_shell_column *overview,
                      uint32_t scroll_n, uint32_t overview_n, uint32_t span, struct xz_shell_beat *grid, uint32_t grid_n,
                      struct xz_shell_column *stems, uint32_t stems_n, uint32_t stems_span) {
 pthread_mutex_lock(&mutex);
 int current=gen==wave_job[d].wanted;
 if(current){
  if(wave_ready[d].gen!=gen)free_wave(d);
  wave_ready[d].gen=gen;
  if(parts&PART_WAVE){
   free(wave_ready[d].scroll);free(wave_ready[d].overview);
   wave_ready[d].scroll=scroll;wave_ready[d].overview=overview;wave_ready[d].scroll_n=scroll_n;wave_ready[d].overview_n=overview_n;
   wave_ready[d].span=span;scroll=overview=NULL;
  }
  if(parts&PART_GRID){free(wave_ready[d].grid);wave_ready[d].grid=grid;wave_ready[d].grid_n=grid_n;grid=NULL;}
  if(parts&PART_STEMS){free(wave_ready[d].stems);wave_ready[d].stems=stems;wave_ready[d].stems_n=stems_n;wave_ready[d].stems_span=stems_span;stems=NULL;}
  wave_ready[d].parts|=parts;dirty_flag=1;
 }
 pthread_mutex_unlock(&mutex);
 free(scroll);free(overview);free(grid);free(stems);
 return current;
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
/* ---- analysis lookup (worker only) ----
   Per mounted USB (shell_analysis.c): the track -> analysis folder index and the track -> stem bundle map,
   built when the stick appears. They are rebuilt only when the stick's analysis signature changes (its
   USBANLZ folders or stem index), never because export.pdb changed (the deck writes its history there)
   and never on a plain miss: a miss checks the signature at most every SIGNATURE_CHECK_MS. */
#define SIGNATURE_CHECK_MS 5000u
static struct {
 struct xz_shana *index; struct xz_shana_stems *stems; char root[1024]; uint64_t signature;
 uint64_t built_us, build_us, stems_us, checked_us; uint32_t files, builds;
} analysis[2];
static void analysis_build(int slot, const char *why) {
 uint64_t t=mono_us();struct xz_shana *next=NULL;struct xz_shana_stems *stems=NULL;uint32_t files=0;
 analysis[slot].signature=xz_shana_signature(analysis[slot].root);
 if(!analysis[slot].root[0]||xz_shana_build(analysis[slot].root,&next,&files))next=NULL;
 uint64_t t1=mono_us();
 if(analysis[slot].root[0]&&xz_shana_stems_build(analysis[slot].root,&stems))stems=NULL;
 xz_shana_free(analysis[slot].index);analysis[slot].index=next;
 xz_shana_stems_free(analysis[slot].stems);analysis[slot].stems=stems;
 analysis[slot].built_us=analysis[slot].checked_us=mono_us();analysis[slot].build_us=t1-t;analysis[slot].stems_us=analysis[slot].built_us-t1;
 analysis[slot].files=files;analysis[slot].builds++;
 if(debug){char line[1200];snprintf(line,sizeof line,"SHELL_ANALYSIS: usb%d reason=%s root=%s entries=%u files=%u build_ms=%u stem_bundles=%u stem_index_ms=%u builds=%u",
  slot+1,why,analysis[slot].root,xz_shana_count(next),files,(unsigned)(analysis[slot].build_us/1000u),xz_shana_stems_count(stems),
  (unsigned)(analysis[slot].stems_us/1000u),analysis[slot].builds);xz_log(line);}
}
static void analysis_drop(int slot) {
 xz_shana_free(analysis[slot].index);xz_shana_stems_free(analysis[slot].stems);memset(&analysis[slot],0,sizeof analysis[slot]);
}
/* After a miss: rebuild only if the stick's analysis really changed. Returns 1 when it rebuilt. */
static int analysis_recheck(int slot) {
 uint64_t now=mono_us();
 if(now-analysis[slot].checked_us<(uint64_t)SIGNATURE_CHECK_MS*1000u)return 0;
 analysis[slot].checked_us=now;
 if(xz_shana_signature(analysis[slot].root)==analysis[slot].signature)return 0;
 analysis_build(slot,"analysis_changed");return 1;
}
static int analysis_slot(const char *root) {
 for(int i=0;i<2;i++)if(analysis[i].root[0]&&!strcmp(analysis[i].root,root))return i;
 return -1;
}
/* 0 and the 3-band data for an absolute track path. *built_ms: index build time spent in this call. */
static int resolve_three_band(const char *source, struct xz_wave_load *w, uint32_t *built_ms, int *slot_out) {
 char root[1024],dir[1024];const char *track;*built_ms=0;*slot_out=-1;
 if(xz_shana_split(source,root,sizeof root,&track))return -1;
 int slot=analysis_slot(root);
 *slot_out=slot;
 if(slot<0)return xz_wave_load_three_band(source,w); /* not a mounted rekordbox USB we index: the old path */
 if(!analysis[slot].index){analysis_build(slot,"first_use");*built_ms+=(uint32_t)(analysis[slot].build_us/1000u);}
 if(!xz_shana_find(analysis[slot].index,track,dir,sizeof dir))return xz_shana_three_band(dir,w);
 if(analysis_recheck(slot)){
  *built_ms+=(uint32_t)(analysis[slot].build_us/1000u);
  if(!xz_shana_find(analysis[slot].index,track,dir,sizeof dir))return xz_shana_three_band(dir,w);
 }
 return -1;
}
/* The stem layer for an absolute track path: the stick's stem map, then only the three PWV7 sections.
   Falls back to the stem loader in layered_wave_source.c for a root this module does not index.
   split_ms: [0] bundle lookup, [1..3] drums, harmonics, vocal reads. */
static int resolve_stems(const char *source, struct xz_wave_load *w, uint32_t split_ms[4]) {
 char root[1024],folder[1024];const char *track;memset(split_ms,0,4*sizeof *split_ms);
 if(xz_shana_split(source,root,sizeof root,&track))return -1;
 int slot=analysis_slot(root);
 if(slot<0){uint64_t t=mono_us();int rc=xz_wave_load_stems(source,w);split_ms[0]=(uint32_t)((mono_us()-t)/1000u);return rc;}
 if(!analysis[slot].index)analysis_build(slot,"first_use");
 uint64_t t=mono_us();
 int found=!xz_shana_stems_find(analysis[slot].stems,track,folder,sizeof folder);
 if(!found&&analysis_recheck(slot))found=!xz_shana_stems_find(analysis[slot].stems,track,folder,sizeof folder);
 split_ms[0]=(uint32_t)((mono_us()-t)/1000u);
 if(!found)return -1;
 return xz_shana_stems_load(folder,w,split_ms+1);
}
/* 3-band columns from a load, binned for the lane and the overview. */
struct binned { struct xz_shell_column *scroll, *overview; uint32_t scroll_n, overview_n, span; };
static int bin_three_band(const struct xz_wave_load *w, struct binned *b) {
 uint32_t peak=band_peak(w->bands,w->count);
 b->scroll=bin_columns(w->bands,w->count,WAVE_SCROLL_CAP,peak,&b->scroll_n);
 b->overview=bin_columns(w->bands,w->count,WAVE_OVERVIEW,peak,&b->overview_n);
 b->span=(uint32_t)((uint64_t)w->count*1000u/PWV7_PER_SECOND);
 if(!b->scroll||!b->overview){free(b->scroll);free(b->overview);memset(b,0,sizeof *b);return -1;}
 return 0;
}
/* LOAD prefetch: the shell knows the track before stock loads it, so the worker reads its 3-band wave
   while the deck loads. It is used only once the deck's reader holds exactly that file. */
static struct { int pending, slot, deck; uint32_t id; uint64_t posted_us; } hint[XZ_SHELL_DECKS]; /* under mutex */
static int scan_soon; /* under mutex: a LOAD did not land; check the sticks now, not in a second */
static struct {
 char path[XZ126_READER_PATH_MAX]; struct binned b; uint64_t posted_us; uint32_t read_ms;
 char stems_path[XZ126_READER_PATH_MAX]; struct xz_shell_column *stems; uint32_t stems_n, stems_span, stems_split[4];
} prefetch[XZ_SHELL_DECKS]; /* worker */
static void prefetch_drop(int d) { free(prefetch[d].b.scroll);free(prefetch[d].b.overview);free(prefetch[d].stems);memset(&prefetch[d],0,sizeof prefetch[d]); }
static struct xz_shell_column *stem_columns(const char *source, uint32_t *n, uint32_t *span, uint32_t split_ms[4]);
static void run_prefetch(int d, int slot, uint32_t id, uint64_t posted_us) {
 prefetch_drop(d);
 struct xz_shlib *lib=usb[slot].lib; /* only this thread frees libraries */
 const struct xz_shlib_track *t=lib?xz_shlib_track(lib,id):NULL;
 const char *rel=t?xz_shlib_text(lib,t->file_path):NULL;
 if(!rel||!rel[0]||!analysis[slot].root[0])return;
 char source[XZ126_READER_PATH_MAX];
 if(snprintf(source,sizeof source,"%s%s",analysis[slot].root,rel)>=(int)sizeof source)return;
 uint64_t t0=mono_us();struct xz_wave_load w;uint32_t built;int s;
 if(resolve_three_band(source,&w,&built,&s))return;
 int ok=!bin_three_band(&w,&prefetch[d].b);xz_wave_load_free(&w);
 if(!ok)return;
 snprintf(prefetch[d].path,sizeof prefetch[d].path,"%s",source);
 prefetch[d].posted_us=posted_us;prefetch[d].read_ms=(uint32_t)((mono_us()-t0)/1000u);
 /* The stem layer too, while the deck is still loading. */
 prefetch[d].stems=stem_columns(source,&prefetch[d].stems_n,&prefetch[d].stems_span,prefetch[d].stems_split);
 if(prefetch[d].stems)snprintf(prefetch[d].stems_path,sizeof prefetch[d].stems_path,"%s",source);
}
static struct xz_shell_column *stem_columns(const char *source, uint32_t *n, uint32_t *span, uint32_t split_ms[4]) {
 struct xz_wave_load w;*n=0;*span=0;
 if(resolve_stems(source,&w,split_ms))return NULL;
 uint32_t peak=band_peak(w.bands,w.count);
 struct xz_shell_column *c=bin_columns(w.bands,w.count,WAVE_SCROLL_CAP,peak,n);
 *span=(uint32_t)((uint64_t)w.count*1000u/PWV7_PER_SECOND);
 xz_wave_load_free(&w);return c;
}
/* Worker. One step of deck d's job; returns 1 when the job is finished, 0 to come back at retry_at. */
static int wave_step(int d, uint32_t gen, uint64_t requested_us, uint32_t *retry_ms) {
 struct xz126_deck_source s,after;
 uint64_t now=mono_us();uint32_t age_ms=(uint32_t)((now-requested_us)/1000u);
 *retry_ms=WAVE_RETRY_MS;
 if(wave_job[d].running_gen!=gen){wave_job[d].running_gen=gen;wave_job[d].stage=0;wave_job[d].grid_done=wave_job[d].stems_done=wave_job[d].cues_done=0;}
 if(!deck_source((unsigned)d,&s)||s.path[0]!='/')return age_ms>WAVE_GIVE_UP_MS;
 if(wave_job[d].stage==0){
  /* The reader can still hold the previous file for a moment after the model sees the new track. */
  if(!strcmp(s.path,wave_path[d])&&age_ms<5000u)return 0;
  struct binned b;memset(&b,0,sizeof b);uint32_t resolve_ms=0,read_ms=0,built=0;int used_prefetch=0,slot=-1;
  uint64_t posted_us=0;
  if(prefetch[d].path[0]&&!strcmp(prefetch[d].path,s.path)){
   b=prefetch[d].b;memset(&prefetch[d].b,0,sizeof prefetch[d].b);used_prefetch=1;read_ms=prefetch[d].read_ms;posted_us=prefetch[d].posted_us;
  }else{
   uint64_t t0=mono_us();struct xz_wave_load w;
   if(resolve_three_band(s.path,&w,&built,&slot)){
    __atomic_add_fetch(&xz_mods_shell_proof_v1.wave_failures,1,__ATOMIC_RELAXED);
    return age_ms>WAVE_GIVE_UP_MS;
   }
   uint64_t t1=mono_us();resolve_ms=(uint32_t)((t1-t0)/1000u);
   int ok=!bin_three_band(&w,&b);xz_wave_load_free(&w);read_ms=(uint32_t)((mono_us()-t1)/1000u);
   if(!ok)return 1;
  }
  free(prefetch[d].b.scroll);free(prefetch[d].b.overview);memset(&prefetch[d].b,0,sizeof prefetch[d].b);prefetch[d].path[0]=0;
  if(!deck_source((unsigned)d,&after)||strcmp(after.path,s.path)){free(b.scroll);free(b.overview);return 0;}
  uint32_t cols=b.scroll_n,span=b.span;
  if(!post_parts(d,gen,PART_WAVE,b.scroll,b.overview,b.scroll_n,b.overview_n,b.span,NULL,0,NULL,0,0))return 1;
  memcpy(wave_path[d],s.path,sizeof wave_path[d]);
  wave_job[d].stage=1;wave_job[d].ready_us=mono_us();
  __atomic_add_fetch(&xz_mods_shell_proof_v1.wave_loads,1,__ATOMIC_RELAXED);
  if(debug){
   char line[260];
   snprintf(line,sizeof line,"SHELL_WAVE: deck=%d columns=%u span_ms=%u request_to_ready_ms=%u resolve_ms=%u read_ms=%u index_build_ms=%u "
    "prefetched=%d load_to_ready_ms=%d usb=%d",d+1,cols,span,(unsigned)((wave_job[d].ready_us-requested_us)/1000u),resolve_ms,read_ms,built,
    used_prefetch,posted_us?(int)((wave_job[d].ready_us-posted_us)/1000u):-1,slot+1);
   xz_log(line);
  }
  *retry_ms=0;return 0; /* the grid and stems next, without waiting */
 }
 /* Stage 1: same file still loaded, else a new job follows. */
 if(strcmp(s.path,wave_path[d]))return 1;
 if(!wave_job[d].grid_done){
  struct xz_shell_beat *grid=NULL;uint32_t grid_n=0;
  if(read_grid((unsigned)d,&grid,&grid_n)){
   __atomic_add_fetch(&xz_mods_shell_proof_v1.grid_loads,1,__ATOMIC_RELAXED);
   if(!post_parts(d,gen,PART_GRID,NULL,NULL,0,0,0,grid,grid_n,NULL,0,0))return 1;
   wave_job[d].grid_done=1;
   if(debug){char line[120];snprintf(line,sizeof line,"SHELL_GRID: deck=%d beats=%u request_to_ready_ms=%u",d+1,grid_n,(unsigned)((mono_us()-requested_us)/1000u));xz_log(line);}
  }else if(age_ms>GRID_GIVE_UP_MS){
   wave_job[d].grid_done=1;
   if(debug){char line[80];snprintf(line,sizeof line,"SHELL_GRID: deck=%d unavailable after_ms=%u",d+1,age_ms);xz_log(line);}
  }
 }
 if(!wave_job[d].cues_done){
  /* Memory cues from the same analysis folder the 3-band wave came from. */
  char root[1024],dir[1024];const char *track;int slot,count=-1;struct xz_shell_cue cues[XZ_SHELL_MEMORY_CUES];
  memset(cues,0,sizeof cues);
  if(!xz_shana_split(s.path,root,sizeof root,&track)&&(slot=analysis_slot(root))>=0&&analysis[slot].index&&
     !xz_shana_find(analysis[slot].index,track,dir,sizeof dir))count=xz_shana_memory_cues(dir,cues);
  wave_job[d].cues_done=1;
  if(count>=0&&!post_cues(d,gen,cues,count))return 1;
  if(debug){char line[120];snprintf(line,sizeof line,"SHELL_CUES: deck=%d memory_cues=%d",d+1,count);xz_log(line);}
 }
 if(!wave_job[d].stems_done){
  uint64_t t0=mono_us();uint32_t n=0,span=0,split[4]={0,0,0,0};int prefetched=0;
  struct xz_shell_column *stems=NULL;
  if(prefetch[d].stems_path[0]&&!strcmp(prefetch[d].stems_path,s.path)){
   stems=prefetch[d].stems;n=prefetch[d].stems_n;span=prefetch[d].stems_span;memcpy(split,prefetch[d].stems_split,sizeof split);
   prefetch[d].stems=NULL;prefetch[d].stems_path[0]=0;prefetched=1;
  }else stems=stem_columns(s.path,&n,&span,split);
  wave_job[d].stems_done=1;
  if(stems&&!post_parts(d,gen,PART_STEMS,NULL,NULL,0,0,0,NULL,0,stems,n,span))return 1;
  if(debug){char line[240];snprintf(line,sizeof line,"SHELL_STEM_WAVE: deck=%d present=%d columns=%u read_ms=%u lookup_ms=%u drums_ms=%u harmonics_ms=%u vocal_ms=%u "
   "prefetched=%d request_to_ready_ms=%u",d+1,stems!=NULL,n,(unsigned)((mono_us()-t0)/1000u),split[0],split[1],split[2],split[3],prefetched,
   (unsigned)((mono_us()-requested_us)/1000u));xz_log(line);}
 }
 if(!wave_job[d].grid_done){*retry_ms=GRID_RETRY_MS;return 0;}
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
/* The export must sit on a mounted filesystem under /media/usbN, per /proc/mounts: a stick that is gone
   must never keep serving a list, even if something is left under the mount point. Writes the mount
   source (e.g. /dev/sda1). 1 = mounted, 0 = not, -1 = /proc/mounts unreadable (then trusted). */
static int mounted(const char *root, char *source, size_t cap) {
 FILE *f=fopen("/proc/mounts","r");
 if(!f)return -1;
 char line[1024],dev[512],point[512];size_t best=0;int found=0;
 if(cap)source[0]=0;
 while(fgets(line,sizeof line,f)){
  /* Two space-separated fields (no sscanf: it binds __isoc99_sscanf, GLIBC_2.7, above the deck's ceiling). */
  char *s=line,*e=strchr(s,' ');if(!e||(size_t)(e-s)>=sizeof dev)continue;
  memcpy(dev,s,(size_t)(e-s));dev[e-s]=0;s=e+1;e=strchr(s,' ');if(!e||(size_t)(e-s)>=sizeof point)continue;
  memcpy(point,s,(size_t)(e-s));point[e-s]=0;
  size_t n=strlen(point);
  if(n<=7||strncmp(point,"/media/",7)||strncmp(root,point,n)||(root[n]&&root[n]!='/'))continue;
  if(n>best){best=n;found=1;if(cap)snprintf(source,cap,"%s",dev);}
 }
 fclose(f);
 return found;
}
/* Everything the worker holds for a stick that went away: prefetched waves for decks that came from it. */
static void forget_root(const char *root) {
 size_t n=strlen(root);
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  if(!n)continue;
  if((prefetch[d].path[0]&&!strncmp(prefetch[d].path,root,n))||(prefetch[d].stems_path[0]&&!strncmp(prefetch[d].stems_path,root,n)))prefetch_drop(d);
 }
}
static void scan_usb(void) {
 static char seen_path[2][1024];static struct stat seen[2];
 for(unsigned slot=0;slot<2;slot++){
  char path[1024]={0},mount_source[256]="";struct stat st;memset(&st,0,sizeof st);
  int found=find_database(slot,path,&st),mount=-1;
  if(found){
   char at[1024];snprintf(at,sizeof at,"%s",path);
   char *cut=strstr(at,"/PIONEER/rekordbox/export.pdb");if(cut)*cut=0;
   mount=mounted(at,mount_source,sizeof mount_source);
   if(!mount)found=0; /* not a mounted stick: treated exactly as absent */
  }
  if(found&&!strcmp(path,seen_path[slot])&&same_file(&st,&seen[slot]))continue;
  if(!found&&!seen_path[slot][0])continue;
  struct xz_shlib *next=NULL;uint64_t free_bytes=0,total_bytes=0;int space=0,rdonly=0;char root[1024]="";
  if(found){
   pthread_mutex_lock(&mutex);usb[slot].status=USB_LOADING;dirty_flag=1;pthread_mutex_unlock(&mutex);
   struct stat after;
   if(xz_shlib_open(path,LIBRARY_ARENA,&next)||!safe_file(path,&after)||!same_file(&st,&after)){
    xz_shlib_free(next);next=NULL;__atomic_add_fetch(&xz_mods_shell_proof_v1.library_errors,1,__ATOMIC_RELAXED);
   }
   snprintf(root,sizeof root,"%s",path);
   char *cut=strstr(root,"/PIONEER/rekordbox/export.pdb");if(cut)*cut=0;
   struct statvfs vfs;
   if(!statvfs(root,&vfs)){free_bytes=(uint64_t)vfs.f_bavail*vfs.f_frsize;total_bytes=(uint64_t)vfs.f_blocks*vfs.f_frsize;space=1;rdonly=!!(vfs.f_flag&ST_RDONLY);}
  }
  pthread_mutex_lock(&mutex);
  if(usb[slot].lib&&retired.libs<4)retired.lib[retired.libs++]=usb[slot].lib;
  usb[slot].lib=next;usb[slot].status=next?USB_READY:found?USB_ERROR:USB_NONE;
  for(int i=0;i<PREVIEW_CACHE;i++)if(previews[i].slot==(int)slot)previews[i].state=0; /* ids belong to the old export */
  for(int i=0;i<CUE_CACHE;i++)if(cue_cache[i].slot==(int)slot)cue_cache[i].state=0;
  if(cue_want.slot==(int)slot)cue_want.pending=0;
  memset(&mys[slot],0,sizeof mys[slot]);if(mys_job.slot==(int)slot)mys_job.pending=0;
  mys_state[slot]=XZ_SHELL_MY_READY;mys_result[slot][0]=0;
  usb[slot].tracks=xz_shlib_track_count(next);usb[slot].space_known=space;
  usb[slot].free_bytes=free_bytes;usb[slot].total_bytes=total_bytes;usb[slot].rdonly=rdonly;
  snprintf(usb[slot].root,sizeof usb[slot].root,"%s",next?root:"");
  /* This stick's playlist edits: read from its sidecar below, or gone with it. */
  xz_shpl_free(&edits[slot]);ref_count[slot]=0;
  xz_mods_shell_proof_v1.library_tracks[slot]=usb[slot].tracks;
  for(int k=0;k<L_COUNT;k++)if(lists[k].source==(int)slot){retire_list(lists[k].result);lists[k].result=NULL;lists[k].requested=0;}
  if(browse_source==(int)slot&&!next)browse_source=-1;
  for(int d=0;d<XZ_SHELL_DECKS;d++)if(!next&&hint[d].pending&&hint[d].slot==(int)slot)hint[d].pending=0;
  if(refreshed)fill_slots_locked();
  dirty_flag=1;
  pthread_mutex_unlock(&mutex);
  drain_retired();
  /* The analysis index and stem map belong to the stick, not to export.pdb (the deck writes its history
     there): keep them while the same stick stays mounted with the same analysis, rebuild on a new
     mount or changed analysis, drop them when it is gone. */
  const char *why="unmounted";
  if(next&&!strcmp(analysis[slot].root,root)&&analysis[slot].index&&xz_shana_signature(root)==analysis[slot].signature)why="kept";
  else if(next){
   why=analysis[slot].root[0]&&!strcmp(analysis[slot].root,root)?"analysis_changed":"mounted";
   forget_root(analysis[slot].root);analysis_drop((int)slot);
   snprintf(analysis[slot].root,sizeof analysis[slot].root,"%s",root);analysis_build((int)slot,why);
  }else{forget_root(analysis[slot].root);analysis_drop((int)slot);}
  if(next){
   struct xz_shpl_set loaded_edits;
   int rc=xz_shpl_load(&loaded_edits,root);
   pthread_mutex_lock(&mutex);
   if(!rc&&usb[slot].lib==next){xz_shpl_free(&edits[slot]);edits[slot]=loaded_edits;xz_shpl_init(&loaded_edits);}
   pthread_mutex_unlock(&mutex);
   static struct xz_shms_files read_back;int files=xz_shms_read(root,&read_back);
   pthread_mutex_lock(&mutex);
   if(usb[slot].lib==next){mys[slot]=read_back;if(refreshed)fill_slots_locked();dirty_flag=1;}
   if(debug){char line[200];snprintf(line,sizeof line,"SHELL_MYSETTINGS: usb%u files=%d states=%d/%d/%d/%d readonly=%d",slot+1,files,
    read_back.state[MS_PLAYER],read_back.state[MS_PLAYER2],read_back.state[MS_MIXER],read_back.state[MS_DEVICE],mys_readonly);xz_log(line);}
   pthread_mutex_unlock(&mutex);
   xz_shpl_free(&loaded_edits);
   if(debug){char line[200];snprintf(line,sizeof line,"SHELL_PLAYLISTS: usb%u sidecar=%s edited=%u readonly=%d",slot+1,rc?"unreadable":"ok",edits[slot].n,rdonly);xz_log(line);}
   publish_refs((int)slot);
  }
  /* A failed open is retried when the file changes, not every second. */
  snprintf(seen_path[slot],sizeof seen_path[slot],"%s",found?path:"");seen[slot]=st;
  if(debug){char line[1400];snprintf(line,sizeof line,"SHELL_LIBRARY: usb%u %s tracks=%u mount=%d source=%s analysis=%s",slot+1,next?path:"none",
   usb[slot].tracks,mount,mount_source[0]?mount_source:"-",why);xz_log(line);}
 }
}

/* Worker: one playlist edit. The stick's own track order is read here (the reader is worker-only), the
   edit applies to the authoritative set under the lock, the set is written to the stick atomically, and
   on a refused write the set goes back to what is on the stick and the shell says why. */
static void publish_refs(int slot) {
 struct xz_shpl_set copy;struct xz_shlib_playlist_ref out[PL_REFS];int n=0;
 pthread_mutex_lock(&mutex);struct xz_shlib *lib=usb[slot].lib;int ok=!xz_shpl_copy(&copy,&edits[slot]);pthread_mutex_unlock(&mutex);
 if(!ok)return;
 if(lib)n=xz_shlib_playlist_refs(lib,&copy,out,PL_REFS);
 xz_shpl_free(&copy);
 pthread_mutex_lock(&mutex);
 if(usb[slot].lib==lib){memcpy(refs[slot],out,(size_t)n*sizeof *out);ref_count[slot]=n;if(refreshed)fill_slots_locked();dirty_flag=1;}
 pthread_mutex_unlock(&mutex);
}
/* TAG LIST and HISTORY edits: applied to a copy of the stick's sidecar set, saved, then swapped in (or
   dropped with a notice when the stick refused the write). Worker only. */
static void run_list_edit(const struct list_edit *j) {
 struct xz_shlib *lib=usb[j->slot].lib;
 if(!lib)return;
 uint32_t *ids=NULL;uint32_t n=0;
 if(j->kind==LE_TAG&&j->op==XZ_SHELL_TAG_ADD_LIST){
  /* Only this thread builds lists and frees libraries, so lib stays valid without the lock. */
  struct xz_shpl_set snapshot;struct xz_shlib_request req=j->req;struct xz_shlib_list *l=NULL;
  lock_as("worker");if(xz_shpl_copy(&snapshot,&edits[j->slot]))xz_shpl_init(&snapshot);pthread_mutex_unlock(&mutex);
  req.edits=&snapshot;
  if(!xz_shlib_build(lib,&req,&l)&&l){
   if((ids=malloc(((size_t)l->total+1)*sizeof *ids)))for(int32_t i=0;i<l->total;i++)if(l->kind[i]==XZ_SHELL_ROW_TRACK)ids[n++]=l->id[i];
   xz_shlib_list_free(l);
  }
  xz_shpl_free(&snapshot);
 }else if(j->kind==LE_HISTORY&&j->op==XZ_SHELL_HISTORY_DELETE_ALL){
  if((ids=malloc(4096*sizeof *ids)))n=(uint32_t)xz_shlib_history_ids(lib,ids,4096);
 }
 lock_as("worker");
 struct xz_shpl_set before,after;int rc=-1,added=0,already=0,full=0,saved=-1,err=0;
 xz_shpl_init(&before);xz_shpl_init(&after);
 char root[1024];snprintf(root,sizeof root,"%s",usb[j->slot].root);
 if(!xz_shpl_copy(&before,&edits[j->slot])){
  struct xz_shpl_set *s=&edits[j->slot];rc=0;
  if(j->kind==LE_TAG)switch(j->op){
  case XZ_SHELL_TAG_ADD:{int r=xz_shpl_list_add(s,XZ_SHPL_TAG_LIST,j->value,XZ_SHELL_TAG_LIST_MAX);added=r>0;already=!r;full=r<0;rc=r<0?-1:0;break;}
  case XZ_SHELL_TAG_REMOVE:xz_shpl_list_remove(s,XZ_SHPL_TAG_LIST,j->value);break;
  case XZ_SHELL_TAG_ADD_LIST:
   for(uint32_t i=0;i<n;i++){int r=xz_shpl_list_add(s,XZ_SHPL_TAG_LIST,ids[i],XZ_SHELL_TAG_LIST_MAX);if(r>0)added++;else if(!r)already++;else{full=1;break;}}
   if(!n)rc=-1;
   break;
  case XZ_SHELL_TAG_CLEAR:xz_shpl_list_clear(s,XZ_SHPL_TAG_LIST);break;
  default:rc=-1;break;
  }else switch(j->op){
  case XZ_SHELL_HISTORY_DELETE:rc=xz_shpl_list_add(s,XZ_SHPL_HISTORY_DELETED,j->value,0)<0?-1:0;break;
  case XZ_SHELL_HISTORY_DELETE_ALL:
   for(uint32_t i=0;i<n&&!rc;i++)rc=xz_shpl_list_add(s,XZ_SHPL_HISTORY_DELETED,ids[i],0)<0?-1:0;
   break;
  case XZ_SHELL_HISTORY_PLAYED:
   xz_shpl_list_remove(s,XZ_SHPL_UNPLAYED,j->value);rc=xz_shpl_list_add(s,XZ_SHPL_PLAYED,j->value,0)<0?-1:0;break;
  case XZ_SHELL_HISTORY_UNPLAYED:
   xz_shpl_list_remove(s,XZ_SHPL_PLAYED,j->value);rc=xz_shpl_list_add(s,XZ_SHPL_UNPLAYED,j->value,0)<0?-1:0;
   for(unsigned i=0;i<played_n;i++)if(played[i].kind==2+j->slot&&played[i].id==j->value){played[i]=played[--played_n];break;}
   break;
  default:rc=-1;break;
  }
  if(!rc&&xz_shpl_copy(&after,&edits[j->slot])){rc=-1;}
  if(rc){xz_shpl_free(&edits[j->slot]);edits[j->slot]=before;xz_shpl_init(&before);}
 }
 pthread_mutex_unlock(&mutex);
 free(ids);
 if(!rc){if(mys_readonly){saved=-1;err=EPERM;}else{saved=xz_shpl_save(&after,root);err=errno;}xz_shpl_free(&after);}
 lock_as("worker");
 char text[48];text[0]=0;
 if(!rc&&saved){
  xz_shpl_free(&edits[j->slot]);edits[j->slot]=before;xz_shpl_init(&before);
  snprintf(text,sizeof text,"%s",err==EPERM?"TRIAL RUN: NOTHING SAVED":err==EROFS?"THIS STICK IS READ-ONLY: NOTHING CHANGES":err==ENOSPC?"THE STICK IS FULL: NOT SAVED":"NOT SAVED ON THE STICK");
 }else if(rc)snprintf(text,sizeof text,"%s",full?"TAG LIST IS FULL: 100 TRACKS":"THAT CHANGE IS NOT APPLIED");
 else if(j->kind==LE_TAG&&j->op==XZ_SHELL_TAG_ADD_LIST)snprintf(text,sizeof text,full?"ADDED %d: TAG LIST IS FULL":"ADDED %d, %d ALREADY ON THE LIST",added,already);
 else if(j->kind==LE_TAG&&j->op==XZ_SHELL_TAG_ADD&&already)snprintf(text,sizeof text,"ALREADY ON THE TAG LIST");
 else if(j->kind==LE_TAG&&j->op==XZ_SHELL_TAG_CLEAR)snprintf(text,sizeof text,"TAG LIST CLEARED");
 else if(j->kind==LE_HISTORY&&j->op==XZ_SHELL_HISTORY_DELETE_ALL)snprintf(text,sizeof text,"ALL HISTORIES DELETED");
 else if(j->kind==LE_HISTORY&&j->op==XZ_SHELL_HISTORY_DELETE)snprintf(text,sizeof text,"HISTORY DELETED");
 if(text[0])notice_locked(now_ms(),text);
 xz_shpl_free(&before);
 for(int k=0;k<L_COUNT;k++)if(lists[k].requested&&lists[k].source==j->slot)list_rebuild(&lists[k]);
 if(refreshed)fill_slots_locked();
 dirty_flag=1;
 pthread_mutex_unlock(&mutex);
 if(debug){char line[200];snprintf(line,sizeof line,"SHELL_LISTEDIT: usb%d kind=%s op=%d value=%u result=%d saved=%d added=%d already=%d full=%d",
  j->slot+1,j->kind==LE_TAG?"tag":"history",j->op,j->value,rc,rc?-1:saved,added,already,full);xz_log(line);}
 publish_refs(j->slot);
}
static void run_playlist_edit(int slot, const struct xz_shell_playlist_edit *e) {
 struct xz_shlib *lib=usb[slot].lib;
 if(!lib)return;
 uint32_t *base=NULL,base_n=0;const char *base_name="";
 int track_op=e->op==XZ_SHELL_PL_ADD_TRACK||e->op==XZ_SHELL_PL_REMOVE_TRACK||e->op==XZ_SHELL_PL_MOVE_TRACK;
 lock_as("worker");
 const struct xz_shpl *had=xz_shpl_find(&edits[slot],e->playlist);
 int need_base=track_op&&!(had&&had->has_tracks)&&e->playlist<XZ_SHPL_FIRST_CREATED;
 char root[1024];snprintf(root,sizeof root,"%s",usb[slot].root);
 pthread_mutex_unlock(&mutex);
 if(need_base){
  if(xz_shlib_playlist_tracks(lib,e->playlist,&base,&base_n)){base=NULL;base_n=0;}
  base_name=xz_shlib_playlist_name(lib,e->playlist);
 }else if(e->op==XZ_SHELL_PL_RENAME)base_name=xz_shlib_playlist_name(lib,e->playlist);
 struct xz_shpl_set before,after;int rc=-1,saved=-1,err=0;uint32_t created=0;
 lock_as("worker");
 if(!xz_shpl_copy(&before,&edits[slot])){
  if(e->op==XZ_SHELL_PL_FROM_TAG_LIST){
   /* A new playlist holding the TAG LIST in its order. */
   struct xz_shell_playlist_edit create=*e;create.op=XZ_SHELL_PL_CREATE;
   rc=xz_shpl_apply(&edits[slot],&create,NULL,0,NULL,&created);
   uint32_t tn=0;const uint32_t *tl=xz_shpl_list(&edits[slot],XZ_SHPL_TAG_LIST,&tn);
   if(!rc&&(!tn||xz_shpl_set_tracks(&edits[slot],created,tl,tn)))rc=-1;
  }else rc=xz_shpl_apply(&edits[slot],e,base,base_n,base_name,&created);
  if(!rc&&xz_shpl_copy(&after,&edits[slot])){rc=-1;xz_shpl_free(&edits[slot]);edits[slot]=before;xz_shpl_init(&before);}
 }
 pthread_mutex_unlock(&mutex);
 free(base);
 if(!rc){if(mys_readonly){saved=-1;err=EPERM;}else{saved=xz_shpl_save(&after,root);err=errno;}xz_shpl_free(&after);}
 lock_as("worker");
 if(!rc&&saved){
  /* Not on the stick: the edit is undone so the shell never shows what a reboot would lose. */
  xz_shpl_free(&edits[slot]);edits[slot]=before;xz_shpl_init(&before);
  notice_locked(now_ms(),err==EPERM?"TRIAL RUN: NOTHING SAVED":err==EROFS?"THIS STICK IS READ-ONLY: PLAYLISTS CANNOT CHANGE":err==ENOSPC?"THE STICK IS FULL: PLAYLIST NOT SAVED":"PLAYLIST NOT SAVED");
 }else if(rc)notice_locked(now_ms(),"PLAYLIST CHANGE NOT APPLIED");
 xz_shpl_free(&before);
 for(int k=0;k<L_COUNT;k++)if(lists[k].requested&&lists[k].source==slot)list_rebuild(&lists[k]);
 pthread_mutex_unlock(&mutex);
 if(debug){char line[200];snprintf(line,sizeof line,"SHELL_PLAYLIST: usb%d op=%d playlist=%u track=%u result=%d saved=%d errno=%d created=%u",
  slot+1,(int)e->op,e->playlist,e->track,rc,rc?-1:saved,saved?err:0,created);xz_log(line);}
 publish_refs(slot);
}

/* ---- worker ---- */
static void *worker(void *unused) {
 (void)unused;
 uint32_t last_scan=0;int first=1;
 lock_as("worker");
 while(running){
  uint32_t now=now_ms();
  if(first){
   pthread_mutex_unlock(&mutex);scan_usb();lock_as("worker");
   last_scan=now;first=0;continue;
  }
  int worked=0;uint32_t wait_ms=250u;
  /* Priority: a deck's wave (what the owner is looking at), then a LOAD prefetch, then lists, then the
     once-a-second USB check. */
  for(int d=0;d<XZ_SHELL_DECKS&&!worked;d++){
   if(wave_job[d].done==wave_job[d].wanted)continue;
   if((int32_t)(now-wave_job[d].retry_at)<0){uint32_t left=wave_job[d].retry_at-now;if(left<wait_ms)wait_ms=left;continue;}
   uint32_t gen=wave_job[d].wanted;uint64_t requested_us=wave_job[d].requested_us;
   pthread_mutex_unlock(&mutex);
   uint32_t retry=WAVE_RETRY_MS;
   int finished=wave_step(d,gen,requested_us,&retry);
   lock_as("worker");
   if(finished){if(wave_job[d].wanted==gen)wave_job[d].done=gen;}
   else wave_job[d].retry_at=now_ms()+retry;
   worked=1;
  }
  for(int d=0;d<XZ_SHELL_DECKS&&!worked;d++){
   if(!hint[d].pending)continue;
   int slot=hint[d].slot;uint32_t id=hint[d].id;uint64_t posted=hint[d].posted_us;hint[d].pending=0;
   pthread_mutex_unlock(&mutex);
   if(slot>=0&&slot<2)run_prefetch(d,slot,id,posted);
   lock_as("worker");
   worked=1;
  }
  if(!worked&&(scan_soon||now-last_scan>=1000u)){
   scan_soon=0;
   pthread_mutex_unlock(&mutex);scan_usb();lock_as("worker");
   last_scan=now_ms();continue;
  }
  if(!worked&&mys_job.pending){
   int slot=mys_job.slot;unsigned kinds=mys_job.kinds;mys_job.pending=0;
   static struct xz_shms_files job;job=mys_job.f;char done[48];snprintf(done,sizeof done,"%s",mys_job.done);
   char root[1024];snprintf(root,sizeof root,"%s",usb[slot].root);struct xz_shlib *lib=usb[slot].lib;
   pthread_mutex_unlock(&mutex);
   int rc=MS_OK;
   for(int k=0;k<MS_KIND_COUNT&&rc==MS_OK;k++)if((kinds>>k)&1u)rc=xz_shms_write(root,&job,(enum ms_kind)k);
   lock_as("worker");
   if(usb[slot].lib==lib){
    /* DURABILITY: renamed, directory sync failed; the stick holds the new file. */
    if(rc==MS_OK||rc==MS_ERR_DURABILITY){mys[slot]=job;notice_locked(now_ms(),done);}
    else{
     char text[48];
     snprintf(text,sizeof text,rc==MS_ERR_READ_ONLY?"USB %d IS LOCKED: NOT SAVED":"USB %d: SETTINGS NOT SAVED",slot+1);
     notice_locked(now_ms(),text);
    }
    if(refreshed)fill_slots_locked();
    dirty_flag=1;
   }
   if(debug){char line[200];snprintf(line,sizeof line,"SHELL_MYSETTINGS: usb%d save kinds=0x%x result=%d (%s)",slot+1,kinds,rc,ms_error_string(rc));xz_log(line);}
   worked=1;
  }
  if(!worked&&le_count){
   struct list_edit job=le_queue[0];memmove(le_queue,le_queue+1,(size_t)(--le_count)*sizeof *le_queue);
   pthread_mutex_unlock(&mutex);
   run_list_edit(&job);
   lock_as("worker");
   worked=1;
  }
  if(!worked&&pl_count){
   int slot=pl_queue[pl_head].slot;struct xz_shell_playlist_edit e=pl_queue[pl_head].e;
   pl_head=(pl_head+1)%PL_QUEUE;pl_count--;
   pthread_mutex_unlock(&mutex);
   run_playlist_edit(slot,&e);
   lock_as("worker");
   worked=1;
  }
  for(int k=0;k<L_COUNT&&!worked;k++){
   struct list_slot *ls=&lists[k];
   if(!ls->requested||ls->built==ls->wanted||ls->source<0||!usb[ls->source].lib)continue;
   struct xz_shlib_request req=ls->req;uint32_t gen=ls->wanted;struct xz_shlib *lib=usb[ls->source].lib;
   /* The build reads the filter and its own copy of the stick's playlist edits. */
   struct xz_shpl_set snapshot;req.filter=active_filter;
   if(req.category==XZ_SHELL_CAT_MATCHING)matching_filter(&req.matching);
   if(xz_shpl_copy(&snapshot,&edits[ls->source]))xz_shpl_init(&snapshot);
   req.edits=&snapshot;
   pthread_mutex_unlock(&mutex);
   /* Only this thread frees libraries, so lib stays valid without the lock. */
   uint32_t began=now_ms();struct xz_shlib_list *result=NULL;
   int rc=xz_shlib_build(lib,&req,&result);
   xz_shpl_free(&snapshot);
   uint32_t took=now_ms()-began;
   lock_as("worker");
   xz_mods_shell_proof_v1.list_build_ms=took;
   if(rc)xz_mods_shell_proof_v1.list_errors++;else xz_mods_shell_proof_v1.list_builds++;
   if(gen==ls->wanted&&usb[ls->source].lib==lib){
    retire_list(ls->result);ls->result=result;ls->built=gen;result=NULL;
    if(refreshed)fill_slots_locked();
    dirty_flag=1;
   }
   retire_list(result);
   pthread_mutex_unlock(&mutex);drain_retired();lock_as("worker");
   worked=1;
  }
  if(!worked&&cue_want.pending&&cue_want.slot>=0&&usb[cue_want.slot].lib){
   int slot=cue_want.slot;uint32_t id=cue_want.id;cue_want.pending=0;
   if(!cue_find(slot,id)){
    struct xz_shlib *lib=usb[slot].lib;const struct xz_shlib_track *t=xz_shlib_track(lib,id);
    char track[1024];snprintf(track,sizeof track,"%s",t?xz_shlib_text(lib,t->file_path):"");
    pthread_mutex_unlock(&mutex);
    static struct xz_shana_cue got[CUE_POINTS];char dir[1024];int n=-1;
    if(track[0]&&analysis[slot].index&&!xz_shana_find(analysis[slot].index,track,dir,sizeof dir))n=xz_shana_cue_list(dir,got,CUE_POINTS);
    lock_as("worker");
    if(usb[slot].lib==lib&&!cue_find(slot,id)){
     struct cue_cache *c=&cue_cache[0];
     for(int i=0;i<CUE_CACHE;i++){if(!cue_cache[i].state){c=&cue_cache[i];break;}if(cue_cache[i].used<c->used)c=&cue_cache[i];}
     c->slot=slot;c->id=id;c->state=n>=0?1:-1;c->n=n>0?n:0;c->used=++preview_clock;if(n>0)memcpy(c->c,got,(size_t)n*sizeof *got);
     if(refreshed)fill_slots_locked();
     dirty_flag=1;
    }
   }
   worked=1;
  }
  if(!worked&&preview_want.n&&preview_want.slot>=0&&usb[preview_want.slot].lib){
   int slot=preview_want.slot;uint32_t id=preview_want.id[--preview_want.n];
   if(!preview_find(slot,id)){
    struct xz_shlib *lib=usb[slot].lib;
    const struct xz_shlib_track *t=xz_shlib_track(lib,id);
    char track[1024];snprintf(track,sizeof track,"%s",t?xz_shlib_text(lib,t->file_path):"");
    pthread_mutex_unlock(&mutex);
    /* Only this thread changes the analysis index and frees libraries. */
    struct xz_shell_column col[PREVIEW_COLS];uint32_t n=0;char dir[1024];uint8_t *bands=NULL;uint32_t count=0;
    if(track[0]&&analysis[slot].index&&!xz_shana_find(analysis[slot].index,track,dir,sizeof dir)&&!xz_shana_preview(dir,&bands,&count)){
     struct xz_shell_column *binned=bin_columns(bands,count,PREVIEW_COLS,band_peak(bands,count),&n);
     if(binned&&n<=PREVIEW_COLS)memcpy(col,binned,(size_t)n*sizeof *col);else n=0;
     free(binned);
    }
    free(bands);
    lock_as("worker");
    if(usb[slot].lib==lib&&!preview_find(slot,id)){
     struct preview *p=&previews[0];
     for(int i=0;i<PREVIEW_CACHE;i++){if(!previews[i].state){p=&previews[i];break;}if(previews[i].used<p->used)p=&previews[i];}
     p->slot=slot;p->id=id;p->state=n?1:-1;p->n=n;p->used=++preview_clock;memcpy(p->col,col,(size_t)n*sizeof *col);
     preview_reads++;
     if(refreshed)fill_slots_locked();
     dirty_flag=1;
    }
   }
   worked=1;
  }
  if(!worked){
   if(now-last_scan<1000u&&1000u-(now-last_scan)<wait_ms)wait_ms=1000u-(now-last_scan);
   if(!wait_ms)wait_ms=1;
   struct timeval tv;gettimeofday(&tv,NULL);
   long ns=tv.tv_usec*1000L+(long)wait_ms*1000000L;
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
/* The playhead's advance this frame against the ideal for a deck that plays smoothly (not following). */
static void step_jitter_locked(void) {
 uint32_t clock=model.clock_ms;int counted=0;
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  const struct xz_shell_deck *k=&model.deck[d];
  int32_t e=xz_shell_elapsed_now(&model,d);
  int live=e>=0&&(k->known&XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYING))&&k->playing&&!((follow_bits>>d)&1u);
  if(live&&step.valid&&step.last[d]>=0&&clock!=step.last_clock){
   int64_t rate=10000+(k->known&K(TEMPO)?k->tempo_x100:0);
   int64_t ideal_us=(int64_t)(int32_t)(clock-step.last_clock)*rate/10;
   int64_t off=(int64_t)(e-step.last[d])*1000-ideal_us;if(off<0)off=-off;
   if(off<1000000){if((uint32_t)off>sec.jitter_us_max)sec.jitter_us_max=(uint32_t)off;counted=1;}
  }
  step.last[d]=live?e:-1;
 }
 if(counted)sec.jitter_frames++;
 step.last_clock=clock;step.valid=1;
}
/* The second's slowest frame call: its parts and what dominated (debug, SHELL_WORST once a second). */
struct worst_frame { uint32_t total_us, lock_wait_us, refresh_us, render_us; const char *holder, *cause; int busy; };
static struct worst_frame worst_pending;
static void roll_second(uint32_t now) {
 if(!sec.start){sec.start=now;return;}
 if(now-sec.start<1000u)return;
 struct xz_shell_proof *p=&xz_mods_shell_proof_v1;
 p->second_frames=sec.frames;p->second_render_calls=sec.calls;p->second_stock_flips=sec.flips;p->second_skipped=sec.skipped;
 p->second_busy=sec.busy;p->second_pump=sec.pump;p->second_ticks=sec.ticks;
 p->second_render_avg_us=sec.frames?(uint32_t)(sec.render_us/sec.frames):0;p->second_render_max_us=sec.render_max;
 p->second_refresh_avg_us=sec.refreshes?(uint32_t)(sec.refresh_us/sec.refreshes):0;p->second_refresh_max_us=sec.refresh_max;
 p->second_refreshes=sec.refreshes;p->second_gap_max_us=sec.shown.gap_max_us;p->second_late=sec.shown.late;
 p->second_tick_busy=sec.tick_busy;p->second_clock_reads=sec.clock_reads;p->second_moving=sec.moving;
 if(storage){p->model_holds=xz_shell_model_holds(storage);p->fx_guard_failures=xz_shell_model_fx_failures(storage);}
 p->handed_off=(uint32_t)handoff.handed;p->caution_yield=(uint32_t)handoff.caution;p->screen=(uint32_t)xz_shell_screen(&shell);
 p->hand_offs=handoff.count;p->resumes=handoff.resumes;p->commands_dropped=queue.dropped;
 if(storage)xz_shell_model_diagnostics(storage,p->model_guard_failures);
 __atomic_add_fetch(&p->sequence,1,__ATOMIC_RELEASE);
 if(debug){
  char line[900];struct stock_parts stock;int stock_now=stock_play_parts(&stock);
  /* fps: frames the shell drew in the last second (each one is presented on the next vsync);
     gap_max_ms/late: the longest frame-to-frame gap and the gaps over 20 ms (missed 60 Hz slots);
     clock: native clock reads that succeeded (two decks per refresh); moving: deck bits playing or moving;
     tick_busy: owner ticks skipped because a frame held the lock; holds: torn reads covered by the
     last snapshot (cumulative). */
  if(worst_pending.total_us){
   char worst[220];
   snprintf(worst,sizeof worst,"SHELL_WORST: total_us=%u cause=%s lock_wait_us=%u held_by=%s refresh_us=%u render_us=%u drawn=%d screen=%s",
    worst_pending.total_us,worst_pending.cause?worst_pending.cause:"render",worst_pending.lock_wait_us,worst_pending.holder?worst_pending.holder:"none",
    worst_pending.refresh_us,worst_pending.render_us,!worst_pending.busy,xz_shell_screen_name(xz_shell_screen(&shell)));
   xz_log(worst);
  }
  int pps=zoom_pps[shell.zoom<0?0:shell.zoom>XZ_SHELL_ZOOM_MAX?XZ_SHELL_ZOOM_MAX:shell.zoom];
  unsigned jitter_px100=(unsigned)((uint64_t)sec.jitter_us_max*(unsigned)pps/10000u);
  snprintf(line,sizeof line,"SHELL: fps=%u gap_max_ms=%u.%u late=%u hist=%u/%u/%u/%u/%u step_jitter_ms=%u.%u step_jitter_px=%u.%02u lock_waits=%u ui_cached=%u calls=%u stock_flips=%u skipped=%u busy=%u render_avg_us=%u render_max_us=%u "
   "refreshes=%u refresh_avg_us=%u refresh_max_us=%u clock=%u moving=0x%x following=0x%x period_ms=%u pump=%u ticks=%u tick_busy=%u owns=%u handed=%u caution=%u "
   "holds=%u wave_loads=%u fx_guard=%u stock_play=%d flag=%u mode=%u menu=%u caution_ack=0x%x keys_dropped=%u screen=%s caps=0x%x",
   sec.frames,sec.shown.gap_max_us/1000u,sec.shown.gap_max_us/100u%10u,sec.shown.late,
   sec.shown.hist[0],sec.shown.hist[1],sec.shown.hist[2],sec.shown.hist[3],sec.shown.hist[4],
   sec.jitter_us_max/1000u,sec.jitter_us_max/100u%10u,jitter_px100/100u,jitter_px100%100u,sec.lock_waits,sec.ui_cached,sec.calls,sec.flips,sec.skipped,sec.busy,
   p->second_render_avg_us,sec.render_max,sec.refreshes,p->second_refresh_avg_us,sec.refresh_max,sec.clock_reads,sec.moving,sec.following,
   (unsigned)period_ms,sec.pump,sec.ticks,sec.tick_busy,(unsigned)owns_flag,(unsigned)handoff.handed,p->caution,
   p->model_holds,p->wave_loads,p->fx_guard_failures,stock_now,stock.flag,stock.mode,stock.menu,caution_state.ack,key_log.dropped,
   xz_shell_screen_name(xz_shell_screen(&shell)),model.capabilities);
  xz_log(line);
 }
 uint64_t last_shown=sec.shown.last_us;
 memset(&sec,0,sizeof sec);sec.start=now;sec.shown.last_us=last_shown;memset(&worst_pending,0,sizeof worst_pending);
}

/* ---- shell-owned display settings ----
   Kept in RAM for the life of the process (across XZ_MODS_SHELL stop/start); a reboot starts from the
   shell's defaults. ids: XZ_SHELL_SET_WAVE_COLOR, XZ_SHELL_SET_WAVE_POSITION, XZ_SHELL_ITEM_KEY_DISPLAY,
   XZ_SHELL_ITEM_WAVE_ZOOM (the zoom level), XZ_SHELL_ITEM_MODE_LAMPS and XZ_SHELL_ITEM_QUANTIZE_LAMP. */
static struct { int id, value, set; } display_saved[6]={{XZ_SHELL_SET_WAVE_COLOR,0,0},{XZ_SHELL_SET_WAVE_POSITION,0,0},
 {XZ_SHELL_ITEM_KEY_DISPLAY,0,0},{XZ_SHELL_ITEM_WAVE_ZOOM,0,0},{XZ_SHELL_ITEM_MODE_LAMPS,0,0},{XZ_SHELL_ITEM_QUANTIZE_LAMP,0,0}};
#define DISPLAY_SAVED (int)(sizeof display_saved/sizeof *display_saved)
static void save_display(int id, int value) {
 for(int i=0;i<DISPLAY_SAVED;i++)if(display_saved[i].id==id){display_saved[i].value=value;display_saved[i].set=1;}
}
static void restore_display(void) {
 for(int i=0;i<DISPLAY_SAVED;i++)if(display_saved[i].set)xz_shell_restore_setting(&shell,display_saved[i].id,display_saved[i].value);
}

/* ---- feedback and logs ---- */
/* The text stays in notice_text until the next notice replaces it, so it outlives XZ_SHELL_NOTICE_MS.
   notice_at_ms is stamped with the clock of the first frame that shows it, never a later thread's clock
   (a stamp ahead of clock_ms would wrap the renderer's age test and hide the toast). */
static void notice_locked(uint32_t now, const char *text) {
 (void)now;snprintf(notice_text,sizeof notice_text,"%s",text);notice_pending=1;dirty_flag=1;
 if(debug){char line[96];snprintf(line,sizeof line,"SHELL_NOTICE: %s",notice_text);xz_log(line);}
}
static void publish_notice_locked(void) {
 if(notice_pending){notice_at=model.clock_ms?model.clock_ms:1;notice_pending=0;}
#ifdef XZ_SHELL_NOTICE_MS
 model.notice=notice_text[0]?notice_text:NULL;model.notice_at_ms=notice_at;
#endif
}
/* At most 20 lines a second; the next line that gets through reports how many were dropped. */
static int key_log_allowed(uint32_t now) {
 if(!debug)return 0;
 if(now-key_log.second>=1000u){key_log.second=now;key_log.lines=0;}
 if(key_log.lines>=20){key_log.dropped++;return 0;}
 key_log.lines++;return 1;
}
static const char *const verdict_names[3]={"pass","pass_hand_off","swallow"};
static const char *const phase_names[3]={"press","hold","release"};

/* ---- actions ---- */
static void queue_command(const struct xz_shrt_command *c) {
 if(!xz_shrt_queue_push(&queue,c))xz_mods_shell_proof_v1.commands_dropped=queue.dropped;
}
static void refuse(void) { xz_mods_shell_proof_v1.commands_refused++; }
static void hand_off_locked(uint32_t now, const char *why, unsigned what) {
 xz_shrt_handoff_begin(&handoff,now);set_owns(0);
 xz_mods_shell_proof_v1.handed_off=1;
 if(debug){
  struct stock_parts s;int play=stock_play_parts(&s);char line[160];
  snprintf(line,sizeof line,"SHELL_HANDOFF: begin %s=0x%x screen=%s stock_play=%d flag=%u mode=%u menu=%u",
   why,what,xz_shell_screen_name(xz_shell_screen(&shell)),play,s.flag,s.mode,s.menu);
  xz_log(line);
 }
}
/* The FILTER panel's filter as the library applies it, with the master deck's BPM and key taken now.
   Every list in use is rebuilt with it on the worker. */
/* The master deck, or NULL. */
static const struct xz_shell_deck *master_deck(int *index) {
 for(int d=0;d<XZ_SHELL_DECKS;d++)if((model.deck[d].known&K(MASTER))&&model.deck[d].master){if(index)*index=d;return &model.deck[d];}
 if(index)*index=-1;
 return NULL;
}
/* MATCHING keeps the tracks within 6% of the master deck's BPM whose key the traffic light marks green or
   amber against the master key; either part alone when only one is known. Rebuilt when the master deck or
   its track changes, not on every tempo move. */
#define MATCHING_BPM_PERCENT 6
static uint32_t matching_signature;
static void matching_filter(struct xz_shlib_filter *f) {
 memset(f,0,sizeof *f);
 const struct xz_shell_deck *m=master_deck(NULL);
 if(!m)return;
 if((m->known&K(BPM))&&m->bpm_x100>0){
  int32_t spread=m->bpm_x100*MATCHING_BPM_PERCENT/100;
  f->bpm_min_x100=m->bpm_x100-spread;f->bpm_max_x100=m->bpm_x100+spread;f->on=1;
 }
 if((m->known&K(KEY))&&m->key&&m->key[0]){snprintf(f->master_key,sizeof f->master_key,"%s",m->key);f->on=1;}
}
static void matching_follow(void) {
 int d;const struct xz_shell_deck *m=master_deck(&d);
 uint32_t sig=m?(uint32_t)(d+1)<<28^(m->known&K(KEY)?1u<<27:0)^(loaded[d].known?loaded[d].id&0x7ffffffu:0):0;
 if(sig==matching_signature)return;
 matching_signature=sig;
 for(int k=0;k<L_COUNT;k++)if(lists[k].requested&&lists[k].req.category==XZ_SHELL_CAT_MATCHING)list_rebuild(&lists[k]);
}
static void apply_filter(int on) {
 memset(&active_filter,0,sizeof active_filter);
 if(on){
  const struct xz_shell_filter *f=&shell.filter;
  const struct xz_shell_deck *master=NULL;
  for(int d=0;d<XZ_SHELL_DECKS;d++)if((model.deck[d].known&K(MASTER))&&model.deck[d].master)master=&model.deck[d];
  active_filter.on=1;
  if(f->bpm==XZ_SHELL_FILTER_BPM_RANGE){active_filter.bpm_min_x100=f->bpm_min_x100;active_filter.bpm_max_x100=f->bpm_max_x100;}
  else if(f->bpm==XZ_SHELL_FILTER_BPM_MASTER){
   if(master&&(master->known&K(BPM))){
    int32_t spread=f->bpm_spread*100;
    active_filter.bpm_min_x100=master->bpm_x100-spread;active_filter.bpm_max_x100=master->bpm_x100+spread;
    if(active_filter.bpm_min_x100<1)active_filter.bpm_min_x100=1;
   }else notice_locked(now_ms(),"NO MASTER DECK: BPM FILTER OFF");
  }
  if(f->key_compatible){
   if(master&&(master->known&K(KEY))&&master->key)snprintf(active_filter.master_key,sizeof active_filter.master_key,"%s",master->key);
   else notice_locked(now_ms(),"NO MASTER KEY: KEY FILTER OFF");
  }
  active_filter.rating_min=f->rating_min;active_filter.colors=f->colors;
  /* MY TAG needs the export's my-tag tables, which the reader does not parse yet. */
  if(f->my_tags)notice_locked(now_ms(),"MY TAG FILTER IS NOT AVAILABLE YET");
 }
 for(int k=0;k<L_COUNT;k++)if(lists[k].requested)list_rebuild(&lists[k]);
}
static void browse_action(int32_t value, int op) {
 int k=list_index(xz_shell_screen(&shell));
 if(k<0)k=shown_list(NULL);
 struct list_slot *ls=&lists[k];
 struct xz_shlib_request *r=&ls->req;
 switch(op){
 case XZ_SHELL_BROWSE_SET_COLUMNS:column_mask=(uint32_t)value|(1u<<XZ_SHELL_COL_TITLE);return;
 case XZ_SHELL_BROWSE_FILTER:apply_filter(value!=0);return;
 case XZ_SHELL_BROWSE_REQUEST_WINDOW:ls->window_start=value<0?0:value;return;
 case XZ_SHELL_BROWSE_OPEN:if(r->depth>=XZ_SHLIB_PATH_DEPTH)return;r->path[r->depth++]=(uint32_t)value;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_UP:if(r->depth>0)r->depth--;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_TOP:r->depth=0;r->sort_set=0;break;
 case XZ_SHELL_BROWSE_CATEGORY:
  if(value<0||value>=XZ_SHELL_CAT_COUNT||!((xz_shlib_categories()>>value)&1u)){refuse();return;}
  r->category=(enum xz_shell_browse_category)value;r->depth=0;r->sort_set=0;
  if(value==XZ_SHELL_CAT_MATCHING&&!master_deck(NULL))notice_locked(now_ms(),"MATCHING NEEDS A MASTER DECK");
  break;
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
/* MY SETTINGS LOAD: the stick's files through the UTILITY setters (shell_mysettings.c), TIME MODE through its
   command, on the owner tick. In a --ram-settings trial stock would save every UTILITY set to flash, so
   LOAD is held back there like UTILITY writes (unless XZ_MODS_SHELL_UTILITY_WRITES=1). */
static void my_settings_load(int source, int deck, uint32_t now) {
 int slot=source==XZ_SHELL_SRC_USB1?0:source==XZ_SHELL_SRC_USB2?1:-1;char text[48];
 if(slot<0||!usb[slot].lib){refuse();return;}
 if(!xz_shms_any(&mys[slot])){snprintf(text,sizeof text,"NO MY SETTINGS ON USB %d",slot+1);notice_locked(now,text);return;}
 if(util.trial){notice_locked(now,"TRIAL RUN: SETTINGS NOT LOADED");return;}
 if(deck<0||deck>=XZ_SHELL_DECKS)deck=shell.deck>=0&&shell.deck<XZ_SHELL_DECKS?shell.deck:0;
 struct xz_shms_step steps[MS_FIELD_COUNT];int not_applied=0,applied=0;
 int n=xz_shms_plan(&mys[slot],xz_shut_defs,XZ_SHUT_DEFS,steps,MS_FIELD_COUNT,&not_applied);
 for(int i=0;i<n;i++){
  struct xz_shrt_command c;
  if(steps[i].def==XZ_SHMS_STEP_TIME_MODE){
   if(!has(G_TIME_MODE)){not_applied++;continue;}
   c=(struct xz_shrt_command){XZ_SHRT_CMD_TIME_MODE,deck,steps[i].arg,0,0,now};
  }else{
   int u=steps[i].def;
   if(xz_shut_reason(&xz_shut_defs[u],util.get_ok[u],util.set_ok[u],util.trial)){not_applied++;continue;}
   c=(struct xz_shrt_command){XZ_SHRT_CMD_UTILITY,u,steps[i].arg,0,0,now};
   util.raw[u]=steps[i].arg;util.known[u]=1;
  }
  queue_command(&c);applied++;
 }
 snprintf(text,sizeof text,"LOADED USB %d: %d APPLIED, %d NOT",slot+1,applied,not_applied);
 notice_locked(now,text);
 mys_state[slot]=applied?XZ_SHELL_MY_DONE:XZ_SHELL_MY_FAILED;snprintf(mys_result[slot],sizeof mys_result[slot],"%s",text);
 if(debug){char line[160];snprintf(line,sizeof line,"SHELL_MYSETTINGS: usb%d load deck=%d applied=%d not_applied=%d",slot+1,deck+1,applied,not_applied);xz_log(line);}
 dirty_flag=1;
}
/* MY SETTINGS SAVE (edited NULL: the deck's current values into the stick's files) and SET (edited: the
   changed files). The worker writes them with my_settings' atomic write. */
static void my_settings_save(int source, uint32_t now, const struct xz_shms_files *edited, unsigned kinds, const char *done) {
 int slot=source==XZ_SHELL_SRC_USB1?0:source==XZ_SHELL_SRC_USB2?1:-1;char text[48];
 if(slot<0||!usb[slot].lib){refuse();return;}
 if(mys_readonly){notice_locked(now,"TRIAL RUN: NOTHING SAVED");return;}
 if(usb[slot].rdonly){snprintf(text,sizeof text,"USB %d IS LOCKED: NOT SAVED",slot+1);notice_locked(now,text);return;}
 if(!xz_shms_any(&mys[slot])){snprintf(text,sizeof text,"NO MY SETTINGS ON USB %d",slot+1);notice_locked(now,text);return;}
 if(edited)mys_job.f=*edited;
 else{
  mys_job.f=mys[slot];
  int d=shell.deck>=0&&shell.deck<XZ_SHELL_DECKS?shell.deck:0;
  int time_known=!!(model.deck[d].known&K(TIME_MODE));
  xz_shms_capture(&mys_job.f,xz_shut_defs,XZ_SHUT_DEFS,util.raw,util.known,time_known,model.deck[d].time_mode==XZ_SHELL_TIME_REMAIN);
  kinds=0;for(int k=0;k<MS_KIND_COUNT;k++)if(mys_job.f.state[k]==XZ_SHMS_OK)kinds|=1u<<k;
 }
 mys_job.slot=slot;mys_job.kinds=kinds;snprintf(mys_job.done,sizeof mys_job.done,done,slot+1);mys_job.pending=1;
 pthread_cond_signal(&wake);
}
static void execute_locked(const struct xz_shell_action *out, size_t n, uint32_t now) {
 for(size_t i=0;i<n;i++){
  const struct xz_shell_action *a=&out[i];struct xz_shrt_command c;
  xz_mods_shell_proof_v1.actions++;
  switch(a->kind){
  case XZ_SHELL_ACT_SET_SETTING:
   /* Shell-owned display settings are already applied in the shell: keep them for the next start. */
   if(xz_shrt_shell_owned_setting(a->id)){save_display(a->id,a->value);break;}
   {
    int u=xz_shut_find(xz_shut_defs,XZ_SHUT_DEFS,a->id);
    if(u>=0){
     /* A stock UTILITY item: the firmware's own setter on the owner tick. */
     const struct xz_shut_def *d=&xz_shut_defs[u];int32_t arg;
     const char *why=xz_shut_reason(d,util.get_ok[u],util.set_ok[u],util.trial);
     if(why||!xz_shut_arg(d,a->value,&arg)){refuse();notice_locked(now,why?why:"THAT CHOICE IS NOT ON THIS ITEM");break;}
     c=(struct xz_shrt_command){XZ_SHRT_CMD_UTILITY,u,arg,0,0,now};
     queue_command(&c);
     util.raw[u]=arg;util.known[u]=1; /* shown at once; the next reading confirms or corrects it */
     dirty_flag=1;break;
    }
   }
   if(!xz_shrt_setting_writable(a->id,has(G_TIME_MODE),has(G_QUANTIZE))||
      !xz_shrt_setting_command(a,now,&c)){refuse();break;}
   queue_command(&c);break;
  case XZ_SHELL_ACT_LOAD:
   load_actions++;
   if(!(model.capabilities&XZ_SHELL_CAP_LOAD)||a->deck<0||a->deck>=XZ_SHELL_DECKS||a->id<=0||browse_source<0||!usb[browse_source].lib){
    refuse();
    notice_locked(now,!load_enabled?"LOAD FROM THIS LIST IS OFF":!has(G_LOAD)?"LOAD IS NOT AVAILABLE ON THIS FIRMWARE":
     browse_source<0?"NO USB TRACK LIST TO LOAD FROM":"THIS ROW CANNOT BE LOADED");
    break;
   }
   c=(struct xz_shrt_command){XZ_SHRT_CMD_LOAD,a->deck,0,(uint32_t)a->id,(uint8_t)(2+browse_source),now};
   queue_command(&c);break;
  case XZ_SHELL_ACT_HAND_OFF:
   /* The stock menu is reached only by passing the physical MENU key to stock (xz_shrt_key_verdict).
      A MENU hand-off from any other input has no native call to make: stock's short MENU on PLAY
      opens nothing, and UiKey_Menu belongs to Ui_EventTask. Stay on screen instead of showing an
      unchanged stock screen for the grace period. */
   if(a->id==XZ_SHELL_HAND_OFF_MENU){refuse();menu_inert=1;break;}
   /* value = 1 + the stock item the shell's SHORTCUT/UTILITY row stands for. Opening stock at that item
      (Util_EnterMenu_*) is not proven safe from these threads, so the hand-off stays plain and the item is logged. */
   if(debug&&a->value>0){char line[120];snprintf(line,sizeof line,"SHELL_HANDOFF: stock_item=%d (opened at its top: Util_EnterMenu_* not called)",(int)a->value-1);xz_log(line);}
   hand_off_locked(now,"action_screen",(unsigned)a->id);break;
  case XZ_SHELL_ACT_OPEN_MODS:
   mods_page=-1;
   for(int t=0;t<(int)mods_model.count;t++)if(mod_tabs[t].page==a->value)mods_page=a->value;
   __atomic_or_fetch(&ui_requests,XZ_SHELL_UI_OPEN_MODS,__ATOMIC_ACQ_REL);ui_requests_at=now?now:1;xz_mods_shell_proof_v1.ui_requests++;break;
  case XZ_SHELL_ACT_TOGGLE_STEMS:
   __atomic_or_fetch(&ui_requests,XZ_SHELL_UI_TOGGLE_STEMS,__ATOMIC_ACQ_REL);ui_requests_at=now?now:1;xz_mods_shell_proof_v1.ui_requests++;break;
  case XZ_SHELL_ACT_BROWSE:browse_action(a->value,a->id);break;
  case XZ_SHELL_ACT_INFO_JUMP:info_jump(a);break;
  case XZ_SHELL_ACT_SET_SOURCE:
   if(a->id==XZ_SHELL_SRC_USB1||a->id==XZ_SHELL_SRC_USB2){
    int slot=a->id==XZ_SHELL_SRC_USB1?0:1;
    if(usb[slot].lib)browse_source=slot;
    else{refuse();notice_locked(now,slot?"USB2 IS NOT CONNECTED":"USB1 IS NOT CONNECTED");}
   }else if((a->id==XZ_SHELL_SRC_LINK||a->id==XZ_SHELL_SRC_COMPUTER)&&source_model.device[a->id-1].status==XZ_SHELL_DEVICE_STOCK&&
            ((source_model.device[a->id-1].known>>XZ_SHELL_D_STATUS)&1)){
    /* Stock's own key for that slot, as one press would: queued for the native key task (ui_runtime.c runs
       it after this encoder push). A touch runs on another thread, where stock's key handlers never run. */
    if(push_input)stock_key_pending=a->id==XZ_SHELL_SRC_COMPUTER;
    else{refuse();notice_locked(now,"TOUCH CANNOT OPEN THIS YET: PUSH THE ENCODER");}
   }else{refuse();notice_locked(now,a->id==XZ_SHELL_SRC_LINK?"LINK BROWSING IS NOT AVAILABLE YET":"COMPUTER BROWSING IS NOT AVAILABLE YET");}
   break;
  case XZ_SHELL_ACT_PLAYLIST:
   if(!(model.capabilities&XZ_SHELL_CAP_PLAYLIST_EDIT)||browse_source<0||pl_count>=PL_QUEUE){
    refuse();notice_locked(now,browse_source>=0&&usb[browse_source].rdonly?"THIS STICK IS READ-ONLY: PLAYLISTS CANNOT CHANGE":"PLAYLIST CHANGE NOT APPLIED");break;
   }
   pl_queue[(pl_head+pl_count)%PL_QUEUE].slot=browse_source;
   pl_queue[(pl_head+pl_count)%PL_QUEUE].e=shell.edit;
   pl_queue[(pl_head+pl_count)%PL_QUEUE].e.op=(enum xz_shell_playlist_op)a->id;
   /* NEW lands in the playlist folder that is open (the shell sends no parent). */
   if(a->id==XZ_SHELL_PL_CREATE)pl_queue[(pl_head+pl_count)%PL_QUEUE].e.playlist=open_folder(browse_source);
   pl_count++;pthread_cond_signal(&wake);
   break;
  case XZ_SHELL_ACT_STEM_GAIN:
   /* The pads' own stem level (XZ_UI_LEVEL, 0..1); a level above 0 unmutes, as on the pads. */
   if(!(model.capabilities&XZ_SHELL_CAP_STEM_GAIN)||a->deck<0||a->deck>=XZ_SHELL_DECKS||a->id<0||a->id>2||a->value<0||a->value>XZ_SHELL_STEM_GAIN_MAX){refuse();break;}
   ui_action_locked(XZ_UI_LEVEL,a->deck,a->id,(float)a->value/(float)XZ_SHELL_STEM_GAIN_MAX,now);
   break;
  case XZ_SHELL_ACT_STEM_MUTE:
   if(!(model.capabilities&XZ_SHELL_CAP_STEM_MUTE)||a->deck<0||a->deck>=XZ_SHELL_DECKS||a->id<0||a->id>2||stem_count>=8){refuse();break;}
   stem_queue[stem_count].deck=a->deck;stem_queue[stem_count].stem=a->id;stem_queue[stem_count].value=a->value!=0;stem_count++;
   __atomic_or_fetch(&ui_requests,XZ_SHELL_UI_STEM_MUTE,__ATOMIC_ACQ_REL);ui_requests_at=now?now:1;
   break;
  case XZ_SHELL_ACT_ZOOM:save_display(XZ_SHELL_ITEM_WAVE_ZOOM,a->value);break; /* shell state, kept */
  case XZ_SHELL_ACT_MY_SETTINGS:
   if(a->id==XZ_SHELL_MY_LOAD)my_settings_load(a->value,a->deck,now);
   else if(a->id==XZ_SHELL_MY_SAVE)my_settings_save(a->value,now,NULL,0,"SAVED TO USB %d");
   else refuse();
   break;
  case XZ_SHELL_ACT_TAG_LIST:case XZ_SHELL_ACT_HISTORY:{
   int cap=a->kind==XZ_SHELL_ACT_TAG_LIST?XZ_SHELL_CAP_TAG_EDIT:XZ_SHELL_CAP_HISTORY_EDIT;
   if(!(model.capabilities&cap)||browse_source<0||le_count>=8){
    refuse();notice_locked(now,browse_source>=0&&usb[browse_source].rdonly?"THIS STICK IS READ-ONLY: NOTHING CHANGES":"THAT CHANGE IS NOT APPLIED");break;
   }
   struct list_edit *q=&le_queue[le_count++];memset(q,0,sizeof *q);
   q->slot=browse_source;q->kind=a->kind==XZ_SHELL_ACT_TAG_LIST?LE_TAG:LE_HISTORY;q->op=a->id;q->value=(uint32_t)a->value;
   if(q->kind==LE_TAG&&a->id==XZ_SHELL_TAG_ADD_LIST){
    /* The folder or playlist row of the shown list: its tracks are the list one level down. */
    int k=shown_list(NULL);q->req=lists[k].req;
    if(q->req.depth>=XZ_SHLIB_PATH_DEPTH){le_count--;refuse();break;}
    q->req.path[q->req.depth++]=(uint32_t)a->value;q->req.sort_set=0;memset(&q->req.filter,0,sizeof q->req.filter);
    if(q->req.screen==XZ_SHELL_SEARCH||q->req.screen==XZ_SHELL_TAG_LIST)q->req.screen=XZ_SHELL_BROWSE;
   }
   pthread_cond_signal(&wake);
   break;
  }
  case XZ_SHELL_ACT_SET_MOD:
   if(a->deck==LOOK_TAB)look_set_locked(a->id,a->value,now);
   else if(a->deck==BUTTONS_TAB&&(a->id==BUTTONS_PRESS||a->id==BUTTONS_SHIFT)&&a->value>=0&&a->value<XZ_UI_RB_CHOICES)
    ui_action_locked(XZ_UI_REKORDBOX_ASSIGN,-1,a->id-BUTTONS_PRESS,(float)a->value,now);
   else refuse();
   break;
  case XZ_SHELL_ACT_DJ_PROFILE:{
   /* deck = the stick; value = the profile index in my_settings.profiles. */
   int slot=a->deck==XZ_SHELL_SRC_USB1?0:a->deck==XZ_SHELL_SRC_USB2?1:-1;
   if(slot!=browse_source||a->value<0||a->value>=mys_model.profile_count){refuse();break;}
   const struct xz_shell_dj_profile *p=&mys_profiles[a->value];
   char text[48];
   if(a->id==XZ_SHELL_PROFILE_LOAD){
    if(p->damaged){notice_locked(now,"THIS DJ PROFILE CANNOT BE READ");break;}
    /* Active on its stick through dj_runtime (the MODS panel's own pick), then the stick's MY SETTINGS:
       a profile carries no settings file of its own. */
    if(mys_model.profiles_switchable&&a->value!=mys_model.profile_active){
     dj_pick=mys_profile_entry[a->value];
     __atomic_or_fetch(&ui_requests,XZ_SHELL_UI_DJ_PICK,__ATOMIC_ACQ_REL);ui_requests_at=now?now:1;
    }
    snprintf(text,sizeof text,"DJ PROFILE: %.30s",p->name?p->name:"---");notice_locked(now,text);
    if(xz_shms_any(&mys[slot])&&!util.trial)my_settings_load(a->deck,shell.deck,now);
   }else if(a->id==XZ_SHELL_PROFILE_SAVE){
    snprintf(text,sizeof text,"SAVED TO %.30s",p->name?p->name:"USB");
    my_settings_save(a->deck,now,NULL,0,text);
   }else refuse();
   break;
  }
  case XZ_SHELL_ACT_MY_SETTING_SET:{
   int slot=a->deck==XZ_SHELL_SRC_USB1?0:a->deck==XZ_SHELL_SRC_USB2?1:-1;
   static struct xz_shms_files edited;enum ms_kind kind;
   if(slot<0||!usb[slot].lib){refuse();break;}
   edited=mys[slot];
   if(xz_shms_set(&edited,a->id,a->value,&kind)){refuse();notice_locked(now,"THAT SETTING CANNOT CHANGE HERE");break;}
   my_settings_save(a->deck,now,&edited,1u<<kind,"USB %d: SETTING SAVED");
   break;
  }
  case XZ_SHELL_ACT_ACTIVE_DECK:case XZ_SHELL_ACT_NONE:break; /* shell state */
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
static void deliver_key_locked(int key, int deck, uint32_t now) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,(enum xz_shell_key)key,deck};
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
   input.key_original=in->key[d].original;input.key_cents=in->key[d].cents;input.key_synced=in->key[d].synced;
   input.known|=K(KEY)|K(KEY_SHIFT)|K(KEY_ORIGINAL)|K(KEY_SYNC);
  }
  struct xz_arcade_track_clock clock;
  engine[d].ok=0;
  if(has(G_CLOCK)&&xz_arcade_native_clock_read(plain_read,NULL,(unsigned)d,&clock)){
   engine[d].ok=1;engine[d].position_ms=clock.position_ms;engine[d].speed=clock.speed;
   input.playing=!!clock.playing;
   int beat=(int)floorf(clock.bar_phase)+1;
   input.bar_phase=beat<1?1:beat>4?4:beat;
   input.known|=K(PLAYING)|K(BAR_PHASE);
   sec.clock_reads++;
  }
  if(grid_count[d]){input.grid=grid_cache[d];input.grid_count=grid_count[d];input.known|=K(GRID);}
  if(memory_count_cache[d]>=0){memcpy(input.memory_cue,memory_cache[d],sizeof input.memory_cue);input.memory_count=memory_count_cache[d];input.known|=K(MEMORY_CUES);}
  /* The loaded track's artist from the export of the stick it came from (the stat frame has none). */
  if(settled(&loaded[d])&&(loaded[d].device_kind==2||loaded[d].device_kind==3)&&usb[loaded[d].device_kind-2].lib){
   const struct xz_shlib *lib=usb[loaded[d].device_kind-2].lib;
   const struct xz_shlib_track *t=xz_shlib_track(lib,loaded[d].id);
   const char *artist=t?xz_shlib_text(lib,t->artist):NULL;
   if(artist&&artist[0]){input.artist=artist;input.known|=K(ARTIST);}
   played_add(loaded[d].device_kind,loaded[d].id);
  }
  unsigned kind=loaded[d].known?loaded[d].device_kind:0;
  if(kind==2||kind==3)input.known|=K(SOURCE);
  /* The stick's MY SETTINGS colours. The background colour waits for proof of where stock reads it. */
  if((kind==2||kind==3)&&settled(&loaded[d])&&xz_shms_any(&mys[kind-2])){
   input.look.has_background=0;input.look.background_rgb=0;input.look.wave_color=xz_shms_wave_color(&mys[kind-2]);
   input.known|=K(SOURCE_LOOK);
  }
  xz_shell_model_set_input(storage,d,&input,kind);
 }
 apply_waves_locked();
 unsigned changed=xz_shell_model_refresh(&model,storage,model_read,now);
 matching_follow();
 if(debug){
  /* BEAT FX channel selector and the rest of the record, raw, on change: the channel label mapping is not
     proven statically (E:/Claude/xz-fx-research-20261018/contracts.md); a selector sweep proves it. */
  static unsigned char last_fx[12];static int last_fx_ok;unsigned char fx[12];
  if(xz_shell_model_fx_raw(storage,fx)&&(!last_fx_ok||memcmp(fx,last_fx,12))){
   char line[120];int at=snprintf(line,sizeof line,"SHELL_FX_RAW: channel_raw=%u record=",fx[1]);
   for(int k=0;k<12&&at>0&&at<(int)sizeof line-3;k++)at+=snprintf(line+at,sizeof line-(size_t)at,"%02x",fx[k]);
   xz_log(line);memcpy(last_fx,fx,12);last_fx_ok=1;
  }
 }
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(changed>>d&1u){
  memory_count_cache[d]=-1;
  /* A new track: drop the old grid now, load the new wave and grid on the worker. */
  wave_job[d].wanted++;wave_job[d].retry_at=0;wave_job[d].requested_us=mono_us();grid_count[d]=0;pthread_cond_signal(&wake);
  xz_shrt_position_reset(&position[d]);memset(&motion[d],0,sizeof motion[d]);
 }
 /* Playhead. The position comes from the audio engine (native clock: the player's sample position and its
    real speed with pitch and sync), read this refresh. The UI stat frame's elapsed time is the fallback
    when the engine read fails, or disagrees by more than ENGINE_AGREE_MS (a reading this module does not
    understand, e.g. a sample rate other than the 44.1 kHz the clock assumes). PLAYING comes from the
    native clock; without it, a stat-frame elapsed time that keeps changing is the proof of motion. The
    published elapsed time is the smoothed line the renderer extrapolates per frame (elapsed_at_ms). */
 unsigned moving=0,following=0;
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  struct xz_shell_deck *k=&model.deck[d];
  if(!(k->known&K(ELAPSED))||!(k->known&K(LOADED))||!k->loaded){xz_shrt_position_reset(&position[d]);memset(&motion[d],0,sizeof motion[d]);continue;}
  int moved=xz_shrt_motion_step(&motion[d],k->elapsed_ms,k->elapsed_at_ms);
  if(!(k->known&K(PLAYING))){k->playing=moved;k->known|=K(PLAYING);}
  int32_t ui_ms=k->elapsed_ms;uint32_t ui_at=k->elapsed_at_ms;
  int32_t source_ms=ui_ms;uint32_t source_at=ui_at;
  int32_t rate=xz_shrt_rate_from_tempo(k->known&K(TEMPO)?k->tempo_x100:0),engine_rate=0;int from_engine=0;int32_t diff=0;
  if(engine[d].ok&&engine[d].position_ms<=(uint32_t)INT32_MAX&&isfinite(engine[d].speed)&&engine[d].speed>.25f&&engine[d].speed<4.f){
   engine_rate=(int32_t)(engine[d].speed*1e6f+.5f);
   /* The stat frame may be an older (held) reading: compare at the same instant. */
   int64_t ui_now=(int64_t)ui_ms+(k->playing?(int64_t)(int32_t)(now-ui_at)*rate/1000000:0);
   diff=(int32_t)((int64_t)engine[d].position_ms-ui_now);
   if(diff<ENGINE_AGREE_MS&&diff> -ENGINE_AGREE_MS){source_ms=(int32_t)engine[d].position_ms;source_at=now?now:1;rate=engine_rate;from_engine=1;}
  }
  /* A jog hold, scratch or brake moves the engine position at a speed other than the pitch speed: the
     playhead then follows each engine reading with no extrapolation (and the model refreshes every frame). */
  if(xz_shrt_position_step(&position[d],k->playing,source_ms,source_at,rate,from_engine,&k->elapsed_ms,&k->elapsed_at_ms))following|=1u<<d;
  if(k->playing)moving|=1u<<d;
  if(debug&&k->playing&&now-sync_logged[d]>=1000u){
   sync_logged[d]=now;char line[220];
   snprintf(line,sizeof line,"SHELL_SYNC: deck=%d source=%s engine_ms=%d ui_ms=%d diff_ms=%d published_ms=%d rate_ppm=%d engine_rate_ppm=%d tempo_x100=%d "
    "observed_ppm=%d following=%d",
    d+1,from_engine?"engine":"stat_frame",engine[d].ok?(int)engine[d].position_ms:-1,(int)ui_ms,engine[d].ok?(int)diff:0,(int)k->elapsed_ms,
    (int)rate,(int)engine_rate,k->known&K(TEMPO)?(int)k->tempo_x100:0,(int)position[d].observed_ppm,(int)((following>>d)&1u));
   xz_log(line);
  }
 }
 sec.moving|=moving;follow_bits=following;sec.following|=following;
 uint32_t raw_caution=xz_shell_model_caution(storage),caution=xz_shrt_caution_filter(&caution_state,raw_caution);
 if(caution!=raw_caution)xz_mods_shell_proof_v1.caution_filtered++;
 xz_shrt_handoff_caution(&handoff,caution);
 xz_mods_shell_proof_v1.caution=caution;
 if(handoff.handed){
  uint32_t since=handoff.since;int left=handoff.left_play;
  if(xz_shrt_handoff_step(&handoff,now,stock_play())){
   dirty_flag=1;
   if(debug){char line[120];snprintf(line,sizeof line,"SHELL_HANDOFF: resume after_ms=%u stock_left_play=%d",now-since,left);xz_log(line);}
  }
 }
 if(caution!=last_caution_logged){
  if(debug){char line[120];snprintf(line,sizeof line,"SHELL_CAUTION: read=0x%x yields=0x%x acknowledged=0x%x",raw_caution,caution,caution_state.ack);xz_log(line);}
  last_caution_logged=caution;
 }
 uint32_t period=xz_shrt_frame_period_ms(xz_shell_screen(&shell),moving!=0);
 if(period<__atomic_load_n(&period_ms,__ATOMIC_RELAXED))dirty_flag=1; /* playback started: draw now */
 __atomic_store_n(&period_ms,period,__ATOMIC_RELAXED);
 refreshed=1;
 fill_slots_locked();
 last_refresh_ms=now;
 uint32_t took=(uint32_t)(now_us()-began);
 sec.refresh_us+=took;sec.refreshes++;if(took>sec.refresh_max)sec.refresh_max=took;
}

/* ---- entry points ---- */
int xz_shell_runtime_owns(void) { return __atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE); }
int xz_shell_runtime_will_refresh(void) {
 return __atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE)&&now_ms()-__atomic_load_n(&last_refresh_ms,__ATOMIC_RELAXED)>=XZ_SHRT_REFRESH_MS-XZ_SHRT_PRESENT_LEAD_MS;
}
int xz_shell_runtime_idle(void) {
 if(!__atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE)||__atomic_load_n(&dirty_flag,__ATOMIC_ACQUIRE))return 0;
 return now_ms()-__atomic_load_n(&last_drawn_ms,__ATOMIC_ACQUIRE)<__atomic_load_n(&period_ms,__ATOMIC_RELAXED);
}
int xz_shell_runtime_frame(uint16_t *pixels, size_t stride, const struct xz_shell_runtime_frame_input *in) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!in||!pixels||stride<XZ_SHELL_WIDTH)return 0;
 uint64_t began_us=now_us();
 uint32_t now=(uint32_t)(began_us/1000u);
 /* Input threads, the owner tick and the worker hold the lock only briefly. Waiting up to FRAME_LOCK_US
    still leaves the frame time to render; giving up at once skipped a whole vsync. */
 if(pthread_mutex_trylock(&mutex)){
  struct timespec until;uint64_t at=began_us+FRAME_LOCK_US;until.tv_sec=(time_t)(at/1000000u);until.tv_nsec=(long)(at%1000000u)*1000L;
  if(pthread_mutex_timedlock(&mutex,&until)){
   __atomic_add_fetch(&sec.busy,1,__ATOMIC_RELAXED);
   /* No frame: the worst kind. The holder is read without the lock (debug text only). */
   uint32_t waited=(uint32_t)(now_us()-began_us);
   if(waited>=worst_pending.total_us){worst_pending=(struct worst_frame){waited,waited,0,0,lock_holder,"lock timeout",1};}
   return __atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE)?-1:0;
  }
  sec.lock_waits++;
 }
 struct worst_frame wf={0,(uint32_t)(now_us()-began_us),0,0,lock_holder,NULL,0};
 lock_holder="frame";
 sec.calls++;xz_mods_shell_proof_v1.render_calls++;
 if(in->cached)sec.ui_cached++;
 if(xz_native_skin_on_owner_thread()){sec.flips++;xz_mods_shell_proof_v1.stock_flips++;}
 last_call_ms=now;last_input=*in;
 int result=0;
 if(!enabled||!candidate(in->eligible,in->theme)){set_owns(0);goto done;}
 int owned=owns_flag,due=dirty_flag||now-last_drawn_ms>=period_ms;
 if(owned&&!due){sec.skipped++;result=-1;goto done;}
 /* Off screen (handed off or yielding to a caution) the model still refreshes, at 10 Hz, to see the way back. */
 if(!owned&&refreshed&&now-last_refresh_ms<100u)goto done;
 /* The model refreshes at most every XZ_SHRT_REFRESH_MS (the native reads cost more than a frame can
    spare); frames in between redraw the same model with a newer clock, and the renderer advances the
    playhead from elapsed_at_ms. */
 if(!owned||!refreshed||follow_bits||now-last_refresh_ms>=XZ_SHRT_REFRESH_MS){
  uint64_t r0=now_us();refresh_locked(now,in);wf.refresh_us=(uint32_t)(now_us()-r0);
 }
 if(!owns_now(in->eligible,in->theme)){set_owns(0);goto done;}
 set_owns(1);
 model.stems_known=in->stems_known;model.stems_on=in->stems_on;
 /* Stem mute state and pad colours from the UI model (the deck's own refresh does not read them). */
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(in->stem_muted_known[d]){model.deck[d].stem_muted=(uint8_t)(in->stem_muted[d]&7u);model.deck[d].known|=K(STEM_MUTE);}
 model.stem_rgb_known=in->stem_rgb_known;memcpy(model.stem_rgb,in->stem_rgb,sizeof model.stem_rgb);
 /* The colours the DJ picked (not THEME DEFAULT); the rest draw the shell's own defaults. */
 model.stem_rgb_custom=0;
 if(in->look_known)for(int i=0;i<3;i++)if(in->stem_colors[i]>0)model.stem_rgb_custom|=(uint8_t)(1u<<i);
 /* The stem row layout: the one stems_overlay setting classic uses too. */
 model.stem_layout_known=in->look_known&&in->stems_overlay>=0&&in->stems_overlay<=XZ_SHELL_STEMS_OUTSIDE_HALF;
 model.stem_layout=model.stem_layout_known?(enum xz_shell_stem_layout)in->stems_overlay:XZ_SHELL_STEMS_HIDDEN;
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(in->stem_level_known[d]){
  for(int i=0;i<3;i++){float v=in->stem_level[d][i];int g=(int)(v*(float)XZ_SHELL_STEM_GAIN_MAX+.5f);model.deck[d].stem_gain[i]=(uint8_t)(g<0?0:g>XZ_SHELL_STEM_GAIN_MAX?XZ_SHELL_STEM_GAIN_MAX:g);}
  model.deck[d].known|=K(STEM_GAIN);
 }
 /* Header requests ui_runtime.c never took (its weak hook missing, or no input thread ran) cannot run. */
 if(ui_requests_at&&now-ui_requests_at>=500u){
  unsigned stale=__atomic_exchange_n(&ui_requests,0u,__ATOMIC_ACQ_REL);ui_requests_at=0;
  if(stale)notice_locked(now,stale&XZ_SHELL_UI_OPEN_MODS?"THE MODS PANEL DID NOT OPEN":"THE STEMS ROW DID NOT CHANGE");
 }
 /* The frame's clock is the moment it is scanned out: the receiver's next vblank (it shows this frame
    then), plus the time the beam takes to reach the middle of the screen. Without the receiver's clock,
    one presenter period ahead. Audio output latency is not known and is not applied. */
 {
  uint64_t present=next_present?next_present():0,t=now_us();
  model.clock_ms=present>t&&present-t<100000u?(uint32_t)((present+SCANOUT_MID_US)/1000u):(uint32_t)(t/1000u)+XZ_SHRT_PRESENT_LEAD_MS;
 }
 publish_notice_locked();
 uint64_t began=now_us();
 result=xz_shell_render(&shell,&model,pixels,stride)?1:-1;
 uint32_t took=(uint32_t)(now_us()-began);
 sec.frames++;sec.render_us+=took;if(took>sec.render_max)sec.render_max=took;
 wf.render_us=took;wf.total_us=(uint32_t)(now_us()-began_us);
 if(wf.total_us>=worst_pending.total_us){
  wf.cause=wf.lock_wait_us>=wf.refresh_us&&wf.lock_wait_us>=wf.render_us?"lock wait":wf.refresh_us>=wf.render_us?"refresh":"render";
  worst_pending=wf;
 }
 /* The receiver presents what this call drew on its next vsync, so the start of the call is the frame's phase. */
 xz_shrt_frame_mark(&sec.shown,began_us);
 xz_mods_shell_proof_v1.frames++;
 step_jitter_locked();
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
 lock_as("key");
 int owns=owns_now(eligible,theme);
 if(phase==XZ_SHRT_PRESS){
  enum xz_shrt_rb_effect effect=XZ_SHRT_RB_NONE;
  enum xz_shrt_verdict v=!owns?XZ_SHRT_PASS:map->rule==XZ_SHRT_ASSIGNED?
   xz_shrt_rekordbox_verdict(__atomic_load_n(&rekordbox_action,__ATOMIC_ACQUIRE),&effect):xz_shrt_key_verdict(&shell,&model,map);
  swallow=v==XZ_SHRT_SWALLOW;
  xz_shrt_contact_press(&contacts,code,channel,swallow);
  if(v==XZ_SHRT_PASS_HAND_OFF){
   /* MENU goes to stock whole, but stock's short MENU on PLAY opens nothing: the shell steps aside only
      when the hold arrives (stock opens UTILITY then). */
   if(map->key==XZ_SHELL_KEY_MENU){menu_passed=1;xz_mods_shell_proof_v1.menu_passes++;}
   else{
    hand_off_locked(now,"key",code);xz_mods_shell_proof_v1.key_hand_offs++;
    /* TAG TRACK leaves stock on its play screen: come back soon after the release. */
    if(map->key==XZ_SHELL_KEY_TAG_TRACK)quick_return=1;
   }
  }
  if(swallow){
   uint32_t loads=load_actions;
   int key=xz_shrt_key_for_phase(map,phase,0);if(key>=0)deliver_key_locked(key,map->deck,now);
   /* REKORDBOX assigned to a shell action: MODS as the MODS key, STEMS as the header STEMS button. */
   if(effect==XZ_SHRT_RB_SHELL_MODS)deliver_key_locked(XZ_SHELL_KEY_MODS,-1,now);
   else if(effect==XZ_SHRT_RB_STEMS){struct xz_shell_action stems={XZ_SHELL_ACT_TOGGLE_STEMS,-1,0,0};execute_locked(&stems,1,now);}
   if(map->key==XZ_SHELL_KEY_TAG_TRACK&&!(model.capabilities&XZ_SHELL_CAP_TAG_TRACK))
    notice_locked(now,"TAG TRACK WORKS FROM THE PLAY SCREEN");
   if(map->key==XZ_SHELL_KEY_LOAD&&loads==load_actions)
    notice_locked(now,list_index(xz_shell_screen(&shell))>=0?"SELECT A TRACK, THEN PRESS LOAD":"OPEN A TRACK LIST TO LOAD");
  }
  if(key_log_allowed(now)){
   char line[200];
   snprintf(line,sizeof line,"SHELL_KEY: code=0x%x channel=%u op=%u phase=press verdict=%s owns=%d shown=%d handed=%d caution=0x%x eligible=%d screen=%s dropped=%u",
    code,channel,op,verdict_names[v],owns,(int)__atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE),handoff.handed,(unsigned)caution_state.shown,
    candidate(eligible,theme),xz_shell_screen_name(xz_shell_screen(&shell)),key_log.dropped);
   key_log.dropped=0;xz_log(line);
  }
 }else{
  int held=0,decision=xz_shrt_contact_follow(&contacts,code,channel,phase,&held);
  if(decision>0){
   swallow=1;
   int key=xz_shrt_key_for_phase(map,phase,held);
   if(key>=0&&owns)deliver_key_locked(key,map->deck,now);
  }else if(decision==0&&map->key==XZ_SHELL_KEY_BACK&&phase==XZ_SHRT_HOLD&&!held&&xz_shrt_handoff_yields(&handoff)&&candidate(eligible,theme)){
   /* BACK held over stock brings the shell back; the key itself still belongs to stock. Over a
      caution it also acknowledges that caution, so a caution id that stays latched cannot keep the
      shell away (it yields again when the caution changes). */
   if(handoff.caution){xz_shrt_caution_ack(&caution_state);handoff.caution=0;}
   if(handoff.handed)xz_shrt_handoff_end(&handoff);
   xz_mods_shell_proof_v1.back_hold_resumes++;dirty_flag=1;
   if(debug)xz_log("SHELL_HANDOFF: resume by BACK hold");
  }else if(decision==0&&map->key==XZ_SHELL_KEY_MENU&&menu_passed){
   /* The MENU passed to stock: its hold opens UTILITY, so the shell steps aside now and comes back when
      stock returns to PLAY. A short release changed nothing in stock and the shell stays. */
   if(phase==XZ_SHRT_HOLD&&!held&&owns){hand_off_locked(now,"menu_hold",code);xz_mods_shell_proof_v1.key_hand_offs++;}
   if(phase==XZ_SHRT_RELEASE)menu_passed=0;
  }
  if(phase==XZ_SHRT_RELEASE&&key_log_allowed(now)){
   char line[200];
   snprintf(line,sizeof line,"SHELL_KEY: code=0x%x channel=%u op=%u phase=%s held=%d result=%s owns=%d%s",
    code,channel,op,phase_names[phase],held,decision<0?"unknown_pass":decision?"swallow":"pass",owns,
    menu_inert?" note=stock_menu_popup_inert(no proven path)":"");
   xz_log(line);
  }
  if(phase==XZ_SHRT_RELEASE)menu_inert=0;
  if(decision==0&&quick_return&&map->key==XZ_SHELL_KEY_TAG_TRACK&&phase==XZ_SHRT_RELEASE){xz_shrt_handoff_shorten(&handoff,now,300u);quick_return=0;}
 }
 if(swallow)xz_mods_shell_proof_v1.keys_swallowed++;else xz_mods_shell_proof_v1.keys_passed++;
 pthread_mutex_unlock(&mutex);
 return swallow;
}
int xz_shell_runtime_touch(int eligible, int theme, int x, int y, int down, int stock_down) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return XZ_SHELL_TOUCH_TO_STOCK;
 uint32_t now=now_ms();
 x=x<0?0:x>=XZ_SHELL_WIDTH?XZ_SHELL_WIDTH-1:x;y=y<0?0:y>=XZ_SHELL_HEIGHT?XZ_SHELL_HEIGHT-1:y;
 lock_as("touch");
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
   push_input=1;deliver_locked(&e,now_ms());push_input=0;xz_mods_shell_proof_v1.encoder_events++;
  }
 }
 pthread_mutex_unlock(&mutex);
 return owns;
}

int xz_shell_runtime_take_stem_mute(int *deck, int *stem, int *value) {
 pthread_mutex_lock(&mutex);
 int got=stem_count>0;
 if(got){*deck=stem_queue[0].deck;*stem=stem_queue[0].stem;*value=stem_queue[0].value;memmove(stem_queue,stem_queue+1,(--stem_count)*sizeof *stem_queue);}
 pthread_mutex_unlock(&mutex);
 return got;
}
int xz_shell_runtime_take_ui_action(int *kind, int *deck, int *index, float *value) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 pthread_mutex_lock(&mutex);
 int got=ui_action_count>0;
 if(got){*kind=ui_actions[0].kind;*deck=ui_actions[0].deck;*index=ui_actions[0].index;*value=ui_actions[0].value;
  memmove(ui_actions,ui_actions+1,(size_t)(--ui_action_count)*sizeof *ui_actions);}
 pthread_mutex_unlock(&mutex);
 return got;
}
int xz_shell_runtime_take_dj_pick(void) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return -1;
 pthread_mutex_lock(&mutex);int entry=dj_pick;dj_pick=-1;pthread_mutex_unlock(&mutex);
 return entry;
}
int xz_shell_runtime_take_mods_page(void) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return -1;
 pthread_mutex_lock(&mutex);int page=mods_page;mods_page=-1;pthread_mutex_unlock(&mutex);
 return page;
}
void xz_shell_runtime_rekordbox_action(int action) { __atomic_store_n(&rekordbox_action,action,__ATOMIC_RELEASE); }
int xz_shell_runtime_mods_key(int eligible, int theme) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 uint32_t now=now_ms();
 lock_as("mods_key");
 int ok=candidate(eligible,theme)&&refreshed&&storage;
 if(ok){
  /* Over a hand-off the DJ asked for MODS: the shell comes back with it (a caution still yields). */
  if(handoff.handed)xz_shrt_handoff_end(&handoff);
  deliver_key_locked(XZ_SHELL_KEY_MODS,-1,now);dirty_flag=1;
 }
 pthread_mutex_unlock(&mutex);
 return ok;
}
int xz_shell_runtime_take_stock_key(void) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return -1;
 uint32_t now=now_ms();
 lock_as("stock_key");
 int which=stock_key_pending;stock_key_pending=-1;
 /* Step aside first, so the stock screen is what the DJ sees the moment stock opens it. */
 if(which>=0){hand_off_locked(now,"source_tile",which?0x202u:0x205u);xz_mods_shell_proof_v1.key_hand_offs++;}
 pthread_mutex_unlock(&mutex);
 return which;
}
void xz_shell_runtime_stock_key_ran(int which, int ran) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
 uint32_t now=now_ms();
 lock_as("stock_key");
 if(ran){
  /* No handler to call: come back and say so. */
  xz_shrt_handoff_end(&handoff);dirty_flag=1;refuse();
  notice_locked(now,which?"EXPORT MODE DID NOT OPEN":"LINK DID NOT OPEN");
 }
 if(debug){char line[96];snprintf(line,sizeof line,"SHELL_STOCK_KEY: %s ran=%d",which?"rekordbox":"link",ran);xz_log(line);}
 pthread_mutex_unlock(&mutex);
}
unsigned xz_shell_runtime_take_ui_requests(void) { ui_requests_at=0;return __atomic_exchange_n(&ui_requests,0u,__ATOMIC_ACQ_REL); }
/* ui_runtime.c reports a header action it took but could not carry out (one short caps sentence). */
void xz_shell_runtime_notice(const char *text) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!text)return;
 pthread_mutex_lock(&mutex);notice_locked(0,text);pthread_mutex_unlock(&mutex);
}

/* ---- native UI thread ---- */
/* LOAD LOCK and every other stock refusal (Total_Main_CheckLoadExec, CheckLoadLock: a playing deck under
   LOAD LOCK, a track search, a jog touch) are decided by stock's own receiver, which shows its caution
   0x145. The shell only declines a deck whose load is already in progress or unreadable. */
enum { LOAD_OK=0, LOAD_GUARD=-1, LOAD_NOT_POSTED=-2, LOAD_STATE_UNREADABLE=-3, LOAD_DECK_LOADING=-4 };
static const char *load_reason(int rc) {
 return rc==LOAD_OK?"posted":rc==LOAD_GUARD?"code_guard":rc==LOAD_NOT_POSTED?"native_returned_0":
  rc==LOAD_STATE_UNREADABLE?"deck_state_unreadable":rc==LOAD_DECK_LOADING?"deck_loading":"unknown";
}
static int load_refused(const struct xz_shrt_command *c) {
 uint32_t state;
 if(!word(TOTAL_WK+0x1610u+(uint32_t)c->deck*0x60u,&state))return LOAD_STATE_UNREADABLE;
 return state==7||state==9?LOAD_DECK_LOADING:LOAD_OK;
}
/* Debug only: the raw LOAD LOCK setting (id 0x60), read in bulk, for the SHELL_COMMAND line. The shell
   does not act on it any more; stock's receiver applies LOAD LOCK itself. UINT32_MAX = unreadable. */
static uint32_t debug_load_lock(void) {
 uint32_t settings,list,count,value=UINT32_MAX;
 if(!has(G_SETTINGS)||!word(0x1c8a404,&settings)||!pointer(settings,0x170)||!word(settings+0x164,&list)||
    !word(settings+0x16c,&count)||!count||count>512||!pointer(list,(size_t)count*0x8c))return value;
 unsigned char *raw=malloc((size_t)count*0x8c);
 if(raw&&!xz_read_memory(list,raw,(size_t)count*0x8c))
  for(uint32_t i=0;i<count;i++){uint32_t id;memcpy(&id,raw+i*0x8c,4);if(id==0x60){memcpy(&value,raw+i*0x8c+0x88,4);break;}}
 free(raw);return value;
}
static int run_command(const struct xz_shrt_command *c) {
 switch(c->kind){
 case XZ_SHRT_CMD_TIME_MODE:
  if(!code_is(0xca308,set_time_mode_code,16))return -1;
  for(int ch=1;ch<=2;ch++)if(c->deck<0||c->deck==ch-1)((void(*)(int,int))(uintptr_t)0xca308)(ch-1,c->value?1:0); /* UiSetKeyTimeMode: 0 = channel 1, else channel 2 */
  return 0;
 case XZ_SHRT_CMD_QUANTIZE_BEAT:
  if(c->value<0||c->value>3||!code_is(0xc9304,set_quantize_code,16))return -1;
  ((void(*)(int))(uintptr_t)0xc9304)(c->value);
  return 0;
 /* XZ_SHRT_CMD_STOCK_MENU is never queued: the physical MENU key goes to stock instead. */
 case XZ_SHRT_CMD_UTILITY:{
  if(c->deck<0||c->deck>=XZ_SHUT_DEFS)return -1;
  const struct xz_shut_def *d=&xz_shut_defs[c->deck];
  if(!d->set||!code_is(d->set,d->set_code,32)||(d->apply&&!code_is(d->apply,d->apply_code,32)))return -1;
  /* Stock's menu applies the highlighted choice live (Pub_GuiCom_Change<item>) and then stores it
     (UiSetUtil<item>, which starts stock's own save). Same order, same argument. */
  if(d->apply)((void(*)(int))(uintptr_t)d->apply)((int)c->value);
  ((void(*)(int))(uintptr_t)d->set)((int)c->value);
  return 0;
 }
 case XZ_SHRT_CMD_LOAD:{
  if(c->deck<0||c->deck>=XZ_SHELL_DECKS||!code_is(0x135728,load_code,16)||!code_is(0x13ba38,music_id_code,8))return LOAD_GUARD;
  int refused=load_refused(c);
  if(refused)return refused;
  /* music_id {node 0 = this player, device kind 2 USB1 / 3 USB2, browse kind 1 (DB namespace, inferred), 0,
     export.pdb track id}. 1 means the request was posted (message 0x40e), not that the deck loaded:
     the owner tick reads the deck's loaded id a second later (SHELL_LOAD_CHECK). */
  unsigned char id[8]={0,c->device_kind,1,0};memcpy(id+4,&c->track_id,4);
  return ((int(*)(int,void *,int))(uintptr_t)0x135728)(c->deck,id,3)?LOAD_OK:LOAD_NOT_POSTED;
 }
 default:return -1;
 }
}
/* Reads every stock UTILITY item through its getter, on the owner tick (the getters read stock's settings
   set; the same thread runs the setters). Every 250 ms while the shell is up, every 2 s otherwise, and at
   once after a set. */
static void poll_utility(uint32_t now) {
 int shown=__atomic_load_n(&owns_flag,__ATOMIC_ACQUIRE);
 if(!util.poll_now&&now-util.polled_ms<(shown?250u:2000u))return;
 int32_t raw[XZ_SHUT_DEFS];uint8_t known[XZ_SHUT_DEFS];char version[24]={0};
 for(int i=0;i<XZ_SHUT_DEFS;i++){
  const struct xz_shut_def *d=&xz_shut_defs[i];known[i]=0;raw[i]=0;
  if(util.get_ok[i]){raw[i]=((int(*)(void))(uintptr_t)d->get)();known[i]=1;}
 }
 if(util.version_ok&&!util.version[0]){
  /* UiGetUtilVersion returns the version text stock prints after "Ver ". */
  uint32_t text=(uint32_t)(uintptr_t)((const char *(*)(void))(uintptr_t)XZ_SHUT_VERSION_GET)();
  char b[16];
  if(pointer(text,sizeof b)&&!xz_read_memory(text,b,sizeof b)){
   b[sizeof b-1]=0;int ok=b[0]!=0;
   for(int k=0;b[k];k++)if(b[k]<32||b[k]>126)ok=0;
   if(ok)snprintf(version,sizeof version,"VER %s",b);
  }
 }
 lock_as("tick");
 int changed=0;
 for(int i=0;i<XZ_SHUT_DEFS;i++)if(known[i]!=util.known[i]||(known[i]&&raw[i]!=util.raw[i])){changed=1;util.changes++;}
 memcpy(util.raw,raw,sizeof raw);memcpy(util.known,known,sizeof known);
 if(version[0]){memcpy(util.version,version,sizeof version);changed=1;}
 util.polled_ms=now;util.poll_now=0;util.polls++;
 if(changed){fill_settings();dirty_flag=1;}
 pthread_mutex_unlock(&mutex);
}
void xz_shell_runtime_owner_tick(void) {
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
 uint32_t now=now_ms();
 struct xz_shrt_command commands[XZ_SHRT_QUEUE];unsigned n=0;int pump=0;
 /* A frame holds the lock for its render; the tick tries again on the next native event. */
 if(pthread_mutex_trylock(&mutex)){__atomic_add_fetch(&sec.tick_busy,1,__ATOMIC_RELAXED);return;}
 lock_holder="tick";
 sec.ticks++;
 int check=0,check_deck=0,check_tries=0;uint32_t check_id=0,check_before=0;uint8_t check_kind=0;
 if(load_check.pending&&(int32_t)(now-load_check.at)>=0){
  check=1;check_deck=load_check.deck;check_id=load_check.id;check_before=load_check.before;check_kind=load_check.device_kind;check_tries=load_check.tries;load_check.pending=0;
 }
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
  const struct xz_shrt_command *c=&commands[i];
  struct loaded before={0,0,0,0};
  if(c->kind==XZ_SHRT_CMD_LOAD&&c->deck>=0&&c->deck<XZ_SHELL_DECKS)before=read_loaded(c->deck);
  int rc=run_command(c);
  uint32_t latency=now-c->at_ms;
  lock_as("tick");
  struct xz_shell_proof *p=&xz_mods_shell_proof_v1;
  p->last_command=(uint32_t)c->kind;p->last_command_result=(uint32_t)rc;
  if(rc)p->commands_refused++;else p->commands_run++;
  if(latency>p->command_latency_max_ms&&latency<60000u)p->command_latency_max_ms=latency;
  if(c->kind==XZ_SHRT_CMD_LOAD&&!rc){
   load_check.pending=1;load_check.deck=c->deck;load_check.id=c->track_id;load_check.device_kind=c->device_kind;
   load_check.before=before.known?before.id:UINT32_MAX;load_check.at=now_ms()+1000u;load_check.tries=0;
   /* The worker reads the new track's wave while the deck loads it. */
   if(c->device_kind==2||c->device_kind==3){
    hint[c->deck].pending=1;hint[c->deck].slot=c->device_kind-2;hint[c->deck].id=c->track_id;hint[c->deck].posted_us=mono_us();
    pthread_cond_signal(&wake);
   }
  }else if(c->kind==XZ_SHRT_CMD_UTILITY){
   if(rc){util.set_fails++;notice_locked(now_ms(),XZ_SHUT_NOTE_FIRMWARE);}else util.sets++;
   util.poll_now=1;
  }else if(c->kind==XZ_SHRT_CMD_LOAD){
   char text[48];
   snprintf(text,sizeof text,rc==LOAD_DECK_LOADING?"DECK %d IS STILL LOADING":"DECK %d DID NOT ACCEPT THE LOAD",c->deck+1);
   notice_locked(now_ms(),text);
  }
  dirty_flag=1;
  pthread_mutex_unlock(&mutex);
  if(debug){
   char line[260];
   if(c->kind==XZ_SHRT_CMD_LOAD)
    snprintf(line,sizeof line,"SHELL_COMMAND: kind=%d deck=%d track_id=%u device_kind=%u result=%d reason=%s state_word=%u loaded_before=%u "
     "load_lock_raw=0x%x latency_ms=%u",(int)c->kind,c->deck,c->track_id,(unsigned)c->device_kind,rc,load_reason(rc),
     before.known?before.state:UINT32_MAX,before.known?before.id:UINT32_MAX,debug_load_lock(),latency);
   else if(c->kind==XZ_SHRT_CMD_UTILITY&&c->deck>=0&&c->deck<XZ_SHUT_DEFS){
    const struct xz_shut_def *d=&xz_shut_defs[c->deck];
    int32_t back=d->get&&code_is(d->get,d->get_code,32)?((int(*)(void))(uintptr_t)d->get)():INT32_MIN;
    snprintf(line,sizeof line,"SHELL_UTILITY: item=%s arg=%d index=%d result=%d readback=%d applied_live=%d trial=%d latency_ms=%u",
     d->key,(int)c->value,xz_shut_index(d,c->value),rc,(int)back,d->apply!=0,util.trial,latency);
   }
   else snprintf(line,sizeof line,"SHELL_COMMAND: kind=%d deck=%d value=%d result=%d latency_ms=%u",(int)c->kind,c->deck,(int)c->value,rc,latency);
   xz_log(line);
  }
 }
 if(check){
  /* The LOAD request is asynchronous: report what the deck holds a second later. */
  struct loaded l=read_loaded(check_deck);
  int match=l.known&&l.id==check_id&&l.device_kind==check_kind;
  if(debug){
   char line[200];
   snprintf(line,sizeof line,"SHELL_LOAD_CHECK: deck=%d requested_id=%u device_kind=%u loaded_before=%u loaded_id=%u loaded_kind=%u state=%u match=%d",
    check_deck,check_id,(unsigned)check_kind,check_before,l.known?l.id:UINT32_MAX,(unsigned)l.device_kind,l.known?l.state:UINT32_MAX,match);
   xz_log(line);
  }
  int loading=l.known&&(l.state==7||l.state==9);
  lock_as("tick");
  if(!match&&loading&&check_tries<4&&!load_check.pending){
   /* Still loading: look again in a second. */
   load_check.pending=1;load_check.deck=check_deck;load_check.id=check_id;load_check.device_kind=check_kind;
   load_check.before=check_before;load_check.at=now_ms()+1000u;load_check.tries=check_tries+1;
  }else if(!match){
   char text[48];snprintf(text,sizeof text,"DECK %d DID NOT LOAD THE TRACK",check_deck+1);notice_locked(now_ms(),text);
   /* The stick may be gone or not ready on the deck: look at the mounts now. */
   scan_soon=1;pthread_cond_signal(&wake);
  }
  pthread_mutex_unlock(&mutex);
 }
 poll_utility(now_ms());
 if(pump)xz_native_skin_request_repaint();
}

static int flag(const char *name) { const char *v=getenv(name);return v&&!strcmp(v,"1"); }
/* Start and stop serialise on lifecycle: the interface switch stops on its own thread (the worker may
   be inside a USB read for a while) and a quick switch back waits for that stop to finish. */
static pthread_mutex_t lifecycle=PTHREAD_MUTEX_INITIALIZER;
static int start_locked(void);
static void stop_locked(void);
int xz_shell_runtime_start(void) { pthread_mutex_lock(&lifecycle);int rc=start_locked();pthread_mutex_unlock(&lifecycle);return rc; }
void xz_shell_runtime_stop(void) { pthread_mutex_lock(&lifecycle);stop_locked();pthread_mutex_unlock(&lifecycle); }
static void *stop_thread(void *unused) { (void)unused;xz_shell_runtime_stop();return NULL; }
int xz_shell_runtime_follow_interface(int modern) {
 if(modern)return __atomic_load_n(&enabled,__ATOMIC_ACQUIRE)?0:xz_shell_runtime_start();
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 /* Stop drawing now; the worker joins on a thread of its own so the caller (the UI lock) never waits. */
 pthread_t t;pthread_attr_t a;int started=0;
 if(!pthread_attr_init(&a)){
  if(!pthread_attr_setdetachstate(&a,PTHREAD_CREATE_DETACHED))started=!pthread_create(&t,&a,stop_thread,NULL);
  pthread_attr_destroy(&a);
 }
 if(!started)xz_shell_runtime_stop();
 xz_log("FLAGSHIP shell stopping: CLASSIC interface");
 return 0;
}
static int start_locked(void) {
 if(__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 /* Load by id is on unless XZ_MODS_SHELL_LOAD_BY_ID=0 (proven on the deck for USB2; see SHELL_LOAD_CHECK). */
 {const char *v=getenv("XZ_MODS_SHELL_LOAD_BY_ID");load_enabled=!(v&&!strcmp(v,"0"));}debug=flag("XZ_MODS_SHELL_DEBUG");
 next_present=(uint64_t(*)(void))dlsym(RTLD_DEFAULT,"xz_receiver_next_present_us_v1");
 /* calloc: shell_model.c requires zeroed storage. */
 storage=calloc(1,xz_shell_model_storage_size());
 if(!storage){xz_log("FLAGSHIP shell unavailable: no memory for the model");return -1;}
 xz_shell_init(&shell);
 restore_display();
 memset(&model,0,sizeof model);memset(&handoff,0,sizeof handoff);memset(&queue,0,sizeof queue);
 memset(position,0,sizeof position);memset(motion,0,sizeof motion);memset(&caution_state,0,sizeof caution_state);
 memset(&load_check,0,sizeof load_check);ui_requests=0;ui_requests_at=0;menu_passed=0;menu_inert=0;notice_pending=0;notice_text[0]=0;
 for(int k=0;k<L_COUNT;k++)list_reset(&lists[k],k);
 ui_action_count=0;look_classic_at=0;stock_key_pending=-1;push_input=0;
 le_count=0;
 memset(mys,0,sizeof mys);memset(&mys_job,0,sizeof mys_job);memset(mys_state,0,sizeof mys_state);memset(mys_result,0,sizeof mys_result);dj_pick=-1;mys_readonly=flag("XZ_MODS_SETTINGS_READONLY");
 memset(&util,0,sizeof util);memset(cue_cache,0,sizeof cue_cache);memset(&cue_want,0,sizeof cue_want);memset(previews,0,sizeof previews);preview_want.n=0;preview_clock=0;
 util.trial=flag("XZ_MODS_SETTINGS_READONLY")&&!flag("XZ_MODS_SHELL_UTILITY_WRITES");util.poll_now=1;
 check_guards();
 running=1;
 if(pthread_create(&worker_thread,NULL,worker,NULL)){running=0;free(storage);storage=NULL;xz_log("FLAGSHIP shell unavailable: worker thread");return -1;}
 worker_created=1;
 xz_mods_shell_proof_v1.enabled=1;xz_mods_shell_proof_v1.load_enabled=(uint32_t)load_enabled;
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);
 char line[200];
 int util_get=0,util_set=0;
 for(int i=0;i<XZ_SHUT_DEFS;i++){util_get+=util.get_ok[i];util_set+=!xz_shut_reason(&xz_shut_defs[i],util.get_ok[i],util.set_ok[i],0);}
 snprintf(line,sizeof line,"FLAGSHIP shell runtime armed: guards=0x%x load_by_id=%d debug=%d utility=%d/%d read %d writable trial=%d (theme 24 shows it)",
  guards,load_enabled,debug,util_get,XZ_SHUT_DEFS,util_set,util.trial);
 xz_log(line);
 return 0;
}
static void stop_locked(void) {
 if(!__atomic_exchange_n(&enabled,0,__ATOMIC_ACQ_REL))return;
 pthread_mutex_lock(&mutex);running=0;pthread_cond_signal(&wake);pthread_mutex_unlock(&mutex);
 if(worker_created){pthread_join(worker_thread,NULL);worker_created=0;}
 pthread_mutex_lock(&mutex);
 set_owns(0);
 model.browse=NULL;model.source=NULL;model.info=NULL;model.settings_screen=NULL;model.mods=NULL;model.my_settings=NULL;model.help=NULL;
 for(int k=0;k<L_COUNT;k++){retire_list(lists[k].result);lists[k].result=NULL;}
 for(int i=0;i<2;i++){if(usb[i].lib&&retired.libs<4)retired.lib[retired.libs++]=usb[i].lib;usb[i].lib=NULL;}
 for(int d=0;d<XZ_SHELL_DECKS;d++){free_wave(d);prefetch_drop(d);}
 for(int i=0;i<2;i++)analysis_drop(i); /* the worker has stopped: nothing else uses them */
 free(storage);storage=NULL;refreshed=0;
 xz_mods_shell_proof_v1.enabled=0;
 pthread_mutex_unlock(&mutex);
 drain_retired();
}
