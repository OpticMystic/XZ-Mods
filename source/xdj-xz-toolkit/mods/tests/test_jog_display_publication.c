/* SPDX-License-Identifier: MIT
 * Real publisher/reader/adapter C; only time, route and native boundaries are fake.
 * Build this TU with jog_display.c, not a second jog_display_runtime.c. */
#define _GNU_SOURCE
#ifdef NDEBUG
#error Jog publication tests require assertions
#endif
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../jog_display_runtime.c"

static _Atomic uint64_t sim_now=10000;
static _Atomic uint32_t sim_control=12,sim_route=3;
static uint16_t native_sequence[2];
static unsigned native_calls,install_calls;
static int install_fail=1,stop_in_install,early_install_calls;
static void (*hook)(void *);
static unsigned char objects[2][0x200];
static unsigned checks;
#define CHECK(x) do { checks++;assert(x); } while(0)

static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void swap_packet(uint8_t *out,const uint8_t *in){for(unsigned i=0;i<64;i++)out[i]=in[i^3u];}
static uint8_t *frame(unsigned side){return objects[side]+0x104;}
static void packet(unsigned side,uint8_t *p){swap_packet(p,frame(side)+12);}
static void original(void *self){
 uint32_t channel;memcpy(&channel,(uint8_t *)self+0x64,4);assert(channel==1||channel==2);
 unsigned side=channel-1;
 /* The adapter must not hold its side guard across the real assembler. */
 assert(!atomic_flag_test_and_set_explicit(&jd_side[side].busy,memory_order_acquire));
 atomic_flag_clear_explicit(&jd_side[side].busy,memory_order_release);
 native_calls++;
 uint8_t *f=(uint8_t *)self+0x104,p[64]={0};memset(f,0,XZ_JD_FRAME);
 f[0]=0xaa;f[3]=f[4]=f[5]=0xff;f[0x4e]=0x40;f[0x53]=(uint8_t)(channel<<4);
 put16(f+8,++native_sequence[side]);p[0]=(uint8_t)(channel<<4);p[1]=0x21;p[12]=57;
 swap_packet(f+12,p);uint32_t crc=xz_jd_crc(f);memcpy(f+144,&crc,4);
}
static uint64_t now(void){return atomic_load(&sim_now);}
static uint32_t control(void){return atomic_load(&sim_control);}
uint32_t xz_extra_decks_control_revision(void){return control();}
uint32_t xz_extra_decks_epoch(void){return atomic_load(&sim_route);}
int xz_hook_arm(uint32_t address,const unsigned char guard[8],void *replacement,void **previous){
 (void)address;(void)guard;install_calls++;
 if(install_fail)return -1;
 hook=(void (*)(void *))replacement;*previous=(void *)original;
 /* Same provider contract as runtime.c: original output precedes replacement
  * activation. A call before xz_hook_arm returns must still run the original. */
 unsigned before=native_calls;hook(objects[0]);assert(native_calls==before+1);early_install_calls++;
 if(stop_in_install)xz_jog_display_runtime_stop();
 return 0;
}
static const struct xz_jog_display_test_ops ops={original,now,control};
static void tick(unsigned side){
 hook(objects[side]);uint32_t crc;memcpy(&crc,frame(side)+144,4);
 CHECK(crc==xz_jd_crc(frame(side)));
}
static void ticks(unsigned side,unsigned n){for(unsigned i=0;i<n;i++)tick(side);}
static void fresh_install(void){
 atomic_store(&sim_now,10000);atomic_store(&sim_route,3);atomic_store(&sim_control,12);
 xz_jog_display_runtime_test_install(&ops,&hook);CHECK(hook);
 for(unsigned i=0;i<2;i++){uint32_t ch=i+1;memcpy(objects[i]+0x64,&ch,4);tick(i);}
}
static struct xz_extra_deck deck(unsigned n,int playing){
 struct xz_extra_deck d={0};memset(d.title,'A'+(int)n,sizeof d.title-1);
 d.loaded=1;d.playing=playing;d.total_ms=180000;d.elapsed_ms=30000+(int32_t)n*1000;
 d.tempo_range=10;d.vinyl=1;d.master_tempo=1;
 d.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE);
 d.timeline.source_frame=44100ull*(30u+n);d.timeline.source_rate=44100;
 d.timeline.at_ms=(uint32_t)now();d.timeline.route_epoch=xz_extra_decks_epoch();
 d.timeline.load_revision=100+n;d.timeline.transport_generation=2;
 d.timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE;
 d.timeline.rate_ppm=1000000;d.timeline.moving=playing;
 return d;
}
static void publish(unsigned side,const struct xz_extra_deck *d){xz_jog_display_runtime_publish_deck((int)side,d,(uint32_t)now());}
static void choose(unsigned side,int extra){uint32_t c=control(),bit=1u<<side;atomic_store(&sim_control,extra?c|bit:c&~bit);}
static void assert_info(unsigned side,int available){
 uint8_t p[64];packet(side,p);CHECK((frame(side)[0x53]>>4)==side+3);
 CHECK(p[0]==(side+1)<<4&&p[1]==0x21);CHECK(p[0x13]==(available?0xbbu:0u));
 CHECK(jd_state[side].unavailable==!available);
}
static void settle(unsigned side){ticks(side,6);assert_info(side,1);}
static void hold_pub(unsigned side){CHECK(!atomic_flag_test_and_set_explicit(&jd_pub[side].busy,memory_order_acquire));}
static void release_pub(unsigned side){atomic_flag_clear_explicit(&jd_pub[side].busy,memory_order_release);}
static void still_held(unsigned side){CHECK(atomic_flag_test_and_set_explicit(&jd_pub[side].busy,memory_order_acquire));}

static void deterministic_busy(void){
 fresh_install();struct xz_extra_deck a=deck(0,1),b=deck(1,1);
 for(unsigned side=0;side<2;side++){
  publish(side,&a);struct jd_sample before,after;CHECK(read_published_deck((int)side,&before)==JD_READ_OK);
  hold_pub(side);publish(side,&b);still_held(side);
  memset(&after,0xa5,sizeof after);struct jd_sample unchanged=after;
  CHECK(read_published_deck((int)side,&after)==JD_READ_BUSY&&!memcmp(&after,&unchanged,sizeof after));still_held(side);
  CHECK(atomic_load(&jd_pub[side].seq)==before.seq);release_pub(side);
  CHECK(read_published_deck((int)side,&after)==JD_READ_OK&&!memcmp(&before,&after,sizeof before));
  publish(side,&b);CHECK(read_published_deck((int)side,&after)==JD_READ_OK&&after.seq!=before.seq);
  CHECK(!memcmp(&after.deck,&b,sizeof b));
 }
 puts("PASS deterministic busy publisher/reader: no overwrite, no flag release, no retry");
}
static void busy_display(void){
 for(unsigned side=0;side<2;side++){
  fresh_install();struct xz_extra_deck d=deck(side,1);publish(side,&d);choose(side,1);settle(side);
  uint64_t generation=jd_state[side].software_generation;uint32_t seq=jd_side[side].cache.seq;
  hold_pub(side);
  for(unsigned t=1;t<=250;t+=7){atomic_store(&sim_now,10000+t);tick(side);assert_info(side,1);CHECK(jd_state[side].phase==XZ_JD_EXTRA&&jd_state[side].software_generation==generation);}
  atomic_store(&sim_now,10250);tick(side);assert_info(side,1);CHECK(jd_side[side].cache.seq==seq&&jd_side[side].cache.published_ms==10000);
#ifdef XZ_JOG_DISPLAY_PROOF
  CHECK((atomic_load(&xz_mods_jog_proof_v1.side[side].read_seq)&0x80000000u)!=0);
  CHECK(atomic_load(&xz_mods_jog_proof_v1.side[side].snapshot_reason)==XZ_JDP_VALID);
#endif
  atomic_store(&sim_now,10251);
  static const uint8_t commands[5]={0x2d,0x30,0x3f,0x2c,0x33};
  for(unsigned i=0;i<5;i++){tick(side);uint8_t p[64];packet(side,p);CHECK(p[0]==(side+1)<<4&&p[1]==commands[i]);CHECK((frame(side)[0x53]>>4)==side+3);}
  ticks(side,12);assert_info(side,0);CHECK(jd_state[side].clear_next==6);still_held(side);release_pub(side);
  /* Successful reads of the same slot also cannot manufacture freshness. */
  ticks(side,8);assert_info(side,0);CHECK(jd_side[side].cache.published_ms==10000);
  atomic_store(&sim_now,10300);d=deck(side,1);publish(side,&d);settle(side);
  tick(1-side);CHECK((frame(1-side)[0x53]>>4)==2-side&&jd_state[1-side].phase==XZ_JD_NATIVE);
 }
 puts("PASS cache reuse through age250; expiry251 clears requested software deck once; sides independent");
}
static void no_initial_and_expiry(void){
 for(unsigned side=0;side<2;side++){
  fresh_install();choose(side,1);hold_pub(side);ticks(side,9);assert_info(side,0);release_pub(side);
  for(unsigned kind=0;kind<3;kind++){
   fresh_install();struct xz_extra_deck d=deck(0,kind!=0);
   if(kind==0)d.timeline.at_ms=10; /* Paused stable anchor may predate this publication. */
   if(kind==1)d.known=0; /* Legacy elapsed-only input still has a publication age. */
   if(kind==2)d.timeline.at_ms=9600; /* A fresh publication cannot renew a stale playing anchor. */
   publish(side,&d);choose(side,1);ticks(side,6);assert_info(side,kind!=2);
   atomic_store(&sim_now,10251);ticks(side,6);assert_info(side,0);
  }
  fresh_install();struct xz_extra_deck empty={0};publish(side,&empty);choose(side,1);ticks(side,8);assert_info(side,0);
  fresh_install();struct xz_extra_deck future=deck(0,1);xz_jog_display_runtime_publish_deck((int)side,&future,10001);choose(side,1);ticks(side,8);assert_info(side,0);
 }
 puts("PASS first absent/busy/empty remains logical3/4; paused/no-timeline publication expires; stale/future cannot renew");
}
static void route_generation_selection(void){
 for(unsigned side=0;side<2;side++){
  fresh_install();struct xz_extra_deck old=deck(0,0);publish(side,&old);choose(side,1);settle(side);
  atomic_store(&sim_route,0);atomic_store(&sim_control,control()&~4u);ticks(side,18);CHECK(jd_state[side].phase==XZ_JD_NATIVE);
  atomic_store(&sim_route,4);atomic_store(&sim_control,control()|4u);ticks(side,8);assert_info(side,0);
  publish(side,&old);ticks(side,8);assert_info(side,0); /* Old route republished after reconnect. */
  struct xz_extra_deck next=deck(1,0);publish(side,&next);settle(side);uint64_t g=jd_state[side].software_generation;
  hold_pub(side);ticks(side,8);assert_info(side,1);CHECK(jd_state[side].software_generation==g);release_pub(side);
  next=deck(2,0);publish(side,&next);settle(side);CHECK(jd_state[side].software_generation!=g);g=jd_state[side].software_generation;
  hold_pub(side);ticks(side,8);CHECK(jd_state[side].software_generation==g);assert_info(side,1);release_pub(side);
  struct xz_extra_deck empty={0};publish(side,&empty);ticks(side,6);assert_info(side,0);
  hold_pub(side);ticks(side,8);assert_info(side,0);release_pub(side); /* Never resurrect the older good generation. */
  publish(side,&next);settle(side);choose(side,0);ticks(side,18);choose(side,1);ticks(side,8);assert_info(side,0);
  publish(side,&next);settle(side);
  /* A whole route down/up can occur between assembler calls. */
  atomic_store(&sim_route,5);ticks(side,8);assert_info(side,0);publish(side,&next);ticks(side,8);assert_info(side,0);
  next=deck(3,0);publish(side,&next);settle(side);
 }
 puts("PASS route down/up, unseen new epoch, generation and selection fences; no old-cache resurrection");
}
static void timestamp_and_sequence_wrap(void){
 fresh_install();atomic_store(&sim_now,0xfffffff0ull);
 struct xz_extra_deck d=deck(0,1);publish(0,&d);choose(0,1);settle(0);hold_pub(0);
 atomic_store(&sim_now,0x100000020ull);tick(0);assert_info(0,1);
 atomic_store(&sim_now,0x1000000eaull);tick(0);assert_info(0,1);
 atomic_store(&sim_now,0x1000000ebull);ticks(0,6);assert_info(0,0);release_pub(0);
 hold_pub(1);atomic_store_explicit(&jd_pub[1].seq,0x3fffffffu,memory_order_relaxed);release_pub(1);
 d=deck(0,0);publish(1,&d);struct jd_sample s;
 CHECK(read_published_deck(1,&s)==JD_READ_OK&&s.seq==1&&!memcmp(&s.deck,&d,sizeof d));
 /* A previously observed cache cannot become young again a whole32-bit
  * clock cycle later. This uses the64-bit serializer clock, no extra syscalls. */
 fresh_install();d=deck(0,0);publish(0,&d);choose(0,1);settle(0);
 atomic_store(&sim_now,10000ull+0x100000000ull);ticks(0,6);assert_info(0,0);
 hold_pub(0);ticks(0,8);assert_info(0,0);release_pub(0);
 puts("PASS 32-bit publication timestamp wrap and nonzero30-bit sequence rollover");
}
static void source_observation_order(void){
 for(unsigned side=0;side<2;side++){
  fresh_install();struct xz_extra_decks older={0},newer={0};
  older.snapshot_revision=100;newer.snapshot_revision=101;
  older.deck[side]=deck(0,0);newer.deck[side]=deck(1,0);
  xz_jog_display_runtime_publish(&older,10000);choose(side,1);settle(side);
  xz_jog_display_runtime_publish(&newer,10000);settle(side);
  uint64_t generation=jd_state[side].software_generation;uint32_t seq=atomic_load(&jd_pub[side].seq);
  atomic_store(&sim_now,10050);xz_jog_display_runtime_publish(&older,10050);
  CHECK(atomic_load(&jd_pub[side].seq)==seq);ticks(side,8);assert_info(side,1);
  CHECK(jd_state[side].software_generation==generation&&jd_side[side].cache.published_ms==10000);
  /* An explicit newer empty view supersedes the loaded generation too. */
  struct xz_extra_decks empty={0};empty.snapshot_revision=102;
  xz_jog_display_runtime_publish(&empty,10050);ticks(side,6);assert_info(side,0);
  xz_jog_display_runtime_publish(&newer,10050);ticks(side,8);assert_info(side,0);
  /* An equal revision is a new, coherent observation, not a cached read. */
  newer.snapshot_revision=103;atomic_store(&sim_now,10300);xz_jog_display_runtime_publish(&newer,10300);settle(side);
  atomic_store(&sim_now,10550);xz_jog_display_runtime_publish(&newer,10550);ticks(side,8);assert_info(side,1);
  CHECK(jd_side[side].cache.published_ms==10550);
 }
 fresh_install();struct xz_extra_decks s={0};s.deck[0]=deck(0,0);s.snapshot_revision=UINT32_MAX;
 xz_jog_display_runtime_publish(&s,10000);s.snapshot_revision=0;s.deck[0]=deck(1,0);
 xz_jog_display_runtime_publish(&s,10000);struct jd_sample sample;
 CHECK(read_published_deck(0,&sample)==JD_READ_OK&&sample.source_ordered&&sample.source_revision==0&&sample.deck.timeline.load_revision==101);
 puts("PASS actual aggregate publisher rejects delayed old loaded/empty revisions; equal observations and revision wrap remain valid");
}
static void lifecycle(void){
 fresh_install();struct xz_extra_deck d=deck(0,0);publish(0,&d);choose(0,1);settle(0);
 hold_pub(0);struct jd_sample before=jd_pub[0].sample;
 xz_jog_display_runtime_stop();CHECK(xz_jog_display_runtime_start()==-1);CHECK(install_calls==1);
 unsigned failed_original=native_calls;tick(0);CHECK(native_calls==failed_original+1);
 CHECK(!memcmp(&jd_pub[0].sample,&before,sizeof before));still_held(0);
 install_fail=0;stop_in_install=1;CHECK(xz_jog_display_runtime_start()==-1);CHECK(install_calls==2&&!atomic_load(&jd_installed));
 stop_in_install=0;CHECK(xz_jog_display_runtime_start()==0&&install_calls==2&&early_install_calls==1);
 CHECK(!memcmp(&jd_pub[0].sample,&before,sizeof before));still_held(0);ticks(0,8);assert_info(0,0);release_pub(0);
 ticks(0,8);assert_info(0,0);publish(0,&d);settle(0);
 xz_jog_display_runtime_stop();unsigned original_before=native_calls;tick(0);CHECK(native_calls==original_before+1&&(frame(0)[0x53]>>4)==1);
 CHECK(xz_jog_display_runtime_start()==0&&install_calls==2);ticks(0,8);assert_info(0,0);publish(0,&d);settle(0);
 /* Test install never clears a live payload or a side state. Reset is deferred
  * to that side's next serializer turn, under its own existing guard. */
 CHECK(!atomic_flag_test_and_set(&jd_side[0].busy));struct xz_jd display=jd_state[0];
 xz_jog_display_runtime_test_install(&ops,&hook);CHECK(hook&&!memcmp(&display,&jd_state[0],sizeof display));
 CHECK(atomic_flag_test_and_set(&jd_side[0].busy));atomic_flag_clear(&jd_side[0].busy);
 hold_pub(0);before=jd_pub[0].sample;xz_jog_display_runtime_test_install(&ops,&hook);still_held(0);
 CHECK(!memcmp(&before,&jd_pub[0].sample,sizeof before));release_pub(0);ticks(0,8);assert_info(0,0);
 CHECK(!atomic_flag_test_and_set(&jd_lifecycle_busy));uint32_t session=atomic_load(&jd_session);
 CHECK(xz_jog_display_runtime_start()==-1);void (*refused)(void *)=original;
 xz_jog_display_runtime_test_install(&ops,&refused);CHECK(!refused&&atomic_load(&jd_session)==session);
 CHECK(atomic_flag_test_and_set(&jd_lifecycle_busy));atomic_flag_clear(&jd_lifecycle_busy);
 xz_jog_display_runtime_test_install(&ops,&hook);CHECK(hook);publish(0,&d);settle(0);
 puts("PASS failed/cancelled hook install, test-install under busy copies, stop/start reuse, lazy reset and lifecycle try-once");
}

static struct xz_extra_deck templates[8];
static _Atomic unsigned start_workers,done_workers;
static void *writer(void *arg){
 unsigned id=(unsigned)(uintptr_t)arg;while(!atomic_load_explicit(&start_workers,memory_order_acquire)){}
 for(unsigned i=0;i<100000;i++){
  const struct xz_extra_deck *d=&templates[id*2+(i&1)];
  xz_jog_display_runtime_publish_deck(0,d,10000);xz_jog_display_runtime_publish_deck(1,d,10000);
 }
 atomic_fetch_add_explicit(&done_workers,1,memory_order_release);return NULL;
}
static void *resetter(void *unused){
 (void)unused;while(!atomic_load_explicit(&start_workers,memory_order_acquire)){}
 for(unsigned i=0;i<2000;i++){void (*ignored)(void *);xz_jog_display_runtime_test_install(&ops,&ignored);}
 atomic_fetch_add_explicit(&done_workers,1,memory_order_release);return NULL;
}
static void stress(void){
 fresh_install();choose(0,1);choose(1,1);
 for(unsigned i=0;i<8;i++)templates[i]=deck(i,0);
 publish(0,&templates[0]);publish(1,&templates[0]);
 pthread_t threads[5];for(unsigned i=0;i<4;i++)CHECK(!pthread_create(&threads[i],NULL,writer,(void *)(uintptr_t)i));
 CHECK(!pthread_create(&threads[4],NULL,resetter,NULL));atomic_store_explicit(&start_workers,1,memory_order_release);
 unsigned accepted=0,busy=0,mixed=0,converted=0;
 do{
  for(unsigned side=0;side<2;side++){
   struct jd_sample s;int result=read_published_deck((int)side,&s);
   if(result!=JD_READ_OK){busy++;continue;}
   accepted++;unsigned matches=0;
   for(unsigned i=0;i<8;i++)if(!memcmp(&s.deck,&templates[i],sizeof s.deck))matches++;
   if(!matches){mixed++;continue;}
   struct xz_jd_snapshot snap;CHECK(xz_jog_display_snapshot_from_deck((int)side,&s.deck,10000,&snap));
   unsigned n=(unsigned)snap.generation-100u;CHECK(n<8&&snap.position_ms==30000+n*1000u);converted++;
  }
  tick(0);tick(1);
 }while(atomic_load_explicit(&done_workers,memory_order_acquire)!=5);
 for(unsigned i=0;i<5;i++)CHECK(!pthread_join(threads[i],NULL));
 CHECK(accepted>0&&converted>0&&!mixed);
 printf("PASS real C multiwriter/reader/reset stress: attempted=800000 accepted=%u busy=%u converted=%u mixed=%u\n",accepted,busy,converted,mixed);
}
int main(void){
 deterministic_busy();busy_display();no_initial_and_expiry();route_generation_selection();timestamp_and_sequence_wrap();source_observation_order();lifecycle();stress();
#ifdef XZ_JOG_DISPLAY_PROOF
 CHECK(sizeof xz_mods_jog_proof_v1==196&&xz_mods_jog_proof_v1.version==1);
 CHECK(atomic_load(&jd_pub[0].drops)>0&&atomic_load(&jd_pub[1].drops)>0);
 puts("PASS optional proof retains version1/196-byte ABI and compatible contention bits");
#endif
 printf("PASS Jog publication checks=%u native_original_calls=%u\n",checks,native_calls);return 0;
}
