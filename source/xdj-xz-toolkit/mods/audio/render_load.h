/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef XZ_RENDER_LOAD_H
#define XZ_RENDER_LOAD_H
#include <stdint.h>
#include <string.h>

#define XZ_RENDER_LOAD_WINDOW 256u
#define XZ_RENDER_BLOCK_US 5805u
#define XZ_RENDER_SAFE_US 3483u /* 60% of a 256-frame period for both software workers. */

struct xz_worker_load {
 uint32_t samples, p99_us, max_us, key_max_us, sampled_ms, key_active;
};
struct xz_render_load {
 uint32_t total[XZ_RENDER_LOAD_WINDOW], key[XZ_RENDER_LOAD_WINDOW];
 unsigned next, count;
 struct xz_worker_load latest;
};
static inline uint32_t xz_render_us(uint64_t begin,uint64_t end) {
 if(!begin||end<begin)return 0;
 uint64_t n=end-begin;
 return n>=UINT32_MAX?UINT32_MAX:(uint32_t)n+1u;
}
static inline void xz_render_load_record(struct xz_render_load *s,uint32_t total,
 uint32_t key,uint32_t frames,uint32_t now) {
 if(!frames||frames>256u)return;
 uint64_t cost=((uint64_t)total*256u+frames-1u)/frames;
 s->total[s->next]=cost>UINT32_MAX?UINT32_MAX:(uint32_t)cost;
 cost=((uint64_t)key*256u+frames-1u)/frames;
 s->key[s->next]=cost>UINT32_MAX?UINT32_MAX:(uint32_t)cost;
 s->next=(s->next+1u)%XZ_RENDER_LOAD_WINDOW;
 if(s->count<XZ_RENDER_LOAD_WINDOW)++s->count;
 uint32_t top[3]={0},key_max=0;
 for(unsigned i=0;i<s->count;i++) {
  uint32_t x=s->total[i];
  if(x>=top[0]){top[2]=top[1];top[1]=top[0];top[0]=x;}
  else if(x>=top[1]){top[2]=top[1];top[1]=x;}
  else if(x>top[2])top[2]=x;
  if(s->key[i]>key_max)key_max=s->key[i];
 }
 unsigned rank=s->count-(s->count*99u+99u)/100u;
 s->latest=(struct xz_worker_load){s->count,top[rank],top[0],key_max,now,0};
}
/* A reserved but not yet rendered shift still consumes its measured allowance.
 * Old samples are retained while paused; load/route changes clear their owner. */
static inline uint64_t xz_render_projected(const struct xz_worker_load load[2],
 const uint32_t reserved[2],unsigned active,unsigned candidate,uint32_t probe) {
 uint64_t sum=0;
 for(unsigned i=0;i<2;i++) {
  uint32_t extra=active&(1u<<i)?reserved[i]:0;
  if(i==candidate&&probe>extra)extra=probe;
  sum+=load[i].max_us;
  if(extra>load[i].key_max_us)sum+=extra-load[i].key_max_us;
 }
 return sum;
}
#endif
