/* SPDX-License-Identifier: MIT */
/* Tracklist on the unit: what the sampler reads, and how the writer stays out of the recorder's way.

   Sampler thread (normal priority, cores 0 and 2, 2 MiB stack), every 500 ms, memory reads only:
   - the recorder: xz_recorder_state (recorder/SEAM.md);
   - the mixer's ON AIR flags: mgr = [0x1c8981c], mixer = [mgr+0x50], one word at mixer+0x194. It is the
     last CRC-valid mixer frame's bytes 24..27, copied whole by MixerCpuCom::interrupt (0x25e1bc) after
     disassemble(); MixerCpuCom::getOnairState (0x25c8e0) returns it and UiGetMixerOnAir4ch (0xc87bc) maps
     channel 1..4 to bytes 3..0, value 1 = on air. Same root and object as the BEAT FX record the shell reads
     live at mixer+0x198 (shell_model.c fx_fields). Guarded by the code bytes of both getters;
   - DECK 1/2: the load record (shell_runtime.c read_loaded), the play flag (arcade/native_clock.c, the
     PlayEngine state block), the title stock shows (shell_model.c R5 record);
   - DECK 3/4: the players' published snapshot (extra_decks.h).
   It never calls a firmware function, never opens a file on a stick and never waits on the writer.

   On and off: tracklist_runtime.h. While RECORDING TRACKLIST is off the sampler does nothing; turning it off ends
   an open list with a note; turning it on skips a recording already running (its list would start mid-file).

   Writer thread (nice 19, idle I/O class): carries out the core's file operations (tracklist_file.c) and looks
   up title and artist by rekordbox id in the stick's export (xz_track_text, shell_runtime.c, at normal I/O
   priority because it may hold the deck 3/4 resolver's lock). Before each file operation it waits up to 2 s
   for the stick to have no request in flight. No descriptor stays open on a stick, no fork, no inotify.

   USB STOP (usb_release.h): the tracklist is a holder of the stick it writes to. The stop's ask (stock's UI thread,
   atomics and one xz_wake only) answers 0 when the tracklist is off or has neither an open list nor a queued
   operation on that slot. Otherwise it posts the stop and wakes the sampler at once; the sampler ends the open list
   (xz_tl_stick_stopped: every entry, the tail rule, the CUE sheet, a footer naming the stop), queues it and hands the
   slot to the writer as urgent. The writer writes every queued operation of that slot ahead of the others, without
   the at-the-end hold and without the quiet-stick wait, at nice 0 and best-effort I/O, answers once, then goes back
   to nice 19 and idle I/O and logs one line. A stick pulled without USB STOP still loses an at-the-end list. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tracklist_runtime.h"
#include "tracklist.h"
#include "tracklist_file.h"
#include "recorder_state.h"
#include "runtime.h"
#include "extra_decks.h"
#include "shell_model.h"
#include "arcade/native_clock.h"
#include "writes.h"
#include "xz_thread.h"
#include "xz_mutex.h"
#include "xz_wake.h"
#include "clock_compat.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#pragma weak xz_recorder_state
#pragma weak xz_extra_decks_snapshot
#pragma weak xz_shell_model_title
#pragma weak xz_arcade_native_clock_valid
#pragma weak xz_native_position_clock_read_timed
/* shell_runtime.c: title and artist of a track by rekordbox id from the stick's export. 0 found. */
int xz_track_text(int source,uint32_t track_id,char *title,size_t title_cap,char *artist,size_t artist_cap);
#pragma weak xz_track_text

__attribute__((visibility("default"))) struct xz_tracklist_proof xz_mods_tracklist_proof_v1={.version=1};
#define P xz_mods_tracklist_proof_v1

#define PERIOD_MS 500u
#define TOTAL_WK 0x2247cacu
#define TEXTS 8
#define IDLE_WAIT_MS 2000u

static pthread_t sampler,writer;
static int running,created,armed,wanted,env_mode=-1;  /* env_mode: -1 not set, 0 off, 1 at the end, 2 live */
static int enabled,skip_next;                         /* the setting as the sampler acts on it */
enum { MODE_NONE, MODE_AT_END, MODE_LIVE };
static pthread_mutex_t lock=XZ_MUTEX_INITIALIZER;
static struct xz_wake wake; /* wakes the writer; never a condition variable (xz_wake.h) */
static struct xz_tl_op ring[XZ_TL_OPS];static unsigned ring_head,ring_n;
static struct text { uint64_t key; int state; uint32_t used; char title[XZ_TL_TEXT], artist[XZ_TL_TEXT]; } texts[TEXTS];
enum { TEXT_EMPTY, TEXT_WANTED, TEXT_BUSY, TEXT_FOUND, TEXT_MISSING };
static struct xz_tl *core;
static uint32_t ring_dropped;
static int at_end_mode,recording_now;   /* at_end: the writer holds its queue while the stick records */

/* USB STOP (usb_release.h; the comment at the top). Weak: a build without usb_release.c has no holders. */
extern int xz_usb_release_holder(const char *,unsigned (*)(void *,int,uint32_t),void *) __attribute__((weak));
extern void xz_usb_release_answer(int,int,uint32_t) __attribute__((weak));
static const char *stick_mount="/media/usb";   /* slot N is "<stick_mount>N/"; a host test points it at a folder */
static int stop_holder=-1;
static struct xz_wake sampler_wake;            /* a stop wakes the sampler; never a condition variable (xz_wake.h) */
static uint32_t stop_asked[2],stop_asked_ms[2];/* atomics: the slot's latest stop the sampler has not taken yet */
static uint32_t stop_urgent[2];                /* atomics, set under lock: the slot's stop the writer serves next */
static int open_slot;                          /* atomic: the slot of the open list's recording, 0 none */
static int pending[2];                         /* atomics: operations queued or being written, per slot */
static long writer_tid;                        /* atomic */
static void writer_priority(long tid,int urgent);
static int slot_of(const char *path){
 size_t n=strlen(stick_mount);
 if(strncmp(path,stick_mount,n)||(path[n]!='1'&&path[n]!='2')||path[n+1]!='/')return 0;
 return path[n]-'0';
}

static uint64_t micros(clockid_t c){struct timespec t;if(xz_clock_gettime_compat(c,&t))return 0;return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static int arcade_read(void *ctx,uint32_t a,void *out,size_t n){(void)ctx;return xz_read_memory(a,out,n);}
static int model_read(uintptr_t a,void *out,size_t n){return a>UINT32_MAX?-1:xz_read_memory((uint32_t)a,out,n);}
static int word(uint32_t a,uint32_t *out){return !xz_read_memory(a,out,4);}
static int pointer(uint32_t p,uint32_t n){return p>=0x10000u&&!(p&3u)&&(uint64_t)p+n<=UINT32_MAX;}

/* ---- ON AIR ---- */
static int air_guard(void){
 static const unsigned char get_state[16]={0x94,0x31,0x90,0xe5,0x00,0x00,0xa0,0xe3,0x08,0xd0,0x4d,0xe2,0x53,0x14,0xe7,0xe7};
 /* UiGetMixerOnAir4ch: channel 0..3 loads byte 11, 10, 9, 8 of the stacked word (bytes 3..0). */
 static const struct { uint32_t at; unsigned char code[4]; } map[4]={
  {0xc8838,{0x0b,0x00,0xdd,0xe5}},{0xc884c,{0x0a,0x00,0xdd,0xe5}},{0xc8874,{0x09,0x00,0xdd,0xe5}},{0xc8860,{0x08,0x00,0xdd,0xe5}}};
 unsigned char b[16];
 if(xz_read_memory(0x25c8e0,b,16)||memcmp(b,get_state,16))return 0;
 for(int i=0;i<4;i++)if(xz_read_memory(map[i].at,b,4)||memcmp(b,map[i].code,4))return 0;
 return 1;
}
static int guard;                                    /* ON AIR code guard: 0 unchecked, 1 ok, -1 refused for good */
static void read_air(int out[4]){
 for(int i=0;i<4;i++)out[i]=-1;
 if(!guard)guard=air_guard()?1:-1;
 P.air_guard=guard==1;
 if(guard!=1)return;
 uint32_t mgr,mixer,value,again,check;P.air_reads++;
 if(!word(0x1c8981c,&mgr)||!pointer(mgr,0x54)||!word(mgr+0x50,&mixer)||!pointer(mixer,0x1a4)||
    !word(mixer+0x194,&value)||!word(mixer+0x194,&again)||value!=again||
    !word(0x1c8981c,&check)||check!=mgr||!word(mgr+0x50,&check)||check!=mixer){P.air_failures++;return;}
 P.air_raw=value;                                     /* little-endian: CH1 is the top byte */
 for(int ch=0;ch<4;ch++)out[ch]=(value>>(8*(3-ch))&0xffu)==1u;
}

/* ---- text by id (writer thread fills, sampler reads) ---- */
static struct text *text_slot(uint64_t key,uint32_t now){
 struct text *free_slot=NULL,*oldest=&texts[0];
 for(int i=0;i<TEXTS;i++){
  if(texts[i].key==key&&texts[i].state!=TEXT_EMPTY){texts[i].used=now;return &texts[i];}
  if(texts[i].state==TEXT_EMPTY&&!free_slot)free_slot=&texts[i];
  if(texts[i].state!=TEXT_BUSY&&(uint32_t)(now-texts[i].used)>(uint32_t)(now-oldest->used))oldest=&texts[i];
 }
 struct text *t=free_slot?free_slot:oldest->state!=TEXT_BUSY?oldest:NULL;
 if(!t)return NULL;
 memset(t,0,sizeof *t);t->key=key;t->state=TEXT_WANTED;t->used=now;
 xz_wake_set(&wake);
 return t;
}
/* Library text when found; otherwise the caller keeps what it has. */
static int text_for(uint64_t key,uint32_t now,char *title,char *artist){
 if(!key||!xz_track_text)return 0;
 pthread_mutex_lock(&lock);
 struct text *t=text_slot(key,now);int found=t&&t->state==TEXT_FOUND;
 if(found){memcpy(title,t->title,XZ_TL_TEXT);memcpy(artist,t->artist,XZ_TL_TEXT);}
 pthread_mutex_unlock(&lock);
 return found;
}

/* ---- the decks ---- */
static void native_deck(int deck,uint32_t now,struct xz_tl_deck_in *d){
 unsigned char a[16],b[16];uint32_t at=TOTAL_WK+0x1610u+(uint32_t)deck*0x60u;
 if(xz_read_memory(at,a,16)||xz_read_memory(at,b,16)||memcmp(a,b,16))return;   /* known stays 0 */
 uint32_t state,id;memcpy(&state,a,4);memcpy(&id,a+12,4);unsigned kind=a[9];
 d->known=1;d->loaded=state!=7&&state!=9&&id&&id!=UINT32_MAX;
 if(!d->loaded)return;
 d->track_key=kind==2||kind==3?(uint64_t)(kind-1)<<32|id:0;   /* USB1 = source 1, USB2 = source 2, as DECK 3/4 */
 struct xz_arcade_track_clock c;
 if(xz_native_position_clock_read_timed&&xz_arcade_native_clock_valid){
  static int clock_ok;if(!clock_ok)clock_ok=xz_arcade_native_clock_valid(arcade_read,NULL)?1:-1;
  if(clock_ok==1){xz_native_position_clock_read_timed(arcade_read,NULL,(unsigned)deck,&c,NULL,NULL);
   d->playing=c.observed&XZ_ARCADE_CLOCK_STATE?(int)c.playing:-1;}
 }
 if(text_for(d->track_key,now,d->title,d->artist)){d->text_known=1;return;}
 if(xz_shell_model_title&&xz_shell_model_title(model_read,(unsigned)deck,d->title,sizeof d->title)==1)d->text_known=1;
}
static void extra_decks(const int air[4],struct xz_tl_deck_in out[2]){
 static struct xz_extra_decks snap;                   /* sampler thread only */
 if(!xz_extra_decks_snapshot)return;
 int route=xz_extra_decks_snapshot(&snap);
 for(int i=0;i<2;i++){
  const struct xz_extra_deck *x=&snap.deck[i];struct xz_tl_deck_in *d=&out[i];
  d->known=1;d->loaded=x->loaded;
  if(!d->loaded)continue;
  d->track_key=(x->source==1||x->source==2)&&x->track_id?(uint64_t)x->source<<32|x->track_id:0;
  d->load_id=x->jog_content_epoch;
  if(x->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE)){snprintf(d->title,sizeof d->title,"%s",x->title);d->text_known=1;}
  if(x->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_ARTIST))snprintf(d->artist,sizeof d->artist,"%s",x->artist);
  d->playing=x->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING)?x->playing:-1;
  /* CH3/CH4 carry DECK 3/4 only while the route is up. The channel's input selector is not checked
     (REPORT.md: a CH3 switched to another source while DECK 3 plays would still count). */
  d->on_air=!route?0:air[2+i];
 }
}

/* ---- sampler ---- */
static void ring_push_locked(struct xz_tl_op *op){
 if(ring_n>=XZ_TL_OPS){xz_tl_op_free(op);ring_dropped++;return;}
 int slot=slot_of(op->path);if(slot)__atomic_add_fetch(&pending[slot-1],1,__ATOMIC_ACQ_REL);
 ring[(ring_head+ring_n)%XZ_TL_OPS]=*op;ring_n++;xz_wake_set(&wake);
}
static void take_ops(void){
 struct xz_tl_op op;
 pthread_mutex_lock(&lock);
 while(xz_tl_take(core,&op))ring_push_locked(&op);
 pthread_mutex_unlock(&lock);
 /* After the push: a stop's ask always sees the list either open or pending on its slot. */
 __atomic_store_n(&open_slot,slot_of(xz_tl_open_wav(core)),__ATOMIC_RELEASE);
}
static void sample(void){
 struct xz_tl_input in;memset(&in,0,sizeof in);
 uint64_t began=micros(CLOCK_MONOTONIC);
 in.now_ms=(uint32_t)(began/1000u);
 int on=__atomic_load_n(&enabled,__ATOMIC_ACQUIRE);P.enabled=(uint32_t)on;
 if(!on){
  /* Off: an open list ends with a note, nothing else is read. The writer still finishes what is queued, under the
     same rule (at the end: never while the stick records). */
  if(!skip_next){xz_tl_turn_off(core,in.now_ms);take_ops();skip_next=1;}
  return;
 }
 if(skip_next){xz_tl_skip_running(core);skip_next=0;}
 struct xz_recorder_state r;memset(&r,0,sizeof r);
 if(xz_recorder_state&&!xz_recorder_state(&r)&&r.known){
  in.rec.known=1;in.rec.recording=!!r.recording;in.rec.generation=r.generation;
  /* elapsed_ms 0 while recording means "not known" in the shared interface. */
  in.rec.elapsed_known=r.recording&&r.elapsed_ms>0;in.rec.elapsed_ms=r.elapsed_ms;
  memcpy(in.rec.file,r.file,sizeof in.rec.file);in.rec.file[sizeof in.rec.file-1]=0;
 }
 /* Kept true for a moment after the recorder becomes unreadable, so the at-the-end writer does not take a blip of
    the seam for the end of the recording: the core keeps its file open over such a gap too. */
 static uint32_t last_recording_ms;
 if(in.rec.known&&in.rec.recording)last_recording_ms=in.now_ms;
 int writer_hold=(in.rec.known&&in.rec.recording)||(!in.rec.known&&(uint32_t)(in.now_ms-last_recording_ms)<10000u);
 __atomic_store_n(&recording_now,writer_hold,__ATOMIC_RELEASE);
 P.rec_known=in.rec.known;P.recording=in.rec.recording;P.generation=in.rec.generation;P.elapsed_ms=in.rec.elapsed_ms;
 int air[4];read_air(air);
 for(int i=0;i<4;i++){in.deck[i].playing=-1;in.deck[i].on_air=air[i];}
 for(int i=0;i<2;i++)native_deck(i,in.now_ms,&in.deck[i]);
 extra_decks(air,&in.deck[2]);
 for(int i=2;i<4;i++)if(in.deck[i].loaded&&!in.deck[i].text_known)
  if(text_for(in.deck[i].track_key,in.now_ms,in.deck[i].title,in.deck[i].artist))in.deck[i].text_known=1;
 xz_tl_step(core,&in);
 take_ops();
 struct xz_tl_status st;xz_tl_status(core,&st);
 P.file_open=st.file_open;P.on_air_mode=st.on_air_mode;P.entries=st.entries;P.files=st.files;
 P.ops_dropped=ring_dropped+st.ops_dropped;P.entries_dropped=st.entries_dropped;P.samples++;
 uint64_t used=micros(CLOCK_MONOTONIC)-began;if(used>P.sample_us_max)P.sample_us_max=used>UINT32_MAX?UINT32_MAX:(uint32_t)used;
}
/* A stop the ask posted: end the slot's open list, queue it, and hand the slot to the writer as urgent. Also when
   the tracklist was turned off since the ask: what is queued for the slot still goes out. */
static void serve_stops(void){
 for(int slot=1;slot<=2;slot++){
  uint32_t stop=__atomic_exchange_n(&stop_asked[slot-1],0,__ATOMIC_ACQ_REL);
  if(!stop)continue;
  char mount[XZ_TL_PATH];snprintf(mount,sizeof mount,"%s%d/",stick_mount,slot);
  P.stop_lists+=(uint32_t)xz_tl_stick_stopped(core,mount,(uint32_t)(micros(CLOCK_MONOTONIC)/1000u));
  take_ops();
  /* An operation the writer is carrying out right now finishes at normal priority too. */
  long tid=__atomic_load_n(&writer_tid,__ATOMIC_ACQUIRE);if(tid)writer_priority(tid,1);
  pthread_mutex_lock(&lock);
  __atomic_store_n(&stop_urgent[slot-1],stop,__ATOMIC_RELEASE);xz_wake_set(&wake);
  pthread_mutex_unlock(&lock);
 }
}
static int stop_waiting(void){
 return __atomic_load_n(&stop_asked[0],__ATOMIC_ACQUIRE)||__atomic_load_n(&stop_asked[1],__ATOMIC_ACQUIRE);
}
static void *sampler_main(void *unused){
 (void)unused;
 uint64_t next=0;
 while(__atomic_load_n(&running,__ATOMIC_ACQUIRE)){
  serve_stops();
  uint64_t now=micros(CLOCK_MONOTONIC);
  if(now>=next){sample();next=now+PERIOD_MS*1000u;continue;}
  /* Until the next sample, or at once for a stop: the ticket is taken before the last look. */
  uint32_t ticket=xz_wake_ticket(&sampler_wake);
  if(stop_waiting())continue;
  xz_wake_wait(&sampler_wake,ticket,(unsigned)((next-now+999u)/1000u));
 }
 serve_stops();
 return NULL;
}

/* ---- writer ---- */
static void io_class_of(long tid,int idle){
#ifdef SYS_ioprio_set
 /* IOPRIO_WHO_PROCESS with id 0 is the calling thread. IDLE = class 3; BE level 4 = class 2. */
 int ok=syscall(SYS_ioprio_set,1,(int)tid,idle?3<<13:(2<<13|4))==0;
 if(idle)P.io_idle=ok;
#else
 (void)tid;(void)idle;
#endif
}
static void io_class(int idle){io_class_of(0,idle);}
/* The writer thread's priority (tid 0: the calling thread): urgent, for a USB STOP's write, nice 0 and best-effort
   I/O; else nice 19 and idle I/O. 19 to 0 needs CAP_SYS_NICE (the player runs as root); stop_nice_ok says if it took. */
static void writer_priority(long tid,int urgent){
 int ok=!setpriority(PRIO_PROCESS,(id_t)tid,urgent?0:19);
 io_class_of(tid,!urgent);
 if(urgent)__atomic_store_n(&P.stop_nice_ok,(uint32_t)ok,__ATOMIC_RELAXED);
}
static int urgent_slot(void){
 for(int slot=1;slot<=2;slot++)if(__atomic_load_n(&stop_urgent[slot-1],__ATOMIC_ACQUIRE))return slot;
 return 0;
}
/* Waits until the stick holding path has no request in flight (field 9 of its block stat), at most 2 s. */
static void wait_idle(const char *path){
 char dir[XZ_TL_PATH+8];snprintf(dir,sizeof dir,"%s",path);char *slash=strrchr(dir,'/');if(slash)*slash=0;
 struct stat st;if(stat(dir,&st))return;
 char stat_path[96];snprintf(stat_path,sizeof stat_path,"/sys/dev/block/%u:%u/stat",major(st.st_dev),minor(st.st_dev));
 P.idle_waits++;
 for(uint32_t waited=0;waited<IDLE_WAIT_MS;waited+=50u){
  if(urgent_slot())return;                            /* a USB STOP waits: no more waiting for a quiet stick */
  FILE *f=fopen(stat_path,"r");if(!f)return;
  char text[256];int ok=fgets(text,sizeof text,f)!=NULL;fclose(f);if(!ok)return;
  /* Not sscanf: the unit's glibc is too old for the toolchain's sscanf symbol (diary/runtime.c). */
  char *p=text;unsigned long long v=0;
  for(int field=0;field<9;field++){while(*p==' ')p++;char *end;v=strtoull(p,&end,10);if(end==p)return;p=end;}
  if(!v)return;
  struct timespec d={0,50000000L};nanosleep(&d,NULL);
 }
 P.idle_timeouts++;
}
static int live_now(void){return __atomic_load_n(&running,__ATOMIC_ACQUIRE);}
static void note_writer(const struct xz_tl_writer *w){
 P.ops=w->ops;P.writes=w->writes;P.syncs=w->syncs;P.failures=w->failures;P.skipped=w->skipped;P.last_errno=(uint32_t)w->last_errno;
}
/* USB STOP of a slot: every queued operation of that slot now, in order, ahead of the rest, with no at-the-end hold
   and no quiet-stick wait, at normal priority; then one answer, whatever happened, and one log line. */
/* A stop's write is followed at once by the unmount, and the unmount writes everything out. Syncing the file and
   its folder first cost the stick two cache flushes: 0.6 to 0.8 s per stop on the unit (r63), once 1.2 s, past the
   1 s the unmount waits for an answer, and that stop left the stick busy for another 10 s. So a stop's write is not
   synced (xz_tl_writer.no_sync). */
static void serve_stop(struct xz_tl_writer *w,int slot){
 writer_priority(0,1);
 unsigned written=0,failed=0;int error=0;
 w->no_sync=1;
 pthread_mutex_lock(&lock);
 uint32_t stop=__atomic_exchange_n(&stop_urgent[slot-1],0,__ATOMIC_ACQ_REL);
 for(;;){
  unsigned i=0;while(i<ring_n&&slot_of(ring[(ring_head+i)%XZ_TL_OPS].path)!=slot)i++;
  if(i==ring_n)break;
  struct xz_tl_op op=ring[(ring_head+i)%XZ_TL_OPS];
  for(;i+1<ring_n;i++)ring[(ring_head+i)%XZ_TL_OPS]=ring[(ring_head+i+1)%XZ_TL_OPS];
  ring_n--;
  pthread_mutex_unlock(&lock);
  uint64_t began=micros(CLOCK_MONOTONIC);
  if(xz_tl_write(w,&op)==XZ_TL_WROTE)written++;
  else{failed++;if(!error)error=w->last_errno?w->last_errno:EIO;}
  uint64_t took=micros(CLOCK_MONOTONIC)-began;if(took>P.write_us_max)P.write_us_max=took>UINT32_MAX?UINT32_MAX:(uint32_t)took;
  __atomic_sub_fetch(&pending[slot-1],1,__ATOMIC_ACQ_REL);
  xz_tl_op_free(&op);
  pthread_mutex_lock(&lock);
 }
 pthread_mutex_unlock(&lock);
 w->no_sync=0;
 uint32_t ms=(uint32_t)(micros(CLOCK_MONOTONIC)/1000u)-__atomic_load_n(&stop_asked_ms[slot-1],__ATOMIC_ACQUIRE);
 if(stop&&xz_usb_release_answer)xz_usb_release_answer(stop_holder,slot,stop);
 writer_priority(0,0);
 note_writer(w);
 P.stop_answers++;P.stop_files+=written;P.stop_failures+=failed;P.stop_ms_last=ms;if(ms>P.stop_ms_max)P.stop_ms_max=ms;
 char line[240];
 if(error)snprintf(line,sizeof line,"USB_RELEASE: tracklist, USB %d: the list could not be written (errno %d, %s); %u of %u "
  "file(s) written; answered %u ms after the stop began",slot,error,strerror(error),written,written+failed,ms);
 else snprintf(line,sizeof line,"USB_RELEASE: tracklist, USB %d: %s; answered %u ms after the stop began",slot,
  written?"the list was written before the unmount":"nothing left to write",ms);
 xz_log(line);
}
static void *writer_main(void *unused){
 (void)unused;
 __atomic_store_n(&writer_tid,(long)syscall(SYS_gettid),__ATOMIC_RELEASE);
 if(setpriority(PRIO_PROCESS,0,19)){}
 io_class(1);
 struct xz_tl_writer w;xz_tl_writer_init(&w);
 for(;;){
  pthread_mutex_lock(&lock);
  struct text *want=NULL;int urgent=0;
  for(;;){
   urgent=urgent_slot();
   for(int i=0;i<TEXTS&&!want;i++)if(texts[i].state==TEXT_WANTED)want=&texts[i];
   int live=__atomic_load_n(&running,__ATOMIC_ACQUIRE);
   /* at_end: nothing goes to a stick while a recording runs, not even the list of a file split off earlier. */
   int held=at_end_mode&&ring_n&&live&&__atomic_load_n(&recording_now,__ATOMIC_ACQUIRE);
   if(urgent||want||(ring_n&&!held)||!live)break;
   xz_wake_wait_locked(&wake,&lock,500);
  }
  if(urgent){pthread_mutex_unlock(&lock);serve_stop(&w,urgent);continue;}
  if(!want&&!ring_n){pthread_mutex_unlock(&lock);break;}
  if(want){
   uint64_t key=want->key;want->state=TEXT_BUSY;pthread_mutex_unlock(&lock);
   char title[XZ_TL_TEXT]="",artist[XZ_TL_TEXT]="";
   io_class(0);
   int found=!xz_track_text((int)(key>>32),(uint32_t)key,title,sizeof title,artist,sizeof artist);
   io_class(1);
   P.text_lookups++;P.text_found+=found;
   pthread_mutex_lock(&lock);
   if(want->key==key&&want->state==TEXT_BUSY){
    want->state=found?TEXT_FOUND:TEXT_MISSING;
    if(found){memcpy(want->title,title,sizeof title);memcpy(want->artist,artist,sizeof artist);}
   }
   pthread_mutex_unlock(&lock);
   continue;
  }
  struct xz_tl_op op=ring[ring_head];ring_head=(ring_head+1)%XZ_TL_OPS;ring_n--;
  int drop=!live_now()&&at_end_mode&&__atomic_load_n(&recording_now,__ATOMIC_ACQUIRE);
  if(drop)ring_dropped++;
  pthread_mutex_unlock(&lock);
  /* At the end mode writes nothing while the stick records, also when the mods stop during a recording: that
     list is lost (counted in ops_dropped), the recording is not touched. */
  int slot=slot_of(op.path);
  if(!drop){
   if(__atomic_load_n(&running,__ATOMIC_ACQUIRE))wait_idle(op.path);
   uint64_t began=micros(CLOCK_MONOTONIC);
   xz_tl_write(&w,&op);
   uint64_t took=micros(CLOCK_MONOTONIC)-began;if(took>P.write_us_max)P.write_us_max=took>UINT32_MAX?UINT32_MAX:(uint32_t)took;
   note_writer(&w);
  }
  if(slot)__atomic_sub_fetch(&pending[slot-1],1,__ATOMIC_ACQ_REL);
  xz_tl_op_free(&op);
 }
 return NULL;
}
/* The stop's ask (usb_release.h): stock's UI thread, under the board's lock, so atomics and one xz_wake only. */
static unsigned tracklist_let_go(void *unused,int slot,uint32_t stop){
 (void)unused;
 if(slot!=1&&slot!=2)return 0;
 if(!__atomic_load_n(&running,__ATOMIC_ACQUIRE)||!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return 0;
 if(__atomic_load_n(&open_slot,__ATOMIC_ACQUIRE)!=slot&&!__atomic_load_n(&pending[slot-1],__ATOMIC_ACQUIRE))return 0;
 __atomic_add_fetch(&P.stop_asks,1u,__ATOMIC_RELAXED);
 __atomic_store_n(&stop_asked_ms[slot-1],(uint32_t)(micros(CLOCK_MONOTONIC)/1000u),__ATOMIC_RELEASE);
 __atomic_store_n(&stop_asked[slot-1],stop,__ATOMIC_RELEASE);
 xz_wake_set(&sampler_wake);
 return 1;
}

/* Cores 0 and 2 (as XZ_WORKER_CREATE, but never core 1, the display's), with a 2 MiB stack instead of the 8 MiB
   default: this player runs out of address space before RAM (memory note xz-address-space-layout). glibc 2.13 takes
   the runtime's static TLS (about 0.8 MiB) out of the same allocation and refuses a stack smaller than that, so
   2 MiB leaves about 1.2 MiB. No fallback to the default stack: a refused thread leaves the feature off. */
static int worker(pthread_t *thread,void *(*entry)(void *)){
 pthread_attr_t a;
 if(pthread_attr_init(&a))return -1;
 cpu_set_t cores;CPU_ZERO(&cores);CPU_SET(0,&cores);CPU_SET(2,&cores);
 (void)pthread_attr_setaffinity_np(&a,sizeof cores,&cores);   /* placement only: the stack size is what matters */
 int error=pthread_attr_setstacksize(&a,2u<<20);
 if(!error)error=pthread_create(thread,&a,entry,NULL);
 pthread_attr_destroy(&a);
 return error?-1:0;
}
/* The ON default of the MODS setting lives in settings.c (XZ_RECORDING_TRACKLIST_DEFAULT). */
static pthread_mutex_t control=XZ_MUTEX_INITIALIZER;   /* start, set_enabled and stop */
static int launch_locked(int mode){
 if(created)return 0;
 if(!xz_mods_may_write())return 0;
 core=malloc(xz_tl_size());if(!core)return -1;
 struct xz_tl_config c;xz_tl_default_config(&c);c.at_end=mode!=MODE_LIVE;at_end_mode=c.at_end;
#ifdef XZ_TL_TEST_CONFIG
 XZ_TL_TEST_CONFIG(&c);                               /* host tests: shorter thresholds, real threads */
#endif
 xz_tl_init(core,&c);
 P.mode=(uint32_t)mode;
 __atomic_store_n(&running,1,__ATOMIC_RELEASE);
 if(worker(&writer,writer_main)){__atomic_store_n(&running,0,__ATOMIC_RELEASE);free(core);core=NULL;P.mode=0;return -1;}
 if(worker(&sampler,sampler_main)){
  __atomic_store_n(&running,0,__ATOMIC_RELEASE);
  pthread_mutex_lock(&lock);xz_wake_set(&wake);pthread_mutex_unlock(&lock);
  pthread_join(writer,NULL);free(core);core=NULL;P.mode=0;return -1;
 }
 created=1;P.started=1;
 if(stop_holder<0&&xz_usb_release_holder)stop_holder=xz_usb_release_holder("tracklist",tracklist_let_go,NULL);
 return 0;
}
int xz_tracklist_start(void){
 /* XZ_MODS_TRACKLIST, for tests: "1" the whole list when the recording ends (nothing on the stick while it
    records), "live" a line about 30 s after each track comes in, fsynced, so a pulled stick keeps the list up to
    that line; "0" off whatever the setting says. */
 const char *on=getenv("XZ_MODS_TRACKLIST");
 pthread_mutex_lock(&control);
 int result=0;
 if(!armed){
  armed=1;
  if(on&&!strcmp(on,"1"))env_mode=MODE_AT_END;else if(on&&!strcmp(on,"live"))env_mode=MODE_LIVE;
  else if(on&&!strcmp(on,"0"))env_mode=MODE_NONE;
  int mode=env_mode>=0?env_mode:wanted?MODE_AT_END:MODE_NONE;
  __atomic_store_n(&enabled,mode!=MODE_NONE,__ATOMIC_RELEASE);
  if(mode!=MODE_NONE)result=launch_locked(mode);
 }
 pthread_mutex_unlock(&control);
 return result;
}
void xz_tracklist_set_enabled(int on){
 pthread_mutex_lock(&control);
 wanted=!!on;
 if(armed&&env_mode<0){
  /* An ON before the first file is a plain start; an ON after an OFF skips the recording already running. */
  __atomic_store_n(&enabled,wanted,__ATOMIC_RELEASE);
  /* Started from the panel, possibly during a recording: that recording gets no list (it would start mid-file). */
  if(wanted&&!created){skip_next=1;if(launch_locked(MODE_AT_END))xz_log("Recording tracklist unavailable");}
 }
 pthread_mutex_unlock(&control);
}
void xz_tracklist_stop(void){
 pthread_mutex_lock(&control);
 int was=created;armed=0;
 pthread_mutex_unlock(&control);
 if(!was)return;
 __atomic_store_n(&running,0,__ATOMIC_RELEASE);
 xz_wake_set(&sampler_wake);
 pthread_join(sampler,NULL);                          /* it hands a stop asked before it ended to the writer */
 /* End an open file with a note; the writer carries it out before it returns. */
 xz_tl_finish(core,(uint32_t)(micros(CLOCK_MONOTONIC)/1000u));
 struct xz_tl_op op;
 pthread_mutex_lock(&lock);
 while(xz_tl_take(core,&op))ring_push_locked(&op);
 xz_wake_set(&wake);pthread_mutex_unlock(&lock);
 pthread_join(writer,NULL);                           /* finishes queued ops without the idle wait */
 /* A stop asked after the sampler ended is answered here, with nothing written: every stop asked is answered. */
 for(int slot=1;slot<=2;slot++){
  uint32_t stop=__atomic_exchange_n(&stop_asked[slot-1],0,__ATOMIC_ACQ_REL);
  if(stop&&xz_usb_release_answer)xz_usb_release_answer(stop_holder,slot,stop);
 }
 free(core);core=NULL;created=0;P.started=0;
}
size_t xz_tracklist_status_json(char *out,size_t capacity){
 if(!out||!capacity)return 0;
 static const char *const modes[3]={"off","at_end","live"};
 int mode=env_mode>=0?env_mode:-1;
 int n=snprintf(out,capacity,
  "{\"started\":%s,\"enabled\":%s,\"mode\":\"%s\",\"env\":%s,\"air_guard\":%s,\"air_raw\":\"0x%08x\",\"air_reads\":%u,"
  "\"air_failures\":%u,\"on_air_mode\":%u,\"file_open\":%u,\"entries\":%u,\"entries_dropped\":%u,\"files\":%u,"
  "\"ops\":%u,\"ops_dropped\":%u,\"writes\":%u,\"syncs\":%u,\"failures\":%u,\"skipped\":%u,\"last_errno\":%u,"
  "\"write_us_max\":%u,\"idle_waits\":%u,\"idle_timeouts\":%u,\"io_idle\":%u,\"samples\":%u,\"sample_us_max\":%u,"
  "\"text_lookups\":%u,\"text_found\":%u,\"stop_asks\":%u,\"stop_lists\":%u,\"stop_answers\":%u,\"stop_files\":%u,"
  "\"stop_failures\":%u,\"stop_ms_last\":%u,\"stop_ms_max\":%u,\"stop_nice_ok\":%u}",
  P.started?"true":"false",__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)?"true":"false",
  modes[P.started&&P.mode<3?P.mode:0],mode<0?"null":mode==MODE_NONE?"\"0\"":mode==MODE_LIVE?"\"live\"":"\"1\"",
  P.air_guard?"true":"false",P.air_raw,P.air_reads,P.air_failures,P.on_air_mode,P.file_open,P.entries,P.entries_dropped,P.files,
  P.ops,P.ops_dropped,P.writes,P.syncs,P.failures,P.skipped,P.last_errno,P.write_us_max,P.idle_waits,P.idle_timeouts,P.io_idle,
  P.samples,P.sample_us_max,P.text_lookups,P.text_found,P.stop_asks,P.stop_lists,P.stop_answers,P.stop_files,
  P.stop_failures,P.stop_ms_last,P.stop_ms_max,P.stop_nice_ok);
 if(n<0||(size_t)n>=capacity){out[0]=0;return 0;}
 return (size_t)n;
}
