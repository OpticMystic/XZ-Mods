/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Sync link runtime tests require assertions
#endif
#include "../sync_link_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <math.h>

int xz_hook_arm(uint32_t address,const unsigned char expected[8],void *replacement,void **original){
 (void)address;(void)expected;(void)replacement;(void)original;return -1;
}
int xz_extra_decks_use_native_master(void){return 0;}

static unsigned char fake_sync_mgr[32];
static unsigned char fake_sync_master[64];
static unsigned char fake_link_mgr[16];
static unsigned char other_link_mgr[16];
static int slot_occupied[2];
static int native_use_calls;
static unsigned char synced_status[56];
static unsigned char synced_beat[44];
static int synced_status_rc,synced_beat_rc;
static struct xz_sync_link_test_ops hooks;
static uint64_t sim_now_us;
static int sim_clock_failed;
static _Atomic int hold_readers,entered_readers;
static int expire_during_read;
static int fake_monotonic_us(uint64_t *out){
 if(sim_clock_failed)return -1;
 *out=sim_now_us;return 0;
}

static void put_le32(unsigned char *p,uint32_t v){
 p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
 p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static uint32_t get_le32(const unsigned char *p){
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}

static int orig_get_player(void *lm,const int *id,void *out){
 (void)lm;
 unsigned char *b=out;memset(b,0,56);
 if(id&&(*id==3||*id==4)&&slot_occupied[*id-3]){
  put_le32(b,(uint32_t)*id);b[4]=0xa4;b[5]=1;put_le32(b+0x1c,12800);return 0;
 }
 put_le32(b+0x1c,0x7fffffffu);
 return 0;
}
static int orig_get_beat(void *lm,const int *id,const uint8_t *lane,void *out){
 (void)lm;(void)id;(void)lane;
 memset(out,0xee,44);
 return 0;
}
static int orig_set_player(void *lm,const int *id,const void *a2,const void *a3,const void *a4,
                           const void *a5,const void *a6,const void *a7,const void *a8,const void *a9,
                           const void *a10,const void *a11,const void *a12,const void *a13){
 (void)lm;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;(void)a8;(void)a9;(void)a10;(void)a11;(void)a12;(void)a13;
 if(id&&(*id==3||*id==4))slot_occupied[*id-3]=1;
 return 0;
}
static int orig_set_beat(void *lm,const int *id,const void *lane,const void *pkt){
 (void)lm;(void)lane;(void)pkt;
 if(id&&(*id==3||*id==4))slot_occupied[*id-3]=1;
 return 0;
}
static int orig_set_master_on(void *sm,const int *master_id){
 if(!sm||!master_id)return -1;
 int32_t m=*master_id;
 memcpy((unsigned char *)sm+0x08,&m,4);
 return 0;
}
static int fake_set_link_master(void *sm,const int *input_id){
 if(!sm||!input_id)return -1;
 int id=*input_id;
 if(id>=3&&id<=4){
  unsigned char st[56];
  if(hooks.get_player_info(fake_link_mgr,&id,st)!=0)return -1;
  if((st[4]&0x80)==0||(st[4]&0x24)!=0x24)return -1;
 }
 int m=id<=0?-1:id-1;
 return hooks.set_master_on(sm,&m);
}
static int orig_sync_mgr(void *mgr){
 (void)mgr;
 if(atomic_load(&hold_readers)){
  atomic_fetch_add(&entered_readers,1);
  while(atomic_load(&hold_readers))sched_yield();
  return 0;
 }
 if(expire_during_read){sim_now_us+=XZ_SYNC_LINK_MAX_AGE_US+1;expire_during_read=0;}
 int32_t m=-1;memcpy(&m,fake_sync_master+0x08,4);
 if(m>=2&&m<=3){
  int id=m+1;uint8_t lane=0;unsigned char ignored[56];
  assert(hooks.get_player_info(other_link_mgr,&id,ignored)==0&&
         get_le32(ignored)==(slot_occupied[id-3]?(uint32_t)id:0u));
  synced_status_rc=hooks.get_player_info(fake_link_mgr,&id,synced_status);
  synced_beat_rc=hooks.get_beat_info(fake_link_mgr,&id,&lane,synced_beat);
 }
 return 0;
}
static void on_native_master(void){native_use_calls++;}

static void reset_sim(void){
 memset(fake_sync_mgr,0,sizeof fake_sync_mgr);
 memset(fake_sync_master,0,sizeof fake_sync_master);
 memset(fake_link_mgr,0,sizeof fake_link_mgr);
 void *sm_ptr=fake_sync_master,*lm_ptr=fake_link_mgr;
 memcpy(fake_sync_mgr+4,&sm_ptr,sizeof sm_ptr);
 memcpy(fake_sync_master+0x30,&lm_ptr,sizeof lm_ptr);
 int32_t init_master=0;
 memcpy(fake_sync_master+0x08,&init_master,4);
 slot_occupied[0]=slot_occupied[1]=0;
 native_use_calls=0;
 sim_now_us=1000000;sim_clock_failed=0;
 atomic_store(&hold_readers,0);atomic_store(&entered_readers,0);expire_during_read=0;
 memset(synced_status,0,sizeof synced_status);
 memset(synced_beat,0,sizeof synced_beat);
 synced_status_rc=synced_beat_rc=-99;
 struct xz_sync_link_test_ops orig={
  .sync_mgr_sync=orig_sync_mgr,.get_player_info=orig_get_player,.get_beat_info=orig_get_beat,
  .set_player_info=orig_set_player,.set_beat_info=orig_set_beat,
  .set_master_on=orig_set_master_on,.set_link_master=fake_set_link_master,
  .on_use_native_master=on_native_master,.monotonic_us=fake_monotonic_us,.legacy_fixture=1
 };
 xz_sync_link_runtime_test_install(&orig,&hooks);
}

static struct xz_extra_beat test_grid[8]={
 {100,1},{600,2},{1100,3},{1600,4},{2100,1},{2600,2},{3100,3},{3600,4}
};

static struct xz_extra_decks make_decks(int master_deck,uint32_t at_ms,uint64_t source_frame,int32_t rate_ppm){
 sim_now_us=(uint64_t)at_ms*1000u;
 struct xz_extra_decks d;memset(&d,0,sizeof d);
 d.route_up=1;
 if(master_deck>=0&&master_deck<2){
  struct xz_extra_deck *dk=&d.deck[master_deck];
  dk->loaded=1;dk->playing=1;dk->master=1;dk->source=1;
  dk->track_bpm_x100=12000;dk->bpm_x100=12600;dk->tempo_x100=500;
  dk->grid=test_grid;dk->grid_count=8;
  dk->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
  dk->timeline.source_frame=source_frame;
  dk->timeline.sink_frame=44100;
  dk->timeline.observed_wire_frame=44100;
  dk->timeline.media_generation=42;
  dk->timeline.source_rate=44100;
  dk->timeline.at_ms=at_ms;
  dk->timeline.route_epoch=7;
  dk->timeline.load_revision=3;
  dk->timeline.transport_generation=5;
  dk->timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_RATE|XZ_EXTRA_TIMELINE_SINK_FRAME|XZ_EXTRA_TIMELINE_MOTION;
  dk->timeline.rate_ppm=rate_ppm;
  /* This fixed-rate synthetic fixture explicitly supplies its accepted/completed provenance. */
  dk->timeline.completed_rate=rate_ppm/1000000.;
  unsigned beat=0;while(beat+1<8&&(double)test_grid[beat+1].ms*44.1<=source_frame)beat++;
  dk->timeline.completed_beat_ms=test_grid[beat].ms;
  dk->timeline.completed_beat_age_frames=(source_frame-test_grid[beat].ms*44.1)/dk->timeline.completed_rate;
  dk->timeline.flags|=XZ_EXTRA_TIMELINE_APPLIED_RATE|XZ_EXTRA_TIMELINE_BEAT_HISTORY;
  dk->timeline.moving=1;
 }
 return d;
}

static void stale_safety_tests(void);
static void publication_failure_test(void);

static void completion_coordinate_tests(void);

int main(void){
 reset_sim();
 struct xz_sync_link_clock_pair clk={.mono_us=1000000,.realtime_us=UINT64_C(1700000000500000),.uncertainty_us=50};
 struct xz_extra_decks decks=make_decks(0,1000,44100,1050000);
 /* First SyncMgr::sync probes slot 3 & 4 absence and observes native master=0. */
 hooks.sync_mgr_sync(fake_sync_mgr);
 assert(!xz_sync_link_runtime_master_committed(0));
 /* Publisher claims slot 3, builds plan from Deck 3 completed PCM, offers bundle, requests master. */
 xz_sync_link_runtime_step(&decks,&clk);
 /* Outside SyncMgr::sync, getLinkPlayerInfo does NOT leak virtual payload. */
 unsigned char outside[56];int id3=3;
 assert(hooks.get_player_info(fake_link_mgr,&id3,outside)==0&&get_le32(outside)==0&&get_le32(outside+0x1c)==0x7fffffffu);
 /* Next SyncMgr::sync drains the master request (setting native master_id=2) and reads status+beat. */
 hooks.sync_mgr_sync(fake_sync_mgr);
 int32_t cur_master=-1;memcpy(&cur_master,fake_sync_master+0x08,4);
 assert(cur_master==2&&synced_status_rc==0&&synced_beat_rc==0);
 assert(get_le32(synced_status)==3&&synced_status[4]==0xa4&&get_le32(synced_status+0x1c)==12000);
 assert(get_le32(synced_beat+0x1c)==12000&&synced_beat[0x20]==2&&synced_beat[0x22]==3);
 assert(native_use_calls==0);
 /* Next publisher step observes master_id==2 and marks Deck 3 master committed. */
 clk.mono_us+=10000;clk.realtime_us+=10000;
 decks=make_decks(0,1010,44563,1050000);
 xz_sync_link_runtime_step(&decks,&clk);
 assert(xz_sync_link_runtime_master_committed(0)&&!xz_sync_link_runtime_master_committed(1));
 /* Dummy master swap does NOT invalidate lease or call on_use_native_master. */
 fake_sync_master[0x15]=1;int dummy_m=1;
 hooks.set_master_on(fake_sync_master,&dummy_m);
 fake_sync_master[0x15]=0;cur_master=2;memcpy(fake_sync_master+0x08,&cur_master,4);
 assert(native_use_calls==0&&xz_sync_link_runtime_master_committed(0));
 /* Graceful release when Deck 3 stops being master restores previous native master (0 -> input_id 1). */
 decks=make_decks(-1,1020,44563,0);
 clk.mono_us+=10000;clk.realtime_us+=10000;
 xz_sync_link_runtime_step(&decks,&clk);
 assert(!xz_sync_link_runtime_master_committed(0));
 hooks.sync_mgr_sync(fake_sync_mgr);
 memcpy(&cur_master,fake_sync_master+0x08,4);
 assert(cur_master==0);
 /* Re-acquire on Deck 4 (source_id=4), then test explicit native operator override. */
 reset_sim();
 clk=(struct xz_sync_link_clock_pair){.mono_us=2000000,.realtime_us=UINT64_C(1700000001000000),.uncertainty_us=50};
 decks=make_decks(1,2000,44100,1000000);
 hooks.sync_mgr_sync(fake_sync_mgr);
 xz_sync_link_runtime_step(&decks,&clk);
 hooks.sync_mgr_sync(fake_sync_mgr);
 clk.mono_us+=10000;clk.realtime_us+=10000;decks=make_decks(1,2010,44541,1000000);
 xz_sync_link_runtime_step(&decks,&clk);
 assert(xz_sync_link_runtime_master_committed(1));
 int op_m=1;hooks.set_master_on(fake_sync_master,&op_m);
 assert(native_use_calls==0&&!xz_sync_link_runtime_master_committed(1));
 /* Native callback queues withdrawal; publisher delivers after its lock. */
 xz_sync_link_runtime_step(&decks,&clk);
 assert(native_use_calls==1);
 /* Foreign external Link writer invalidates lease immediately and preserves foreign slot. */
 reset_sim();
 clk=(struct xz_sync_link_clock_pair){.mono_us=3000000,.realtime_us=UINT64_C(1700000002000000),.uncertainty_us=50};
 decks=make_decks(0,3000,44100,1000000);
 hooks.sync_mgr_sync(fake_sync_mgr);
 xz_sync_link_runtime_step(&decks,&clk);
 hooks.sync_mgr_sync(fake_sync_mgr);
 clk.mono_us+=10000;clk.realtime_us+=10000;decks=make_decks(0,3010,44541,1000000);
 xz_sync_link_runtime_step(&decks,&clk);
 assert(xz_sync_link_runtime_master_committed(0));
 hooks.set_beat_info(fake_link_mgr,&id3,NULL,NULL);
 assert(!xz_sync_link_runtime_master_committed(0));
 clk.mono_us+=10000;clk.realtime_us+=10000;decks=make_decks(0,3020,44982,1000000);
 xz_sync_link_runtime_step(&decks,&clk);
 assert(!xz_sync_link_runtime_master_committed(0));
 stale_safety_tests();
 publication_failure_test();
 completion_coordinate_tests();
 puts("PASS runtime Link bridge: Deck 3/4 master handoff, consumer-scoped status/beat reads, operator override, foreign writer preemption and previous-master restore");
 return 0;
}

static int32_t simulated_master(void){
 int32_t m;memcpy(&m,fake_sync_master+0x08,4);return m;
}
static void establish_master(int committed,struct xz_extra_decks *decks,struct xz_sync_link_clock_pair *clk){
 reset_sim();
 *clk=(struct xz_sync_link_clock_pair){1000000,UINT64_C(1700000000500000),50};
 *decks=make_decks(0,1000,44100,1000000);
 hooks.sync_mgr_sync(fake_sync_mgr);
 xz_sync_link_runtime_step(decks,clk);
 if(committed){
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(simulated_master()==2);
  clk->mono_us+=10000;clk->realtime_us+=10000;
  *decks=make_decks(0,1010,44541,1000000);
  xz_sync_link_runtime_step(decks,clk);
  assert(xz_sync_link_runtime_master_committed(0));
 }
}
static void stale_safety_tests(void){
 struct xz_extra_decks decks;struct xz_sync_link_clock_pair clk;
 /* Both accepted and undrained requests must be withdrawn by a stale producer step. */
 for(int committed=0;committed<=1;committed++){
  establish_master(committed,&decks,&clk);
  clk.mono_us+=5000000;clk.realtime_us+=5000000;sim_now_us=clk.mono_us;
  xz_sync_link_runtime_step(&decks,&clk);
  assert(!xz_sync_link_runtime_master_committed(0));
  synced_status_rc=synced_beat_rc=-99;
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(simulated_master()==0&&synced_status_rc!=0&&synced_beat_rc!=0);
 }
 /* No publisher call: expiry must block both old data and a late master request. */
 for(int committed=0;committed<=1;committed++){
  establish_master(committed,&decks,&clk);
  sim_now_us=clk.mono_us+XZ_SYNC_LINK_MAX_AGE_US+1;
  assert(!xz_sync_link_runtime_master_committed(0));
  synced_status_rc=synced_beat_rc=-99;
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(simulated_master()==(committed?2:0));
  assert(synced_status_rc!=0&&synced_beat_rc!=0);
  /* Fresh PCM resumes only after guarded cleanup of the expired generation. */
  clk.realtime_us+=sim_now_us-clk.mono_us;clk.mono_us=sim_now_us;
  decks=make_decks(0,(uint32_t)(clk.mono_us/1000),48951,1000000);
  xz_sync_link_runtime_step(&decks,&clk);
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(simulated_master()==0);
 }
 /* Invalid phase inputs and clocks must not retain a committed virtual master. */
 for(unsigned fault=0;fault<15;fault++){
  establish_master(1,&decks,&clk);
  clk.mono_us+=10000;clk.realtime_us+=10000;
  decks=make_decks(0,1020,44982,1000000);
  switch(fault){
   case 0:decks.deck[0].playing=0;break;
   case 1:decks.deck[0].loaded=0;break;
   case 2:decks.route_up=0;break;
   case 3:decks.deck[0].timeline.flags=0;break;
   case 4:decks.deck[0].timeline.route_epoch++;break;
   case 5:decks.deck[0].timeline.load_revision++;break;
   case 6:decks.deck[0].timeline.transport_generation++;break;
   case 7:decks.deck[0].timeline.media_generation++;break;
   case 8:decks.deck[0].track_bpm_x100=30000;break;
   case 9:clk.uncertainty_us=XZ_SYNC_LINK_MAX_UNCERTAINTY_US+1;break;
   case 10:clk.realtime_us+=3000;break;
   case 11:clk.mono_us-=20000;break;
   case 12:decks.deck[0].timeline.at_ms+=100;break;
   case 13:decks.deck[0].known=0;break;
   case 14:decks.deck[0].timeline.sink_frame=0;break;
  }
  sim_now_us=clk.mono_us;
  xz_sync_link_runtime_step(&decks,&clk);
  assert(!xz_sync_link_runtime_master_committed(0));
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(simulated_master()==0);
 }
 /* Missing clock sampling is also an immediate withdrawal. */
 establish_master(1,&decks,&clk);
 xz_sync_link_runtime_step(&decks,NULL);
 assert(!xz_sync_link_runtime_master_committed(0));
 hooks.sync_mgr_sync(fake_sync_mgr);assert(simulated_master()==0);
 /* Cleanup may not override an operator choice or a foreign writer arriving later. */
 for(int foreign=0;foreign<=2;foreign++){
  establish_master(1,&decks,&clk);
  clk.mono_us+=5000000;clk.realtime_us+=5000000;sim_now_us=clk.mono_us;
  xz_sync_link_runtime_step(&decks,&clk);
  if(foreign==1){int id=3;hooks.set_beat_info(fake_link_mgr,&id,NULL,NULL);}
  else if(foreign==2){int32_t master=1;memcpy(fake_sync_master+0x08,&master,4);}
  else{int master=1;hooks.set_master_on(fake_sync_master,&master);}
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(simulated_master()==(foreign==1?2:1));
  assert(!xz_sync_link_runtime_master_committed(0));
 }
 /* Fresh data still carries the exact completed-PCM-derived beat time. */
 establish_master(1,&decks,&clk);
 hooks.sync_mgr_sync(fake_sync_mgr);
 assert(synced_beat_rc==0&&get_le32(synced_beat+0x1c)==12000);
 int32_t sec,usec;memcpy(&sec,synced_beat+0x24,4);memcpy(&usec,synced_beat+0x28,4);
 assert(sec==1700000000&&usec==100000);
 /* The hook must recheck age within a frame, before each payload read. */
 expire_during_read=1;synced_status_rc=synced_beat_rc=-99;
 hooks.sync_mgr_sync(fake_sync_mgr);
 assert(synced_status_rc!=0&&synced_beat_rc!=0&&!xz_sync_link_runtime_master_committed(0));
 /* Failed or backwards consumer clocks fail closed even with fresh publication. */
 for(int failed=0;failed<=1;failed++)for(int committed=0;committed<=1;committed++){
  establish_master(committed,&decks,&clk);
  sim_clock_failed=failed;sim_now_us=1;
  assert(!xz_sync_link_runtime_master_committed(0));
  synced_status_rc=synced_beat_rc=-99;
  hooks.sync_mgr_sync(fake_sync_mgr);
  assert(synced_status_rc!=0&&synced_beat_rc!=0);
  assert(simulated_master()==(committed?2:0));
 }
 /* Boundary remains fresh at 100 ms; one more microsecond cancels pending intent. */
 establish_master(0,&decks,&clk);
 sim_now_us+=XZ_SYNC_LINK_MAX_AGE_US;
 hooks.sync_mgr_sync(fake_sync_mgr);assert(simulated_master()==2&&synced_beat_rc==0);
 establish_master(0,&decks,&clk);
 sim_now_us+=XZ_SYNC_LINK_MAX_AGE_US+1;
 hooks.sync_mgr_sync(fake_sync_mgr);assert(simulated_master()==0);
 puts("PASS stale/invalid PCM and clock cleanup, stalled-consumer expiry, guarded restore and real PCM beat timestamp");
}

static void *held_consumer(void *unused){
 (void)unused;hooks.sync_mgr_sync(fake_sync_mgr);return NULL;
}
static void publication_failure_test(void){
 struct xz_extra_decks decks;struct xz_sync_link_clock_pair clk;
 establish_master(1,&decks,&clk);
 pthread_t readers[2];
 atomic_store(&hold_readers,1);
 for(unsigned i=0;i<2;i++){
  assert(pthread_create(&readers[i],NULL,held_consumer,NULL)==0);
  while(atomic_load(&entered_readers)<(int)i+1)sched_yield();
  clk.mono_us+=1000;clk.realtime_us+=1000;
  decks=make_decks(0,(uint32_t)(clk.mono_us/1000),44541+44*(i+1),1000000);
  xz_sync_link_runtime_step(&decks,&clk);
  assert(xz_sync_link_runtime_master_committed(0));
 }
 /* Two pinned old slots and the current publication leave no writable slot. */
 clk.mono_us+=1000;clk.realtime_us+=1000;
 decks=make_decks(0,(uint32_t)(clk.mono_us/1000),44673,1000000);
 xz_sync_link_runtime_step(&decks,&clk);
 assert(!xz_sync_link_runtime_master_committed(0));
 atomic_store(&hold_readers,0);
 for(unsigned i=0;i<2;i++)assert(pthread_join(readers[i],NULL)==0);
 hooks.sync_mgr_sync(fake_sync_mgr);assert(simulated_master()==0);
 puts("PASS publication failure withdraws owned lease and restores prior master after pinned readers finish");
}

static void completion_coordinate_tests(void){
 for(int side=0;side<2;side++){
  struct xz_extra_decks d=make_decks(side,1000,44100,1000000);
  struct xz_extra_deck *dk=&d.deck[side];struct xz_sync_link_pcm_anchor a;
  assert(xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  assert(a.captured_mono_us==1000000&&a.uncertainty_us==1250);
  dk->timeline.observed_wire_frame=dk->timeline.sink_frame+256;
  assert(xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  assert(a.captured_mono_us==1000000-UINT64_C(256000000)/44100);
  dk->timeline.rate_ppm=1066406;dk->timeline.completed_rate=1.07;
  assert(xz_sync_link_anchor_from_deck(side,dk,1000000,&a)&&a.effective_speed==1.07&&a.completed_beat_history);
  uint32_t flags=dk->timeline.flags;
  dk->timeline.flags&=~XZ_EXTRA_TIMELINE_APPLIED_RATE;assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  dk->timeline.flags=flags&~XZ_EXTRA_TIMELINE_BEAT_HISTORY;assert(xz_sync_link_anchor_from_deck(side,dk,1000000,&a)&&!a.completed_beat_history&&a.effective_speed==1.07);
  dk->timeline.flags=flags;double age=dk->timeline.completed_beat_age_frames;
  dk->timeline.completed_beat_age_frames=NAN;assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  dk->timeline.completed_beat_age_frames=-1;assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  dk->timeline.completed_beat_age_frames=age;
  dk->timeline.observed_wire_frame=0;
  assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  dk->timeline.observed_wire_frame=dk->timeline.sink_frame-1;
  assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  dk->timeline.observed_wire_frame=dk->timeline.sink_frame+4410;
  assert(xz_sync_link_anchor_from_deck(side,dk,1000000,&a)&&a.captured_mono_us==900000);
  dk->timeline.observed_wire_frame++;
  assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
  dk->timeline.observed_wire_frame=UINT64_MAX;
  assert(!xz_sync_link_anchor_from_deck(side,dk,1000000,&a));
 }
 puts("PASS completed-wire timestamps: tagged endpoint lag, missing/torn/old/overflow coordinates and uncertainty");
}
