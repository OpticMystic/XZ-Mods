// SPDX-License-Identifier: GPL-2.0-only
/* Included after the existing fake transport and player fixture helpers. */
static void recovery_setup(uint32_t epoch) {
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,order_resolve};
 clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(epoch);step();
}
static void recovery_close(void) { xz_xp_destroy(p);p=NULL;assert(!xz_extra_order_live()); }
static void recovery_pcm(int side,double source,double rate) {
 assert(f.captured_frames[side]>=32);
 for(unsigned n=0;n<32;n++)for(unsigned ch=0;ch<2;ch++)
  assert(fabs(f.captured[side][n*2+ch]-.5*sin(6.283185307179586*440*
    (source+rate*debug(side).source_rate/XZ_USB_RATE*n)/debug(side).source_rate))<.001);
}
static void recovery_capture(int side) { f.capture[side]=1;f.captured_frames[side]=0; }

static void test_recovery_pcm(void) {
 for(unsigned id=2;id<=4;id+=2)for(int pitch=-10000;pitch<=10000;pitch+=10000){
  recovery_setup(800);
  for(int side=0;side<2;side++){
   load(side,id);seek(side,1000);tempo(side,pitch);
   press(side,XZ_EXTRA_IN_VINYL);assert(view.deck[side].vinyl);
   press(side,XZ_EXTRA_IN_VINYL);assert(!view.deck[side].vinyl);
   press(side,XZ_EXTRA_IN_VINYL);assert(view.deck[side].vinyl);
   assert(!view.deck[1-side].vinyl||side==1);
  }
  for(int playing=0;playing<2;playing++){
   for(int side=0;side<2;side++){
    seek(side,1000);if(playing)press(side,XZ_EXTRA_IN_PLAY);
    assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,1)));
   }
   step();
   uint64_t start[2]={debug(0).position,debug(1).position};
   for(int side=0;side<2;side++){
    assert(debug(side).state==XZ_XP_SCRATCHING&&!view.deck[side].playing);
    recovery_capture(side);jog(side,side?-8:8);
   }
   step();clock_ms++;step();
   for(int side=0;side<2;side++)recovery_pcm(side,(double)start[side],side?-2.5:2.5);
   advance(100);
   for(int side=0;side<2;side++){
    int64_t distance=(int64_t)debug(side).position-(int64_t)start[side];
    assert(distance==(side?-1:1)*(int64_t)(debug(side).source_rate*40u/1000u));
    assert(debug(side).position==debug(side).scratch_target);
    assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));
   }
   step();
   for(int side=0;side<2;side++){
    assert(debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
    if(playing)press(side,XZ_EXTRA_IN_PLAY);
   }
  }
  recovery_close();
 }
 puts("PASS recovery PCM: simultaneous signed TOP scratch, both Vinyl toggles, paused/play release, rates and tempos");
}

static void test_recovery_timing(void) {
 const uint32_t gaps[]={0,1,2,4,8,20,100,101,200,1000};
 for(int side=0;side<2;side++)for(int sign=-1;sign<=1;sign+=2)
 for(unsigned i=0;i<sizeof gaps/sizeof *gaps;i++){
  recovery_setup(810);load(side,4);seek(side,1000);press(side,XZ_EXTRA_IN_VINYL);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);double start=(double)debug(side).position;
  jog(side,sign);clock_ms+=gaps[i];jog(side,sign);
  /* Worker latency must not become the packet interval. */
  clock_ms+=500;recovery_capture(side);step();
  double interval=gaps[i]>16?16:gaps[i];
  double rate=sign*(gaps[i]?5.0/interval:10.0/16.0);
  /* The first accepted interval keeps its 16 ms fallback traversal even when
     a later packet is already queued with a different arrival interval. */
  recovery_pcm(side,start,sign*(gaps[i]?5.0/16.0:10.0/16.0));
  assert(fabs(debug(side).speed-rate)<1e-8||debug(side).position==debug(side).scratch_target);
  assert(debug(side).scratch_target==(uint64_t)(start+sign*441.0));
  recovery_close();
 }
 /* A reversal after quiet time uses the same bounded audible onset. */
 for(int side=0;side<2;side++){
  recovery_setup(811);load(side,4);seek(side,1000);press(side,XZ_EXTRA_IN_VINYL);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);jog(side,1);step();advance(200);
  double start=44100.0+220.5;recovery_capture(side);jog(side,-1);step();clock_ms++;step();
  /* One 16 ms stroke uses 705.6 samples, leaving a 0.4-sample carry. */
  recovery_pcm(side,start-.4*.3125,-.3125);
  assert(debug(side).position==debug(side).scratch_target&&debug(side).speed==0);
  recovery_close();
 }
 const uint32_t first[]={0,UINT32_MAX-5};const uint32_t next[]={5,4};
 for(unsigned i=0;i<2;i++){
  recovery_setup(812);load(0,4);seek(0,1000);press(0,XZ_EXTRA_IN_VINYL);
  hold(0,XZ_EXTRA_IN_JOG_TOUCH,1);clock_ms=first[i];jog(0,1);clock_ms=next[i];jog(0,1);
  clock_ms+=500;recovery_capture(0);step();recovery_pcm(0,44100,.3125);
  recovery_close();
 }
 /* Playing rim bend sees the same arrivals with eager or delayed consumption. */
 recovery_setup(813);
 for(int side=0;side<2;side++){load(side,4);press(side,XZ_EXTRA_IN_PLAY);}
 for(unsigned i=0;i<24;i++){
  clock_ms+=5;jog(0,1);jog(1,1);stepping_side=0;xz_xp_step(p,0);
 }
 step();assert(fabs(debug(0).speed-debug(1).speed)<1e-7);recovery_close();
 puts("PASS recovery timing: slow/fast inputs, delayed worker, same-ms batches, reversals, timestamp0/wrap and rim bend");
}

static void test_recovery_pressure(void) {
 const enum xz_extra_input held[]={XZ_EXTRA_IN_CUE,XZ_EXTRA_IN_JOG_TOUCH,XZ_EXTRA_IN_LOOP_IN};
 for(int reverse=0;reverse<2;reverse++){
  recovery_setup(820);
  for(int side=0;side<2;side++){
   load(side,4);press(side,XZ_EXTRA_IN_VINYL);
   for(unsigned k=0;k<3;k++)assert(input(event(side,held[k],1)));
  }
  step();
  for(int side=0;side<2;side++){
   for(unsigned q=0;q<XZ_XP_QUEUE;q++)jog(side,0);
   assert(!input(event(side,XZ_EXTRA_IN_PLAY,1)));
   for(unsigned k=0;k<3;k++)assert(input(event(side,held[reverse?2-k:k],0)));
   assert(!input(event(side,XZ_EXTRA_IN_CUE,1)));
  }
  step();
  for(int side=0;side<2;side++){
   assert(debug(side).state==XZ_XP_CUED);press(side,XZ_EXTRA_IN_PLAY);
   assert(debug(side).state==XZ_XP_PLAYING);assert(!view.deck[side].loop.active);
  }
  recovery_close();
 }
 /* A press refused by an entirely full non-jog queue creates no release debt. */
 recovery_setup(821);load(0,4);
 for(unsigned q=0;q<XZ_XP_QUEUE;q++)assert(input(event(0,XZ_EXTRA_IN_SYNC,0)));
 assert(!input(event(0,XZ_EXTRA_IN_CUE,1)));assert(!input(event(0,XZ_EXTRA_IN_JOG_TOUCH,1)));
 assert(!input(event(0,XZ_EXTRA_IN_CUE,0)));step();assert(debug(0).state==XZ_XP_CUED);
 /* The admitted press at the last ordinary slot still owns an available release. */
 for(unsigned q=0;q<XZ_XP_QUEUE-1;q++)assert(input(event(0,XZ_EXTRA_IN_SYNC,0)));
 assert(input(event(0,XZ_EXTRA_IN_CUE,1)));assert(input(event(0,XZ_EXTRA_IN_CUE,0)));
 step();assert(debug(0).state==XZ_XP_CUED);recovery_close();
 puts("PASS recovery pressure: 64 motion entries plus all3 ordered critical releases, both sides, full edge queue refusal");
}

static void test_recovery_epochs(void) {
 for(int side=0;side<2;side++)for(int kind=0;kind<XZ_EXTRA_INPUT_COUNT;kind++){
  if(kind==XZ_EXTRA_IN_LOAD)continue;
  recovery_setup(830);load(side,4);seek(side,500);
  struct xz_extra_deck_event e=event(side,(enum xz_extra_input)kind,1);
  if(kind==XZ_EXTRA_IN_JOG_TURN)e.jog.delta=8;
  if(kind==XZ_EXTRA_IN_SEEK)e.seek_ms=1500;
  if(kind==XZ_EXTRA_IN_TEMPO)e.tempo_position=10000;
  assert(input(e));fake_route(831);step();
  assert(debug(side).state==XZ_XP_CUED&&debug(side).position==22050);
  assert(!view.deck[side].vinyl&&!view.deck[side].master_tempo&&!view.deck[side].master);
  assert(!view.deck[side].loop.active&&!view.deck[side].sync.requested&&view.deck[side].tempo_x100==0);
  assert(debug(side).tempo_range==10&&!debug(side).tempo_reset);
  recovery_close();
 }
 for(int side=0;side<2;side++){
  recovery_setup(832);load(side,4);press(side,XZ_EXTRA_IN_VINYL);hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));fake_route(833);
  assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,1)));step();
  assert(debug(side).state==XZ_XP_SCRATCHING);hold(side,XZ_EXTRA_IN_JOG_TOUCH,0);
  assert(debug(side).state==XZ_XP_CUED);recovery_close();
 }
 /* Stale LOAD at a full retire barrier must neither resolve nor retain its order forever. */
 recovery_setup(834);load(0,4);
 for(unsigned i=0;i<XZ_XP_RETIRE_SLOTS;i++)load(0,4);
 assert(debug(0).retired==XZ_XP_RETIRE_SLOTS);
 uint32_t ids[]={2};uint8_t kinds[]={1};
 struct xz_extra_order *order=xz_extra_order_create(1,42,ids,kinds,1,1);assert(order);
 struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_LOAD,1);
 e.load.source=1;e.load.track_id=2;e.load.route_epoch=834;e.load.media_generation=42;e.load.order=order;
 for(unsigned q=0;q<XZ_XP_QUEUE;q++)assert(input(e));xz_extra_order_release(order);
 assert(xz_extra_order_live()==1);step();assert(xz_extra_order_live()==1);
 fake_route(835);step();assert(!xz_extra_order_live());
 assert(debug(0).source_rate==44100&&!strcmp(view.deck[0].title,"AIFF"));recovery_close();
 puts("PASS recovery epochs: every queued control, old release/new press, stale LOAD retire barrier and reference cleanup");
}

static void test_recovery_distance(void) {
 for(int side=0;side<2;side++)for(unsigned id=2;id<=4;id+=2)for(int sign=-1;sign<=1;sign+=2){
  uint64_t target[2];
  for(int vinyl=0;vinyl<2;vinyl++){
   recovery_setup(840);load(side,id);seek(side,1000);
   if(vinyl){press(side,XZ_EXTRA_IN_VINYL);hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);}
   for(unsigned q=0;q<37;q++){jog(side,sign);step();}
   target[vinyl]=vinyl?debug(side).scratch_target:debug(side).position;
   /* Malformed/extreme counts are bounded before conversion; zero is harmless. */
   jog(side,0);step();assert(isfinite(debug(side).speed));
   jog(side,INT32_MAX);step();assert(debug(side).position<=debug(side).source_rate*2u);
   jog(side,INT32_MIN);step();assert(debug(side).position<=debug(side).source_rate*2u);
   assert(isfinite(debug(side).speed));recovery_close();
  }
  assert(target[0]==target[1]);
 }
 puts("PASS recovery distance: fractional paused/scratch packet parity, signed source rates and extreme deltas");
}

#include "test_extra_player_epochs.h"
static void test_player_recovery(void) {
 test_recovery_pcm();test_recovery_timing();test_recovery_pressure();test_recovery_epochs();test_recovery_distance();
 test_recovery_enqueue_races();
}
