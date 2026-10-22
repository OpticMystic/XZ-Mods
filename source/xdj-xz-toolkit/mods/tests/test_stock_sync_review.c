/* Independent R28 cases drive public input/step/read and the retained PCM fake sink. */
#define main retained_player_main
#include "test_extra_player.c"
#undef main
static unsigned checks,failed;
#define CHECK(x) do { ++checks; if(!(x)){ ++failed; fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); } } while(0)
static int scenario,copy_count,hold_capture,jitter;
static uint32_t capture_at;
static int r28_native(struct xz_sync_native_snapshot *out){
 ++copy_count;
 if(scenario==1){memset(out,0,sizeof *out);return 0;}
 if(scenario==2){*out=(struct xz_sync_native_snapshot){.reason=XZ_SYNC_NATIVE_EXTERNAL_MASTER};return 0;}
 int rc=copy_sim_native(out);
 if(hold_capture)out->captured_ms=capture_at;
 if(jitter)out->bar_phase=(float)((clock_ms/10u)%2u?.1:.48);
 return rc;
}
static void init_native(int quantize){
 wire_horizon=882;clock_ms=1000;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,0};
 env.start_with_quantize=quantize;
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,r28_native);fake_route(2800);step();
 sync_loop_track(0,1,350);sync_loop_track(1,2,350);
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.bpm=120.;
 sim_native.source_revision=28;sim_native.track_identity=0x280001;sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 press(0,XZ_EXTRA_IN_SYNC);press(1,XZ_EXTRA_IN_SYNC);advance(2200);
 CHECK(view.deck[0].sync.requested&&view.deck[1].sync.requested);
 if(quantize)CHECK(view.deck[0].sync.locked&&view.deck[1].sync.locked);
}
static void dump(const char *stage){
 printf("R28 %s t=%u copy=%d master3=%d master4=%d req=%d/%d lock=%d/%d reason=%d/%d tempo=%d/%d gen=%u/%u flush=%u/%u source=%llu/%llu error_us=%.3f/%.3f\n",
 stage,clock_ms,copy_count,view.deck[0].master,view.deck[1].master,view.deck[0].sync.requested,view.deck[1].sync.requested,
 view.deck[0].sync.locked,view.deck[1].sync.locked,view.deck[0].sync.reason,view.deck[1].sync.reason,
 view.deck[0].tempo_x100,view.deck[1].tempo_x100,debug(0).generation,debug(1).generation,flushes(0),flushes(1),
 (unsigned long long)completed_tag(0),(unsigned long long)completed_tag(1),actual_native_sync_error(0)*1e6,actual_native_sync_error(1)*1e6);
}
static void identity(void){
 init_native(1);dump("identity-before");unsigned old=flushes(0);
 sim_native.native_deck=1;sim_native.source_revision++;sim_native.track_identity++;sim_native_set_bpm(126.);
 advance(5000);dump("identity-current-coherent-5s");
 CHECK(view.deck[0].sync.master_deck==1&&view.deck[1].sync.master_deck==1);
 CHECK(view.deck[0].tempo_x100==500&&view.deck[1].tempo_x100==500);
 CHECK(view.deck[0].sync.locked&&view.deck[1].sync.locked);
 CHECK(fabs(actual_native_sync_error(0))<=.001&&fabs(actual_native_sync_error(1))<=.001);
 CHECK(flushes(0)<=old+1);
}
static void incoherent(void){
 init_native(1);dump("incoherent-before");unsigned old[2]={flushes(0),flushes(1)};jitter=1;
 unsigned locks=0,last_snap[2]={0,0},seen[2]={old[0],old[1]};
 for(unsigned t=0;t<4500;t+=10){
  advance(10);locks+=view.deck[0].sync.locked+view.deck[1].sync.locked;
  for(int d=0;d<2;d++)if(flushes(d)!=seen[d]){
   printf("R28_SNAP deck=%d t=%u previous=%u delta_ms=%u\n",d+3,clock_ms,last_snap[d],last_snap[d]?clock_ms-last_snap[d]:0);
   if(last_snap[d])CHECK((uint32_t)(clock_ms-last_snap[d])>=XZ_XP_SYNC_RESNAP_MS);
   last_snap[d]=clock_ms;seen[d]=flushes(d);
  }
  if(!(t%500))dump("incoherent-alternating");
 }
 dump("incoherent-end");printf("R28 incoherent snap_delta=%u/%u locks=%u\n",flushes(0)-old[0],flushes(1)-old[1],locks);
 CHECK(!locks);CHECK(flushes(0)==old[0]&&flushes(1)==old[1]);
 jitter=0;advance(3000);dump("incoherent-coherent-return");
 CHECK(view.deck[0].sync.locked&&view.deck[1].sync.locked);
}
static void debounce(const char *which){
 init_native(0);sim_native.playing=0;
 if(!strcmp(which,"zero"))clock_ms=0;
 if(!strcmp(which,"wrap"))clock_ms=UINT32_MAX-99u;
 if(!strcmp(which,"rollback"))clock_ms=20000;
 step();dump("debounce-first-stopped");CHECK(!view.deck[0].master&&!view.deck[1].master);
 if(!strcmp(which,"rollback"))clock_ms-=10;
 if(strcmp(which,"identity")&&strcmp(which,"wrap")){step();dump("debounce-second-same-or-rollback");CHECK(!view.deck[0].master&&!view.deck[1].master);}
 if(!strcmp(which,"wrap")){advance(290);dump("debounce-wrap-290");CHECK(!view.deck[0].master&&!view.deck[1].master);advance(20);CHECK(view.deck[0].master);}
 if(!strcmp(which,"identity")){
  advance(200);sim_native.native_deck=1;sim_native.track_identity++;sim_native.source_revision++;step();
  advance(120);dump("debounce-new-identity-120");CHECK(!view.deck[0].master&&!view.deck[1].master);
  advance(190);CHECK(view.deck[0].master);
 }
}
static void negatives(void){
 init_native(0);sim_native.playing=0;
 for(scenario=1;scenario<=2;scenario++){advance(1000);dump("unknown-or-link");CHECK(!view.deck[0].master&&!view.deck[1].master);}
 scenario=0;sim_native.playing=1;hold_capture=1;capture_at=clock_ms-151u;advance(1000);CHECK(!view.deck[0].master&&!view.deck[1].master);
 hold_capture=0;press(0,XZ_EXTRA_IN_SYNC);press(1,XZ_EXTRA_IN_SYNC);int count=copy_count;advance(1000);CHECK(copy_count==count);
 press(0,XZ_EXTRA_IN_SYNC);advance(40);CHECK(copy_count>count);
}
static void tempo_only(void){
 init_native(0);CHECK(view.deck[0].sync.reason==XZ_EXTRA_SYNC_TEMPO_FOLLOW);
 for(int master_deck=0;master_deck<2;master_deck++){
  sim_native.native_deck=master_deck;sim_native.track_identity++;sim_native.source_revision++;
  for(int bpm=120;bpm<=132;bpm+=6){sim_native_set_bpm(bpm);advance(200);CHECK(view.deck[0].tempo_x100==(bpm-120)*10000/120);CHECK(!view.deck[0].sync.locked);}
 }
 xz_xp_destroy(p);p=NULL;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);fake_route(2801);step();
 load(0,4);seek(0,0);press(0,XZ_EXTRA_IN_LOOP_IN);seek(0,1900);press(0,XZ_EXTRA_IN_LOOP_OUT);
 load(1,4);seek(1,0);press(1,XZ_EXTRA_IN_LOOP_IN);seek(1,1900);press(1,XZ_EXTRA_IN_LOOP_OUT);
 press(0,XZ_EXTRA_IN_MASTER);press(1,XZ_EXTRA_IN_SYNC);
 tempo(0,XZ_EXTRA_TEMPO_SPAN/2);advance(300);dump("software-no-grid-tempo-only");
 CHECK(view.deck[1].sync.requested&&view.deck[1].sync.reason==XZ_EXTRA_SYNC_TEMPO_FOLLOW);
 CHECK(view.deck[1].tempo_x100==500);
}
static void failed_master(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,1};
 p=xz_xp_create(&env);assert(p);fake_route(2802);step();sync_loop_track(0,1,100);sync_loop_track(1,2,350);
 press(0,XZ_EXTRA_IN_MASTER);press(1,XZ_EXTRA_IN_SYNC);advance(2200);
 CHECK(view.deck[0].master&&view.deck[1].sync.locked);load(0,3);advance(500);dump("failed-master-load");
 CHECK(debug(0).state==XZ_XP_ERROR&&debug(1).state==XZ_XP_PLAYING);
 CHECK(view.deck[1].master&&!view.deck[0].master&&view.deck[1].sync.requested);
}
static void good(void){test_stock_sync();test_native_master_handoff();test_native_phase_sync();test_sync_nonzero_loop();test_sync_missing_sources();test_checked_resolver();test_sync_concurrent_groups();test_recovery_pcm();test_recovery_epochs();test_reconnect_during_render();test_ordered_next();}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 if(!strcmp(argv[2],"identity"))identity();else if(!strcmp(argv[2],"incoherent"))incoherent();
 else if(!strncmp(argv[2],"debounce-",9))debounce(argv[2]+9);
 else if(!strcmp(argv[2],"negative"))negatives();else if(!strcmp(argv[2],"tempo-only"))tempo_only();else if(!strcmp(argv[2],"failed-master"))failed_master();else if(!strcmp(argv[2],"retained"))good();else assert(0);
 if(p)xz_xp_destroy(p);pthread_mutex_destroy(&f.lock);printf("RESULT checks=%u failures=%u\n",checks,failed);return failed?1:0;
}
