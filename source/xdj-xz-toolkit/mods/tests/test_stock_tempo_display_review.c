#define main retained_main
#include "test_extra_player.c"
#undef main
static unsigned checks,failed,cases;static int jitter,no_phase,no_grid;
#define CHECK(x) do{checks++;if(!(x)){failed++;printf("FAIL case=%u line=%d %s\n",cases,__LINE__,#x);}}while(0)
static int mismatch_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=sync_resolve(source,id,out);out->track_bpm_x100=12500;if(no_grid)out->anlz_dir[0]=0;return rc;
}
static int native_clock(struct xz_sync_native_snapshot *out){
 int rc=copy_sim_native(out);if(jitter)out->bar_phase=(float)((clock_ms/10u)%2u?.1:.48);
 if(no_phase)out->phase_available=0;return rc;
}
static void one(int mode,int missing,int source){
 cases++;no_grid=missing;no_phase=mode==1;jitter=0;wire_horizon=882;clock_ms=1000;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,mismatch_resolve,master_bpm,NULL,NULL,0,mode!=2};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,native_clock);fake_route(4000+cases);step();
 sync_loop_track(0,(uint32_t)source,350);
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.bpm=120.;sim_native.source_revision=28;sim_native.track_identity=0x280001;
 sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 press(0,XZ_EXTRA_IN_SYNC);advance(2200);
 if(mode==0&&!missing){CHECK(view.deck[0].sync.locked);CHECK(view.deck[0].tempo_x100==0);}
 unsigned generation=debug(0).generation;
 if(mode==0)jitter=1;
 advance(800);
 int tempo=missing?-400:0;double rate=missing?.96:1.;
 printf("CASE mode=%s grid=%s rate=%u metadata125 native120 tempo=%d speed=%.8f reason=%d\n",mode==0?"phase-wait":mode==1?"no-phase":"quantize-off",missing?"missing":"120",debug(0).source_rate,view.deck[0].tempo_x100,debug(0).speed,view.deck[0].sync.reason);
 CHECK(view.deck[0].tempo_x100==tempo);CHECK(fabs(debug(0).speed-rate)<.0001);CHECK(view.deck[0].bpm_x100==12000);
 printf("PUBLISHED source=%d effective=%d expected=12000\n",view.deck[0].track_bpm_x100,view.deck[0].bpm_x100);
 CHECK(debug(0).generation==generation);CHECK(!view.deck[0].sync.locked);
 if(mode==1)CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE);
 if(mode==2)CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_TEMPO_FOLLOW);
 /* Keep following tempo changes while phase is deliberately unavailable. */
 const double tempos[]={126.,123.,120.};
 for(unsigned ti=0;ti<sizeof tempos/sizeof tempos[0];ti++){
  sim_native_set_bpm(tempos[ti]);advance(300);
  double expected=tempos[ti]/(missing?125.:120.);
  CHECK(view.deck[0].tempo_x100==(int32_t)llround((expected-1.)*10000.));
  CHECK(fabs(debug(0).speed-expected)<.0001);CHECK(view.deck[0].bpm_x100==(int32_t)llround(tempos[ti]*100.));
  CHECK(!view.deck[0].sync.locked);CHECK(debug(0).generation==generation);
 }
 if(mode==0&&!missing){jitter=0;advance(3000);CHECK(view.deck[0].sync.locked);CHECK(view.deck[0].tempo_x100==0);}
 xz_xp_destroy(p);p=NULL;
}
static int software_variant;
static int software_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=sync_resolve(source,id,out);if(rc)return rc;
 out->track_bpm_x100=(id==1?(software_variant==1?12000:12500):(software_variant==0?12000:12500));
 if((id==1&&software_variant==2)||(id==2&&software_variant==3))out->anlz_dir[0]=0;
 return rc;
}
static void software_tempo(void){
 for(int master=0;master<2;master++)for(software_variant=0;software_variant<4;software_variant++){
  cases++;clock_ms=1000;wire_horizon=882;position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,software_resolve,master_bpm,NULL,NULL,0,0};
  p=xz_xp_create(&env);assert(p);fake_route(7600+cases);step();
  sync_loop_track(master,1,350);sync_loop_track(1-master,2,700);press(1-master,XZ_EXTRA_IN_TEMPO_RANGE);
  press(master,XZ_EXTRA_IN_MASTER);press(1-master,XZ_EXTRA_IN_SYNC);
  const int positions[]={5000,8000,2000};
  for(unsigned t=0;t<3;t++){
   tempo(master,positions[t]);advance(300);
   double bpm=(software_variant==2?125.:120.)*(1.+positions[t]/100000.);
   double rate=bpm/(software_variant==3?125.:120.);
   printf("SOFTWARE master=%d variant=%d expected_bpm=%.3f actual_rate=%.7f expected_rate=%.7f published=%d/%d\n",master+3,software_variant,bpm,debug(1-master).speed,rate,view.deck[master].bpm_x100,view.deck[1-master].bpm_x100);
   CHECK(fabs(debug(1-master).speed-rate)<.0001);
   CHECK(view.deck[master].bpm_x100==(int32_t)llround(bpm*100.));
   CHECK(view.deck[1-master].bpm_x100==(int32_t)llround(bpm*100.));
   CHECK(view.deck[1-master].sync.requested&&!view.deck[1-master].sync.locked);
  }
  xz_xp_destroy(p);p=NULL;
 }
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 for(int mode=0;mode<3;mode++)for(int missing=0;missing<2;missing++)for(int source=1;source<=2;source++)one(mode,missing,source);
 software_tempo();
 printf("RESULT cases=%u checks=%u failures=%u\n",cases,checks,failed);return failed?1:0;
}
