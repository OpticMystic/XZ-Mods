/* SPDX-License-Identifier: MPL-2.0 */
#ifndef XZ_DISPLAY_PIPELINE_CORE_H
#define XZ_DISPLAY_PIPELINE_CORE_H
#include <stdint.h>
#include <string.h>
enum { XZ_DP_FREE, XZ_DP_DRAWING, XZ_DP_READY, XZ_DP_COPYING };
struct xz_dp_request { uint32_t epoch,tick,accepted; uint64_t target_wall; };
struct xz_dp_meta {
 uint32_t epoch,token,target_tick,ready_tick;
 uint64_t target_wall,begin_mono,finished_mono,cpu_begin,cpu_end,packed_mono;
 unsigned cpu_valid;
};
struct xz_dp_slot { uint32_t state; struct xz_dp_meta meta; unsigned char *pixels; };
struct xz_dp_core { uint32_t epoch,active; struct xz_dp_slot slot[2]; uint32_t next_token,waiting_token,waiting_epoch; };
/* Single consumer writer, single producer reader. Atomic payload words avoid
   a C data race even when the bounded reader loses a seqlock attempt. */
struct xz_dp_mailbox { uint32_t guard,word[5]; };
static inline void xz_dp_mail(struct xz_dp_mailbox *m,const struct xz_dp_request *r){
 uint32_t g=__atomic_load_n(&m->guard,__ATOMIC_RELAXED);
 __atomic_store_n(&m->guard,g+1u,__ATOMIC_SEQ_CST);
 uint32_t w[5]={r->epoch,r->tick,r->accepted,(uint32_t)r->target_wall,(uint32_t)(r->target_wall>>32)};
 for(unsigned i=0;i<5;i++)__atomic_store_n(&m->word[i],w[i],__ATOMIC_RELAXED);
 __atomic_store_n(&m->guard,g+2u,__ATOMIC_RELEASE);
}
static inline int xz_dp_receive(struct xz_dp_mailbox *m,struct xz_dp_request *r){
 for(unsigned attempt=0;attempt<2;attempt++){
  uint32_t a=__atomic_load_n(&m->guard,__ATOMIC_ACQUIRE),w[5];if(a&1u)continue;
  for(unsigned i=0;i<5;i++)w[i]=__atomic_load_n(&m->word[i],__ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  if(a!=__atomic_load_n(&m->guard,__ATOMIC_ACQUIRE))continue;
  *r=(struct xz_dp_request){w[0],w[1],w[2],(uint64_t)w[3]|(uint64_t)w[4]<<32};return 1;
 }return 0;
}
static inline uint32_t xz_dp_load(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
/* Consumer owns epoch/active. A DRAWING slot remains producer-owned until its
   final publish check; a COPYING slot remains consumer-owned until release. */
static inline uint32_t xz_dp_epoch(struct xz_dp_core *p,int active){
 __atomic_store_n(&p->active,0u,__ATOMIC_RELEASE);
 uint32_t epoch=__atomic_add_fetch(&p->epoch,1u,__ATOMIC_ACQ_REL);if(!epoch)epoch=__atomic_add_fetch(&p->epoch,1u,__ATOMIC_ACQ_REL);
 for(unsigned i=0;i<2;i++){uint32_t ready=XZ_DP_READY;__atomic_compare_exchange_n(&p->slot[i].state,&ready,XZ_DP_FREE,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
 __atomic_store_n(&p->active,active!=0,__ATOMIC_RELEASE);return epoch;
}
static inline int xz_dp_begin(struct xz_dp_core *p,const struct xz_dp_request *r){
 if(!xz_dp_load(&p->active)||r->epoch!=xz_dp_load(&p->epoch))return -1;
 if(p->waiting_epoch==r->epoch&&p->waiting_token&&r->accepted!=p->waiting_token)return -1;
 for(unsigned i=0;i<2;i++)if(xz_dp_load(&p->slot[i].state)==XZ_DP_READY)return -1;
 for(unsigned i=0;i<2;i++){
  uint32_t free_state=XZ_DP_FREE;
  if(!__atomic_compare_exchange_n(&p->slot[i].state,&free_state,XZ_DP_DRAWING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
  memset(&p->slot[i].meta,0,sizeof p->slot[i].meta);if(!++p->next_token)++p->next_token;
  p->slot[i].meta.epoch=r->epoch;p->slot[i].meta.token=p->next_token;p->slot[i].meta.target_tick=r->tick+1;p->slot[i].meta.target_wall=r->target_wall;return (int)i;
 }
 return -1;
}
static inline int xz_dp_publish(struct xz_dp_core *p,unsigned i,int usable){
 struct xz_dp_slot *s=&p->slot[i];
 if(!usable||!xz_dp_load(&p->active)||s->meta.epoch!=xz_dp_load(&p->epoch)){__atomic_store_n(&s->state,XZ_DP_FREE,__ATOMIC_RELEASE);return 0;}
 p->waiting_token=s->meta.token;p->waiting_epoch=s->meta.epoch;
 __atomic_store_n(&s->state,XZ_DP_READY,__ATOMIC_RELEASE);
 if(!xz_dp_load(&p->active)||s->meta.epoch!=xz_dp_load(&p->epoch)){
  uint32_t ready=XZ_DP_READY;__atomic_compare_exchange_n(&s->state,&ready,XZ_DP_FREE,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);return 0;
 }
 return 1;
}
/* One bounded scan; never wait for the producer and never take a future frame. */
enum { XZ_DP_UNREADY, XZ_DP_INACTIVE, XZ_DP_STALE, XZ_DP_FUTURE };
static inline int xz_dp_claim_reason(struct xz_dp_core *p,uint32_t tick,unsigned *reason){
 *reason=XZ_DP_UNREADY;
 if(!xz_dp_load(&p->active)){*reason=XZ_DP_INACTIVE;return -1;}
 for(unsigned i=0;i<2;i++){
  uint32_t ready=XZ_DP_READY;
  if(!__atomic_compare_exchange_n(&p->slot[i].state,&ready,XZ_DP_COPYING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
  if(p->slot[i].meta.epoch!=xz_dp_load(&p->epoch)||!xz_dp_load(&p->active)){*reason=XZ_DP_STALE;__atomic_store_n(&p->slot[i].state,XZ_DP_FREE,__ATOMIC_RELEASE);continue;}
  if((int32_t)(tick-p->slot[i].meta.target_tick)<0){*reason=XZ_DP_FUTURE;__atomic_store_n(&p->slot[i].state,XZ_DP_READY,__ATOMIC_RELEASE);continue;}
  return (int)i;
 }
 return -1;
}
static inline int xz_dp_claim(struct xz_dp_core *p,uint32_t tick){unsigned reason;return xz_dp_claim_reason(p,tick,&reason);}
static inline void xz_dp_release(struct xz_dp_core *p,unsigned i){__atomic_store_n(&p->slot[i].state,XZ_DP_FREE,__ATOMIC_RELEASE);}
static inline int xz_dp_drawing(const struct xz_dp_core *p){return xz_dp_load(&p->slot[0].state)==XZ_DP_DRAWING||xz_dp_load(&p->slot[1].state)==XZ_DP_DRAWING;}
#endif
