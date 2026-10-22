/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <unistd.h>
#include <time.h>
#include "jog_display_runtime.h"
#include "runtime.h"
#ifdef __linux__
#include "clock_compat.h"
#endif
#include <stdatomic.h>
#include <string.h>
#ifdef XZ_JOG_DISPLAY_PROOF
#include "jog_display_proof.h"
#include "jog_display_latch.h"
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

static void publish_deck(int deck_index,const struct xz_extra_deck *deck,uint32_t now_ms,uint32_t revision,int ordered){
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
 publish_deck(deck_index,deck,now_ms,0,0);
}

void xz_jog_display_runtime_publish(const struct xz_extra_decks *decks,uint32_t now_ms){
 if(!decks)return;
 for(int i=0;i<XZ_EXTRA_DECKS;i++)publish_deck(i,&decks->deck[i],now_ms,decks->snapshot_revision,1);
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
   double span=(double)(d->loop.out_ms-d->loop.in_ms);
   double rel=base_ms-(double)d->loop.in_ms;
   while(rel>=span)rel-=span;
   base_ms=(double)d->loop.in_ms+rel;
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
  .tempo_known=1
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
 return p->seq&&p->seq!=side->floor_seq&&p->session==session&&route_epoch&&p->route_epoch==route_epoch&&
  (!p->deck.timeline.route_epoch||p->deck.timeline.route_epoch==route_epoch);
}
static int sample_snapshot(unsigned side_idx,const struct jd_sample *p,uint64_t publication_ms,uint64_t now_ms,struct xz_jd_snapshot *out){
 if(publication_ms>now_ms||now_ms-publication_ms>XZ_JD_RUNTIME_FRESH_MS)return 0;
 uint32_t age=(uint32_t)now_ms-p->published_ms;
 if((int32_t)age<0||age>XZ_JD_RUNTIME_FRESH_MS||now_ms<age)return 0;
 if(!xz_jog_display_snapshot_from_deck((int)side_idx,&p->deck,now_ms,out))return 0;
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
    value.desired==side_idx+3u&&value.encoded_frame==value.desired&&value.encoded_packet==value.desired))
  latch_store(&proof->event[XZ_JDL_REASON_COUNT+XZ_JDL_RESULT_COUNT+copied],&value);
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
 uint32_t route_epoch=xz_extra_decks_epoch();
 if(side->session!=session){
  /* Only this side's serializer resets its display/cache, after the native call.
   * Publishers never access these objects. Lifecycle never clears a live flag. */
  prime_side_baseline(side_idx);
  side->session=session;side->route_epoch=route_epoch;side->floor_seq=0;side->cached=0;
 }
 if(side->route_epoch!=route_epoch){side->cached=0;side->route_epoch=route_epoch;}
 uint8_t *frame=(uint8_t *)self+0x104;
 uint64_t now_ms=default_now_ms();
 uint32_t ctrl=current_control_revision();
 int route_up=!!(ctrl&4u);
 int on_extra=route_up&&!!(ctrl&(side_idx==0?1u:2u));
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
 if(!on_extra){
#ifdef XZ_JOG_DISPLAY_PROOF
  selected_result=
#else
  (void)
#endif
  xz_jd_select(&jd_state[side_idx],channel,NULL,now_ms);
 }else if(have_snap){
#ifdef XZ_JOG_DISPLAY_PROOF
  selected_result=
#else
  (void)
#endif
  xz_jd_select(&jd_state[side_idx],channel+2u,&snap,now_ms);
 }else{
#ifdef XZ_JOG_DISPLAY_PROOF
  selected_result=
#else
  (void)
#endif
  xz_jd_select_unavailable(&jd_state[side_idx],channel+2u);
 }
 uint8_t out[XZ_JD_FRAME];
#ifdef XZ_JOG_DISPLAY_PROOF
 unsigned phase_before=(unsigned)jd_state[side_idx].phase;
 int step_result=
#else
 (void)
#endif
 xz_jd_step(&jd_state[side_idx],frame,on_extra&&have_snap?&snap:NULL,now_ms,out);
#ifdef XZ_JOG_DISPLAY_PROOF
 unsigned candidate_changed=memcmp(frame,out,XZ_JD_FRAME)!=0;
 atomic_fetch_add_explicit(&proof->seq,1u,memory_order_acq_rel);
 JDP_SET(proof->channel,channel);JDP_SET(proof->control,ctrl);
 JDP_SET(proof->desired,on_extra?channel+2u:channel);JDP_SET(proof->snapshot_reason,why);
 JDP_SET(proof->select_result,selected_result);JDP_SET(proof->step_result,step_result);
 JDP_SET(proof->phase,jd_state[side_idx].phase);
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
 if(atomic_load(&jd_installed)&&session==atomic_load_explicit(&jd_session,memory_order_acquire)){
  memcpy(frame,out,XZ_JD_FRAME);
#ifdef XZ_JOG_DISPLAY_PROOF
  copied=1;
#endif
 }
#ifdef XZ_JOG_DISPLAY_PROOF
 latch_observe(side_idx,session,ctrl,route_epoch,now_ms,why,selected_result,step_result,phase_before,out,candidate_changed,copied,read_result);
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
