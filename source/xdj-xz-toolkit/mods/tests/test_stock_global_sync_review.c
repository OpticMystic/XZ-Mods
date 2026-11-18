/* Owner 2026-10-22 contract: physical input -> selector -> real player -> PCM sink. */
#define main retained_player_main
#include "test_extra_player.c"
#undef main
#include "deck_select.h"
static unsigned checks,failed,cases;
static int follower;
static const char *label;
static struct xz_deck_select selector;
#define CHECK(x) do {checks++;if(!(x)){failed++;fprintf(stderr,"RED case=%u deck=%d %s line=%d %s\n",cases,follower+3,label,__LINE__,#x);}}while(0)
static void physical(uint16_t code,unsigned op,float value){
 struct xz_extra_decks feedback=view;feedback.route_up=!!f.link;
 xz_deck_select_feedback(&selector,&feedback);
 struct xz_ds_key k={.side=follower,.code=code,.op=op,.value=value};
 struct xz_ds_outcome o=xz_deck_select_global_player(&selector,&k,clock_ms);
 if(o.verdict==XZ_DS_DELIVER)CHECK(input(o.event));
 step();
}
static void begin(int d,int quantize,const char *name){
 follower=d;label=name;cases++;clock_ms=1000;wire_horizon=882;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,quantize};
 p=xz_xp_create(&env);assert(p);fake_route(80000+cases);step();
 sync_loop_track(0,1,350);sync_loop_track(1,2,600);
 press(1-d,XZ_EXTRA_IN_MASTER);tempo(1-d,5000);advance(100);
 xz_deck_select_init(&selector);xz_deck_select_route(&selector,1);xz_deck_select_toggle(&selector,d,NULL);
 physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,0);
}
static void end(void){xz_xp_destroy(p);p=NULL;}
static void immediate(void){
 for(int d=0;d<2;d++)for(int q=0;q<2;q++){
  begin(d,q,"SYNC-down-before-release");
  physical(0x4313,0,0);advance(30);
  CHECK(view.deck[d].sync.requested);CHECK(view.deck[d].tempo_x100==500);
  if(q)CHECK(view.deck[d].sync.reason!=XZ_EXTRA_SYNC_WAIT_MASTER);
  physical(0x4313,0,0);CHECK(view.deck[d].sync.requested);
  physical(0x4313,2,0);CHECK(view.deck[d].sync.requested);
  advance(2200);CHECK(view.deck[d].tempo_x100==500);
  if(q){CHECK(view.deck[d].sync.locked);CHECK(fabs(actual_sync_error(1-d,d))<=.001);}
  tempo(1-d,8000);advance(300);CHECK(view.deck[d].tempo_x100==800);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,-.7f);CHECK(view.deck[d].tempo_x100==800);
  physical(0x4313,0,0);CHECK(!view.deck[d].sync.requested);
  physical(0x4313,2,0);CHECK(!view.deck[d].sync.requested);end();
 }
}
static void pickup(void){
 for(int d=0;d<2;d++)for(int promote=0;promote<2;promote++){
  begin(d,0,promote?"become-master-rearms-pickup":"SYNC-off-rearms-pickup");
  press(d,XZ_EXTRA_IN_SYNC);advance(200);CHECK(view.deck[d].tempo_x100==500);
  /* Cross the old target while SYNC owns speed. Its pickup must not count after exit. */
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.5f);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.8f);CHECK(view.deck[d].tempo_x100==500);
  press(d,promote?XZ_EXTRA_IN_MASTER:XZ_EXTRA_IN_SYNC);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.7f);CHECK(view.deck[d].tempo_x100==500);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.6f);CHECK(view.deck[d].tempo_x100==500);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.5f);CHECK(view.deck[d].tempo_x100==500);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.6f);CHECK(view.deck[d].tempo_x100==600);
  end();
 }
}
static void pitch_lock(void){
 for(int d=0;d<2;d++)for(int reset=0;reset<2;reset++){
  begin(d,0,reset?"synced-tempo-reset-cannot-disable-sync":"synced-range-cannot-change-rate");
  press(d,XZ_EXTRA_IN_SYNC);advance(200);CHECK(view.deck[d].tempo_x100==500);
  unsigned gen=debug(d).generation;int range=view.deck[d].tempo_range;
  physical(reset?0x4109:0x4107,0,0);physical(reset?0x4109:0x4107,2,0);
  CHECK(view.deck[d].sync.requested);CHECK(view.deck[d].tempo_x100==500);
  CHECK(debug(d).generation==gen);if(!reset)CHECK(view.deck[d].tempo_range==range);
  end();
 }
}
static void paused_master(void){
 for(int d=0;d<2;d++)for(int moving=0;moving<2;moving++)for(int q=0;q<2;q++){
  begin(d,q,"explicit-paused-master-initial-sync");
  press(d,XZ_EXTRA_IN_PLAY);press(1-d,XZ_EXTRA_IN_PLAY);
  seek(1-d,600);seek(d,850);press(1-d,XZ_EXTRA_IN_MASTER);
  if(moving)press(d,XZ_EXTRA_IN_PLAY);
  press(d,XZ_EXTRA_IN_SYNC);advance(100);
  CHECK(view.deck[1-d].master&&!view.deck[d].master);
  CHECK(view.deck[d].sync.requested);CHECK(view.deck[d].tempo_x100==500);
  CHECK(view.deck[d].sync.reason!=XZ_EXTRA_SYNC_WAIT_MASTER);
  if(!moving&&q){CHECK(debug(d).position==ms_frames(d,1100)||debug(d).position==ms_frames(d,600));}
  if(!moving&&!q)CHECK(debug(d).position==ms_frames(d,850));
  tempo(1-d,8000);advance(400);CHECK(view.deck[d].tempo_x100==800);
  CHECK(view.deck[1-d].master&&!view.deck[d].master);
  end();
 }
}
static void paused_native(void){
 for(int d=0;d<2;d++)for(int moving=0;moving<2;moving++)for(int q=0;q<2;q++){
  begin(d,q,"selected-native-already-paused");
  press(0,XZ_EXTRA_IN_PLAY);press(1,XZ_EXTRA_IN_PLAY);seek(d,850);
  live_native_state=(struct xz_sync_native_snapshot){.available=1,.phase_available=1,.native_deck=0,.master_id=0,
   .source_revision=42,.track_identity=123,.bpm=126,.bar_phase=1,.uncertainty_us=500};
  xz_xp_set_native_sync_clock(p,live_native);xz_xp_use_native_master(p);
  if(moving)press(d,XZ_EXTRA_IN_PLAY);
  press(d,XZ_EXTRA_IN_SYNC);advance(100);
  CHECK(!view.deck[0].master&&!view.deck[1].master);CHECK(view.deck[d].tempo_x100==500);
  CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_TEMPO_FOLLOW&&!view.deck[d].sync.locked);
  unsigned settled=flushes(d);
  if(!moving&&q)CHECK(debug(d).position==ms_frames(d,1100)||debug(d).position==ms_frames(d,600));
  if(!moving&&!q)CHECK(debug(d).position==ms_frames(d,850));
  advance(1000);CHECK(!view.deck[0].master&&!view.deck[1].master);CHECK(flushes(d)==settled);
  live_native_state.bpm=129.6f;advance(50);CHECK(abs(view.deck[d].tempo_x100-800)<=1);
  CHECK(view.deck[d].sync.requested);end();
 }
}
static void grid_setting(unsigned denominator,int known){
#ifdef XZ_XP_HAS_QUANTIZE_GRID
 xz_xp_set_quantize_grid(p,denominator,known);
#else
 (void)denominator;(void)known;
#endif
}
static void subdivisions(void){
 for(int d=0;d<2;d++)for(unsigned den=1;den<=8;den*=2)for(int known=0;known<2;known++){
  begin(d,1,"configured-quantize-nearest-point");
  press(d,XZ_EXTRA_IN_PLAY);press(1-d,XZ_EXTRA_IN_PLAY);
  seek(1-d,600);seek(d,805);press(1-d,XZ_EXTRA_IN_MASTER);grid_setting(den,known);
  press(d,XZ_EXTRA_IN_SYNC);advance(100);
  CHECK(view.deck[d].tempo_x100==500);
  double quantum=500./den;
  double expected=known?600.+floor((805.-600.)/quantum+.5)*quantum:805.;
  CHECK(near_frames(debug(d).position,(uint64_t)llround(expected*debug(d).source_rate/1000.),1));
  CHECK(!view.deck[d].sync.locked);
  if(!known)CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_NO_GRID);
  end();
 }
}
static void running_subdivisions(void){
 for(int d=0;d<2;d++)for(unsigned den=1;den<=8;den*=2){
  begin(d,1,"configured-quantize-completed-PCM-follow");grid_setting(den,1);
  press(d,XZ_EXTRA_IN_SYNC);advance(2200);
  CHECK(view.deck[d].sync.requested&&view.deck[d].sync.locked);CHECK(view.deck[d].tempo_x100==500);
  double phase=actual_sync_error(1-d,d)*2.*1.05*den;
  phase-=floor(phase+.5);CHECK(fabs(phase)/(2.*1.05*den)<=.001);
  tempo(1-d,8000);advance(2000);CHECK(view.deck[d].tempo_x100==800&&view.deck[d].sync.locked);
  phase=actual_sync_error(1-d,d)*2.*1.08*den;phase-=floor(phase+.5);CHECK(fabs(phase)/(2.*1.08*den)<=.001);
  unsigned gen=debug(d).generation;grid_setting(den,0);advance(100);
  CHECK(view.deck[d].sync.requested&&!view.deck[d].sync.locked);CHECK(view.deck[d].tempo_x100==800&&debug(d).generation==gen);
  grid_setting(den,1);advance(2000);CHECK(view.deck[d].sync.locked);end();
 }
}
static void fresh_pickup(void){
 for(int d=0;d<2;d++)for(int promote=0;promote<2;promote++){
  begin(d,0,"pickup-needs-post-release-motion");press(d,XZ_EXTRA_IN_SYNC);advance(200);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.5f);
  press(d,promote?XZ_EXTRA_IN_MASTER:XZ_EXTRA_IN_SYNC);
  /* A cached at-target position and stationary repeat cannot satisfy a new pickup. */
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.5f);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.6f);CHECK(view.deck[d].tempo_x100==500);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.7f);CHECK(view.deck[d].tempo_x100==500);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.5f);CHECK(view.deck[d].tempo_x100==500);
  physical(XZ_DS_KEY_TEMPO,XZ_DS_OP_FADER,.6f);CHECK(view.deck[d].tempo_x100==600);end();
 }
}
static int grid_pulse;
static int grid_native(struct xz_sync_native_snapshot *out){
 if(grid_pulse){int pulse=grid_pulse;grid_pulse=0;grid_setting(pulse==1?8:1,pulse!=2);grid_setting(1,1);}
 return live_native(out);
}
static void grid_aba(void){
 for(int d=0;d<2;d++)for(int pulse=1;pulse<=3;pulse++){
  begin(d,1,"quantum-ABA-invalidates-in-flight-alignment");
  press(0,XZ_EXTRA_IN_PLAY);press(1,XZ_EXTRA_IN_PLAY);seek(d,805);
  live_native_state=(struct xz_sync_native_snapshot){.available=1,.phase_available=1,.native_deck=0,.master_id=0,
   .source_revision=61,.track_identity=321,.bpm=126,.bar_phase=1,.uncertainty_us=500};
  xz_xp_set_native_sync_clock(p,grid_native);xz_xp_use_native_master(p);grid_pulse=0;
  live_native_state.phase_available=0; /* Inject the race on the first usable phase, not after an immediate press snap. */
  press(d,XZ_EXTRA_IN_SYNC);unsigned gen=debug(d).generation;
  live_native_state.phase_available=1;grid_pulse=pulse;clock_ms+=10;step();CHECK(!grid_pulse);
  CHECK(pulse==3?debug(d).generation!=gen:debug(d).generation==gen);
  CHECK(debug(d).position==ms_frames(d,pulse==3?600:805));
  clock_ms+=10;step();CHECK(debug(d).position==ms_frames(d,600));
  CHECK(view.deck[d].tempo_x100==500);end();
 }
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"immediate"))immediate();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"pickup"))pickup();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"pitch-lock"))pitch_lock();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"paused"))paused_master();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"paused-native"))paused_native();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"subdivisions"))subdivisions();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"running-subdivisions"))running_subdivisions();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"fresh-pickup"))fresh_pickup();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"grid-aba"))grid_aba();
 pthread_mutex_destroy(&f.lock);printf("RESULT global-sync cases=%u checks=%u failures=%u\n",cases,checks,failed);return failed?1:0;
}
