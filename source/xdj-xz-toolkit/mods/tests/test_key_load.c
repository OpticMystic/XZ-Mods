#define _GNU_SOURCE
/* Worker admission and timing windows. No device or native adapters. */
#ifdef NDEBUG
#error Active assertions required
#endif
#include <stdint.h>
static uint64_t fake_us=1000000;
static uint64_t load_clock(void){fake_us+=10;return fake_us;}
#define XZ_XP_RENDER_CLOCK_US load_clock
#include "extra_player.c"
#include <assert.h>
static uint32_t load_tick(void){return 1000;}
static void window_test(void) {
 struct xz_render_load s={0};
 for(unsigned i=0;i<256;i++)xz_render_load_record(&s,100,10,256,1000);
 assert(s.latest.samples==256&&s.latest.p99_us==100&&s.latest.max_us==100);
 xz_render_load_record(&s,900,40,256,1001);
 xz_render_load_record(&s,800,30,256,1002);
 assert(s.latest.p99_us==100&&s.latest.max_us==900&&s.latest.key_max_us==40);
 xz_render_load_record(&s,700,20,256,1003);assert(s.latest.p99_us==700);
 for(unsigned i=0;i<256;i++)xz_render_load_record(&s,100,10,256,2000);
 assert(s.latest.p99_us==100&&s.latest.max_us==100&&s.latest.key_max_us==10);
 xz_render_load_record(&s,100,10,128,2001);assert(s.latest.max_us==200);
 assert(xz_render_us(10,9)==0&&xz_render_us(0,12)==0&&xz_render_us(1,UINT64_MAX)==UINT32_MAX);
 struct xz_worker_load v[2]={{.max_us=100},{.max_us=200}};uint32_t reserve[2]={50,60};
 assert(xz_render_projected(v,reserve,1,1,70)==420);
 v[0].key_max_us=40;assert(xz_render_projected(v,reserve,1,1,70)==380);
 puts("PASS exact rolling p99/max, expiry, partial blocks, clock bounds, pending-shift projection");
}
int main(void) {
 window_test();
 static struct xz_xp_players p;static struct reader readers[2];
 p.env.now_ms=load_tick;pthread_mutex_init(&p.publish_lock,NULL);atomic_init(&p.key_active,0);
 for(int i=0;i<2;i++) {
  struct deck *d=&p.deck[i];d->index=i;d->r=&readers[i];d->r->rate=44100;d->state=XZ_XP_CUED;
  d->epoch=1;d->load_revision=2;key_history_reset(d);
 }
 struct xz_key_command step={.kind=XZ_KEY_STEP,.step=1,.source_owner={.route_epoch=1,.load_revision=2}};
 key_command(&p,&p.deck[0],&step);key_command(&p,&p.deck[1],&step);
 assert(p.deck[0].key.desired==100&&p.deck[1].key.desired==100&&atomic_load(&p.key_active)==3);
 p.view[0].worker_load.max_us=100000;p.deck[0].render_load.latest.max_us=100000;
 key_command(&p,&p.deck[0],&step);assert(p.deck[0].key.desired==200);
 assert(p.deck[1].key.desired==100);
 atomic_fetch_and(&p.key_active,~2u);p.deck[1].key.desired=0;
 unsigned revision=p.deck[1].key.revision;
 key_command(&p,&p.deck[1],&step);
 assert(!p.deck[1].key.desired&&!p.deck[1].master_tempo&&!p.deck[1].key.engaged);
 assert(p.deck[1].key.revision==revision&&strstr(p.notice,"AUDIO LOAD TOO HIGH"));
 p.view[0].worker_load.max_us=0;p.deck[0].render_load.latest.max_us=0;
 key_command(&p,&p.deck[1],&step);assert(p.deck[1].key.desired==100);
 atomic_fetch_and(&p.key_active,~2u);p.deck[1].key.desired=0;p.key_reserved_us[0]=XZ_RENDER_SAFE_US;
 key_command(&p,&p.deck[1],&step);assert(!p.deck[1].key.desired);
 step.kind=XZ_KEY_RESET;key_command(&p,&p.deck[0],&step);assert(!p.deck[0].key.desired);
 pthread_mutex_destroy(&p.publish_lock);
 puts("PASS two shifts admitted, measured overload refusal, pending allowance, engaged-shift continuity and reset");
 return 0;
}
