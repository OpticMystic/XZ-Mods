#define main retained_main
#include "test_extra_player.c"
#undef main
static unsigned checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;if(failed<12)printf("FAIL line=%d %s\n",__LINE__,#x);}}while(0)
static int mode_bpm,variable,injected_bpm=-1;
static unsigned grid_ms[40],grid_bpms[40];
static int own_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=sync_resolve(source,id,out);out->track_bpm_x100=12500;return rc;
}
static void grid_file(void){
 file_start(&dat);uint32_t s=open_section(&dat,"PQTZ",24);
 u32(&dat,0);u32(&dat,0x80000);u32(&dat,40);
 double at=100.;
 for(unsigned i=0;i<40;i++){
  unsigned bpm=variable&&at>=800.?12600:(unsigned)mode_bpm;
  grid_ms[i]=(unsigned)llround(at);grid_bpms[i]=bpm;
  u16(&dat,i%4+1);u16(&dat,injected_bpm>=0?(unsigned)injected_bpm:bpm);u32(&dat,(uint32_t)llround(at));at+=6000000./bpm;
 }
 close_section(&dat,s);file_end(&dat);write_file("ANLZ0000.DAT",&dat);
}
static void one(int bpm,int var,int quant,int sync){
 mode_bpm=bpm;variable=var;build_analysis();grid_file();clock_ms=1000;wire_horizon=882;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,own_resolve,master_bpm,NULL,NULL,0,quant};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_sim_native);fake_route(6000+checks);step();
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;
 sim_native.bpm=bpm/100.;sim_native.source_revision=28;sim_native.track_identity=0x280001;
 sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 for(int d=0;d<2;d++){sync_loop_track(d,d+1,350);if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);if(sync==1)press(d,XZ_EXTRA_IN_SYNC);}
 if(sync==2){press(0,XZ_EXTRA_IN_MASTER);advance(40);press(1,XZ_EXTRA_IN_SYNC);}
 int tmin[2]={99999,99999},tmax[2]={-99999,-99999},bmin[2]={99999,99999},bmax[2]={0,0};
 double smin[2]={99,99},smax[2]={0,0},cmin[2]={99,99},cmax[2]={0,0};unsigned trims=0,completed[2]={0,0};
 for(unsigned i=0;i<400;i++){
  advance(10);
  for(int d=0;d<2;d++){
   int t=view.deck[d].tempo_x100,b=view.deck[d].bpm_x100;double speed_now=debug(d).speed;
   if(t<tmin[d])tmin[d]=t;if(t>tmax[d])tmax[d]=t;if(b<bmin[d])bmin[d]=b;if(b>bmax[d])bmax[d]=b;
   if(speed_now<smin[d])smin[d]=speed_now;if(speed_now>smax[d])smax[d]=speed_now;
   if(view.deck[d].timeline.flags&XZ_EXTRA_TIMELINE_APPLIED_RATE){
    double cr=view.deck[d].timeline.completed_rate;completed[d]++;
    if(cr<cmin[d])cmin[d]=cr;if(cr>cmax[d])cmax[d]=cr;
    if(!var&&!quant&&i>40)CHECK(fabs(cr-1.)<1e-6);
   }
   if(view.deck[d].sync.correction_ppm)trims++;
   if(!var){CHECK(t==0);CHECK(b==bpm);CHECK(view.deck[d].track_bpm_x100==bpm);if(!quant)CHECK(fabs(speed_now-1.)<1e-6);}
   if(var){
    unsigned index=0;while(index+1<40&&grid_ms[index+1]<=(unsigned)view.deck[d].elapsed_ms)index++;
    int expected=grid_bpms[index];CHECK(view.deck[d].track_bpm_x100==expected);
    if(!sync)CHECK(b==expected);
    if(sync&&i>30){CHECK(abs(t-(int)llround((12000./expected-1.)*10000.))<=1);CHECK(abs(b-12000)<=1);}
   }
  }
 }
 for(int d=0;d<2;d++)printf("CASE bpm=%d variable=%d quantize=%d sync=%d deck=%d tempo=[%d,%d] bpm=[%d,%d] source_speed=[%.9f,%.9f] completed_rate=[%.9f,%.9f] completed_samples=%u trim_samples=%u reason=%d\n",bpm,var,quant,sync,d+3,tmin[d],tmax[d],bmin[d],bmax[d],smin[d],smax[d],cmin[d],cmax[d],completed[d],trims,view.deck[d].sync.reason);
 for(int d=0;d<2;d++)CHECK(completed[d]>100);
 xz_xp_destroy(p);p=NULL;
}

static void parser_fallback(void){
 build_analysis();file_start(&dat);uint32_t sec=open_section(&dat,"PQTZ",24);
 unsigned values[]={0,1,1999,60001,65535,17200};
 u32(&dat,0);u32(&dat,0x80000);u32(&dat,6);
 for(unsigned i=0;i<6;i++){u16(&dat,i%4+1);u16(&dat,values[i]);u32(&dat,100+500*i);}
 close_section(&dat,sec);file_end(&dat);
 struct xz_extra_beat *grid=NULL;uint32_t count=0;int32_t bpm=99;
 CHECK(xz_anlz_beat_grid(dat.b,dat.n,&grid,&count,&bpm)==0);CHECK(count==6);CHECK(bpm==0);
 for(unsigned i=0;i<6;i++){CHECK(grid[i].ms==(int)(100+500*i));CHECK(grid[i].bpm_x100==(i==5?17200:0));}
 free(grid);
 CHECK(xz_anlz_beat_grid(dat.b,12,&grid,&count,&bpm)==-1);CHECK(!grid&&!count&&!bpm);
 puts("PASS parser missing/malformed per-beat BPM initialized to zero, valid tempo retained, truncated file refused");
}
static int wait_mode;
static int wait_clock(struct xz_sync_native_snapshot *out){
 int rc=copy_sim_native(out);
 if(wait_mode==1){out->reason=XZ_SYNC_NATIVE_CLOCK_UNAVAILABLE;out->available=0;return 0;}
 if(wait_mode==2)out->phase_available=0;
 return rc;
}
static void wait_paths(void){
 build_analysis();clock_ms=1000;wire_horizon=882;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,1};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,wait_clock);fake_route(9900);step();
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.bpm=120.;sim_native.source_revision=28;sim_native.track_identity=0x280001;sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 wait_mode=1;
 for(int d=0;d<2;d++){sync_loop_track(d,d+1,350);if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);press(d,XZ_EXTRA_IN_SYNC);}
 advance(100);
 for(int d=0;d<2;d++){CHECK(view.deck[d].sync.requested);CHECK(!view.deck[d].sync.locked);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);}
 wait_mode=2;advance(300);
 for(int d=0;d<2;d++){CHECK(!view.deck[d].sync.locked);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE);CHECK(view.deck[d].tempo_x100==0);}
 wait_mode=0;sim_native.manual=1;advance(100);
 for(int d=0;d<2;d++)CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_MANUAL);
 sim_native.manual=0;wait_mode=1;
 for(int d=0;d<2;d++)press(d,XZ_EXTRA_IN_PLAY);
 advance(100);
 for(int d=0;d<2;d++){CHECK(!view.deck[d].playing);CHECK(view.deck[d].sync.requested);CHECK(!view.deck[d].sync.locked);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);}
 xz_xp_destroy(p);p=NULL;
 puts("PASS own-C WAIT_MASTER on unavailable clock, NATIVE_PHASE_UNAVAILABLE on tempo-only clock, MANUAL, paused requested unlocked WAIT_MASTER");
}
int retained_grid_main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);
 printf("LAYOUT size=%zu align=%zu ms=%zu beat=%zu bpm=%zu\n",sizeof(struct xz_extra_beat),_Alignof(struct xz_extra_beat),offsetof(struct xz_extra_beat,ms),offsetof(struct xz_extra_beat,beat_in_bar),offsetof(struct xz_extra_beat,bpm_x100));
 CHECK(sizeof(struct xz_extra_beat)==8);CHECK(offsetof(struct xz_extra_beat,ms)==0);CHECK(offsetof(struct xz_extra_beat,beat_in_bar)==4);CHECK(offsetof(struct xz_extra_beat,bpm_x100)==6);
 parser_fallback();wait_paths();
 const int bpms[]={12000,17200,12345};
 for(unsigned i=0;i<3;i++){one(bpms[i],0,0,0);one(bpms[i],0,0,1);one(bpms[i],0,1,1);one(bpms[i],0,1,2);}
 injected_bpm=0;one(12000,0,0,1);injected_bpm=1;one(12000,0,0,1);injected_bpm=65535;one(12000,0,0,1);injected_bpm=-1;
 one(12000,1,0,0);one(12000,1,0,1);
 printf("RESULT checks=%u failures=%u\n",checks,failed);return failed?1:0;
}

#include "deck_select.h"
static int exact_clock(struct xz_sync_native_snapshot *out){
 int rc=copy_sim_native(out);out->nominal_bpm=(float)sim_native.bpm;out->speed=1.f;return rc;
}
static struct xz_deck_select selector;
static struct xz_master_observation actual;
static unsigned cases;
static int observation(struct xz_master_observation *out){*out=actual;out->observed_ms=clock_ms;return 1;}
static void feedback(void){view.route_up=1;xz_deck_select_feedback(&selector,&view);}
static enum xz_ds_verdict physical(int d,float value){
 feedback();struct xz_ds_key k={.side=d,.code=XZ_DS_KEY_TEMPO,.op=XZ_DS_OP_FADER,.value=value};
 struct xz_ds_outcome o=xz_deck_select_global_player(&selector,&k,clock_ms);
 if(o.verdict==XZ_DS_DELIVER)CHECK(input(o.event));step();feedback();return o.verdict;
}
static void button(int d,uint16_t code){
 feedback();struct xz_ds_key k={.side=d,.code=code,.op=0};
 struct xz_ds_outcome o=code==0x4312?xz_deck_select_global_key(&selector,d+1,code,0,clock_ms):xz_deck_select_global_player(&selector,&k,clock_ms);
 CHECK(o.verdict==XZ_DS_DELIVER);CHECK(o.event.deck==d);CHECK(input(o.event));step();
 k.op=2;o=code==0x4312?xz_deck_select_global_key(&selector,d+1,code,2,clock_ms):xz_deck_select_global_player(&selector,&k,clock_ms);if(o.verdict==XZ_DS_DELIVER)CHECK(input(o.event));step();feedback();
}
static void setup(int quant,int range,int same){
 cases++;mode_bpm=17400;variable=0;injected_bpm=-1;build_analysis();grid_file();clock_ms=1000;wire_horizon=882;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 actual=(struct xz_master_observation){.deck=1,.revision=1,.operator_epoch=1};
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,own_resolve,master_bpm,NULL,NULL,0,quant};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,exact_clock);xz_xp_set_master_observer(p,observation);fake_route(8800+cases);step();
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.native_deck=1;
 sim_native.bpm=150.;sim_native.source_revision=3;sim_native.track_identity=0x300001;sim_native.uncertainty_us=536;sim_native.obs_age_us=2000;
 sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 xz_deck_select_init(&selector);xz_deck_select_route_instance(&selector,1,fake_link(NULL));
 for(int d=0;d<2;d++){
  sync_loop_track(d,same?1:d+1,350);if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);
  for(int i=0;i<4&&view.deck[d].tempo_range!=range;i++)press(d,XZ_EXTRA_IN_TEMPO_RANGE);
  CHECK(view.deck[d].tempo_range==range);xz_deck_select_toggle(&selector,d,NULL);physical(d,0);
 }
}
static void rate(int d,double wanted){
 double nominal=debug(d).speed-view.deck[d].sync.correction_ppm/1000000.;
 CHECK(fabs(nominal-wanted)<.0000011);CHECK(abs(view.deck[d].tempo_x100-(int)llround((wanted-1)*10000))<=1);
}
static void completed(int d,double wanted,int quant){
 unsigned n=0;double lo=99,hi=0;uint64_t before=f.position.deck[d].source_completed_wire_frame;
 for(int i=0;i<50;i++){
  advance(10);rate(d,wanted);CHECK(view.deck[d].sync.reason!=XZ_EXTRA_SYNC_OUT_OF_RANGE);
  if(i>15&&(view.deck[d].timeline.flags&XZ_EXTRA_TIMELINE_APPLIED_RATE)){
   double r=view.deck[d].timeline.completed_rate;n++;if(r<lo)lo=r;if(r>hi)hi=r;
   CHECK(fabs(r-wanted)<(quant?.011*wanted:1e-6));
  }
 }
 CHECK(n>25);CHECK(f.position.deck[d].source_completed_wire_frame>before);
 printf("PCM case=%u deck=%d Hz=%u quant=%d nominal=%.12f completed=[%.12f,%.12f] samples=%u range=%d reason=%d\n",cases,d+3,debug(d).source_rate,quant,wanted,lo,hi,n,view.deck[d].tempo_range,view.deck[d].sync.reason);
}
static void promote(int d){
 button(d,0x4312);
 const struct xz_extra_timeline *t=&view.deck[d].timeline;
 actual=(struct xz_master_observation){.deck=d+2,.revision=actual.revision+1,.operator_epoch=actual.operator_epoch,
  .request_epoch=view.master_intent.epoch,.lease_generation=7,.route_epoch=t->route_epoch,
  .load_revision=t->load_revision,.transport_generation=t->transport_generation};step();feedback();CHECK(view.deck[d].master);
}
static void direct_matrix(void){
 int ranges[]={6,10,16};
 for(int q=0;q<2;q++)for(int r=0;r<3;r++)for(int same=0;same<2;same++){
  setup(q,ranges[r],same);for(int d=0;d<2;d++)button(d,0x4313);advance(200);
  for(int d=0;d<2;d++){rate(d,150./174.);CHECK(view.deck[d].tempo_range==ranges[r]);completed(d,150./174.,q);}
  sim_native_set_bpm(140.);advance(200);for(int d=0;d<2;d++){rate(d,140./174.);completed(d,140./174.,q);CHECK(view.deck[d].tempo_range==ranges[r]);}
  xz_xp_destroy(p);p=NULL;
 }
}
static void takeover_matrix(void){
 for(int d=0;d<2;d++)for(int promotion=0;promotion<2;promotion++)for(int sw=0;sw<4;sw++){
  setup(0,10,0);button(d,0x4313);advance(200);feedback();rate(d,150./174.);
  CHECK(selector.side[d].held[XZ_DS_EXTRA]< -XZ_EXTRA_TEMPO_SPAN);
  CHECK(selector.side[d].extra_revision==view.deck[d].tempo_revision);
  physical(d,-1.f);if(sw==2)xz_deck_select_toggle(&selector,d,NULL);
  if(promotion){if(sw==2)xz_deck_select_toggle(&selector,d,NULL);promote(d);if(sw==2)xz_deck_select_toggle(&selector,d,NULL);}
  else{press(d,XZ_EXTRA_IN_SYNC);feedback();}
  CHECK(selector.side[d].fresh_pickup[XZ_DS_EXTRA]);
  if(sw==1||sw==3)xz_deck_select_toggle(&selector,d,NULL);
  if(sw==3){physical(d,.9f);physical(d,-.3f);}
  if(sw)xz_deck_select_toggle(&selector,d,NULL);
  CHECK(physical(d,-1.f)==XZ_DS_SWALLOW);CHECK(physical(d,1.f)==XZ_DS_SWALLOW);CHECK(physical(d,-1.f)==XZ_DS_SWALLOW);
  advance(200);rate(d,150./174.);CHECK(view.deck[d].tempo_range==10);
  uint32_t rev=view.deck[d].tempo_revision;unsigned gen=debug(d).generation;
  press(d,XZ_EXTRA_IN_TEMPO_RANGE);feedback();CHECK(view.deck[d].tempo_range==16);CHECK(view.deck[d].tempo_revision!=rev);
  CHECK(debug(d).generation==gen);rate(d,150./174.);CHECK(selector.side[d].pickup.require_motion);
  CHECK(physical(d,-1.f)==XZ_DS_SWALLOW);rate(d,150./174.);
  completed(d,150./174.,0);CHECK(physical(d,-.5f)==XZ_DS_DELIVER);CHECK(fabs(debug(d).speed-.92)<1e-10);
  CHECK(!selector.side[d].fresh_pickup[XZ_DS_EXTRA]);
  press(d,XZ_EXTRA_IN_TEMPO_RANGE);CHECK(view.deck[d].tempo_range==100);CHECK(fabs(debug(d).speed-.5)<1e-10);
  CHECK(fabs(debug(1-d).speed-1.)<1e-10);xz_xp_destroy(p);p=NULL;
 }
}
static void range_remap_and_engine(void){
 for(int d=0;d<2;d++){
  setup(0,10,0);button(d,0x4313);advance(200);button(d,0x4313);rate(d,150./174.);
  int ranges[]={16,100,6,10,16};
  for(unsigned i=0;i<5;i++){
   uint32_t rev=view.deck[d].tempo_revision;press(d,XZ_EXTRA_IN_TEMPO_RANGE);feedback();
   CHECK(view.deck[d].tempo_range==ranges[i]);CHECK(view.deck[d].tempo_revision!=rev);rate(d,150./174.);
   CHECK(selector.side[d].fresh_pickup[XZ_DS_EXTRA]);CHECK(physical(d,0)==XZ_DS_SWALLOW);rate(d,150./174.);
  }
  button(d,0x4313);sim_native_set_bpm(348.);advance(200);rate(d,2.);completed(d,2.,0);
  sim_native_set_bpm(350.);advance(200);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_OUT_OF_RANGE);rate(d,2.);
  printf("ENGINE deck=%d nominal2 accepted; 350/174 refused, prior exact rate held; manual range=%d\n",d+3,view.deck[d].tempo_range);
  xz_xp_destroy(p);p=NULL;
 }
}
static void native_gap(void){
 for(int d=0;d<2;d++)for(int q=0;q<2;q++){
  setup(q,10,0);button(d,0x4313);advance(1200);rate(d,150./174.);
  unsigned gen=debug(d).generation;
  /* Keep the fake sink healthy but leave a >150ms native observation gap. */
  clock_ms+=200;consume(0,441);consume(1,441);sim_native_set_bpm(140.);step();
  CHECK(debug(d).generation==gen);rate(d,140./174.);CHECK(view.deck[d].sync.reason!=XZ_EXTRA_SYNC_OUT_OF_RANGE);
  printf("GAP deck=%d quant=%d reason=%d rate=%.12f generation=%u\n",d+3,q,view.deck[d].sync.reason,debug(d).speed,gen);
  advance(2500);rate(d,140./174.);completed(d,140./174.,q);
  CHECK(view.deck[d].sync.reason==(q?XZ_EXTRA_SYNC_LOCKED:XZ_EXTRA_SYNC_TEMPO_FOLLOW));
  if(q){CHECK(view.deck[d].sync.locked);CHECK(abs(view.deck[d].sync.phase_error_us)<=1000);}
  xz_xp_destroy(p);p=NULL;
 }
}
static void software_matrix(void){
 for(int d=0;d<2;d++)for(int q=0;q<2;q++){
  setup(q,10,0);button(d,0x4313);advance(200);promote(d);button(1-d,0x4313);advance(300);
  rate(d,150./174.);completed(1-d,150./174.,q);CHECK(view.deck[1-d].sync.master_scope&XZ_EXTRA_MASTER_SCOPE_SOFTWARE);
  press(d,XZ_EXTRA_IN_TEMPO_RANGE);rate(d,150./174.);tempo(d,-10000);advance(300);
  rate(d,.84);completed(1-d,.84,q);CHECK(view.deck[1-d].tempo_range==10);xz_xp_destroy(p);p=NULL;
 }
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);
 direct_matrix();takeover_matrix();software_matrix();range_remap_and_engine();native_gap();
 pthread_mutex_destroy(&f.lock);printf("RESULT sync-range-r3 cases=%u checks=%u failures=%u\n",cases,checks,failed);return failed?1:0;
}
