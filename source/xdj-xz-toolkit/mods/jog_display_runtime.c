/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <unistd.h>
#include <time.h>
#include "jog_display_runtime.h"
#include "runtime.h"
#include "deck_select.h"
#ifdef __linux__
#include "clock_compat.h"
#endif
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
extern int xz_runtime_deck_selection_snapshot(struct xz_ds_selection *) __attribute__((weak));
extern uint32_t xz_extra_decks_jog_content_epoch(int deck) __attribute__((weak));
extern uint64_t xz_extra_source_generation(int source) __attribute__((weak));
extern enum xz_extra_wave_copy_result xz_extra_decks_copy_wave(int,const struct xz_extra_wave_key *,uint32_t,struct xz_extra_wave_chunk *) __attribute__((weak));
#ifdef XZ_JOG_DISPLAY_PROOF
#include "jog_display_proof.h"
#include "jog_display_latch.h"
#include "jog_position_proof.h"
__attribute__((visibility("default"))) struct xz_jpos xz_mods_jog_position_v1={
 .version=1,.size=sizeof(struct xz_jpos),.sides=2,.slots=XZ_JPOS_SLOTS,
 .record_bytes=sizeof(struct xz_jpos_record),.frame_bytes=XZ_JD_FRAME,.interval_ms=XZ_JPOS_INTERVAL_MS
};
__attribute__((visibility("default"))) struct xz_jdp xz_mods_jog_proof_v1={.version=1};
__attribute__((visibility("default"))) struct xz_jdl xz_mods_jog_latch_v2={
 .version=2,.size=sizeof(struct xz_jdl),.sides=2,.slots=XZ_JDL_SLOTS,
 .event_bytes=sizeof(struct xz_jdl_event),.counter_words=25
};
#endif

#define XZ_JD_RUNTIME_FRESH_MS 250u

static uint8_t jd_storage[XZ_EXTRA_DECKS][XZ_JD_STORAGE_BYTES];
static struct xz_jd jd_state[XZ_EXTRA_DECKS];
static _Atomic int jd_installed;

/* Every payload access uses one bounded try. Neither lifecycle nor a busy
 * reader resets the slot, and no guard is held across the native assembler. */
struct jd_sample {
 struct xz_extra_deck deck;
 struct xz_master_observation master_actual;
 uint32_t seq, published_ms, session, route_epoch, source_revision;
 int source_ordered;
};
struct jd_pub_slot {
 atomic_flag busy;
 _Atomic uint32_t seq;
 struct jd_sample sample;
#ifdef XZ_JOG_DISPLAY_PROOF
 _Atomic unsigned drops;
#endif
};
static struct jd_pub_slot jd_pub[XZ_EXTRA_DECKS]={{.busy=ATOMIC_FLAG_INIT},{.busy=ATOMIC_FLAG_INIT}};
struct jd_side_runtime {
 atomic_flag busy;
 uint32_t session, route_epoch, floor_seq;
 uint64_t cache_publication_ms;
 int cached;
 struct jd_sample cache;
 uint64_t selection_epoch;
 uint32_t content_epoch;
 int have_wave;
 struct xz_extra_wave_key wave_key;
 struct xz_jd_wave wave;
};
static struct jd_side_runtime jd_side[XZ_EXTRA_DECKS]={{.busy=ATOMIC_FLAG_INIT},{.busy=ATOMIC_FLAG_INIT}};
#ifdef XZ_JOG_DISPLAY_PROOF
/* Owned by the existing per-side serializer. Context is an observed diagnostic
 * ordinal, not an authoritative selector revision. No lifecycle writer resets
 * a live slot. Bins are replaced lazily under the existing guard. */
struct jd_latch_key {
 uint32_t session,control,route,present,source_session,source_route,timeline_route,load_revision;
 uint32_t source_device,loaded;
 uint64_t media_generation;
};
static struct jd_latch_key jd_latch_key[XZ_EXTRA_DECKS];
#endif
#ifdef XZ_JOG_DISPLAY_PROOF
/* Diagnostic-only: the assembler publishes one reason word. The connector
 * reports a persistent failure off the display/audio thread, without log storms. */
extern void xz_log(const char *) __attribute__((weak));
static _Atomic unsigned jd_reject_reason[XZ_EXTRA_DECKS],jd_reject_session[XZ_EXTRA_DECKS];
static uint32_t jd_reject_report_session;
static atomic_flag jd_reject_report_busy=ATOMIC_FLAG_INIT;
static unsigned jd_reject_seen[XZ_EXTRA_DECKS],jd_reject_reported[XZ_EXTRA_DECKS];
static uint64_t jd_reject_since[XZ_EXTRA_DECKS];
static void report_jog_rejections(uint64_t now,uint32_t session){
 if(!xz_log||atomic_flag_test_and_set_explicit(&jd_reject_report_busy,memory_order_acquire))return;
 if(jd_reject_report_session!=session){memset(jd_reject_seen,0,sizeof jd_reject_seen);memset(jd_reject_reported,0,sizeof jd_reject_reported);jd_reject_report_session=session;}
 static const char *const names[]={"NONE","NO_SEQUENCE","NATIVE_FLOOR","SESSION","ROUTE","CONTENT_EPOCH","MEDIA_GENERATION","TIMELINE_ROUTE"};
 for(unsigned side=0;side<XZ_EXTRA_DECKS;side++){
  unsigned reason=atomic_load_explicit(&jd_reject_session[side],memory_order_acquire)==session?
   atomic_load_explicit(&jd_reject_reason[side],memory_order_relaxed):0;
  if(reason!=jd_reject_seen[side]){jd_reject_seen[side]=reason;jd_reject_since[side]=now;}
  if(!reason&&jd_reject_reported[side]){
   char text[96];snprintf(text,sizeof text,"JOG_STATE_RECOVERED: side=%u",side+1);xz_log(text);jd_reject_reported[side]=0;
  }else if(reason<sizeof names/sizeof names[0]&&reason&&reason!=jd_reject_reported[side]&&
           now>=jd_reject_since[side]&&now-jd_reject_since[side]>=XZ_JD_RUNTIME_FRESH_MS){
   char text[144];snprintf(text,sizeof text,"JOG_STATE_REJECT: side=%u reason=%s persistent_ms=%llu",side+1,names[reason],(unsigned long long)(now-jd_reject_since[side]));xz_log(text);jd_reject_reported[side]=reason;
  }
 }
 atomic_flag_clear_explicit(&jd_reject_report_busy,memory_order_release);
}
#else
#define report_jog_rejections(now,session) ((void)0)
#endif
static atomic_flag jd_lifecycle_busy=ATOMIC_FLAG_INIT;
static _Atomic uint32_t jd_session=1;
static int jd_hooked; /* Only the lifecycle owner changes this. Stop keeps the hook. */

typedef void (*jd_assemble_fn)(void *);
typedef uint64_t (*jd_now_fn)(void);
typedef uint32_t (*jd_control_fn)(void);
/* The hook provider fills this BEFORE activating the replacement. Keep its
 * existing output slot rather than delaying that handoff until start returns. */
static jd_assemble_fn orig_jog_assemble;
static _Atomic(jd_assemble_fn) test_assemble;
static _Atomic int jd_test_mode;
static _Atomic(jd_now_fn) test_now_ms;
static _Atomic(jd_control_fn) test_control_revision;

static uint64_t default_now_ms(void){
 jd_now_fn clock=atomic_load_explicit(&test_now_ms,memory_order_acquire);
 if(clock)return clock();
 struct timespec t;
#ifdef __linux__
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
#else
 timespec_get(&t,TIME_UTC);
#endif
 return (uint64_t)t.tv_sec*1000u+(uint64_t)(t.tv_nsec/1000000L);
}

static uint32_t current_control_revision(void){
 jd_control_fn control=atomic_load_explicit(&test_control_revision,memory_order_acquire);
 if(control)return control();
 return xz_extra_decks_control_revision();
}
static int selection_snapshot(struct xz_ds_selection *out){
 if(xz_runtime_deck_selection_snapshot)return xz_runtime_deck_selection_snapshot(out);
 /* Legacy synthetic fixtures do not link the selector adapter. Production
    requires the controls owner's coherent getter; it has no mask fallback. */
 if(!atomic_load_explicit(&jd_test_mode,memory_order_acquire))return 0;
 uint32_t c=current_control_revision();
 *out=(struct xz_ds_selection){.epoch={1,1},.deck={c&4u&&c&1u?3:1,c&4u&&c&2u?4:2},
  .route_up=!!(c&4u),.route_epoch=xz_extra_decks_epoch()};return 1;
}
static uint32_t current_content(unsigned side){
 if(xz_extra_decks_jog_content_epoch)return xz_extra_decks_jog_content_epoch((int)side);
 return atomic_load_explicit(&jd_test_mode,memory_order_acquire)?1:0;
}
static int same_wave_key(const struct xz_extra_wave_key *a,const struct xz_extra_wave_key *b){
 return a->media_generation==b->media_generation&&a->route_epoch==b->route_epoch&&a->load_revision==b->load_revision&&
  a->source==b->source&&a->column_count==b->column_count&&a->span_ms==b->span_ms&&a->normalization==b->normalization;
}
static int media_current(const struct xz_extra_wave_key *key){
 return key->media_generation&&key->source>=1&&key->source<=2&&xz_extra_source_generation&&
  xz_extra_source_generation(key->source)==key->media_generation;
}
struct jd_wave_tag { uint32_t session,content; uint64_t selection; struct xz_extra_wave_key key; };
struct jd_wave_value { struct jd_wave_tag tag; struct xz_jd_wave wave; };
struct jd_wave_slot { atomic_flag busy; int valid; struct jd_wave_value value; };
struct jd_wave_job { int active,ready,done; struct jd_wave_value value; struct xz_jd_overview overview; };
static struct jd_wave_slot jd_wave_slot[2]={{.busy=ATOMIC_FLAG_INIT},{.busy=ATOMIC_FLAG_INIT}};
static struct jd_wave_job jd_wave_job[2]; /* existing connector is the sole writer */
static struct xz_extra_wave_chunk jd_wave_chunk; /* connector-owned scratch, no borrowed pointers */
static int wave_tag_current(unsigned side,const struct jd_wave_tag *tag,int selection){
 if(!atomic_load_explicit(&jd_installed,memory_order_acquire)||tag->session!=atomic_load_explicit(&jd_session,memory_order_acquire)||
  !tag->content||tag->content!=current_content(side)||!tag->key.route_epoch||
  tag->key.route_epoch!=xz_extra_decks_epoch()||!media_current(&tag->key))return 0;
 if(selection){
  struct xz_ds_selection current;if(!selection_snapshot(&current))return -1;
  if(!current.route_up||current.route_epoch!=tag->key.route_epoch||current.epoch[side]!=tag->selection)return 0;
 }
 return 1;
}
static int service_wave_step(unsigned side,uint32_t session,const struct xz_ds_selection *selection){
 struct jd_wave_job *job=&jd_wave_job[side];
 if(job->active){
  int current=wave_tag_current(side,&job->value.tag,1);
  if(current<0)return 0;
  if(!current)job->active=0;
 }
 if(!job->active){
  uint32_t content=current_content(side);if(!content||!selection->epoch[side])return 0;
  enum xz_extra_wave_copy_result result=xz_extra_decks_copy_wave((int)side,NULL,0,&jd_wave_chunk);
  if(result!=XZ_EXTRA_WAVE_COPY_OK||!jd_wave_chunk.count)return 0;
  struct jd_wave_tag tag={.session=session,.content=content,.selection=selection->epoch[side],.key=jd_wave_chunk.key};
  if(wave_tag_current(side,&tag,1)!=1)return 0;
  if(!xz_jd_overview_begin(&job->overview,tag.key.column_count,tag.key.normalization))return 0;
  job->value.tag=tag;job->active=1;job->ready=job->done=0;
  if(!xz_jd_overview_add(&job->overview,jd_wave_chunk.first,jd_wave_chunk.columns,jd_wave_chunk.count)){job->active=0;return 0;}
  return 1;
 }
 if(job->done)return 0;
 if(!job->ready){
  enum xz_extra_wave_copy_result result=xz_extra_decks_copy_wave((int)side,&job->value.tag.key,job->overview.next,&jd_wave_chunk);
  if(result==XZ_EXTRA_WAVE_COPY_BUSY)return 0;
  if(result!=XZ_EXTRA_WAVE_COPY_OK||!same_wave_key(&job->value.tag.key,&jd_wave_chunk.key)){job->active=0;return 0;}
  int current=wave_tag_current(side,&job->value.tag,1);
  if(current<0)return 0;
  if(!current){job->active=0;return 0;}
  if(jd_wave_chunk.count){
   if(!xz_jd_overview_add(&job->overview,jd_wave_chunk.first,jd_wave_chunk.columns,jd_wave_chunk.count))job->active=0;
   return 1;
  }
  /* This zero-count copy revalidated the complete current view under its lock. */
  if(!xz_jd_overview_encode(&job->overview,&job->value.wave)){job->active=0;return 0;}
  current=wave_tag_current(side,&job->value.tag,1);
  if(current<0)return 0;
  if(!current){job->active=0;return 0;}
  job->ready=1;
 }
 struct jd_wave_slot *slot=&jd_wave_slot[side];
 if(atomic_flag_test_and_set_explicit(&slot->busy,memory_order_acquire))return 0;
 slot->valid=0;
 if(wave_tag_current(side,&job->value.tag,1)==1){
  slot->value=job->value;
  slot->valid=wave_tag_current(side,&job->value.tag,1)==1;
  job->done=slot->valid;
 }
 atomic_flag_clear_explicit(&slot->busy,memory_order_release);return job->done;
}
void xz_jog_display_runtime_service_assets(void){
 if(!xz_extra_decks_copy_wave||!atomic_load_explicit(&jd_installed,memory_order_acquire))return;
 struct xz_ds_selection selection;if(!selection_snapshot(&selection)||!selection.route_up||!selection.route_epoch)return;
 uint32_t session=atomic_load_explicit(&jd_session,memory_order_acquire);uint64_t began=default_now_ms();
 report_jog_rejections(began,session);
 /* At most 64 * 512 columns per deck, with a 2 ms cooperative budget. This is
    connector work only. Both sides get an attempt before checking the clock. */
 for(unsigned round=0;round<64;round++){
  int progress=service_wave_step(0,session,&selection);progress|=service_wave_step(1,session,&selection);
  uint64_t now=default_now_ms();if(!progress||now<began||now-began>=2u)break;
 }
}
static struct xz_extra_wave_key sample_wave_key(const struct xz_extra_deck *d){
 return (struct xz_extra_wave_key){.media_generation=d->timeline.media_generation,.route_epoch=d->timeline.route_epoch,
  .load_revision=d->timeline.load_revision,.source=d->source,.column_count=d->wave.count,
  .span_ms=d->wave.span_ms,.normalization=d->wave.normalization};
}
static void adopt_wave(unsigned index,uint32_t session){
 struct jd_side_runtime *side=&jd_side[index];const struct xz_extra_deck *d=&side->cache.deck;
 struct xz_extra_wave_key key=sample_wave_key(d);
 if(!(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE))||!key.column_count||!media_current(&key)){side->have_wave=0;return;}
 /* The LCD maps the whole overview onto INFO length. Permit one analysis
    column of endpoint rounding, never stretch mismatched analysis to a track. */
 uint64_t span=key.span_ms,total=d->total_ms>0?(uint32_t)d->total_ms:0;
 uint64_t tolerance=(span+key.column_count-1u)/key.column_count;
 if(!span||!total||(span>total?span-total:total-span)>tolerance){side->have_wave=0;return;}
 if(side->have_wave&&same_wave_key(&side->wave_key,&key))return;
 side->have_wave=0;struct jd_wave_slot *slot=&jd_wave_slot[index];
 if(atomic_flag_test_and_set_explicit(&slot->busy,memory_order_acquire))return;
 if(slot->valid&&slot->value.tag.session==session&&slot->value.tag.content==d->jog_content_epoch&&
  same_wave_key(&slot->value.tag.key,&key)&&wave_tag_current(index,&slot->value.tag,0)==1){
  side->wave=slot->value.wave;side->wave_key=key;side->have_wave=1;
 }
 atomic_flag_clear_explicit(&slot->busy,memory_order_release);
}

static void publish_deck(int deck_index,const struct xz_extra_deck *deck,uint32_t now_ms,uint32_t revision,int ordered,
 const struct xz_master_observation *actual){
 if(deck_index<0||deck_index>=XZ_EXTRA_DECKS||!deck)return;
 uint32_t session=atomic_load_explicit(&jd_session,memory_order_acquire);
 uint32_t route_epoch=xz_extra_decks_epoch();
 struct jd_pub_slot *s=&jd_pub[deck_index];
 if(atomic_flag_test_and_set_explicit(&s->busy,memory_order_acquire)){
#ifdef XZ_JOG_DISPLAY_PROOF
  JDP_INC(s->drops);
#endif
  return;
 }
 if(session!=atomic_load_explicit(&jd_session,memory_order_acquire)){
  atomic_flag_clear_explicit(&s->busy,memory_order_release);return;
 }
 /* Snapshot callers can arrive out of order after xz_xp_read releases its lock.
  * Preserve the newest player observation within this lifecycle and route.
  * Equal revisions may renew a genuine observation of a stable paused view. */
 if(ordered&&s->sample.source_ordered&&s->sample.session==session&&s->sample.route_epoch==route_epoch&&
    (int32_t)(revision-s->sample.source_revision)<0){
  atomic_flag_clear_explicit(&s->busy,memory_order_release);return;
 }
 /* Reserve the upper two proof read_seq bits for contention observations. */
 uint32_t next=(atomic_load_explicit(&s->seq,memory_order_relaxed)+1u)&0x3fffffffu;
 if(!next)next=1;
 s->sample.deck=*deck;
 s->sample.master_actual=actual?*actual:(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};
 /* The Jog publication retains values only; the connector copy seam owns all
    source-array access. Keep descriptors for identity, never analysis pointers. */
 s->sample.deck.wave.columns=NULL;s->sample.deck.grid=NULL;
 s->sample.seq=next;s->sample.published_ms=now_ms;
 s->sample.session=session;s->sample.route_epoch=route_epoch;
 s->sample.source_revision=revision;s->sample.source_ordered=ordered;
 atomic_store_explicit(&s->seq,next,memory_order_release);
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_INC(xz_mods_jog_proof_v1.side[deck_index].publications);
 JDP_SET(xz_mods_jog_proof_v1.side[deck_index].published_seq,next);
 JDP_SET(xz_mods_jog_proof_v1.side[deck_index].published_ms,now_ms);
#endif
 atomic_flag_clear_explicit(&s->busy,memory_order_release);
}

void xz_jog_display_runtime_publish_deck(int deck_index,const struct xz_extra_deck *deck,uint32_t now_ms){
 publish_deck(deck_index,deck,now_ms,0,0,NULL);
}

void xz_jog_display_runtime_publish(const struct xz_extra_decks *decks,uint32_t now_ms){
 if(!decks)return;
 for(int i=0;i<XZ_EXTRA_DECKS;i++)publish_deck(i,&decks->deck[i],now_ms,decks->snapshot_revision,1,&decks->master_actual);
}

enum jd_read_result { JD_READ_BUSY=-1,JD_READ_EMPTY,JD_READ_OK };
static int read_published_deck(int deck_index,struct jd_sample *out){
 if(deck_index<0||deck_index>=XZ_EXTRA_DECKS||!out)return JD_READ_EMPTY;
 struct jd_pub_slot *s=&jd_pub[deck_index];
 if(atomic_flag_test_and_set_explicit(&s->busy,memory_order_acquire))return JD_READ_BUSY;
 uint32_t seq=atomic_load_explicit(&s->seq,memory_order_relaxed);
 if(seq)*out=s->sample;
 atomic_flag_clear_explicit(&s->busy,memory_order_release);
 return seq?JD_READ_OK:JD_READ_EMPTY;
}

int xz_jog_display_snapshot_from_deck(int deck_index,const struct xz_extra_deck *d,uint64_t now_ms,struct xz_jd_snapshot *out){
 if(!d||!out||deck_index<0||deck_index>=XZ_EXTRA_DECKS)return 0;
 memset(out,0,sizeof *out);
 if(!d->loaded||d->total_ms<=0||(uint32_t)d->total_ms>=128u*60000u)return 0;
 uint32_t length_ms=(uint32_t)d->total_ms;
 uint32_t pos_ms=d->elapsed_ms>0?(uint32_t)d->elapsed_ms:0u;
 uint64_t cap_ms=now_ms;
 if((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE))&&(d->timeline.flags&XZ_EXTRA_TIMELINE_ANCHOR)&&d->timeline.source_rate){
  uint32_t now32=(uint32_t)now_ms;
  uint32_t age32=now32-d->timeline.at_ms;
  if((int32_t)age32<0||age32>XZ_JD_RUNTIME_FRESH_MS){
   if(!d->playing)age32=0;
   else return 0;
  }
  double base_ms=(double)d->timeline.source_frame*1000./(double)d->timeline.source_rate;
  if(d->timeline.moving&&(d->timeline.flags&XZ_EXTRA_TIMELINE_RATE)){
   base_ms+=(double)age32*((double)d->timeline.rate_ppm/1000000.);
  }
  if(d->loop.active&&d->loop.out_ms>d->loop.in_ms&&base_ms>=(double)d->loop.out_ms){
   double span=(double)d->loop.out_ms-(double)d->loop.in_ms;
   double rel=base_ms-(double)d->loop.in_ms;
   /* Supported tracks fit in 32-bit milliseconds; corrupt anchors must not stall the serializer. */
   if(rel>=4294967296.)return 0;
   uint32_t whole=(uint32_t)rel;
   base_ms=(double)d->loop.in_ms+(double)(whole%(uint32_t)span)+(rel-(double)whole);
  }
  if(base_ms<0.)base_ms=0.;
  if(base_ms>(double)length_ms)base_ms=(double)length_ms;
  pos_ms=(uint32_t)(base_ms+.5);
  cap_ms=d->playing&&age32<=XZ_JD_RUNTIME_FRESH_MS?now_ms-age32:now_ms;
 }
 if(pos_ms>length_ms)pos_ms=length_ms;
 uint8_t range=(uint8_t)d->tempo_range;
 if(range!=0&&range!=6&&range!=10&&range!=16&&range!=100)range=10;
 int32_t tempo=d->tempo_x100;
 if(tempo<-32768)tempo=-32768;
 if(tempo>32767)tempo=32767;
 uint64_t gen=((uint64_t)(d->timeline.route_epoch?d->timeline.route_epoch:1u)<<32)|
              (uint64_t)(d->timeline.load_revision?d->timeline.load_revision:1u);
 uint32_t cue_ms=d->cue_ms>0?(uint32_t)d->cue_ms:0u;
 uint8_t cued=(uint8_t)(!d->playing&&pos_ms==cue_ms);
 uint8_t sync_mode=!d->sync.requested?XZ_JD_SYNC_OFF:
  d->sync.locked?XZ_JD_SYNC_LOCKED:XZ_JD_SYNC_BLINK;
 uint8_t loop_active=(uint8_t)(d->loop.active&&d->loop.out_ms>d->loop.in_ms&&d->loop.in_ms>=0);
 int bpm_known=!!(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM));
 uint16_t bpm=bpm_known&&d->bpm_x100>0&&d->bpm_x100<65535?(uint16_t)d->bpm_x100:0u;
 uint16_t track_bpm=bpm_known&&d->track_bpm_x100>0&&d->track_bpm_x100<65535?(uint16_t)d->track_bpm_x100:0u;
 *out=(struct xz_jd_snapshot){
  .generation=gen,
  .captured_ms=cap_ms,
  .position_ms=pos_ms,
  .length_ms=length_ms,
  .logical_deck=(uint8_t)(deck_index+3),
  .paused=(uint8_t)!d->playing,
  .tempo_rate=(int16_t)tempo,
  .tempo_range=range,
  .vinyl_mode=(uint8_t)!!d->vinyl,
  .master_tempo=(uint8_t)!!d->master_tempo,
  .tempo_known=1,
  .status_known=1,
  .cued=cued,
  .master=0, /* Only aggregate confirmed authority can set MASTER below. */
  .sync_mode=sync_mode,
  .loop_active=loop_active,
  .slip=(uint8_t)!!d->slip,
  .slip_active=(uint8_t)!!d->slip_active,
  .loop_edit=(uint8_t)(d->loop.edit>=1&&d->loop.edit<=2?d->loop.edit:0),
  .bpm_x100=bpm,
  .track_bpm_x100=track_bpm,
  .key_code=(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY))?xz_jd_key_code(d->key):0,
  .remain=(uint8_t)((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIME_MODE))&&d->remain),
  .loop_in_ms=loop_active?(uint32_t)d->loop.in_ms:0u,
  .loop_out_ms=loop_active?(uint32_t)d->loop.out_ms:0u
 };
 return 1;
}

static void prime_side_baseline(unsigned side_idx){
 unsigned side=side_idx+1u;
 struct xz_jd *s=&jd_state[side_idx];
 xz_jd_init(s,side,jd_storage[side_idx],XZ_JD_STORAGE_BYTES,XZ_JD_RUNTIME_FRESH_MS);
 xz_jd_native_epoch(s,1);
 static const uint8_t neutral_info[64]={
  0x10,0x21,0x0a,0x00,0x20,0x41,0x01,0x00,0x00,0x5e,0x80,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0xff,0xff,0x00,0x00,0x00,0x00,0x99,0x95,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x99,
  0x59,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0d,0x00,0x00
 };
 uint8_t pkt[64];
 memcpy(pkt,neutral_info,64);
 pkt[0]=(uint8_t)(side<<4);
 (void)xz_jd_capture(s,pkt);
 for(unsigned fam=1;fam<=5;fam++){
  xz_jd_clear_packet(side,fam,pkt);
  (void)xz_jd_capture(s,pkt);
 }
}

/* Publication time is a separate upper bound even for paused/no-timeline
 * inputs. Re-reading a slot, or reusing it under contention, never renews it. */
static int sample_current(const struct jd_sample *p,const struct jd_side_runtime *side,uint32_t session,uint32_t route_epoch){
 unsigned reason=!p->seq?1:p->seq==side->floor_seq?2:p->session!=session?3:
  (!route_epoch||p->route_epoch!=route_epoch)?4:
  (!p->deck.jog_content_epoch||p->deck.jog_content_epoch!=side->content_epoch)?5:
  (p->deck.timeline.media_generation&&xz_extra_source_generation&&
   xz_extra_source_generation(p->deck.source)!=p->deck.timeline.media_generation)?6:
  (p->deck.timeline.route_epoch&&p->deck.timeline.route_epoch!=route_epoch)?7:0;
#ifdef XZ_JOG_DISPLAY_PROOF
 atomic_store_explicit(&jd_reject_reason[side-jd_side],reason,memory_order_relaxed);
 atomic_store_explicit(&jd_reject_session[side-jd_side],session,memory_order_release);
#endif
 return reason==0;
}
static int sample_snapshot(unsigned side_idx,const struct jd_sample *p,uint64_t publication_ms,uint64_t now_ms,struct xz_jd_snapshot *out){
 if(publication_ms>now_ms||now_ms-publication_ms>XZ_JD_RUNTIME_FRESH_MS)return 0;
 uint32_t age=(uint32_t)now_ms-p->published_ms;
 if((int32_t)age<0||age>XZ_JD_RUNTIME_FRESH_MS||now_ms<age)return 0;
 if(!xz_jog_display_snapshot_from_deck((int)side_idx,&p->deck,now_ms,out))return 0;
 const struct xz_master_observation *master=&p->master_actual;
 const struct xz_extra_timeline *timeline=&p->deck.timeline;
 out->master=(uint8_t)(master->deck==(int)side_idx+2&&master->revision&&
  (uint32_t)((uint32_t)now_ms-master->observed_ms)<=XZ_MASTER_MAX_AGE_MS&&
  master->route_epoch==timeline->route_epoch&&master->load_revision==timeline->load_revision&&
  master->transport_generation==timeline->transport_generation);
 if(out->captured_ms>publication_ms)out->captured_ms=publication_ms;
 return 1;
}

#ifdef XZ_JOG_DISPLAY_PROOF
/* At most three132-byte publications, with lazy per-bin context replacement.
 * Existing side ownership serializes writers. Readers never acquire a lock. */
struct jd_latch_values {
#define XZ_JDL_VALUE(name) unsigned name;
 XZ_JDL_EVENT_FIELDS(XZ_JDL_VALUE)
#undef XZ_JDL_VALUE
};
static void latch_store(struct xz_jdl_event *event,const struct jd_latch_values *value){
 if(atomic_load_explicit(&event->valid,memory_order_relaxed)&&
    atomic_load_explicit(&event->context,memory_order_relaxed)==value->context)return;
 atomic_fetch_add_explicit(&event->seq,1u,memory_order_acq_rel);
#define XZ_JDL_STORE(name) JDP_SET(event->name,value->name);
 XZ_JDL_EVENT_FIELDS(XZ_JDL_STORE)
#undef XZ_JDL_STORE
 atomic_fetch_add_explicit(&event->seq,1u,memory_order_release);
}
static void latch_observe(unsigned side_idx,uint32_t session,uint32_t ctrl,uint32_t route,uint64_t now,
 unsigned why,int selected,int step,unsigned phase_before,const uint8_t out[XZ_JD_FRAME],unsigned changed,unsigned copied,int read_result){
 struct xz_jdl_side *proof=&xz_mods_jog_latch_v2.side[side_idx];
 const struct jd_side_runtime *side=&jd_side[side_idx];
 const struct jd_sample *sample=side->cached?&side->cache:NULL;
 struct jd_latch_key key={0};
 key.session=session;key.control=ctrl;key.route=route;key.present=sample!=NULL;
 if(sample){
  key.source_session=sample->session;key.source_route=sample->route_epoch;
  key.timeline_route=sample->deck.timeline.route_epoch;key.load_revision=sample->deck.timeline.load_revision;
  key.media_generation=sample->deck.timeline.media_generation;
  key.source_device=(uint32_t)sample->deck.source;key.loaded=(uint32_t)!!sample->deck.loaded;
 }
 if(memcmp(&key,&jd_latch_key[side_idx],sizeof key)){
  jd_latch_key[side_idx]=key;
  JDP_INC(proof->context_changes);JDP_SET(proof->session,session);
 }
 JDP_INC(proof->callbacks);
 if(why<XZ_JDL_REASON_COUNT)JDP_INC(proof->reason[why]);
 if((unsigned)selected<XZ_JDL_RESULT_COUNT)JDP_INC(proof->select[selected]);
 if((unsigned)step<XZ_JDL_RESULT_COUNT)JDP_INC(proof->step[step]);
 JDP_INC(proof->final[copied]);
 struct jd_latch_values value={
  .valid=1,.context=atomic_load_explicit(&proof->context_changes,memory_order_relaxed),
  .session=session,.control=ctrl,.route_epoch=route,.at_ms_lo=(uint32_t)now,.at_ms_hi=(uint32_t)(now>>32),
  .source_present=key.present,.source_session=key.source_session,.source_route_epoch=key.source_route,
  .source_timeline_route=key.timeline_route,.source_load_revision=key.load_revision,
  .source_media_lo=(uint32_t)key.media_generation,.source_media_hi=(uint32_t)(key.media_generation>>32),
  .snapshot_reason=why,.select_result=(unsigned)selected,.step_result=(unsigned)step,
  .phase_before=phase_before,.phase=(unsigned)jd_state[side_idx].phase,.desired=(ctrl&4u)&&(ctrl&(1u<<side_idx))?side_idx+3u:side_idx+1u,
  .encoded_frame=out[0x53]>>4,.encoded_packet=out[15]>>4,.packet_command=out[14],
  .candidate_changed=changed,.final_copied=copied,
  .fallback=(ctrl&4u)&&(ctrl&(1u<<side_idx))&&(out[0x53]>>4)!=side_idx+3u,
  .read_result=(unsigned)read_result
 };
 if(sample){
  value.source_revision=sample->source_revision;value.source_transport_generation=sample->deck.timeline.transport_generation;
  value.source_device=(unsigned)sample->deck.source;value.publication_seq=sample->seq;value.publication_ms=sample->published_ms;
 }
 if(why<XZ_JDL_REASON_COUNT)latch_store(&proof->event[why],&value);
 if((unsigned)step<XZ_JDL_RESULT_COUNT)latch_store(&proof->event[XZ_JDL_REASON_COUNT+(unsigned)step],&value);
 /* The successful-copy bin belongs to normal loaded EXTRA INFO only. Native
  * restoration, clear packets and guard rejection cannot displace that evidence.
  * All actual copies/skips still contribute to the independent lifetime totals. */
 if(!copied||(why==XZ_JDP_VALID&&sample&&key.loaded&&step==XZ_JD_OK&&
    phase_before==XZ_JD_EXTRA&&value.phase==XZ_JD_EXTRA&&value.packet_command==0x21u&&
    value.desired==side_idx+3u&&value.encoded_frame==value.desired&&value.encoded_packet==side_idx+1u))
  latch_store(&proof->event[XZ_JDL_REASON_COUNT+XZ_JDL_RESULT_COUNT+copied],&value);
}
#endif

#ifdef XZ_JOG_DISPLAY_PROOF
struct jd_position_values {
#define XZ_JPOS_VALUE(name) unsigned name;
 XZ_JPOS_FIELDS(XZ_JPOS_VALUE)
#undef XZ_JPOS_VALUE
};
/* No transport generation or position value is inferred from independent
 * counters. This key and every record come from this callback's guarded copy. */
#define JD_POSITION_KEY(X) \
 X(session) X(control) X(route_epoch) X(source_present) X(source_session) \
 X(source_route_epoch) X(timeline_route_epoch) X(load_revision) X(media_lo) X(media_hi) \
 X(transport_generation) X(source_device) X(source_loaded)
struct jd_position_key {
#define JD_POSITION_KEY_FIELD(name) unsigned name;
 JD_POSITION_KEY(JD_POSITION_KEY_FIELD)
#undef JD_POSITION_KEY_FIELD
};
struct jd_position_side {
 struct jd_position_key key;
 unsigned context,ordinal,latest_seen,active_seen,committed,has_committed;
 unsigned committed_context,pending_context,pending_count,next;
 uint64_t latest_at,active_at;
};
static struct jd_position_side jd_position[2];

static int position_due(uint64_t now,uint64_t previous,unsigned seen){
 return !seen||(now>=previous&&now-previous>=XZ_JPOS_INTERVAL_MS);
}
static void position_store(struct xz_jpos_record *record,const struct jd_position_values *v,const uint8_t *frame){
 atomic_fetch_add_explicit(&record->seq,1u,memory_order_acq_rel);
#define XZ_JPOS_STORE(name) JDP_SET(record->name,v->name);
 XZ_JPOS_FIELDS(XZ_JPOS_STORE)
#undef XZ_JPOS_STORE
 for(unsigned i=0;i<37;i++){
  uint32_t word;memcpy(&word,frame+4u*i,4);JDP_SET(record->frame[i],word);
 }
 atomic_fetch_add_explicit(&record->seq,1u,memory_order_release);
}
static void position_observe(unsigned side_idx,uint32_t session,uint32_t ctrl,uint32_t route,uint64_t now,
 unsigned why,int have_snap,const struct xz_jd_snapshot *snap,int selected,int step,unsigned phase_before,
 const uint8_t out[XZ_JD_FRAME],unsigned copied,int read_result){
 struct jd_position_side *state=&jd_position[side_idx];
 const struct jd_side_runtime *side=&jd_side[side_idx];
 const struct jd_sample *sample=side->cached?&side->cache:NULL;
 unsigned normal=copied&&why==XZ_JDP_VALID&&have_snap&&sample&&sample->deck.loaded&&
  step==XZ_JD_OK&&phase_before==XZ_JD_EXTRA&&jd_state[side_idx].phase==XZ_JD_EXTRA&&
  out[14]==0x21u&&(out[0x53]>>4)==side_idx+3u&&(out[15]>>4)==side_idx+1u;
 unsigned active=normal&&(sample->deck.playing||sample->deck.timeline.moving);
 unsigned latest_due=position_due(now,state->latest_at,state->latest_seen);
 unsigned active_due=active&&position_due(now,state->active_at,state->active_seen);
 if(!latest_due&&!active_due)return;
 struct jd_position_values v={
  .valid=1,.session=session,.control=ctrl,.route_epoch=route,.physical_side=side_idx+1u,
  .logical_id=(ctrl&4u)&&(ctrl&(1u<<side_idx))?side_idx+3u:side_idx+1u,
  .at_ms_lo=(uint32_t)now,.at_ms_hi=(uint32_t)(now>>32),.source_present=sample!=NULL,
  .snapshot_valid=(unsigned)have_snap,.snapshot_reason=why,.read_result=(unsigned)read_result,
  .select_result=(unsigned)selected,.step_result=(unsigned)step,.phase_before=phase_before,
  .phase=(unsigned)jd_state[side_idx].phase,.final_copied=copied
 };
 if(sample){
  const struct xz_extra_deck *d=&sample->deck;
  v.source_session=sample->session;v.source_route_epoch=sample->route_epoch;
  v.source_ordered=(unsigned)sample->source_ordered;v.source_revision=sample->source_revision;
  v.publication_seq=sample->seq;v.publication_ms=sample->published_ms;
  v.source_known=d->known;v.source_loaded=(unsigned)!!d->loaded;v.source_device=(unsigned)d->source;
  v.source_elapsed_ms=(unsigned)d->elapsed_ms;v.source_elapsed_at_ms=d->elapsed_at_ms;
  v.source_total_ms=(unsigned)d->total_ms;v.source_playing=(unsigned)!!d->playing;
  v.source_frame_lo=(uint32_t)d->timeline.source_frame;v.source_frame_hi=(uint32_t)(d->timeline.source_frame>>32);
  v.source_rate=d->timeline.source_rate;v.source_at_ms=d->timeline.at_ms;v.source_flags=d->timeline.flags;
  v.source_rate_ppm=(unsigned)d->timeline.rate_ppm;v.source_moving=(unsigned)!!d->timeline.moving;
  v.timeline_revision=d->timeline.revision;v.timeline_route_epoch=d->timeline.route_epoch;
  v.load_revision=d->timeline.load_revision;v.media_lo=(uint32_t)d->timeline.media_generation;
  v.media_hi=(uint32_t)(d->timeline.media_generation>>32);v.transport_generation=d->timeline.transport_generation;
 }
 if(have_snap){
  v.snapshot_generation_lo=(uint32_t)snap->generation;v.snapshot_generation_hi=(uint32_t)(snap->generation>>32);
  v.snapshot_captured_lo=(uint32_t)snap->captured_ms;v.snapshot_captured_hi=(uint32_t)(snap->captured_ms>>32);
  v.snapshot_position_ms=snap->position_ms;v.snapshot_length_ms=snap->length_ms;
  v.snapshot_paused=snap->paused;v.snapshot_status_known=snap->status_known;v.snapshot_logical_id=snap->logical_deck;
 }
 struct jd_position_key key={0};
#define JD_POSITION_COPY_KEY(name) key.name=v.name;
 JD_POSITION_KEY(JD_POSITION_COPY_KEY)
#undef JD_POSITION_COPY_KEY
 if(!state->context||memcmp(&key,&state->key,sizeof key)){
  state->key=key;if(!++state->context)++state->context;
 }
 v.context=state->context;if(!++state->ordinal)++state->ordinal;v.ordinal=state->ordinal;
 if(latest_due){
  position_store(&xz_mods_jog_position_v1.side[side_idx][6],&v,out);
  state->latest_seen=1;state->latest_at=now;
 }
 if(active_due){
  unsigned bank=state->committed;
  if(!state->has_committed||state->committed_context!=v.context){
   bank=state->has_committed?1u-state->committed:0u;
   if(state->pending_context!=v.context){state->pending_context=v.context;state->pending_count=0;state->next=0;}
  }
  position_store(&xz_mods_jog_position_v1.side[side_idx][bank*3u+state->next],&v,out);
  state->next=(state->next+1u)%3u;
  if(!state->has_committed||state->committed_context!=v.context){
   if(++state->pending_count==3u){
    state->committed=bank;state->committed_context=v.context;state->has_committed=1;
   }
  }
  state->active_seen=1;state->active_at=now;
 }
}
#endif

static void hook_jog_assemble(void *self){
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_INC(xz_mods_jog_proof_v1.calls);
#endif
 uint32_t session=atomic_load_explicit(&jd_session,memory_order_acquire);
 jd_assemble_fn original=atomic_load_explicit(&jd_test_mode,memory_order_acquire)?
  atomic_load_explicit(&test_assemble,memory_order_acquire):orig_jog_assemble;
 if(original)original(self);
#ifdef XZ_JOG_DISPLAY_PROOF
 if(!atomic_load(&jd_installed))JDP_INC(xz_mods_jog_proof_v1.disabled_calls);
 if(!self)JDP_INC(xz_mods_jog_proof_v1.null_self);
#endif
 if(!atomic_load(&jd_installed)||!self)return;
 uint32_t channel=0;
 memcpy(&channel,(const uint8_t *)self+0x64,4);
 if(channel!=1u&&channel!=2u){
#ifdef XZ_JOG_DISPLAY_PROOF
  JDP_INC(xz_mods_jog_proof_v1.invalid_channel);
  JDP_SET(xz_mods_jog_proof_v1.last_invalid_channel,channel);
#endif
  return;
 }
 unsigned side_idx=channel-1u;
 struct jd_side_runtime *side=&jd_side[side_idx];
 if(atomic_flag_test_and_set_explicit(&side->busy,memory_order_acquire)){
#ifdef XZ_JOG_DISPLAY_PROOF
  JDP_INC(xz_mods_jog_latch_v2.side[side_idx].contention);
#endif
  return;
 }
 if(!atomic_load(&jd_installed)||session!=atomic_load_explicit(&jd_session,memory_order_acquire)){
  atomic_flag_clear_explicit(&side->busy,memory_order_release);return;
 }
 struct xz_ds_selection selection;
 if(!selection_snapshot(&selection)||!selection.epoch[side_idx]){
  atomic_flag_clear_explicit(&side->busy,memory_order_release);return;
 }
 uint32_t route_epoch=selection.route_epoch;
 if(side->session!=session){
  /* Only this side's serializer resets its display/cache, after the native call.
   * Publishers never access these objects. Lifecycle never clears a live flag. */
  prime_side_baseline(side_idx);
  side->session=session;side->route_epoch=route_epoch;side->floor_seq=0;side->cached=0;side->have_wave=0;
 }
 uint32_t content=current_content(side_idx);
 if(side->route_epoch!=route_epoch||side->selection_epoch!=selection.epoch[side_idx]||side->content_epoch!=content){
  side->cached=0;side->have_wave=0;side->route_epoch=route_epoch;
  side->selection_epoch=selection.epoch[side_idx];side->content_epoch=content;
  jd_state[side_idx].software_generation=0;jd_state[side_idx].software_wave=NULL;jd_state[side_idx].wave_revision=0;
 }
 uint8_t *frame=(uint8_t *)self+0x104;
 uint64_t now_ms=default_now_ms();
#ifdef XZ_JOG_DISPLAY_PROOF
 uint32_t ctrl=(current_control_revision()&8u)|(selection.route_up?4u:0u)|
  (selection.deck[0]==3?1u:0u)|(selection.deck[1]==4?2u:0u);
#endif
 int route_up=selection.route_up;
 int on_extra=route_up&&selection.deck[side_idx]==(int)channel+2;
 struct jd_sample sample;
 int read_result=JD_READ_EMPTY;
 struct xz_jd_snapshot snap;
 int have_snap=0;
#ifdef XZ_JOG_DISPLAY_PROOF
 struct xz_jdp_side *proof=&xz_mods_jog_proof_v1.side[side_idx];
 unsigned why=on_extra?XZ_JDP_NO_PUBLICATION:XZ_JDP_NATIVE;
 int selected_result=-1;
 JDP_INC(proof->calls);
#endif
 if(!on_extra){
#ifdef XZ_JOG_DISPLAY_PROOF
  atomic_store_explicit(&jd_reject_reason[side_idx],0,memory_order_relaxed);
  atomic_store_explicit(&jd_reject_session[side_idx],session,memory_order_release);
#endif
  side->cached=0;
  side->floor_seq=atomic_load_explicit(&jd_pub[side_idx].seq,memory_order_acquire);
 }else{
  read_result=read_published_deck((int)side_idx,&sample);
  if(read_result==JD_READ_OK){
   int same=side->cached&&side->cache.seq==sample.seq&&side->cache.session==sample.session;
   side->cached=sample_current(&sample,side,session,route_epoch);
   if(side->cached){
    if(!same){
     uint32_t age=(uint32_t)now_ms-sample.published_ms;
     side->cache_publication_ms=(int32_t)age>=0&&now_ms>=age?now_ms-age:UINT64_MAX;
    }
    side->cache=sample;
   }
  }
  if(side->cached&&sample_current(&side->cache,side,session,route_epoch)){
   have_snap=sample_snapshot(side_idx,&side->cache,side->cache_publication_ms,now_ms,&snap);
#ifdef XZ_JOG_DISPLAY_PROOF
   const struct xz_extra_deck *d=&side->cache.deck;
   uint32_t age=(uint32_t)now_ms-side->cache.published_ms;
   why=have_snap?XZ_JDP_VALID:!d->loaded?XZ_JDP_EMPTY:
    d->total_ms<=0||(uint32_t)d->total_ms>=128u*60000u?XZ_JDP_LENGTH:
    (int32_t)age<0||(int32_t)((uint32_t)now_ms-d->timeline.at_ms)<0?XZ_JDP_FUTURE:XZ_JDP_STALE;
#endif
  }else side->cached=0;
 }
 if(on_extra&&have_snap)adopt_wave(side_idx,session);else side->have_wave=0;
 struct xz_jd_transaction transaction;
#ifdef XZ_JOG_DISPLAY_PROOF
 unsigned phase_before=(unsigned)jd_state[side_idx].phase;
#endif
 int step_result=xz_jd_prepare(&jd_state[side_idx],frame,on_extra?channel+2u:channel,on_extra&&have_snap?&snap:NULL,
  now_ms,side->have_wave?&side->wave:NULL,side->have_wave?content:0,&transaction);
 if(step_result==XZ_JD_UNSUPPORTED){atomic_flag_clear_explicit(&side->busy,memory_order_release);return;}
 uint8_t *out=transaction.frame;
#ifdef XZ_JOG_DISPLAY_PROOF
 selected_result=transaction.selected;
 phase_before=(unsigned)transaction.phase_before;
 unsigned candidate_changed=memcmp(frame,out,XZ_JD_FRAME)!=0;
 atomic_fetch_add_explicit(&proof->seq,1u,memory_order_acq_rel);
 JDP_SET(proof->channel,channel);JDP_SET(proof->control,ctrl);
 JDP_SET(proof->desired,on_extra?channel+2u:channel);JDP_SET(proof->snapshot_reason,why);
 JDP_SET(proof->select_result,selected_result);JDP_SET(proof->step_result,step_result);
 JDP_SET(proof->phase,transaction.next.phase);
 JDP_SET(proof->encoded_frame,out[0x53]>>4);JDP_SET(proof->encoded_packet,out[12+3]>>4);
 JDP_SET(proof->changed,candidate_changed);
 JDP_SET(proof->fallback,on_extra&&(out[0x53]>>4)!=channel+2u);
 /* ABI unchanged: low 30 bits identify the coherent sample, bit 31 means
  * this read contended, bit 30 means a publisher has dropped on contention.
  * Publications/published_seq still count only completed copies. */
 JDP_SET(proof->read_seq,(side->cached?side->cache.seq:0u)|
  (read_result==JD_READ_BUSY?0x80000000u:0u)|
  (atomic_load_explicit(&jd_pub[side_idx].drops,memory_order_relaxed)?0x40000000u:0u));
 atomic_fetch_add_explicit(&proof->seq,1u,memory_order_release);
#endif
#ifdef XZ_JOG_DISPLAY_PROOF
 unsigned copied=0;
#endif
 /* Preflight precedes the final clock callback, so time spent here is covered
    by the age checks. It is not the frame acceptance fence. */
 struct xz_ds_selection final_selection;
 int final_valid=selection_snapshot(&final_selection)&&final_selection.epoch[side_idx]==selection.epoch[side_idx]&&
  final_selection.deck[side_idx]==selection.deck[side_idx]&&final_selection.route_up==selection.route_up&&
  final_selection.route_epoch==route_epoch&&current_content(side_idx)==content;
 uint64_t final_now=default_now_ms();
 if(on_extra&&have_snap){
  final_valid=final_valid&&final_now>=now_ms&&final_now>=side->cache_publication_ms&&
   final_now-side->cache_publication_ms<=XZ_JD_RUNTIME_FRESH_MS&&final_now>=snap.captured_ms&&
   final_now-snap.captured_ms<=XZ_JD_RUNTIME_FRESH_MS;
  if(snap.master)final_valid=final_valid&&
   (uint32_t)((uint32_t)final_now-side->cache.master_actual.observed_ms)<=XZ_MASTER_MAX_AGE_MS;
 }
 /* Recollect after every clock/callback-capable input. These owner getters are
    qualified bounded copies/atomic reads. No clock, callback or I/O follows
    this fence before the frame and cursor commit. A later owner event may
    still race that commit; this does not make the physical send atomic. */
 final_valid=final_valid&&selection_snapshot(&final_selection)&&
  final_selection.epoch[side_idx]==selection.epoch[side_idx]&&final_selection.deck[side_idx]==selection.deck[side_idx]&&
  final_selection.route_up==selection.route_up&&final_selection.route_epoch==route_epoch&&current_content(side_idx)==content;
 if(on_extra&&have_snap&&side->cache.deck.timeline.media_generation&&xz_extra_source_generation)
  final_valid=final_valid&&xz_extra_source_generation(side->cache.deck.source)==side->cache.deck.timeline.media_generation;
 if(final_valid&&atomic_load(&jd_installed)&&session==atomic_load_explicit(&jd_session,memory_order_acquire)){
  memcpy(frame,out,XZ_JD_FRAME);
  xz_jd_commit(&jd_state[side_idx],&transaction);
#ifdef XZ_JOG_DISPLAY_PROOF
  copied=1;
#endif
 }
#ifdef XZ_JOG_DISPLAY_PROOF
 latch_observe(side_idx,session,ctrl,route_epoch,now_ms,why,selected_result,step_result,phase_before,out,candidate_changed,copied,read_result);
 position_observe(side_idx,session,ctrl,route_epoch,now_ms,why,have_snap,&snap,selected_result,step_result,phase_before,out,copied,read_result);
#endif
 atomic_flag_clear_explicit(&side->busy,memory_order_release);
}

int xz_jog_display_runtime_start(void){
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_INC(xz_mods_jog_proof_v1.start_attempts);
#endif
 if(atomic_flag_test_and_set_explicit(&jd_lifecycle_busy,memory_order_acquire))return -1;
 if(atomic_load(&jd_installed)){
  atomic_flag_clear_explicit(&jd_lifecycle_busy,memory_order_release);return 0;
 }
 uint32_t session=atomic_load_explicit(&jd_session,memory_order_acquire);
 int previous_test_mode=atomic_load_explicit(&jd_test_mode,memory_order_acquire);
 atomic_store_explicit(&jd_test_mode,0,memory_order_release);
 if(!jd_hooked){
  static const unsigned char g_assemble[8]={0xf0,0x4f,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
  if(xz_hook_arm(0x2591e0,g_assemble,(void *)(uintptr_t)hook_jog_assemble,(void **)&orig_jog_assemble)){
   atomic_store_explicit(&jd_test_mode,previous_test_mode,memory_order_release);
#ifdef XZ_JOG_DISPLAY_PROOF
   JDP_INC(xz_mods_jog_proof_v1.install_failed);
#endif
   atomic_flag_clear_explicit(&jd_lifecycle_busy,memory_order_release);return -1;
  }
  jd_hooked=1;
#ifdef XZ_JOG_DISPLAY_PROOF
  JDP_INC(xz_mods_jog_proof_v1.install_ok);
#endif
 }
 /* A concurrent stop cancels activation. No payload/state is reset here. */
 uint32_t next=session+1u;
 int activate=atomic_compare_exchange_strong_explicit(&jd_session,&session,next,memory_order_acq_rel,memory_order_acquire);
 if(activate){
  atomic_store(&jd_installed,1);
  if(atomic_load_explicit(&jd_session,memory_order_acquire)!=next){atomic_store(&jd_installed,0);activate=0;}
 }
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_SET(xz_mods_jog_proof_v1.installed,atomic_load(&jd_installed));
#endif
 atomic_flag_clear_explicit(&jd_lifecycle_busy,memory_order_release);
 return activate?0:-1;
}

void xz_jog_display_runtime_stop(void){
 atomic_fetch_add_explicit(&jd_session,1u,memory_order_acq_rel);
 atomic_store(&jd_installed,0);
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_SET(xz_mods_jog_proof_v1.installed,0);
#endif
}

void xz_jog_display_runtime_test_install(const struct xz_jog_display_test_ops *originals,void (**assemble_hook_out)(void *)){
 if(assemble_hook_out)*assemble_hook_out=NULL;
 if(atomic_flag_test_and_set_explicit(&jd_lifecycle_busy,memory_order_acquire))return;
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_INC(xz_mods_jog_proof_v1.test_installs);
#endif
 atomic_store(&jd_installed,0);
 atomic_store_explicit(&test_assemble,originals?originals->assemble:NULL,memory_order_release);
 atomic_store_explicit(&test_now_ms,originals?originals->now_ms:NULL,memory_order_release);
 atomic_store_explicit(&test_control_revision,originals?originals->control_revision:NULL,memory_order_release);
 atomic_fetch_add_explicit(&jd_session,1u,memory_order_acq_rel);
 atomic_store_explicit(&jd_test_mode,1,memory_order_release);
 atomic_store(&jd_installed,1);
#ifdef XZ_JOG_DISPLAY_PROOF
 JDP_SET(xz_mods_jog_proof_v1.installed,1);
#endif
 if(assemble_hook_out)*assemble_hook_out=hook_jog_assemble;
 atomic_flag_clear_explicit(&jd_lifecycle_busy,memory_order_release);
}

const struct xz_jd *xz_jog_display_runtime_state(int side_index){
 return side_index>=0&&side_index<XZ_EXTRA_DECKS?&jd_state[side_index]:NULL;
}
