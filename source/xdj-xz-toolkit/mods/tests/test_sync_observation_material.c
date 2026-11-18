#include "sync_observation.h"
#include "sync_link_runtime.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
int xz_hook_arm(uint32_t a,const unsigned char b[8],void *c,void **d){(void)a;(void)b;(void)c;(void)d;return -1;}
int xz_extra_decks_use_native_master(void){return 0;}
static _Atomic unsigned finished;
static void *writer(void *arg){
 uint32_t base=(uint32_t)(uintptr_t)arg;
 for(uint32_t i=1;i<=100000;i++){
  uint32_t v[]={base+i,(base+i)^0x7ecab129u,i};
  xz_syo_record(SYO_R_LINK,1,v,3,0);
 }
 atomic_fetch_add(&finished,1);return NULL;
}
int main(void){
 uint32_t v[]={19,19^0x7ecab129u,1};xz_syo_record(SYO_R_LINK,1,v,3,0);
 uint32_t seq=atomic_load(&xz_mods_sync_observation_v1.records[SYO_R_LINK].seq);
 for(unsigned i=0;i<100000;i++){v[2]++;xz_syo_record(SYO_R_LINK,1,v,3,0);}
 assert(atomic_load(&xz_mods_sync_observation_v1.records[SYO_R_LINK].seq)==seq);
 pthread_t a,b;assert(!pthread_create(&a,NULL,writer,(void *)(uintptr_t)1000000));assert(!pthread_create(&b,NULL,writer,(void *)(uintptr_t)3000000));
 unsigned accepted=0,rejected=0;
 do{
  struct xz_syo_record *r=&xz_mods_sync_observation_v1.records[SYO_R_LINK];
  uint32_t s0=atomic_load_explicit(&r->seq,memory_order_acquire),x[28],y[28];
  for(unsigned i=0;i<28;i++)x[i]=atomic_load_explicit(&r->words[i],memory_order_relaxed);
  uint32_t s1=atomic_load_explicit(&r->seq,memory_order_acquire);
  for(unsigned i=0;i<28;i++)y[i]=atomic_load_explicit(&r->words[i],memory_order_relaxed);
  uint32_t s2=atomic_load_explicit(&r->seq,memory_order_acquire);
  int same=s0&&!(s0&1u)&&s0==s1&&s0==s2;
  for(unsigned i=0;i<28;i++)same= same&&x[i]==y[i];
  if(same){assert(y[2]==(y[1]^0x7ecab129u));accepted++;}else rejected++;
 }while(atomic_load(&finished)<2);
 assert(!pthread_join(a,NULL)&&!pthread_join(b,NULL));assert(accepted+rejected>0);
 printf("PASS bounded concurrent material records accepted=%u rejected=%u busy=%u stable_position_updates=100000\n",accepted,rejected,atomic_load(&xz_mods_sync_observation_v1.counters[SYO_C_RECORD_BUSY]));
 return 0;
}
