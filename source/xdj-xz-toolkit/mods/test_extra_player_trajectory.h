// SPDX-License-Identifier: GPL-2.0-only
/* Real FIFO -> player C -> accepted PCM. The oracle walks prescribed source
   endpoints and velocities without reading player-private trajectory state. */
#define TRAJECTORY_TEST_SEGMENTS 160u
static struct {
 double end[TRAJECTORY_TEST_SEGMENTS],rate[TRAJECTORY_TEST_SEGMENTS],begin[TRAJECTORY_TEST_SEGMENTS],at,target;
 int delta[TRAJECTORY_TEST_SEGMENTS];
 uint32_t arrival,interval;unsigned group_first;int arrival_valid;
 unsigned available,count,head,frames,bad,calls,max_calls,checks,failures,cases;
 uint64_t hash;
 int side,source_rate,trace,fault_stage,fault_left;
 xz_usb_result fault;
} tr;
static void tr_check(const char *name,int pass){
 tr.checks++;if(!pass){tr.failures++;printf("RED trajectory %s\n",name);}
}
/* Analytical path at an absolute OUTPUT sample time, from the externally
   prescribed signed displacements and one measured interval per arrival group.
   No player state, Q32 step rounding, or per-endpoint ceil in this oracle. */
static double tr_source_at(double time,unsigned *head){
 unsigned i=0;
 for(;i<tr.available;i++){
  double duration=fabs(tr.end[i]-tr.begin[i])/((double)tr.source_rate/XZ_USB_RATE*fabs(tr.rate[i]));
  if(time+.000001<duration){*head=i;return tr.begin[i]+(tr.end[i]-tr.begin[i])*(time/duration);}
  time-=duration;
 }
 *head=i;return i?tr.end[i-1]:tr.at;
}
static void tr_pcm(int side,const float *pcm,uint32_t frames,uint64_t after){
 if(!tr.trace||side!=tr.side||debug(side).state!=XZ_XP_SCRATCHING)return;
 for(unsigned n=0;n<frames;n++){
  unsigned leg;double at=tr_source_at(tr.frames+n,&leg);
  if(at>=tr.source_rate*2.)at=tr.source_rate*2.-1;
  double expected=.5*sin(6.283185307179586*440*at/tr.source_rate);
  for(int ch=0;ch<2;ch++){
   if(fabs(pcm[n*2+ch]-expected)>.001){
    if(!tr.bad)printf("TRACE PCM case=%u side=%d rate=%d sample=%u at=%.9f leg=%u actual=%.9f expected=%.9f\n",tr.cases,side,tr.source_rate,tr.frames+n,at,leg,pcm[n*2+ch],expected);
    tr.bad++;
   }
   unsigned char bytes[sizeof(float)];memcpy(bytes,pcm+n*2+ch,sizeof bytes);
   for(unsigned b=0;b<sizeof bytes;b++){tr.hash^=bytes[b];tr.hash*=1099511628211ULL;}
  }
 }
 tr.frames+=frames;tr.at=tr_source_at(tr.frames,&tr.head);
 if(fabs((double)after-tr.at)>1.000001){
  if(!tr.bad)printf("TRACE TAG case=%u side=%d rate=%d at=%.9f actual=%llu expected=%.0f\n",tr.cases,side,tr.source_rate,tr.at,(unsigned long long)after,floor(tr.at));
  tr.bad++;
 }
}
static xz_usb_result tr_fault(int side,int stage){
 tr.calls++;
 if(side==tr.side&&stage==tr.fault_stage&&tr.fault_left){
  if(tr.fault_left>0)tr.fault_left--;
  if(tr.fault==XZ_USB_STALE)f.queue_generation[side]++;
  return tr.fault;
 }
 return XZ_USB_OK;
}
static xz_usb_result tr_submit(void *ctx,uint32_t epoch,int side,uint32_t generation,const float *pcm,uint32_t frames,uint64_t after){
 xz_usb_result r=tr_fault(side,1);if(r!=XZ_USB_OK)return r;
 r=fake_submit(ctx,epoch,side,generation,pcm,frames,after);if(r==XZ_USB_OK)tr_pcm(side,pcm,frames,after);return r;
}
static xz_usb_result tr_transition(void *ctx,uint32_t epoch,int side,uint32_t expected,const float *pcm,uint32_t frames,uint64_t after,uint32_t *generation){
 xz_usb_result r=tr_fault(side,2);if(r!=XZ_USB_OK)return r;
 r=fake_transition(ctx,epoch,side,expected,pcm,frames,after,generation);if(r==XZ_USB_OK)tr_pcm(side,pcm,frames,after);return r;
}
static void tr_step(void){
 tr.available=tr.arrival_valid&&tr.arrival==clock_ms?tr.group_first:tr.count;
 tr.calls=0;step();if(tr.calls>tr.max_calls)tr.max_calls=tr.calls;
 tr_check("existing bounded sink-call budget",tr.calls<=18);
}
static int tr_clock_inject,tr_clock_inside,tr_clock_side,tr_clock_delta;
static void tr_add(int,uint32_t);
static uint32_t tr_clock_link(void *ctx){
 uint32_t link=fake_link(ctx);
 if(tr_clock_inject&&!tr_clock_inside&&stepping_side==tr_clock_side){
  tr_clock_inside=1;tr_clock_inject=0;clock_ms++;
  tr_add(tr_clock_delta,clock_ms);tr_clock_inside=0;
 }
 return link;
}
static void tr_setup(int side,int rate,int playing,int ms){
 tr.side=side;tr.source_rate=rate;tr.trace=tr.count=tr.head=tr.frames=tr.bad=tr.calls=tr.max_calls=0;
 tr.arrival_valid=0;tr.fault_left=0;tr.hash=1469598103934665603ULL;tr.cases++;
 struct xz_xp_env env={{NULL,tr_clock_link,tr_submit,fake_flush,fake_position,tr_transition},now_ms,resolve,master_bpm,NULL,NULL};
 clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(1900);step();load(side,rate==44100?4:2);seek(side,ms);
 press(side,XZ_EXTRA_IN_VINYL);if(playing)press(side,XZ_EXTRA_IN_PLAY);
 hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);tr.at=tr.target=(double)debug(side).position;tr.trace=1;
}
static void tr_add(int delta,uint32_t at){
 if(delta){
  if(!tr.arrival_valid||tr.arrival!=at){
   int32_t elapsed=(int32_t)(at-tr.arrival);
   tr.interval=tr.arrival_valid&&elapsed>0
    ?(elapsed>16?16u:(uint32_t)elapsed):16u;
   tr.arrival=at;tr.arrival_valid=1;tr.group_first=tr.count;
  }
  double target=fmin(tr.source_rate*2.,fmax(0,tr.target+(double)delta*tr.source_rate*XZ_XP_SEARCH_MS_PER_REV/(1000.*XZ_XP_JOG_TICKS_PER_REV)));
  if(target!=tr.target){
   assert(tr.count<TRAJECTORY_TEST_SEGMENTS);
   tr.begin[tr.count]=tr.target;tr.end[tr.count]=target;tr.delta[tr.count++]=delta;tr.target=target;
  }
  double ticks=0;for(unsigned i=tr.group_first;i<tr.count;i++)ticks+=fabs((double)tr.delta[i]);
  double speed=fmin(8.,ticks*XZ_XP_SEARCH_MS_PER_REV/(XZ_XP_JOG_TICKS_PER_REV*tr.interval));
  for(unsigned i=tr.group_first;i<tr.count;i++)tr.rate[i]=tr.delta[i]>0?speed:-speed;
 }
 clock_ms=at;unsigned calls=tr.calls;jog(tr.side,delta);
 tr_check("input only enqueues without PCM sink work",tr.calls==calls);
}
static void tr_tick(unsigned ms){
 while(ms--){clock_ms++;consume(0,44.1);consume(1,44.1);tr_step();}
}
static void tr_finish(unsigned ms){
 tr_tick(ms);
 tr_check("every accepted PCM sample follows its own signed interval",!tr.bad);
 tr_check("all prescribed trajectory segments rendered",tr.head==tr.count);
 tr_check("signed final target preserved",fabs((double)debug(tr.side).scratch_target-floor(tr.target+.00001))<1.);
 tr_check("final render position reaches target",fabs((double)debug(tr.side).position-floor(tr.target+.00001))<1.);
}
static void tr_close(void){tr.trace=0;xz_xp_destroy(p);p=NULL;}
static void test_trajectory_schedules(void){
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int sign=-1;sign<=1;sign+=2)for(int playing=0;playing<2;playing++){
  uint64_t hash=0;unsigned frames=0;
  for(int batch=0;batch<2;batch++){
   tr_setup(side,rate,playing,700);
   tr_add(sign,2000);tr_step();tr_tick(100);
   tr_add(sign*8,2100);if(!batch)tr_step();
   tr_add(sign,2101);tr_step();tr_finish(160);
   if(!batch){hash=tr.hash;frames=tr.frames;}
   else tr_check("interleaved/batched PCM bytes and frame counts identical",hash==tr.hash&&frames==tr.frames);
   tr_close();
  }
 }
 puts("PASS trajectory per-segment rate: both sides/rates/directions/prior transport, all PCM and byte-identical worker schedules");
}
static void test_trajectory_turns(void){
 const unsigned completed[]={0,256,706};
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int sign=-1;sign<=1;sign+=2)for(int playing=0;playing<2;playing++){
  uint64_t hash=0;unsigned frames=0;
  for(unsigned schedule=0;schedule<4;schedule++){
   tr_setup(side,rate,playing,700);uint32_t generation=debug(side).generation;
   tr_add(sign*8,2000);
   if(schedule){tr_step();consume(side,completed[schedule-1]);tr_step();}
   tr_add(-sign*8,2020);tr_step();tr_finish(160);
   tr_check("reversal preserves generation and source origin",debug(side).generation==generation);
   tr_check("balanced turn retains original source endpoint",debug(side).position==(uint64_t)rate*700/1000);
   if(!schedule){hash=tr.hash;frames=tr.frames;}
   else tr_check("reversal PCM independent of partial completion",hash==tr.hash&&frames==tr.frames);
   tr_close();
  }
 }
 puts("PASS trajectory reversals: queued, submitted, partially and fully completed, signed PCM and original endpoint");
}
static void test_trajectory_timestamps(void){
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int sign=-1;sign<=1;sign+=2)for(int batch=0;batch<2;batch++){
  tr_setup(side,rate,0,700);
  tr_add(sign,0);if(!batch)tr_step();
  tr_add(sign,0);if(!batch)tr_step();
  tr_add(-sign,0);if(!batch)tr_step();
  tr_add(0,0);tr_add(-sign,1);tr_step();tr_finish(100);tr_close();
  tr_setup(side,rate,0,700);
  tr_add(sign,UINT32_MAX-5);if(!batch)tr_step();
  tr_add(sign,4);if(!batch)tr_step();
  tr_add(-sign,5);tr_step();tr_finish(100);tr_close();
 }
 puts("PASS trajectory zero/tiny/wrapped timestamps, zero delta and equal-arrival signed batches");
}
static void test_trajectory_edges(void){
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int edge=0;edge<2;edge++){
  int sign=edge?1:-1;tr_setup(side,rate,0,edge?1990:10);
  tr_add(sign*8,2000);tr_add(-sign*8,2020);tr_step();tr_finish(100);tr_close();
 }
 puts("PASS trajectory clipped source edges turn continuously from the clamped endpoint");
}
static void test_trajectory_faults(void){
 const xz_usb_result faults[]={XZ_USB_BUSY,XZ_USB_AGAIN,XZ_USB_STALE};
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int stage=1;stage<=2;stage++)for(unsigned fault=0;fault<3;fault++){
  tr_setup(side,rate,0,700);tr.fault=faults[fault];tr.fault_stage=stage;tr.fault_left=1;
  if(stage==2){hold(side,XZ_EXTRA_IN_JOG_TOUCH,0);assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,1)));}
  tr_add(8,2000);tr_add(-8,2020);tr_step();tr_finish(180);tr_close();
 }
 /* A pending first block cannot hide a net-zero trajectory from TOP release. */
 for(int side=0;side<2;side++)for(int playing=0;playing<2;playing++){
  tr_setup(side,44100,playing,700);tr_add(8,2000);tr_add(-8,2020);
  clock_ms=2040;assert(input(event(side,XZ_EXTRA_IN_JOG_TOUCH,0)));tr_step();
  for(unsigned ms=0;ms<64&&debug(side).state==XZ_XP_SCRATCHING;ms++)tr_tick(1);
  tr_check("net-zero released trajectory produces both signed legs",fabs(tr.frames-32.*44.1)<1.00001&&!tr.bad&&tr.head==tr.count);
  tr_check("net-zero release restores prior state",debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
  tr_check("net-zero release completed source endpoint",view.deck[side].timeline.source_frame==30870);
  tr_close();
 }
 puts("PASS trajectory BUSY/AGAIN/STALE retry and net-zero release without dropped or duplicated PCM");
}
static void test_trajectory_capacity(void){
 for(int side=0;side<2;side++){
  tr_setup(side,44100,0,700);tr.fault_stage=1;tr.fault=XZ_USB_BUSY;tr.fault_left=-1;
  for(unsigned pass=0;pass<2;pass++){
   for(unsigned n=0;n<64;n++){
    int sign=n&1?-1:1;tr_add(sign,2000+pass*64+n);
   }
   tr_step();
  }
  tr.fault_left=0;tr_step();tr_finish(400);tr_close();
 }
 for(int side=0;side<2;side++)for(int playing=0;playing<2;playing++){
  tr_setup(side,44100,playing,700);tr.trace=0;
  tr.fault_stage=1;tr.fault=XZ_USB_BUSY;tr.fault_left=-1;
  /* Two 64-event drains exactly fill the bounded 128-segment trajectory. */
  for(unsigned pass=0;pass<2;pass++){
   for(unsigned n=0;n<XZ_XP_QUEUE;n++){clock_ms++;jog(side,n&1?-1:1);}
   tr_step();tr_check("full trajectory is retained before explicit overflow",!strstr(view.notice,"BUFFER FULL"));
  }
  clock_ms++;jog(side,1);tr_step();
  tr_check("overflow is explicit and held silent",strstr(view.notice,"SCRATCH BUFFER FULL / RETOUCH")&&debug(side).state==XZ_XP_SCRATCHING);
  tr_check("overflow cancels to verified completed source",debug(side).position==30870&&debug(side).scratch_target==30870);
  unsigned submits=f.submits[side];
  for(unsigned n=0;n<64;n++){clock_ms++;jog(side,n&1?-8:8);}tr.fault_left=0;tr_step();
  tr_check("overflow latch cannot resurrect canceled motion",f.submits[side]==submits&&debug(side).position==30870);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,0);
  tr_check("overflow release restores prior transport",debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
  if(playing)press(side,XZ_EXTRA_IN_PLAY);
  seek(side,700);hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);clock_ms++;jog(side,1);tr_step();
  clock_ms++;tr_step();tr_check("new contact recovers from bounded overflow",debug(side).position>30870);tr_close();
 }
 /* Distinct one-frame endpoints stress bounded work without the overflow path. */
 for(int side=0;side<2;side++){
  tr_setup(side,44100,0,700);tr.trace=0;tr.fault_stage=1;tr.fault=XZ_USB_AGAIN;tr.fault_left=-1;
  for(unsigned n=0;n<64;n++){clock_ms++;jog(side,n&1?-1:1);}tr_step();
  tr.fault_left=0;tr_step();tr_tick(300);
  tr_check("trajectory ring drains and reuses slots",debug(side).position==30870);
  for(unsigned n=0;n<64;n++){clock_ms++;jog(side,n&1?-1:1);}tr_step();tr_tick(600);
  tr_check("wrapped ring retains balanced displacement",debug(side).position==30870);tr_close();
 }
 puts("PASS trajectory exact capacity, explicit cancellation/re-touch, slot reuse and bounded worker sink calls");
}
static void test_trajectory_cancel(void){
 for(int side=0;side<2;side++)for(int mode=0;mode<4;mode++){
  tr_setup(side,44100,0,700);tr.trace=0;tr.fault_stage=1;tr.fault=XZ_USB_BUSY;tr.fault_left=-1;
  clock_ms=2000;jog(side,8);clock_ms=2020;jog(side,-8);tr_step();tr.fault_left=0;
  if(mode==0)seek(side,200);
  else if(mode==1)load(side,2);
  else if(mode==2){fake_route(0);tr_step();fake_route(1901);tr_step();}
  else press(side,XZ_EXTRA_IN_CUE);
  unsigned submits=f.submits[side];tr_tick(100);
  tr_check("transport/load/route cancellation cannot replay old trajectory",f.submits[side]==submits);
  tr_close();
 }
 puts("PASS trajectory seek/load/route/CUE cancellation has no stale motion resurrection");
}
/* The sink link refills the actual FIFO between removals. This exercises the
   worker's 67-control cap with an active producer, without a timing race. */
static int tr_refill,tr_refill_inside,tr_refill_side;
static unsigned tr_refill_count;
static uint32_t tr_refill_link(void *ctx){
 uint32_t route=fake_link(ctx);
 if(tr_refill&&!tr_refill_inside){
  tr_refill_inside=1;if(input(event(tr_refill_side,XZ_EXTRA_IN_SYNC,0)))tr_refill_count++;
  tr_refill_inside=0;
 }
 return route;
}
static void test_trajectory_fifo_budget(void){
 for(int side=0;side<2;side++){
  struct xz_xp_env env={{NULL,tr_refill_link,release_submit,fake_flush,fake_position,release_transition},now_ms,resolve,master_bpm,NULL,NULL};
  clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(1900);step();load(side,4);seek(side,700);press(side,XZ_EXTRA_IN_VINYL);
  release_side=side;release_from=30870;release_trace=release_frames=release_bad_pcm=0;release_fault_left=0;
  release_enqueue(side,4,0);for(unsigned i=3;i<64;i++)assert(input(event(side,XZ_EXTRA_IN_SYNC,0)));
  tr_refill_side=side;tr_refill_count=0;tr_refill=1;stepping_side=side;xz_xp_step(p,side);tr_refill=0;
  tr_check("continuously refilled FIFO yields after exactly 67 controls",tr_refill_count==67);
  tr_check("bounded control drain still renders accepted scratch PCM",release_frames==706&&!release_bad_pcm);
  release_drain(side,1);tr_check("continuous producer preserves completed release endpoint",debug(side).state==XZ_XP_CUED&&debug(side).position==31752);
  release_close();
 }
 puts("PASS trajectory continuous producer drains exactly 67 controls before valid PCM, both sides");
}
/* Duration regressions use an absolute output clock and closed-form source
   paths. They do not prescribe one block or a rounded frame count per leg. */
static struct {
 unsigned frames,groups,available,mode,bad,calls,max_calls,accepted;
 uint32_t at;
 int side,rate,sign,linear;double origin;
} dt;
static double dt_source_at(double frame){
 double ms=frame*1000./XZ_USB_RATE;
 double total=dt.mode?dt.available*16.:dt.available?15.+dt.available:0.;
 if(ms>total)ms=total;
 unsigned group;double fraction;
 if(!dt.mode&&ms<16.){group=0;fraction=ms/16.;}
 else{
  double t=dt.mode?ms/16.:ms-15.;group=(unsigned)floor(t);fraction=t-group;
 }
 if(group>=dt.available){group=dt.available?dt.available-1:0;fraction=1.;}
 double counts;
 if(dt.linear)counts=group+fraction;
 else if(dt.mode==2)counts=fraction<=.5?fraction*8.:(1.-fraction)*8.;
 else{
  unsigned half=dt.mode?4:32,cycle=group%(2*half);
  double before=cycle<=half?cycle:2*half-cycle;
  counts=(before+(cycle<half?fraction:-fraction))*(dt.mode?8.:1.);
 }
 return dt.origin+dt.sign*counts*dt.rate*XZ_XP_SEARCH_MS_PER_REV/(1000.*XZ_XP_JOG_TICKS_PER_REV);
}
static void dt_pcm(int side,const float *pcm,unsigned frames,uint64_t after){
 if(side!=dt.side||debug(side).state!=XZ_XP_SCRATCHING)return;
 for(unsigned n=0;n<frames;n++){
  double at=dt_source_at(dt.frames+n);
  double expected=.5*sin(6.283185307179586*440*at/dt.rate);
  for(unsigned ch=0;ch<2;ch++)if(fabs(pcm[n*2+ch]-expected)>.001)dt.bad++;
 }
 dt.frames+=frames;
 if(fabs((double)after-dt_source_at(dt.frames))>1.000001)dt.bad++;
}
static xz_usb_result dt_submit(void *ctx,uint32_t epoch,int side,uint32_t generation,const float *pcm,uint32_t n,uint64_t after){
 dt.calls++;xz_usb_result r=fake_submit(ctx,epoch,side,generation,pcm,n,after);
 if(r==XZ_USB_OK)dt_pcm(side,pcm,n,after);return r;
}
static xz_usb_result dt_transition(void *ctx,uint32_t epoch,int side,uint32_t generation,const float *pcm,uint32_t n,uint64_t after,uint32_t *next){
 dt.calls++;xz_usb_result r=fake_transition(ctx,epoch,side,generation,pcm,n,after,next);
 if(r==XZ_USB_OK)dt_pcm(side,pcm,n,after);return r;
}
static void dt_step(void){dt.available=dt.groups-(dt.groups&&dt.at==clock_ms);dt.calls=0;step();if(dt.calls>dt.max_calls)dt.max_calls=dt.calls;}
static void dt_setup(int side,int rate,int sign,int playing){
 memset(&dt,0,sizeof dt);dt.side=side;dt.rate=rate;dt.sign=sign;
 struct xz_xp_env env={{NULL,fake_link,dt_submit,fake_flush,fake_position,dt_transition},now_ms,resolve,master_bpm,NULL,NULL};
 clock_ms=1000;p=xz_xp_create(&env);assert(p);fake_route(1977);step();load(side,rate==44100?4:2);seek(side,700);
 press(side,XZ_EXTRA_IN_VINYL);if(playing)press(side,XZ_EXTRA_IN_PLAY);
 hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);dt.origin=debug(side).position;
}
static void test_duration_pressure(void){
 for(int rate=44100;rate<=48000;rate+=3900)for(int sign=-1;sign<=1;sign+=2)
 for(int playing=0;playing<2;playing++)for(unsigned mode=0;mode<3;mode++){
  int side=playing;dt_setup(side,rate,sign,playing);dt.mode=mode;
  unsigned interval=mode?16:1,groups=24000/interval;
  clock_ms=2000;unsigned first_full=0;
  for(unsigned g=0;g<groups;g++){
   unsigned packets=mode?8:1;
   for(unsigned i=0;i<packets;i++){
    int direction=mode==2?(i<4?1:-1):mode==1?(g%8<4?1:-1):(g%64<32?1:-1);
    struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_JOG_TURN,1);e.jog.delta=sign*direction;
    dt.accepted+=input(e);
   }
   dt.at=clock_ms;dt.groups++;
   for(unsigned ms=0;ms<interval;ms++){
    /* Actual player worker sleeps 2 ms. No faults or sink starvation. */
    if(!(clock_ms&1))dt_step();
    if(!first_full&&strstr(view.notice,"BUFFER FULL"))first_full=g+1;
    clock_ms++;consume(0,44.1);consume(1,44.1);
   }
  }
  for(unsigned ms=0;ms<100;ms++){clock_ms++;consume(0,44.1);consume(1,44.1);if(!(clock_ms&1))dt_step();}
  dt_step();double duration=(mode?groups*16.:groups+15.)*XZ_USB_RATE/1000.;
  printf("DURATION24 rate=%d sign=%d playing=%d mode=%u worker_ms=2 accepted=%u frames=%u expected=%.6f bad_pcm_tags=%u first_full=%u calls=%u final=%llu\n",rate,sign,playing,mode,dt.accepted,dt.frames,duration,dt.bad,first_full,dt.max_calls,(unsigned long long)debug(side).position);
  tr_check("24s ordinary/bursty/mixed input is all accepted",dt.accepted==groups*(mode?8:1));
  tr_check("24s ordinary/bursty/mixed path never overflows",!first_full);
  tr_check("24s conserved total duration within one output sample",fabs(dt.frames-duration)<1.00001);
  tr_check("24s every accepted PCM sample and source-after tag follows output time",!dt.bad);
  tr_check("24s sink-call budget stays bounded",dt.max_calls<=17);
  tr_check("24s completion reaches prescribed path endpoint",fabs((double)completed_tag(side)-dt_source_at(duration))<1.1);
  xz_xp_destroy(p);p=NULL;
 }
 puts("PASS duration 24s: ordinary 1ms, same-time bursts, mixed equal-time legs, 2ms worker, both source rates/signs/states, every PCM sample/tag");
}
static void test_duration_first_progress(void){
 const uint32_t at[]={0,UINT32_MAX,2000};
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int sign=-1;sign<=1;sign+=2)for(int playing=0;playing<2;playing++)for(unsigned t=0;t<3;t++){
  tr_setup(side,rate,playing,700);tr_add(sign*8,at[t]);tr_step();
  tr_check("unsealed same-clock input has no speculative PCM",tr.frames==0);
  /* No new input or sleep. The first later-clock worker pass must progress. */
  clock_ms=at[t]+1;tr_step();
  tr_check("first later-clock worker produces signed PCM without new input",tr.frames>=32&&!tr.bad);
  tr_finish(30);tr_close();
 }
 puts("PASS duration first progress: first later-clock pass, no input/sleep wait, clock0/wrap, both sides/rates/signs/resume states");
}
/* The producer advances its clock and enqueues during link(), after the
   worker captured now. A merely different timestamp may still be in its future. */
static void test_duration_worker_clock(void){
 const uint32_t before[]={0,UINT32_MAX,2000};
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int sign=-1;sign<=1;sign+=2)for(int playing=0;playing<2;playing++)for(unsigned t=0;t<3;t++){
  tr_setup(side,rate,playing,700);clock_ms=before[t];
  tr_clock_side=side;tr_clock_delta=sign*4;tr_clock_inject=1;tr_step();
  tr_check("older worker clock cannot seal a newly captured future timestamp",!tr.frames);
  tr_add(sign*4,clock_ms);tr_step();
  tr_check("later same-arrival packet still joins unsealed group",!tr.frames);
  clock_ms++;tr_step();
  tr_check("first genuinely later worker seals entire shared interval",tr.frames==706&&!tr.bad);
  tr_finish(30);tr_close();
 }
 puts("PASS duration worker older clock: real FIFO producer during pass, clock0/wrap, shared interval and signed PCM, both sides/rates/states");
}
static void test_duration_splits(void){
 for(int rate=44100;rate<=48000;rate+=3900)for(int sign=-1;sign<=1;sign+=2)
 for(int playing=0;playing<2;playing++)for(int mixed=0;mixed<2;mixed++){
  uint64_t hash=0;unsigned frames=0;
  unsigned leg_counts=XZ_XP_JOG_TICKS_PER_REV==360?8:64;
  for(unsigned packets=1;packets<=leg_counts;packets*=2){
   tr_setup(playing,rate,playing,700);
   unsigned legs=mixed?2:1;
   for(unsigned leg=0;leg<legs;leg++)for(unsigned n=0;n<packets;n++){
    tr_add(sign*(leg?-1:1)*(int)leg_counts/(int)packets,2000);tr_step();
   }
   /* The 6600 profile covers all seven splits through 64 packets. */
   clock_ms=2001;tr_step();tr_finish(60);
   tr_check("shared equal-time group has one 16ms output interval",fabs(tr.frames-705.6)<1.00001);
   if(packets==1){hash=tr.hash;frames=tr.frames;}
   else tr_check("causally complete packet splits preserve exact PCM and duration",hash==tr.hash&&frames==tr.frames);
   tr_close();
  }
 }
 puts("PASS duration packet splitting: one shared interval, exact PCM bytes, equal-time ordered reversals");
}

/* A motion arriving after quiet time starts a new audible stroke. Its elapsed
   idle time cannot be placed on the future output clock. This independent
   oracle derives every signed endpoint from input counts and every sample
   duration from the documented onset/cadence rule. */
static void test_trajectory_causal_quiet(void){
 const unsigned gaps[]={2,20,200,500};
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int playing=0;playing<2;playing++)for(int sign=-1;sign<=1;sign+=2)
 for(unsigned g=0;g<sizeof gaps/sizeof gaps[0];g++){
  tr_setup(side,rate,playing,700);
  tr_add(sign*8,2000);tr_step();tr_tick(gaps[g]);
  for(unsigned n=0;n<120;n++){
   int turn=sign*(n%7==6?-1:1)*(n%3==0?2:1);
   tr_add(turn,clock_ms);tr_step();tr_tick(1);
  }
  tr_finish(180);
  double onset=gaps[g]>16?16:gaps[g];
  double duration=16.+fmax(onset,2.*XZ_XP_SEARCH_MS_PER_REV/(8.*XZ_XP_JOG_TICKS_PER_REV));
  for(unsigned n=1;n<120;n++){
   double counts=n%3==0?2.:1.;
   duration+=fmax(1.,counts*XZ_XP_SEARCH_MS_PER_REV/(8.*XZ_XP_JOG_TICKS_PER_REV));
  }
  double expected=duration*XZ_USB_RATE/1000.;
  tr_check("causal quiet interval has one audible onset, then exact 1ms cadence",fabs(tr.frames-expected)<1.00001);
  tr_check("healthy variable-velocity signed path never overflows",!strstr(view.notice,"BUFFER FULL"));
  tr_close();
 }
 puts("PASS causal quiet: short/long gaps, changing 1/2-count velocity, ordered reversals, both sides/rates/transport states, exact PCM/tags");
}
static int test_player_trajectory(void){
 tr.checks=tr.failures=tr.cases=0;
 test_trajectory_schedules();test_trajectory_turns();test_trajectory_timestamps();test_trajectory_edges();
 test_trajectory_faults();test_trajectory_capacity();test_trajectory_cancel();test_trajectory_fifo_budget();
 test_trajectory_causal_quiet();test_duration_first_progress();test_duration_worker_clock();test_duration_splits();test_duration_pressure();
 printf("RESULT trajectory cases=%u checks=%u failures=%u\n",tr.cases,tr.checks,tr.failures);
 return tr.failures==0;
}
