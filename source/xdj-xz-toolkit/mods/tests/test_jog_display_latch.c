/* Own-code callback/metadata/lifecycle and concurrent atomic reader test.
 * Only the existing original-assembler, clock, route and hook installer are fake.
 * Build with -DXZ_JOG_DISPLAY_PROOF, this TU and jog_display.c. */
#define _GNU_SOURCE
#ifdef NDEBUG
#error Assertions required
#endif
#ifndef XZ_JOG_DISPLAY_PROOF
#error Optional proof build required
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "../jog_display_runtime.h"
static int observed_step(struct xz_jd *,const uint8_t *,const struct xz_jd_snapshot *,uint64_t,uint8_t *);
#define xz_jd_step observed_step
#include "../jog_display_runtime.c"
#undef xz_jd_step

static uint64_t clock_ms=10000;
static uint32_t controls=14,route=3;
static unsigned sequence[2],action;
static void (*hook)(void *);
static uint8_t objects[2][0x200],original_frame[2][XZ_JD_FRAME];
static _Atomic unsigned done;
static unsigned accepted;
static uint64_t now(void){return clock_ms;}
uint32_t xz_extra_decks_control_revision(void){return controls;}
uint32_t xz_extra_decks_epoch(void){return route;}
static void original(void *self){
 uint32_t ch;memcpy(&ch,(uint8_t *)self+0x64,4);assert(ch==1||ch==2);
 uint8_t *f=(uint8_t *)self+0x104,p[64]={0};memset(f,0,XZ_JD_FRAME);
 f[0]=0xaa;f[3]=f[4]=f[5]=0xff;f[0x4e]=0x40;f[0x53]=(uint8_t)(ch<<4);
 unsigned n=++sequence[ch-1];f[8]=(uint8_t)n;f[9]=(uint8_t)(n>>8);
 p[0]=(uint8_t)(ch<<4);p[1]=(n&1)?0x21:0x2d;p[2]=p[4]=1;
 for(unsigned i=0;i<64;i++)f[12+i]=p[i^3u];
 uint32_t crc=xz_jd_crc(f);memcpy(f+144,&crc,4);memcpy(original_frame[ch-1],f,XZ_JD_FRAME);
}
int xz_hook_arm(uint32_t address,const unsigned char guard[8],void *replacement,void **previous){
 (void)address;(void)guard;hook=(void (*)(void *))replacement;*previous=(void *)original;return 0;
}
static int observed_step(struct xz_jd *state,const uint8_t *in,const struct xz_jd_snapshot *snap,uint64_t at,uint8_t *out){
 int result=xz_jd_step(state,in,snap,at,out);
 unsigned pending=action;action=0;
 if(pending){xz_jog_display_runtime_stop();if(pending==2)assert(!xz_jog_display_runtime_start());}
 return result;
}
static struct xz_extra_deck deck(uint32_t revision){
 struct xz_extra_deck d={0};d.loaded=d.playing=1;d.total_ms=180000;d.elapsed_ms=30000;
 d.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE);d.tempo_range=10;
 d.timeline.source_frame=44100*30;d.timeline.source_rate=44100;d.timeline.at_ms=(uint32_t)clock_ms;
 d.timeline.route_epoch=route;d.timeline.load_revision=revision;d.timeline.media_generation=0xdeadbeef00000000ULL+revision*3u;
 d.timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE;d.timeline.rate_ppm=1000000;d.timeline.moving=1;
 return d;
}
static void tick(unsigned side){hook(objects[side]);}
static void publish(uint32_t revision){
 struct xz_extra_deck d=deck(revision);xz_jog_display_runtime_publish_deck(1,&d,(uint32_t)clock_ms);
}
#define GET(x) atomic_load_explicit(&(x),memory_order_relaxed)
static void *reader(void *unused){
 (void)unused;
 do{
  for(unsigned side=0;side<2;side++)for(unsigned slot=0;slot<XZ_JDL_SLOTS;slot++){
   const struct xz_jdl_event *event=&xz_mods_jog_latch_v2.side[side].event[slot];
   unsigned first=atomic_load_explicit(&event->seq,memory_order_acquire);
   struct jd_latch_values value;
#define READ_VALUE(name) value.name=GET(event->name);
   XZ_JDL_EVENT_FIELDS(READ_VALUE)
#undef READ_VALUE
   atomic_thread_fence(memory_order_acquire);
   unsigned last=GET(event->seq);
   if(first!=last||(last&1u)||!value.valid)continue;
   accepted++;
   assert(value.valid==1&&value.context&&value.session);
   assert(value.desired==side+1u||value.desired==side+3u);
   assert(value.snapshot_reason<XZ_JDL_REASON_COUNT&&value.step_result<XZ_JDL_RESULT_COUNT);
   assert(value.final_copied<=1&&value.candidate_changed<=1);
   if(slot<XZ_JDL_REASON_COUNT)assert(value.snapshot_reason==slot);
   else if(slot<XZ_JDL_REASON_COUNT+XZ_JDL_RESULT_COUNT)assert(value.step_result==slot-XZ_JDL_REASON_COUNT);
   else assert(value.final_copied==slot-XZ_JDL_REASON_COUNT-XZ_JDL_RESULT_COUNT);
   if(value.source_present){
    assert(value.source_session==value.session&&value.source_route_epoch==value.route_epoch);
    assert(value.source_media_hi==0xdeadbeef&&value.source_media_lo==value.source_load_revision*3u);
   }
  }
 }while(!atomic_load_explicit(&done,memory_order_acquire));
 return NULL;
}
int main(void){
 test_now_ms=now;
 for(unsigned i=0;i<2;i++){uint32_t channel=i+1;memcpy(objects[i]+0x64,&channel,4);}
 assert(!xz_jog_display_runtime_start()&&hook);tick(0);publish(7);for(unsigned i=0;i<8;i++)tick(1);
 struct xz_jdl_side *s=&xz_mods_jog_latch_v2.side[1];
 unsigned seq=GET(s->event[XZ_JDP_VALID].seq);
 for(unsigned i=0;i<100;i++){clock_ms++;publish(7);tick(1);}
 assert(GET(s->event[XZ_JDP_VALID].seq)==seq);
 unsigned copied=GET(s->final[1]);action=2;tick(1);
 assert(GET(s->final[1])==copied&&GET(s->final[0])==1);
 assert(!memcmp(objects[1]+0x104,original_frame[1],XZ_JD_FRAME));
 unsigned old=GET(s->session);tick(1);assert(GET(s->session)!=old);
 assert(GET(s->event[XZ_JDP_VALID].valid)&&GET(s->event[XZ_JDP_VALID].session)==old);
 assert(GET(s->event[14].valid)&&GET(s->event[14].session)==old);
 assert(GET(s->event[13].valid)&&GET(s->event[13].session)==old&&!GET(s->event[13].final_copied));
 pthread_t thread;assert(!pthread_create(&thread,NULL,reader,NULL));
 for(unsigned i=0;i<20000;i++){
  clock_ms++;publish(8+i/33);tick(1);
  if(i%211==0){xz_jog_display_runtime_stop();assert(!xz_jog_display_runtime_start());tick(1);}
 }
 atomic_store_explicit(&done,1,memory_order_release);assert(!pthread_join(thread,NULL));assert(accepted>0);
 assert(sizeof(xz_mods_jog_proof_v1)==196&&sizeof(xz_mods_jog_latch_v2)==4184);
 printf("PASS actual callback latch: repeated cadence, lifecycle guard, side reset, concurrent reader accepted=%u mixed=0\n",accepted);
 return 0;
}
