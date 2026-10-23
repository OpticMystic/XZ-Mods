/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "sync_observation.h"
#include <unistd.h>
#include <time.h>
#include "sync_link_runtime.h"
#include "runtime.h"
#include "clock_compat.h"
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

#if XZ_SYNC_OBSERVATION
__attribute__((visibility("default"),used))
struct xz_syo_object xz_mods_sync_observation_v1={
 .magic=0x53594f31u,.version=1,.size=sizeof(struct xz_syo_object),
 .counter_count=SYO_COUNTER_COUNT,.record_count=SYO_RECORD_COUNT,.record_words=SYO_WORDS
};
#endif
#define XZ_VA_WRITER UINT32_MAX

static void va_copy(void *to,const void *from,unsigned n){
 unsigned char *d=to;const unsigned char *s=from;
 for(unsigned i=0;i<n;i++)d[i]=s[i];
}
static void put_le32(unsigned char *p,uint32_t v){
 p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
 p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static uint32_t get_le32(const unsigned char *p){
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t get_be32(const unsigned char *p){
 return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|(uint32_t)p[3];
}

void xz_va_init(struct xz_va *s){
 atomic_init(&s->foreign_epoch[0],1);atomic_init(&s->foreign_epoch[1],1);
 atomic_init(&s->operator_epoch,1);atomic_init(&s->active,0);
 atomic_init(&s->pending,0);atomic_init(&s->published,0);
 atomic_init(&s->source_id,0);atomic_init(&s->claim_foreign,0);
 atomic_init(&s->claim_operator,0);atomic_init(&s->lease_generation,0);
 atomic_init(&s->valid_generation,0);
 atomic_init(&s->producer_guard,0);
 for(unsigned i=0;i<3;i++){
  atomic_init(&s->slots[i].refs,0);
  memset(&s->slots[i].value,0,sizeof s->slots[i].value);
 }
}
uint32_t xz_va_probe_epoch(struct xz_va *s,uint32_t id){
 return id<3||id>4?0:atomic_load(&s->foreign_epoch[id-3]);
}
static int producer_enter(struct xz_va *s){
 uint32_t zero=0;
 return atomic_compare_exchange_strong(&s->producer_guard,&zero,1);
}
static int producer_leave(struct xz_va *s,int result){
 atomic_store_explicit(&s->producer_guard,0,memory_order_release);
 return result;
}
int xz_va_claim(struct xz_va *s,uint32_t id,int absent,uint32_t probed){
 if(!producer_enter(s))return 0;
 if(!absent||id<3||id>4||atomic_load(&s->active))return producer_leave(s,0);
 uint32_t f=xz_va_probe_epoch(s,id),o=atomic_load(&s->operator_epoch);
 uint32_t generation=atomic_load(&s->lease_generation);
 if(!f||!o||f!=probed||generation==UINT32_MAX)return producer_leave(s,0);
 generation++;
 atomic_store(&s->source_id,id);atomic_store(&s->claim_foreign,f);
 atomic_store(&s->claim_operator,o);atomic_store(&s->pending,0);
 atomic_store(&s->published,0);atomic_store(&s->lease_generation,generation);
 atomic_store(&s->valid_generation,generation);
 atomic_store(&s->active,1);
 if(xz_va_probe_epoch(s,id)==f&&atomic_load(&s->operator_epoch)==o)return producer_leave(s,1);
 atomic_store(&s->active,0);return producer_leave(s,0);
}
static int va_live(struct xz_va *s){
 return atomic_load(&s->active)&&
  atomic_load(&s->valid_generation)==atomic_load(&s->lease_generation)&&
  atomic_load(&s->claim_foreign)==xz_va_probe_epoch(s,atomic_load(&s->source_id))&&
  atomic_load(&s->claim_operator)==atomic_load(&s->operator_epoch);
}
uint32_t xz_va_ticket(struct xz_va *s){
 return va_live(s)?atomic_load(&s->lease_generation):0;
}
int xz_va_offer(struct xz_va *s,const struct xz_va_bundle *v,uint32_t prepared){
 if(!producer_enter(s))return 0;
 uint32_t generation=atomic_load(&s->lease_generation),id=atomic_load(&s->source_id);
 uint32_t foreign=atomic_load(&s->claim_foreign),op=atomic_load(&s->claim_operator);
 uint32_t status_id=get_le32(v->status);
 if(!prepared||prepared!=generation||!va_live(s)||v->source_id!=id||status_id!=v->source_id)
  return producer_leave(s,0);
 uint32_t pub=atomic_load_explicit(&s->published,memory_order_acquire);
 if(pub>UINT32_MAX-4)return producer_leave(s,0);
 for(unsigned i=0;i<3;i++){
  if(pub&&(pub&3)==i+1)continue;
  uint32_t expected=0;
  if(!atomic_compare_exchange_strong(&s->slots[i].refs,&expected,XZ_VA_WRITER))continue;
  va_copy(&s->slots[i].value,v,sizeof *v);
  atomic_store_explicit(&s->slots[i].refs,0,memory_order_release);
  if(!va_live(s)||generation!=atomic_load(&s->lease_generation)||id!=atomic_load(&s->source_id)||
     foreign!=atomic_load(&s->claim_foreign)||op!=atomic_load(&s->claim_operator))
   return producer_leave(s,0);
  uint32_t expected_pub=pub;
  int accepted=atomic_compare_exchange_strong_explicit(&s->published,&expected_pub,((pub&~3u)+4)|(i+1),
   memory_order_release,memory_order_relaxed);
  return producer_leave(s,accepted);
 }
 return producer_leave(s,0);
}
void xz_va_explicit_request(struct xz_va *s){
 uint32_t generation=atomic_load(&s->lease_generation),expected=atomic_load(&s->pending);
 for(unsigned n=0;n<2;n++){
  if(!va_live(s)||generation!=atomic_load(&s->lease_generation))return;
  if(atomic_compare_exchange_strong(&s->pending,&expected,generation))return;
 }
}
static void va_advance(_Atomic uint32_t *p){
 uint32_t old=atomic_load(p);
 for(unsigned n=0;n<2&&old;n++)
  if(atomic_compare_exchange_strong(p,&old,old==UINT32_MAX?0:old+1))return;
 /* Exhaustion or competing operators must fail closed, never reuse a token. */
 if(old)atomic_store(p,0);
}
void xz_va_foreign(struct xz_va *s,uint32_t id){
 if(id<3||id>4)return;
 va_advance(&s->foreign_epoch[id-3]);
 if(id!=atomic_load(&s->source_id))return;
 atomic_store(&s->pending,0);atomic_store(&s->active,0);
}
void xz_va_operator(struct xz_va *s){
 va_advance(&s->operator_epoch);
 atomic_store(&s->pending,0);atomic_store(&s->active,0);
}
int xz_va_begin(struct xz_va *s,struct xz_va_frame *f){
 if(f->depth==UINT32_MAX)return -1;
 if(f->depth){f->depth++;return (int)f->mode;}
 f->depth=1;f->mode=0;f->source_id=atomic_load(&s->source_id);
 f->foreign_epoch=atomic_load(&s->claim_foreign);
 f->operator_epoch=atomic_load(&s->claim_operator);
 f->lease_generation=atomic_load(&s->lease_generation);
 f->expected_link_mgr=NULL;f->draining_sync_master=NULL;f->probing_original=0;f->draining_request=0;
 if(!f->source_id||f->operator_epoch!=atomic_load(&s->operator_epoch))return 0;
 if(f->foreign_epoch!=xz_va_probe_epoch(s,atomic_load(&s->source_id))){f->mode=2;return 2;}
 if(!va_live(s)){f->mode=f->operator_epoch==atomic_load(&s->operator_epoch)?2:0;return (int)f->mode;}
 for(unsigned attempt=0;attempt<2;attempt++){
  uint32_t pub=atomic_load_explicit(&s->published,memory_order_acquire);
  if(!(pub&3))return 0;
  unsigned i=(pub&3)-1;uint32_t refs=atomic_load(&s->slots[i].refs);
  if(refs>=XZ_VA_WRITER-1||!atomic_compare_exchange_strong(&s->slots[i].refs,&refs,refs+1))continue;
  if(pub==atomic_load_explicit(&s->published,memory_order_acquire)&&va_live(s)&&
     f->source_id==atomic_load(&s->source_id)&&f->foreign_epoch==atomic_load(&s->claim_foreign)&&
     f->operator_epoch==atomic_load(&s->claim_operator)&&f->lease_generation==atomic_load(&s->lease_generation)){
   f->slot=i;f->mode=1;return 1;
  }
  atomic_fetch_sub(&s->slots[i].refs,1);
 }
 f->mode=2;return 2;
}
int xz_va_take_request(struct xz_va *s,struct xz_va_frame *f){
 if(!f->depth||f->mode!=1||!va_live(s)||f->source_id!=atomic_load(&s->source_id)||
    f->foreign_epoch!=atomic_load(&s->claim_foreign)||f->operator_epoch!=atomic_load(&s->claim_operator)||
    f->lease_generation!=atomic_load(&s->lease_generation))return 0;
 uint32_t expected=f->lease_generation;
 return atomic_compare_exchange_strong(&s->pending,&expected,0)?(int)f->source_id:0;
}
int xz_va_get(struct xz_va *s,struct xz_va_frame *f,unsigned kind,void *out){
 if(!f->depth||!f->mode)return 0;
 if(f->mode==2)return -1;
 if(!va_live(s)||f->lease_generation!=atomic_load(&s->lease_generation))return -1;
 if(kind>1)return -1;
 const struct xz_va_bundle *v=&s->slots[f->slot].value;
 va_copy(out,kind?v->beat:v->status,kind?44:56);
 return 1;
}
void xz_va_end(struct xz_va *s,struct xz_va_frame *f){
 if(!f->depth)return;
 if(--f->depth)return;
 if(f->mode==1)atomic_fetch_sub(&s->slots[f->slot].refs,1);
 f->mode=0;f->expected_link_mgr=NULL;f->draining_sync_master=NULL;f->probing_original=0;f->draining_request=0;
}
void xz_va_registry_init(struct xz_va_registry *r){
 for(unsigned i=0;i<4;i++){
  memset(&r->slots[i].frame,0,sizeof r->slots[i].frame);
  atomic_init(&r->slots[i].owner,0);
 }
}
struct xz_va_frame *xz_va_registry_lookup(struct xz_va_registry *r,uintptr_t key){
 if(!key||key==UINTPTR_MAX)return NULL;
 for(unsigned i=0;i<4;i++)
  if(atomic_load_explicit(&r->slots[i].owner,memory_order_acquire)==key)return &r->slots[i].frame;
 return NULL;
}
struct xz_va_frame *xz_va_registry_enter(struct xz_va *s,struct xz_va_registry *r,uintptr_t key){
 if(!key||key==UINTPTR_MAX)return NULL;
 struct xz_va_frame *f=xz_va_registry_lookup(r,key);
 if(f)return xz_va_begin(s,f)<0?NULL:f;
 for(unsigned i=0;i<4;i++){
  uintptr_t expected=0;
  if(!atomic_compare_exchange_strong(&r->slots[i].owner,&expected,UINTPTR_MAX))continue;
  f=&r->slots[i].frame;f->depth=0;
  atomic_store_explicit(&r->slots[i].owner,key,memory_order_release);
  xz_va_begin(s,f);
  return f;
 }
 return NULL;
}
void xz_va_registry_leave(struct xz_va *s,struct xz_va_registry *r,uintptr_t key){
 for(unsigned i=0;i<4;i++)if(atomic_load_explicit(&r->slots[i].owner,memory_order_acquire)==key){
  xz_va_end(s,&r->slots[i].frame);
  if(!r->slots[i].frame.depth)atomic_store_explicit(&r->slots[i].owner,0,memory_order_release);
  return;
 }
}

void xz_sync_link_bundle_from_plan(const struct xz_sync_link_plan *p,struct xz_va_bundle *b){
 if(!p||!b)return;
 memset(b,0,sizeof *b);
 b->source_id=p->source_id;
 put_le32(b->status+0x00,p->source_id);
 b->status[0x04]=0xa4;b->status[0x05]=0x01;b->status[0x06]=0;b->status[0x07]=0;
 memcpy(b->status+0x10,p->music_id,8);
 b->status[0x18]=1;
 put_le32(b->status+0x1c,p->bpm100);
 put_le32(b->status+0x20,p->q20_speed);
 put_le32(b->status+0x24,p->q20_speed);
 b->status[0x28]=1;
 put_le32(b->status+0x2c,p->bpm100);
 put_le32(b->status+0x30,p->q20_speed);
 put_le32(b->status+0x34,p->q20_speed);
 for(unsigned i=0;i<6;i++)put_le32(b->beat+i*4,get_be32(p->beat_packet+i*4));
 put_le32(b->beat+0x18,p->q20_speed);
 put_le32(b->beat+0x1c,p->bpm100);
 b->beat[0x20]=p->beat_packet[0x38];
 b->beat[0x21]=p->beat_packet[0x39];
 b->beat[0x22]=p->beat_packet[0x3b];
 b->beat[0x23]=0;
 memcpy(b->beat+0x24,&p->beat_sec,4);
 memcpy(b->beat+0x28,&p->beat_usec,4);
}

static void derive_music_id(int deck_index,const struct xz_extra_deck *d,unsigned char out[8]){
 uint32_t lo=d->timeline.load_revision^((uint32_t)(deck_index+3)<<24)^((uint32_t)(d->source&0xff)<<16);
 uint32_t hi=(uint32_t)d->timeline.media_generation^(d->timeline.route_epoch<<8)^0x585a4d44u;
 if(!lo&&!hi)lo=1;
 put_le32(out,lo);put_le32(out+4,hi);
}

int xz_sync_link_anchor_from_deck(int deck_index,const struct xz_extra_deck *d,uint64_t now_mono_us,struct xz_sync_link_pcm_anchor *out){
 if(!d||!out||deck_index<0||deck_index>=XZ_EXTRA_DECKS)return 0;
 memset(out,0,sizeof *out);
 const uint32_t req_flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_SINK_FRAME|
  XZ_EXTRA_TIMELINE_APPLIED_RATE;
 int history=!!(d->timeline.flags&XZ_EXTRA_TIMELINE_BEAT_HISTORY);
 if(!d->loaded||!d->playing||!(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE))||
    !(d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID))||!d->grid||d->grid_count<2||
    (d->timeline.flags&req_flags)!=req_flags||d->timeline.source_rate<8000||!isfinite(d->timeline.completed_rate)||d->timeline.completed_rate<=0||
    (history&&(!isfinite(d->timeline.completed_beat_age_frames)||d->timeline.completed_beat_age_frames<0))||
    !d->timeline.route_epoch||!d->timeline.load_revision||!d->timeline.transport_generation||!d->timeline.sink_frame||
    d->timeline.observed_wire_frame<d->timeline.sink_frame)
  return 0;
 uint32_t now_ms=(uint32_t)(now_mono_us/1000u);
 uint32_t age_ms=now_ms-d->timeline.at_ms;
 if((int32_t)age_ms<0||age_ms>90u)return 0;
 uint64_t cap_us=(uint64_t)d->timeline.at_ms*1000u+(now_mono_us%1000u);
 if(cap_us>now_mono_us)cap_us=now_mono_us;
 /* The source tag belongs to the completed block end, not the later observation.
    Keep the existing monotonic mapping and subtract only the known wire gap. */
 uint64_t gap=d->timeline.observed_wire_frame-d->timeline.sink_frame;
 if(gap>4410u)return 0; /* 100 ms: refuse stale or inconsistent completion coordinates */
 uint64_t lag_us=gap*1000000ULL/44100u;
 if(lag_us>cap_us)return 0;
 cap_us-=lag_us;
 double ms=(double)d->timeline.source_frame*1000./(double)d->timeline.source_rate;
 unsigned lo=0,hi=d->grid_count;
 if(history){
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(d->grid[mid].ms<d->timeline.completed_beat_ms)lo=mid+1;else hi=mid;}
  if(lo==d->grid_count||d->grid[lo].ms!=d->timeline.completed_beat_ms)return 0;
 }else{
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(d->grid[mid].ms<=ms)lo=mid+1;else hi=mid;}
  if(!lo)return 0; /* No known predecessor before the first grid beat. */
  lo--;
 }
 unsigned idx=lo;
 int32_t period_ms=idx+1<d->grid_count?d->grid[idx+1].ms-d->grid[idx].ms:d->grid[idx].ms-d->grid[idx-1].ms;
 if(period_ms<250||period_ms>1500||d->grid[idx].beat_in_bar<1||d->grid[idx].beat_in_bar>4)return 0;
 double beat_frame=(double)d->grid[idx].ms*(double)d->timeline.source_rate/1000.;
 double src_frame=(double)d->timeline.source_frame;
 if(src_frame+1.<=beat_frame){
  if(!history||!d->loop.active||d->loop.out_ms<=d->loop.in_ms||ms<d->loop.in_ms||ms>=d->loop.out_ms||
     d->grid[idx].ms<d->loop.in_ms||d->grid[idx].ms>=d->loop.out_ms)return 0;
  src_frame+=(double)(d->loop.out_ms-d->loop.in_ms)*d->timeline.source_rate/1000.;
 }
 for(unsigned k=0;k<8;k++){
  if(!d->loop.active&&idx+1+k<d->grid_count&&d->grid[idx+1+k].ms>d->grid[idx].ms){
   uint32_t delta=(uint32_t)(d->grid[idx+1+k].ms-d->grid[idx].ms);
   if(k&&delta<=out->future_beat_ms[k-1])delta=out->future_beat_ms[k-1]+(uint32_t)period_ms;
   out->future_beat_ms[k]=delta;
  }else{
   out->future_beat_ms[k]=(k?out->future_beat_ms[k-1]:0u)+(uint32_t)period_ms;
  }
 }
 out->route_epoch=d->timeline.route_epoch;
 out->source_revision=d->timeline.load_revision;
 out->playback_epoch=d->timeline.transport_generation;
 out->completed_output_frame=d->timeline.sink_frame;
 out->captured_mono_us=cap_us;
 derive_music_id(deck_index,d,out->music_id);
 out->completed_pcm=1;
 out->clock_domain_mapped=1;
 out->playing=1;
 out->phase_eligible=1;
 out->source_rate=d->timeline.source_rate;
 out->bpm100=d->track_bpm_x100>0?(uint32_t)d->track_bpm_x100:(uint32_t)llround(6000000./(double)period_ms);
 out->uncertainty_us=1250; /* existing 250 us allowance plus millisecond observation quantization */
 out->source_frame=src_frame;
 out->beat_source_frame=beat_frame;
 out->effective_speed=d->timeline.completed_rate;
 out->completed_beat_age_frames=history?d->timeline.completed_beat_age_frames:0;out->completed_beat_history=history;
 out->beat_in_bar=d->grid[idx].beat_in_bar;
 out->lane=(unsigned)deck_index;
 return 1;
}

static struct xz_va rt_va;
static struct xz_va_registry rt_registry;
static pthread_mutex_t rt_pub_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_sync_link_lease rt_lease;
static struct xz_sync_link_clock rt_clock;
static _Atomic int rt_installed;
static _Atomic int rt_slot_absent[2];
static _Atomic uint32_t rt_slot_probed_epoch[2];
static _Atomic int32_t rt_observed_master_id;
static _Atomic int rt_observed_ready;
static _Atomic int rt_committed_deck;
static _Atomic uint32_t rt_request_epoch,rt_request_operator,rt_ack_generation,rt_notify_pending;
static _Atomic uint32_t rt_initialized,rt_global_mode,rt_legacy_mode;
static _Atomic uint32_t rt_consumer_sequence,rt_actual_guard,rt_actual_serial,rt_actual_exhausted;
static _Atomic uint32_t rt_invalid_sequence;
/* Every shared payload word is atomic, including split timestamps. The bounded
   serialized writer owns rt_last_sample; readers never access that plain object. */
static _Atomic uint32_t rt_actual_words[16];
struct rt_master_sample {
 struct xz_master_observation actual;
 uint64_t at_us,pcm_us;
 uint32_t foreign_epoch,sequence;
 int32_t native_id;
};
static struct rt_master_sample rt_last_sample;
_Static_assert(sizeof(struct xz_master_observation)==9*sizeof(uint32_t),"canonical observation words");
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"native callbacks require lock-free 32-bit atomics");
_Static_assert(XZ_SYNC_LINK_MAX_AGE_US==XZ_MASTER_MAX_AGE_MS*1000u,"shared authority age");


static _Atomic int rt_restore_pending;
static _Atomic int rt_restore_input_id;
static _Atomic int32_t rt_restore_expected_master;
static _Atomic uint32_t rt_restore_operator_epoch;
static _Atomic uint32_t rt_restore_foreign_epoch;
static _Atomic uint32_t rt_restore_source_id;

static int (*orig_sync_mgr_sync)(void *);
static int (*orig_get_player_info)(void *,const int *,void *);
static int (*orig_get_beat_info)(void *,const int *,const uint8_t *,void *);
static int (*orig_set_player_info)(void *,const int *,const void *,const void *,const void *,
                                   const void *,const void *,const void *,const void *,const void *,
                                   const void *,const void *,const void *,const void *);
static int (*orig_set_beat_info)(void *,const int *,const void *,const void *);
static int (*orig_set_master_on)(void *,const int *);
static int (*test_set_link_master)(void *,const int *);
static void (*test_on_use_native_master)(void);
static int (*test_monotonic_us)(uint64_t *);

static int runtime_monotonic_us(uint64_t *out){
 if(test_monotonic_us)return test_monotonic_us(out);
 struct timespec now;
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&now)||now.tv_sec<0)return -1;
 *out=(uint64_t)now.tv_sec*1000000ULL+(uint64_t)(now.tv_nsec/1000L);
 return 0;
}
/* Only revoke the generation borrowed by this frame. A newer claim cannot be
   cancelled by a consumer finishing an old frame. Slots remain reader-pinned. */
#if XZ_SYNC_OBSERVATION
static void syo_fresh(struct xz_va_frame *f,unsigned result,uint64_t now,uint64_t captured){
 SYO_REC(FRESH,5,result,f?f->mode:0,f?f->depth:0,f?f->source_id:0,f?f->lease_generation:0,
  SYO_LO(now),SYO_HI(now),SYO_LO(captured),SYO_HI(captured));
}
#define SYO_FRESH(...) syo_fresh(__VA_ARGS__)
#else
#define SYO_FRESH(...) ((void)0)
#endif
static int runtime_frame_fresh(struct xz_va_frame *f){
 if(!f||f->mode!=1){SYO_INC(FRESH_WRONG_SCOPE);SYO_FRESH(f,1,0,0);return 0;}
 if(!va_live(&rt_va)){SYO_INC(FRESH_DEAD);SYO_FRESH(f,2,0,0);return 0;}
 if(f->lease_generation!=atomic_load(&rt_va.lease_generation)){SYO_INC(FRESH_GENERATION);SYO_FRESH(f,3,0,0);return 0;}
 uint64_t now,captured=rt_va.slots[f->slot].value.captured_mono_us;
 int clock_rc=runtime_monotonic_us(&now);
 if(!clock_rc&&now>=captured&&now-captured<=XZ_SYNC_LINK_MAX_AGE_US){SYO_INC(FRESH_OK);SYO_FRESH(f,0,now,captured);return 1;}
 if(clock_rc){SYO_INC(FRESH_CLOCK_FAIL);SYO_FRESH(f,4,0,captured);}
 else if(now<captured){SYO_INC(FRESH_FUTURE);SYO_FRESH(f,5,now,captured);}
 else{SYO_INC(FRESH_EXPIRED);SYO_FRESH(f,6,now,captured);}
 uint32_t generation=f->lease_generation;
 if(atomic_compare_exchange_strong(&rt_va.valid_generation,&generation,0)){
  SYO_INC(EXPIRED_WITHDRAW);
  generation=f->lease_generation;
  (void)atomic_compare_exchange_strong(&rt_va.pending,&generation,0);
  atomic_store(&rt_committed_deck,-1);
 }
 return 0;
}
static int runtime_frame_request_current(struct xz_va_frame *f){
 if(!runtime_frame_fresh(f))return 0;
 const struct xz_va_bundle *v=&rt_va.slots[f->slot].value;
 if(!v->request_epoch)return atomic_load(&rt_legacy_mode)&&!atomic_load(&rt_global_mode);
 return v->request_epoch==atomic_load(&rt_request_epoch)&&
  f->operator_epoch==atomic_load(&rt_request_operator)&&f->operator_epoch==atomic_load(&rt_va.operator_epoch);
}

static uintptr_t current_thread_key(void){
 uintptr_t k=(uintptr_t)pthread_self();
 if(!k||k==UINTPTR_MAX)k=1;
 return k;
}
static int call_set_link_master(void *sm,const int *input_id){
 SYO_INC(MASTER_CALL);
#if XZ_SYNC_OBSERVATION
 uint32_t requested_id=input_id?(uint32_t)*input_id:UINT32_MAX;
#endif
 int rc;
 if(test_set_link_master)rc=test_set_link_master(sm,input_id);
 else {
#ifdef __arm__
  rc=((int (*)(void *,const int *))(uintptr_t)0x5984c)(sm,input_id);
#else
  (void)sm;rc=0;
#endif
 }
 if(rc)SYO_INC(MASTER_CALL_FAIL);else SYO_INC(MASTER_CALL_OK);
 SYO_REC(MASTER_CALL,4,requested_id,rc,xz_syo_request_seq(0),xz_syo_request_seq(1));
 return rc;
}
static void notify_use_native_master(void){
 atomic_store(&rt_notify_pending,1);
}
static void deliver_native_master(uint32_t epoch,uint32_t request){
 /* Global player cancellation uses its admission token and the observer under
    its own lock. An unconditional callback here could erase a newer press. */
 if(atomic_load(&rt_global_mode)||!atomic_load(&rt_legacy_mode))return;
 if(epoch!=atomic_load(&rt_va.operator_epoch)||request!=atomic_load(&rt_request_epoch))return;
 if(test_on_use_native_master){test_on_use_native_master();return;}
 (void)xz_extra_decks_use_native_master();
}
static void invalidate_sample(uint32_t sequence){
 uint32_t old=atomic_load(&rt_invalid_sequence);
 for(unsigned n=0;n<2&&old<sequence;n++)
  if(atomic_compare_exchange_strong(&rt_invalid_sequence,&old,sequence))return;
 if(old<sequence)atomic_store(&rt_actual_exhausted,1);
}
static int sample_live(const struct rt_master_sample *s,uint64_t now){
 const struct xz_master_observation *o=&s->actual;
 if(!atomic_load(&rt_installed)||atomic_load(&rt_actual_exhausted)||!o->revision||
    s->sequence<=atomic_load(&rt_invalid_sequence)||
    !o->operator_epoch||o->operator_epoch!=atomic_load(&rt_va.operator_epoch)||
    now<s->at_us||now-s->at_us>XZ_SYNC_LINK_MAX_AGE_US)return 0;
 if(o->deck==XZ_MASTER_DECK3||o->deck==XZ_MASTER_DECK4){
  uint32_t id=(uint32_t)o->deck+1;
  return o->request_epoch&&o->request_epoch==atomic_load(&rt_request_epoch)&&va_live(&rt_va)&&
   atomic_load(&rt_va.source_id)==id&&o->lease_generation==atomic_load(&rt_va.lease_generation)&&
   o->lease_generation==atomic_load(&rt_ack_generation)&&s->foreign_epoch==xz_va_probe_epoch(&rt_va,id)&&
   now>=s->pcm_us&&now-s->pcm_us<=XZ_SYNC_LINK_MAX_AGE_US;
 }
 if(s->native_id==2||s->native_id==3)
  if(s->foreign_epoch!=xz_va_probe_epoch(&rt_va,(uint32_t)s->native_id+1))return 0;
 return o->deck>=XZ_MASTER_NONE&&o->deck<=XZ_MASTER_DECK2;
}
static int read_master_sample(struct rt_master_sample *s){
 for(unsigned attempt=0;attempt<2;attempt++){
  uint32_t serial=atomic_load(&rt_actual_serial),w[16];
  if(!serial||(serial&1))continue;
  for(unsigned i=0;i<16;i++)w[i]=atomic_load(&rt_actual_words[i]);
  if(serial!=atomic_load(&rt_actual_serial))continue;
  memcpy(&s->actual,w,sizeof s->actual);
  s->at_us=(uint64_t)w[9]|(uint64_t)w[10]<<32;
  s->pcm_us=(uint64_t)w[11]|(uint64_t)w[12]<<32;
  s->foreign_epoch=w[13];s->sequence=w[14];memcpy(&s->native_id,&w[15],4);
  uint64_t now;
  if(runtime_monotonic_us(&now)||!sample_live(s,now))return 0;
  return serial==atomic_load(&rt_actual_serial)&&sample_live(s,now);
 }
 return 0;
}
int xz_sync_link_runtime_master_observe(struct xz_master_observation *out){
 if(!out)return 0;
 memset(out,0,sizeof *out);out->deck=XZ_MASTER_UNKNOWN;
 struct rt_master_sample s;
 if(read_master_sample(&s)){*out=s.actual;return 1;}
 /* This is a coherent control token, not confirmed engine authority. Admission
    must see a new operator fence before any deferred consumer publication. */
 for(unsigned n=0;n<2;n++){
  uint32_t op=atomic_load(&rt_va.operator_epoch);
  if(!op||!atomic_load(&rt_installed)||atomic_load(&rt_actual_exhausted))return 0;
  if(op==atomic_load(&rt_va.operator_epoch)&&atomic_load(&rt_installed)){
   out->operator_epoch=op;return 1;
  }
 }
 return 0;
}
static void publish_master_sample(struct rt_master_sample *s){
 uint32_t zero=0;
 if(!atomic_compare_exchange_strong(&rt_actual_guard,&zero,1)){invalidate_sample(s->sequence);return;}
 uint32_t serial=atomic_load(&rt_actual_serial);
 if(atomic_load(&rt_actual_exhausted)||serial>UINT32_MAX-2||
    rt_last_sample.actual.revision==UINT32_MAX){
  atomic_store(&rt_actual_exhausted,1);goto done;
 }
 if(s->sequence<=rt_last_sample.sequence||!atomic_load(&rt_installed)||
    s->actual.operator_epoch!=atomic_load(&rt_va.operator_epoch))goto done;
 s->actual.revision=rt_last_sample.actual.revision;
 struct xz_master_observation a=s->actual,b=rt_last_sample.actual;
 a.revision=b.revision=0;a.observed_ms=b.observed_ms=0;
 if(!s->actual.revision||memcmp(&a,&b,sizeof a)||s->native_id!=rt_last_sample.native_id||
    s->foreign_epoch!=rt_last_sample.foreign_epoch||
    s->at_us<rt_last_sample.at_us||s->at_us-rt_last_sample.at_us>XZ_SYNC_LINK_MAX_AGE_US)
  s->actual.revision++;
 if(!sample_live(s,s->at_us))goto done;
 uint32_t w[16];memcpy(w,&s->actual,sizeof s->actual);
 w[9]=(uint32_t)s->at_us;w[10]=(uint32_t)(s->at_us>>32);
 w[11]=(uint32_t)s->pcm_us;w[12]=(uint32_t)(s->pcm_us>>32);
 w[13]=s->foreign_epoch;w[14]=s->sequence;memcpy(&w[15],&s->native_id,4);
 atomic_store(&rt_actual_serial,serial+1);
 for(unsigned i=0;i<16;i++)atomic_store(&rt_actual_words[i],w[i]);
 rt_last_sample=*s;
 atomic_store(&rt_observed_master_id,s->native_id);atomic_store(&rt_observed_ready,1);
 atomic_store(&rt_actual_serial,serial+2);
 done:atomic_store(&rt_actual_guard,0);
}
static void observe_consumer(struct xz_va_frame *f,int32_t id,uint32_t op,uint32_t sequence,int requested,int setter_rc){
 uint64_t now;
 if(!sequence)return;
 if(runtime_monotonic_us(&now)||op!=atomic_load(&rt_va.operator_epoch)||id< -1){invalidate_sample(sequence);return;}
 struct rt_master_sample s={.at_us=now,.sequence=sequence,.native_id=id};
 s.actual=(struct xz_master_observation){.deck=id==-1?XZ_MASTER_NONE:id>=0&&id<2?id:XZ_MASTER_EXTERNAL,
  .observed_ms=(uint32_t)(now/1000),.operator_epoch=op};
 if(id==2||id==3)s.foreign_epoch=xz_va_probe_epoch(&rt_va,(uint32_t)id+1);
 if(f->mode==1&&runtime_frame_fresh(f)&&id==(int32_t)f->source_id-1){
  /* This pointer is borrowed only until registry_leave. Copy provenance now. */
  const struct xz_va_bundle *v=&rt_va.slots[f->slot].value;
  if(v->request_epoch==atomic_load(&rt_request_epoch)&&
     (!v->request_epoch||atomic_load(&rt_request_operator)==op)){
   uint32_t ack=atomic_load(&rt_ack_generation);
   if(requested==(int)f->source_id&&!setter_rc&&ack<f->lease_generation)
    (void)atomic_compare_exchange_strong(&rt_ack_generation,&ack,f->lease_generation);
   if(v->request_epoch&&atomic_load(&rt_ack_generation)==f->lease_generation){
    s.actual.deck=id;s.actual.request_epoch=v->request_epoch;s.actual.lease_generation=f->lease_generation;
    s.actual.route_epoch=v->route_epoch;s.actual.load_revision=v->load_revision;
    s.actual.transport_generation=v->transport_generation;
    s.pcm_us=v->captured_mono_us;s.foreign_epoch=f->foreign_epoch;
   }
  }
 }
 publish_master_sample(&s);
}

static int hook_sync_mgr_sync(void *sync_mgr){
 SYO_INC(CONSUMER);
 if(!atomic_load(&rt_installed)||!sync_mgr){SYO_INC(CONSUMER_DISABLED);if(orig_sync_mgr_sync)SYO_INC(CONSUMER_ORIGINAL);return orig_sync_mgr_sync?orig_sync_mgr_sync(sync_mgr):0;}
 void *sm=NULL,*link_mgr=NULL;
 memcpy(&sm,(const unsigned char *)sync_mgr+4,sizeof sm);
 if(sm)memcpy(&link_mgr,(const unsigned char *)sm+0x30,sizeof link_mgr);
 uintptr_t key=current_thread_key();
 struct xz_va_frame *f=xz_va_registry_enter(&rt_va,&rt_registry,key);
 if(!f){SYO_INC(CONSUMER_NO_FRAME);return 0;}
 int32_t syo_before=-2;
 uint32_t observed_op=atomic_load(&rt_va.operator_epoch),sequence=0;
 int requested=0,setter_rc=-1;
 if(f->depth==1){
  uint32_t old=atomic_fetch_add(&rt_consumer_sequence,1);
  if(old==UINT32_MAX)atomic_store(&rt_actual_exhausted,1);
  else sequence=old+1;
 }
 if(f->depth==1){
  f->expected_link_mgr=link_mgr;
  if(link_mgr&&orig_get_player_info){
   for(int id=3;id<=4;id++){
    uint32_t p_epoch=xz_va_probe_epoch(&rt_va,(uint32_t)id);
    unsigned char raw[56];memset(raw,0,sizeof raw);
    f->probing_original=1;
    int prc=orig_get_player_info(link_mgr,&id,raw);
    f->probing_original=0;
    int absent=(prc==0&&get_le32(raw)==0&&get_le32(raw+0x1c)==0x7fffffffu);
    atomic_store(&rt_slot_probed_epoch[id-3],p_epoch);
    atomic_store(&rt_slot_absent[id-3],absent);
   }
  }
  int32_t cur_master=-2;
  if(sm){
   uint8_t dummy=((const uint8_t *)sm)[0x15];
   memcpy(&cur_master,(const unsigned char *)sm+(dummy?0x18:0x08),4);
  }
  SYO_INC(MASTER_BEFORE);syo_before=cur_master;

  int req_id=sm&&link_mgr&&runtime_frame_request_current(f)?xz_va_take_request(&rt_va,f):0;
  if((req_id==3||req_id==4)&&sm&&runtime_frame_request_current(f)){
   atomic_store(&rt_restore_pending,0);
   f->draining_sync_master=sm;
   f->draining_request=1;
   requested=req_id;setter_rc=call_set_link_master(sm,&req_id);
   f->draining_sync_master=NULL;f->draining_request=0;
  }else if(sm&&atomic_exchange(&rt_restore_pending,0)){
   int32_t exp_master=atomic_load(&rt_restore_expected_master);
   uint32_t exp_op=atomic_load(&rt_restore_operator_epoch);
   uint32_t exp_for=atomic_load(&rt_restore_foreign_epoch);
   uint32_t src_id=atomic_load(&rt_restore_source_id);
   int restore_id=atomic_load(&rt_restore_input_id);
   if(cur_master==exp_master&&atomic_load(&rt_va.operator_epoch)==exp_op&&
      xz_va_probe_epoch(&rt_va,src_id)==exp_for){
    f->draining_sync_master=sm;
    if(restore_id>0)(void)call_set_link_master(sm,&restore_id);
    else if(orig_set_master_on){int none=-1;(void)orig_set_master_on(sm,&none);}
    f->draining_sync_master=NULL;
   }
  }
 }
 if(orig_sync_mgr_sync)SYO_INC(CONSUMER_ORIGINAL);
 int rc=orig_sync_mgr_sync?orig_sync_mgr_sync(sync_mgr):0;
 if(f->depth==1&&sm){
  int32_t after_master=-2;
  uint8_t dummy=((const uint8_t *)sm)[0x15];
  memcpy(&after_master,(const unsigned char *)sm+(dummy?0x18:0x08),4);
  SYO_INC(MASTER_AFTER);
  SYO_REC(MASTER_OBSERVED,3,syo_before,after_master,rc);
  if(orig_sync_mgr_sync&&!rc&&link_mgr)observe_consumer(f,after_master,observed_op,sequence,requested,setter_rc);
  else invalidate_sample(sequence);
 }
 else if(f->depth==1)invalidate_sample(sequence);
 (void)syo_before;
 xz_va_registry_leave(&rt_va,&rt_registry,key);
 return rc;
}

static int hook_get_player_info(void *link_mgr,const int *id_ptr,void *out){
 SYO_INC(STATUS_READ);
 if(atomic_load(&rt_installed)&&id_ptr&&(*id_ptr==3||*id_ptr==4)&&out){
  struct xz_va_frame *f=xz_va_registry_lookup(&rt_registry,current_thread_key());
  if(f&&f->depth>0&&!f->probing_original&&f->expected_link_mgr==link_mgr&&f->source_id==(uint32_t)*id_ptr){
   int vrc=f->mode==1&&!runtime_frame_fresh(f)?-1:xz_va_get(&rt_va,f,0,out);
   SYO_INC(STATUS_SCOPE);
   if(vrc==1){SYO_INC(STATUS_OK);return 0;}
   if(vrc<0){SYO_INC(STATUS_REFUSED);return -1;}
  }else if(!f||!f->depth||f->probing_original)SYO_INC(STATUS_NO_SCOPE);
  else if(f->expected_link_mgr!=link_mgr)SYO_INC(STATUS_WRONG_MANAGER);
  else if(f->source_id!=(uint32_t)*id_ptr)SYO_INC(STATUS_WRONG_SOURCE);
 }
 SYO_INC(STATUS_FALLBACK);
 return orig_get_player_info?orig_get_player_info(link_mgr,id_ptr,out):-1;
}

static int hook_get_beat_info(void *link_mgr,const int *id_ptr,const uint8_t *lane_ptr,void *out){
 SYO_INC(BEAT_READ);
 if(atomic_load(&rt_installed)&&id_ptr&&(*id_ptr==3||*id_ptr==4)&&out){
  struct xz_va_frame *f=xz_va_registry_lookup(&rt_registry,current_thread_key());
  if(f&&f->depth>0&&!f->probing_original&&f->expected_link_mgr==link_mgr&&f->source_id==(uint32_t)*id_ptr){
   int vrc=f->mode==1&&!runtime_frame_fresh(f)?-1:xz_va_get(&rt_va,f,1,out);
   SYO_INC(BEAT_SCOPE);
   if(vrc==1){SYO_INC(BEAT_OK);return 0;}
   if(vrc<0){SYO_INC(BEAT_REFUSED);return -1;}
  }else if(!f||!f->depth||f->probing_original)SYO_INC(BEAT_NO_SCOPE);
  else if(f->expected_link_mgr!=link_mgr)SYO_INC(BEAT_WRONG_MANAGER);
  else if(f->source_id!=(uint32_t)*id_ptr)SYO_INC(BEAT_WRONG_SOURCE);
 }
 SYO_INC(BEAT_FALLBACK);
 return orig_get_beat_info?orig_get_beat_info(link_mgr,id_ptr,lane_ptr,out):-1;
}

static int hook_set_player_info(void *link_mgr,const int *id_ptr,const void *a2,const void *a3,const void *a4,
                                const void *a5,const void *a6,const void *a7,const void *a8,const void *a9,
                                const void *a10,const void *a11,const void *a12,const void *a13){
 if(atomic_load(&rt_installed)&&id_ptr&&(*id_ptr==3||*id_ptr==4)){
  SYO_INC(FOREIGN_STATUS);
  xz_va_foreign(&rt_va,(uint32_t)*id_ptr);
  atomic_store(&rt_slot_absent[*id_ptr-3],0);
  atomic_store(&rt_committed_deck,-1);
 }
 return orig_set_player_info?orig_set_player_info(link_mgr,id_ptr,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13):0;
}

static int hook_set_beat_info(void *link_mgr,const int *id_ptr,const void *lane_ptr,const void *packet){
 if(atomic_load(&rt_installed)&&id_ptr&&(*id_ptr==3||*id_ptr==4)){
  SYO_INC(FOREIGN_BEAT);
  xz_va_foreign(&rt_va,(uint32_t)*id_ptr);
  atomic_store(&rt_slot_absent[*id_ptr-3],0);
  atomic_store(&rt_committed_deck,-1);
 }
 return orig_set_beat_info?orig_set_beat_info(link_mgr,id_ptr,lane_ptr,packet):0;
}

static int hook_set_master_on(void *sync_master,const int *master_id){
 unsigned syo_class=0;
 if(atomic_load(&rt_installed)&&sync_master){
  struct xz_va_frame *f=xz_va_registry_lookup(&rt_registry,current_thread_key());
  int internal=(f&&f->depth>0&&f->draining_sync_master==sync_master);
  if(internal&&f->draining_request&&!runtime_frame_request_current(f))return -1;
  int dummy=((const uint8_t *)sync_master)[0x15]!=0;
  syo_class=internal?1:dummy?2:3;
  if(internal)SYO_INC(OPERATOR_INTERNAL);else if(dummy)SYO_INC(OPERATOR_DUMMY);else SYO_INC(OPERATOR_EXTERNAL);
  if(!internal&&!dummy){
   xz_va_operator(&rt_va);
   atomic_store(&rt_restore_pending,0);
   atomic_store(&rt_committed_deck,-1);
   notify_use_native_master();
  }
 }
 int rc=orig_set_master_on?orig_set_master_on(sync_master,master_id):0;
 SYO_REC(OPERATOR,3,master_id?*master_id:UINT32_MAX,syo_class,rc);
 (void)syo_class;return rc;
}

static void apply_cleanup(const struct xz_sync_link_cleanup *clean,uint32_t leased_id,const struct xz_sync_link_observation *obs){
 if(!clean)return;
 if(clean->inhibit_phase){
  atomic_store(&rt_va.valid_generation,0);
  atomic_store(&rt_va.pending,0);
  atomic_store(&rt_va.active,0);
 }
 if(clean->restore_master&&obs){
  atomic_store(&rt_restore_input_id,clean->restore_input_id);
  atomic_store(&rt_restore_expected_master,(int32_t)leased_id-1);
  atomic_store(&rt_restore_operator_epoch,obs->master_choice_epoch);
  atomic_store(&rt_restore_foreign_epoch,obs->external_writer_epoch);
  atomic_store(&rt_restore_source_id,leased_id);
  atomic_store(&rt_restore_pending,1);
 }
 atomic_store(&rt_committed_deck,-1);
}

static struct xz_sync_link_observation runtime_observation(uint32_t leased_id){
 return (struct xz_sync_link_observation){
  .external_writer_epoch=xz_va_probe_epoch(&rt_va,leased_id),
  .master_choice_epoch=atomic_load(&rt_va.operator_epoch),
  .foreign_present=!atomic_load(&rt_slot_absent[leased_id-3])||xz_va_probe_epoch(&rt_va,leased_id)!=rt_lease.claim.external_writer_epoch,
  .payload_still_ours=atomic_load(&rt_va.active)&&atomic_load(&rt_va.source_id)==leased_id,
  .current_master_id=atomic_load(&rt_observed_master_id)
 };
}
static void release_runtime_lease(void){
 if(!rt_lease.active)return;
 uint32_t id=rt_lease.claim.source_id;
 struct xz_sync_link_observation obs=runtime_observation(id);
 struct xz_sync_link_cleanup clean={0};
 xz_sync_link_lease_release(&rt_lease,&obs,&clean);
 apply_cleanup(&clean,id,&obs);
}

#if XZ_SYNC_OBSERVATION
static void syo_link(unsigned outcome,int selected,int clock_result,int anchor_ok,const struct xz_sync_link_pcm_anchor *a,const struct xz_sync_link_clock_pair *pair,const struct xz_extra_decks *decks){
 uint64_t gap=anchor_ok?decks->deck[selected].timeline.observed_wire_frame-decks->deck[selected].timeline.sink_frame:0;
 SYO_REC(LINK,12,outcome,selected,clock_result,anchor_ok,rt_lease.active,rt_lease.master_committed,
  rt_lease.active?rt_lease.claim.source_id:0,atomic_load(&rt_observed_ready),atomic_load(&rt_observed_master_id),
  anchor_ok?a->route_epoch:0,anchor_ok?a->source_revision:0,anchor_ok?a->playback_epoch:0,
  anchor_ok?SYO_LO(a->captured_mono_us):0,anchor_ok?SYO_HI(a->captured_mono_us):0,
  pair?SYO_LO(pair->mono_us):0,pair?SYO_HI(pair->mono_us):0,SYO_LO(gap),SYO_HI(gap));
}
#define SYO_LINK(outcome) syo_link(outcome,selected,clock_rc,anchor_ok,&anchor,clock_pair,decks)
#else
#define SYO_LINK(...) ((void)0)
#endif
static void runtime_step_locked(const struct xz_extra_decks *decks,const struct xz_sync_link_clock_pair *clock_pair){
 SYO_INC(STEP);
 if(!atomic_load(&rt_installed)){SYO_INC(STEP_DISABLED);return;}
 if(rt_clock.stepped&&!rt_lease.active)memset(&rt_clock,0,sizeof rt_clock);
 int clock_rc=xz_sync_link_clock_update(&rt_clock,clock_pair);
 int clock_ok=clock_rc==XZ_SYNC_LINK_OK;
 int selected=-1;
 if(decks&&decks->route_up){
  if(decks->master_intent.epoch){
   int i=decks->master_intent.deck-XZ_EXTRA_FIRST_DECK;
   if(i>=0&&i<XZ_EXTRA_DECKS&&decks->deck[i].loaded&&
      decks->master_intent.operator_epoch==atomic_load(&rt_request_operator)&&
      atomic_load(&rt_request_operator)==atomic_load(&rt_va.operator_epoch))selected=i;
  }else if(atomic_load(&rt_legacy_mode)&&!atomic_load(&rt_global_mode)){
   /* Compatibility for isolated pre-observer fixtures only. Epoch zero never
      produces an owned global observation. */
   for(int i=0;i<XZ_EXTRA_DECKS;i++)if(decks->deck[i].loaded&&decks->deck[i].master){selected=i;break;}
  }
 }
 struct xz_sync_link_pcm_anchor anchor;
 int anchor_ok=clock_ok&&selected>=0&&
  xz_sync_link_anchor_from_deck(selected,&decks->deck[selected],clock_pair->mono_us,&anchor);
 if(selected<0)SYO_INC(NO_MASTER);else if(!clock_ok)SYO_INC(CLOCK_BAD);else if(!anchor_ok)SYO_INC(ANCHOR_BAD);
 unsigned syo_outcome=selected<0?1:!clock_ok?2:!anchor_ok?3:0;
 if(rt_lease.active){
  uint32_t leased_id=rt_lease.claim.source_id;
  struct xz_sync_link_observation obs=runtime_observation(leased_id);
  struct xz_sync_link_cleanup clean={0};
  SYO_REC(LEASE,10,leased_id,rt_lease.active,rt_lease.master_committed,obs.current_master_id,rt_lease.claim.master_choice_epoch,obs.master_choice_epoch,rt_lease.claim.external_writer_epoch,obs.external_writer_epoch,obs.foreign_present,obs.payload_still_ours);
  int keep=anchor_ok&&va_live(&rt_va)&&(uint32_t)(selected+3)==leased_id&&
   decks->deck[selected].timeline.route_epoch==rt_lease.claim.route_epoch&&
   decks->deck[selected].timeline.load_revision==rt_lease.claim.source_revision&&
   decks->deck[selected].timeline.transport_generation==rt_lease.claim.playback_epoch;
  if(!keep){
   SYO_INC(LEASE_RELEASE);
   xz_sync_link_lease_release(&rt_lease,&obs,&clean);
   apply_cleanup(&clean,leased_id,&obs);
   SYO_LINK(4);
   return;
  }else {
   if(obs.current_master_id==(int32_t)leased_id-1&&
      atomic_load(&rt_ack_generation)!=atomic_load(&rt_va.lease_generation))
    obs.current_master_id=rt_lease.claim.previous_master_id;
   if(xz_sync_link_lease_observe(&rt_lease,&obs,&clean)==XZ_SYNC_LINK_LEASE_LOST){
   SYO_INC(LEASE_LOST);SYO_LINK(5);
   apply_cleanup(&clean,leased_id,&obs);
   if(obs.master_choice_epoch!=rt_lease.claim.master_choice_epoch||
      (obs.current_master_id>=0&&obs.current_master_id<2)){
    notify_use_native_master();
   }
   return;
   }
  }
 }
 if(anchor_ok){
   uint32_t src_id=(uint32_t)(selected+3);
   if(!rt_lease.active&&atomic_load(&rt_observed_ready)&&atomic_load(&rt_slot_absent[selected])){
    uint32_t probed_f=atomic_load(&rt_slot_probed_epoch[selected]);
    uint32_t op_epoch=atomic_load(&rt_va.operator_epoch);
    struct rt_master_sample observed;
    int observed_ok=read_master_sample(&observed);
    int32_t prev_master=observed_ok?observed.native_id:-2;
    int request_current=!atomic_load(&rt_request_epoch)||
     (op_epoch==atomic_load(&rt_request_operator)&&op_epoch==decks->master_intent.operator_epoch);
    if(request_current&&prev_master>=-1&&prev_master<=5&&prev_master!=(int32_t)src_id-1){
     struct xz_sync_link_claim claim={
      .source_id=src_id,.owner=1,.route_epoch=anchor.route_epoch,
      .source_revision=anchor.source_revision,.playback_epoch=anchor.playback_epoch,
      .external_writer_epoch=probed_f,.master_choice_epoch=op_epoch,
      .music_id_known=1,.foreign_present=0,.previous_master_id=prev_master
     };
     memcpy(claim.music_id,anchor.music_id,8);
     if(xz_sync_link_lease_claim(&rt_lease,&claim)==XZ_SYNC_LINK_OK){
      SYO_INC(CLAIM_OK);
      if(!xz_va_claim(&rt_va,src_id,1,probed_f)||atomic_load(&rt_va.claim_operator)!=op_epoch||
         atomic_load(&rt_va.operator_epoch)!=op_epoch){
       SYO_INC(VA_CLAIM_FAIL);syo_outcome=8;
       atomic_store(&rt_va.valid_generation,0);atomic_store(&rt_va.pending,0);atomic_store(&rt_va.active,0);
       memset(&rt_lease,0,sizeof rt_lease);
      }
     }else{SYO_INC(CLAIM_FAIL);syo_outcome=7;}
    }else{SYO_INC(PREVIOUS_MASTER_INVALID);syo_outcome=9;}
   }else if(!rt_lease.active){
    if(!atomic_load(&rt_observed_ready)){SYO_INC(NOT_READY);syo_outcome=10;}
    else{SYO_INC(OCCUPIED);syo_outcome=11;}
   }
   if(rt_lease.active){
    uint32_t ticket=xz_va_ticket(&rt_va);
    struct xz_sync_link_plan plan;
    if(ticket&&xz_sync_link_build(&rt_lease,&rt_clock,&anchor,clock_pair->mono_us,&plan)){
     struct xz_va_bundle bundle;
     xz_sync_link_bundle_from_plan(&plan,&bundle);
     bundle.captured_mono_us=anchor.captured_mono_us;
     bundle.request_epoch=atomic_load(&rt_request_epoch);
     bundle.route_epoch=(uint32_t)anchor.route_epoch;bundle.load_revision=(uint32_t)anchor.source_revision;
     bundle.transport_generation=(uint32_t)anchor.playback_epoch;
     if(xz_va_offer(&rt_va,&bundle,ticket)){
      SYO_INC(OFFER_OK);syo_outcome=0;
      if(plan.request_master){SYO_INC(MASTER_PENDING);xz_va_explicit_request(&rt_va);}
     }else{SYO_INC(OFFER_FAIL);syo_outcome=12;release_runtime_lease();}
    }else{if(!ticket)SYO_INC(NO_TICKET);else{SYO_INC(BUILD_FAIL);SYO_REASON(PLAN_REASON_0,plan.reason,12);}syo_outcome=ticket?13:14;release_runtime_lease();}
   }
 }
 SYO_LINK(syo_outcome);(void)syo_outcome;
 atomic_store(&rt_committed_deck,rt_lease.active&&rt_lease.master_committed?(int)rt_lease.claim.source_id-3:-1);
}

void xz_sync_link_runtime_step(const struct xz_extra_decks *decks,const struct xz_sync_link_clock_pair *clock_pair){
 if(!atomic_load(&rt_installed))return;
 pthread_mutex_lock(&rt_pub_mutex);
 if(decks&&(decks->master_intent.epoch||decks->master_intent.explicit_choice||decks->master_intent.operator_epoch))
  atomic_store(&rt_global_mode,1);
 uint32_t old_request=atomic_load(&rt_request_epoch);
 uint32_t old_operator=atomic_load(&rt_request_operator);
 uint32_t request=decks?decks->master_intent.epoch:0;
 uint32_t operator_epoch=decks?decks->master_intent.operator_epoch:0;
 /* Players can be recreated while the bridge retains its lifetime counters.
    A newer operator admits their first request even if its number is reused. */
 if(request&&old_request&&((operator_epoch&&old_operator&&operator_epoch<old_operator)||
    ((!operator_epoch||operator_epoch==old_operator)&&request<old_request))){pthread_mutex_unlock(&rt_pub_mutex);return;}
 if(!request&&atomic_load(&rt_global_mode)){request=old_request;operator_epoch=old_operator;}
 int changed=old_request!=request||old_operator!=operator_epoch;
 if(changed)atomic_store(&rt_request_operator,operator_epoch);
 atomic_store(&rt_request_epoch,request);
 if(changed&&rt_lease.active){
  /* A same-source explicit reselection retains the guarded prior-master restore,
     but gets a new lease generation and must drain a fresh request. */
  unsigned side=rt_lease.claim.source_id-3;
  if(decks&&decks->route_up&&decks->master_intent.deck==(int32_t)side+2&&
     decks->deck[side].timeline.route_epoch==rt_lease.claim.route_epoch&&
     decks->deck[side].timeline.load_revision==rt_lease.claim.source_revision&&
     decks->deck[side].timeline.transport_generation==rt_lease.claim.playback_epoch&&va_live(&rt_va)){
   atomic_store(&rt_va.active,0);atomic_store(&rt_va.pending,0);
   rt_lease.master_committed=0;atomic_store(&rt_committed_deck,-1);
   if(!xz_va_claim(&rt_va,side+3,atomic_load(&rt_slot_absent[side]),
                   (uint32_t)rt_lease.claim.external_writer_epoch))release_runtime_lease();
  }else release_runtime_lease();
 }
 runtime_step_locked(decks,clock_pair);
 uint32_t op=atomic_load(&rt_va.operator_epoch);
 int notify=atomic_exchange(&rt_notify_pending,0)&&old_request==request;
 pthread_mutex_unlock(&rt_pub_mutex);
 if(notify)deliver_native_master(op,request);
}

void xz_sync_link_runtime_poll(const struct xz_extra_decks *decks){
 if(!atomic_load(&rt_installed)||!decks)return;
#ifdef __linux__
 struct timespec m0,rt,m1;
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&m0)||
    xz_clock_gettime_compat(CLOCK_REALTIME,&rt)||
    xz_clock_gettime_compat(CLOCK_MONOTONIC,&m1)){
  xz_sync_link_runtime_step(decks,NULL);return;
 }
 uint64_t u0=(uint64_t)m0.tv_sec*1000000ULL+(uint64_t)(m0.tv_nsec/1000L);
 uint64_t u1=(uint64_t)m1.tv_sec*1000000ULL+(uint64_t)(m1.tv_nsec/1000L);
 if(u1<u0||rt.tv_sec<=0||u1-u0>2ULL*XZ_SYNC_LINK_MAX_UNCERTAINTY_US){
  xz_sync_link_runtime_step(decks,NULL);return;
 }
 struct xz_sync_link_clock_pair pair={
  .mono_us=u0+(u1-u0)/2ULL,
  .realtime_us=(uint64_t)rt.tv_sec*1000000ULL+(uint64_t)(rt.tv_nsec/1000L),
  .uncertainty_us=(uint32_t)((u1-u0)/2ULL+1ULL)
 };
 xz_sync_link_runtime_step(decks,&pair);
#else
 (void)decks;
#endif
}

int xz_sync_link_runtime_master_committed(int deck_index){
 if(atomic_load(&rt_global_mode)){
  struct xz_master_observation actual;
  return deck_index>=0&&deck_index<XZ_EXTRA_DECKS&&
   xz_sync_link_runtime_master_observe(&actual)&&actual.deck==deck_index+XZ_EXTRA_FIRST_DECK;
 }
 if(deck_index<0||deck_index>=XZ_EXTRA_DECKS||atomic_load(&rt_committed_deck)!=deck_index)return 0;
 struct xz_va_frame frame={0};
 xz_va_begin(&rt_va,&frame);
 int fresh=runtime_frame_fresh(&frame);
 xz_va_end(&rt_va,&frame);
 return fresh&&atomic_load(&rt_committed_deck)==deck_index;
}

static void reset_runtime_state(void){
 SYO_INC(RESETS);
 pthread_mutex_lock(&rt_pub_mutex);
 memset(&rt_lease,0,sizeof rt_lease);
 memset(&rt_clock,0,sizeof rt_clock);
 atomic_store(&rt_slot_absent[0],0);atomic_store(&rt_slot_absent[1],0);
 atomic_store(&rt_slot_probed_epoch[0],0);atomic_store(&rt_slot_probed_epoch[1],0);
 atomic_store(&rt_observed_master_id,-2);
 atomic_store(&rt_observed_ready,0);
 atomic_store(&rt_committed_deck,-1);
 /* Keep pins, cache identities, operator/lease counters and exhausted state
    across stop/start. Surviving requests must never see a reused token. */
 if(!atomic_exchange(&rt_initialized,1)){
 xz_va_init(&rt_va);
 xz_va_registry_init(&rt_registry);
 atomic_init(&rt_request_epoch,0);atomic_init(&rt_request_operator,1);
 atomic_init(&rt_ack_generation,0);atomic_init(&rt_notify_pending,0);atomic_init(&rt_invalid_sequence,0);
 atomic_init(&rt_consumer_sequence,0);atomic_init(&rt_actual_guard,0);
 atomic_init(&rt_actual_serial,0);atomic_init(&rt_actual_exhausted,0);
 memset(&rt_last_sample,0,sizeof rt_last_sample);
 for(unsigned i=0;i<16;i++)atomic_init(&rt_actual_words[i],0);
 }
 atomic_store(&rt_notify_pending,0);
 atomic_store(&rt_restore_pending,0);
 atomic_store(&rt_restore_input_id,0);
 atomic_store(&rt_restore_expected_master,-2);
 atomic_store(&rt_restore_operator_epoch,0);
 atomic_store(&rt_restore_foreign_epoch,0);
 atomic_store(&rt_restore_source_id,0);
 pthread_mutex_unlock(&rt_pub_mutex);
}

int xz_sync_link_runtime_start(void){
 SYO_INC(INSTALL_ATTEMPT);
 if(atomic_load(&rt_installed)){SYO_INC(INSTALL_ALREADY);return 0;}
 atomic_store(&rt_global_mode,1);atomic_store(&rt_legacy_mode,0);
 reset_runtime_state();
 static const unsigned char g_sync_mgr[8]={0xf8,0x40,0x2d,0xe9,0x14,0x50,0x80,0xe2};
 static const unsigned char g_get_player[8]={0x00,0x30,0x91,0xe5,0x01,0x10,0x43,0xe2};
 static const unsigned char g_get_beat[8]={0x00,0xc0,0x91,0xe5,0x04,0x40,0x2d,0xe5};
 static const unsigned char g_set_player[8]={0x00,0xc0,0x91,0xe5,0x10,0x40,0x2d,0xe9};
 static const unsigned char g_set_beat[8]={0x00,0x00,0x53,0xe3,0x30,0x00,0x2d,0xe9};
 static const unsigned char g_set_master[8]={0x00,0x20,0x91,0xe5,0x08,0x40,0x2d,0xe9};
 SYO_REC(INSTALL,4,1,0,0,0);
 int install_rc_1=xz_hook_arm(0x59f70,g_sync_mgr,(void *)(uintptr_t)hook_sync_mgr_sync,(void **)&orig_sync_mgr_sync);
 if(install_rc_1){SYO_INC(INSTALL_FAILED);SYO_REC(INSTALL,4,1,install_rc_1,0,0);return -1;}
 SYO_REC(INSTALL,4,2,0,0,1);
 int install_rc_2=xz_hook_arm(0x532c0,g_get_player,(void *)(uintptr_t)hook_get_player_info,(void **)&orig_get_player_info);
 if(install_rc_2){SYO_INC(INSTALL_FAILED);SYO_REC(INSTALL,4,2,install_rc_2,0,1);return -1;}
 SYO_REC(INSTALL,4,3,0,0,3);
 int install_rc_3=xz_hook_arm(0x53140,g_get_beat,(void *)(uintptr_t)hook_get_beat_info,(void **)&orig_get_beat_info);
 if(install_rc_3){SYO_INC(INSTALL_FAILED);SYO_REC(INSTALL,4,3,install_rc_3,0,3);return -1;}
 SYO_REC(INSTALL,4,4,0,0,7);
 int install_rc_4=xz_hook_arm(0x531d8,g_set_player,(void *)(uintptr_t)hook_set_player_info,(void **)&orig_set_player_info);
 if(install_rc_4){SYO_INC(INSTALL_FAILED);SYO_REC(INSTALL,4,4,install_rc_4,0,7);return -1;}
 SYO_REC(INSTALL,4,5,0,0,15);
 int install_rc_5=xz_hook_arm(0x53090,g_set_beat,(void *)(uintptr_t)hook_set_beat_info,(void **)&orig_set_beat_info);
 if(install_rc_5){SYO_INC(INSTALL_FAILED);SYO_REC(INSTALL,4,5,install_rc_5,0,15);return -1;}
 SYO_REC(INSTALL,4,6,0,0,31);
 int install_rc_6=xz_hook_arm(0x597c8,g_set_master,(void *)(uintptr_t)hook_set_master_on,(void **)&orig_set_master_on);
 if(install_rc_6){SYO_INC(INSTALL_FAILED);SYO_REC(INSTALL,4,6,install_rc_6,0,31);return -1;}
 atomic_store(&rt_installed,1);
 SYO_INC(INSTALL_OK);SYO_REC(INSTALL,4,7,0,1,63);
 return 0;
}

void xz_sync_link_runtime_stop(void){
 SYO_INC(STOPS);
 if(atomic_exchange(&rt_installed,0))xz_va_operator(&rt_va);
 atomic_store(&rt_va.valid_generation,0);
 atomic_store(&rt_committed_deck,-1);
 SYO_REC(INSTALL,4,8,0,0,0);
}

void xz_sync_link_runtime_test_install(const struct xz_sync_link_test_ops *originals,struct xz_sync_link_test_ops *hooks_out){
 SYO_INC(TEST_INSTALL);
 xz_sync_link_runtime_stop();
 atomic_store(&rt_legacy_mode,originals&&originals->legacy_fixture&&!atomic_load(&rt_global_mode));
 reset_runtime_state();
 orig_sync_mgr_sync=originals?originals->sync_mgr_sync:NULL;
 orig_get_player_info=originals?originals->get_player_info:NULL;
 orig_get_beat_info=originals?originals->get_beat_info:NULL;
 orig_set_player_info=originals?originals->set_player_info:NULL;
 orig_set_beat_info=originals?originals->set_beat_info:NULL;
 orig_set_master_on=originals?originals->set_master_on:NULL;
 test_set_link_master=originals?originals->set_link_master:NULL;
 test_on_use_native_master=originals?originals->on_use_native_master:NULL;
 test_monotonic_us=originals?originals->monotonic_us:NULL;
 atomic_store(&rt_installed,1);
 if(hooks_out){
  hooks_out->sync_mgr_sync=hook_sync_mgr_sync;
  hooks_out->get_player_info=hook_get_player_info;
  hooks_out->get_beat_info=hook_get_beat_info;
  hooks_out->set_player_info=hook_set_player_info;
  hooks_out->set_beat_info=hook_set_beat_info;
  hooks_out->set_master_on=hook_set_master_on;
  hooks_out->set_link_master=call_set_link_master;
  hooks_out->on_use_native_master=notify_use_native_master;
  hooks_out->monotonic_us=runtime_monotonic_us;
 }
}
