/* Minimal own-C REDs. Uses only existing observer, clock, fake sink and selector. */
#define main retained_player_main
#include "test_extra_player.c"
#undef main
#include "deck_select.h"
static struct xz_master_observation actual;
static int route_change;
static unsigned checks, failures;
#define EXPECT(x) do{checks++;if(!(x)){failures++;printf("RED line=%d %s\n",__LINE__,#x);}}while(0)
static int observe(struct xz_master_observation *out){*out=actual;out->observed_ms=clock_ms;return 1;}
static int native_clock(struct xz_sync_native_snapshot *out){
 int ok=copy_sim_native(out);
 if(route_change){route_change=0;fake_route(fake_link(NULL)+1);}
 return ok;
}
static void begin(int quantize){
 clock_ms=1000;wire_horizon=882;route_change=0;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 actual=(struct xz_master_observation){.deck=0,.revision=1,.operator_epoch=1};
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.bpm=126;
 sim_native.source_revision=11;sim_native.track_identity=123;sim_native.anchor_us=1000000;sim_native.anchor_phase=.3;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,quantize};
 p=xz_xp_create(&env);assert(p);xz_xp_set_master_observer(p,observe);xz_xp_set_native_sync_clock(p,native_clock);
 fake_route(123000);step();sync_loop_track(0,1,350);sync_loop_track(1,2,700);advance(50);
}
static struct xz_deck_select select_side;
static void fader(float value){
 view.route_up=1;xz_deck_select_feedback(&select_side,&view);
 struct xz_ds_key k={.side=0,.code=XZ_DS_KEY_TEMPO,.op=XZ_DS_OP_FADER,.value=value};
 struct xz_ds_outcome o=xz_deck_select_global_player(&select_side,&k,clock_ms);
 if(o.verdict==XZ_DS_DELIVER)assert(input(o.event));step();
}
static void pickup(int toggle){
 begin(0);xz_deck_select_init(&select_side);xz_deck_select_route_instance(&select_side,1,fake_link(NULL));xz_deck_select_toggle(&select_side,0,NULL);
 fader(0);press(0,XZ_EXTRA_IN_SYNC);advance(100);fader(.5f);EXPECT(view.deck[0].tempo_x100==500);
 press(0,XZ_EXTRA_IN_SYNC);view.route_up=1;xz_deck_select_feedback(&select_side,&view);
 EXPECT(select_side.side[0].pickup.armed&&select_side.side[0].pickup.require_motion);
 if(toggle){xz_deck_select_toggle(&select_side,0,NULL);xz_deck_select_toggle(&select_side,0,NULL);}
 fader(.5f);fader(.6f);advance(100);
 printf("PICKUP toggle=%d tempo=%d speed=%.6f completed_source=%llu\n",toggle,view.deck[0].tempo_x100,debug(0).speed,(unsigned long long)f.position.deck[0].source_after_completed);
 EXPECT(view.deck[0].tempo_x100==500);EXPECT(fabs(debug(0).speed-1.05)<.00001);
 xz_xp_destroy(p);p=NULL;
}
static void route(int change){
 begin(1);press(0,XZ_EXTRA_IN_PLAY);press(1,XZ_EXTRA_IN_PLAY);seek(0,805);
 sim_native.playing=0;sim_native.anchor_phase=1.;press(0,XZ_EXTRA_IN_SYNC);clock_ms+=10;step();
 EXPECT(view.deck[0].tempo_x100==500);press(0,XZ_EXTRA_IN_SYNC);seek(0,805);press(0,XZ_EXTRA_IN_SYNC);
 uint64_t before=debug(0).position;uint32_t gen=debug(0).generation,old_route=fake_link(NULL);
 sim_native.bpm=129.6;route_change=change;clock_ms+=10;stepping_side=0;xz_xp_step(p,0);xz_xp_read(p,&view);
 printf("ROUTE changed=%d old_route=%u new_route=%u source_before=%llu source_after=%llu gen_before=%u gen_after=%u tempo=%d\n",change,old_route,fake_link(NULL),(unsigned long long)before,(unsigned long long)debug(0).position,gen,debug(0).generation,view.deck[0].tempo_x100);
 if(change){EXPECT(fake_link(NULL)!=old_route);EXPECT(debug(0).position==before);EXPECT(view.deck[0].tempo_x100==500);}
 else{EXPECT(fake_link(NULL)==old_route);EXPECT(debug(0).position!=before);EXPECT(view.deck[0].tempo_x100==800);}
 xz_xp_destroy(p);p=NULL;
}
int retained_minimal_main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 if(!strcmp(argv[2],"pickup"))pickup(1);
 else if(!strcmp(argv[2],"route"))route(1);
 else if(!strcmp(argv[2],"controls")){pickup(0);route(0);}
 else{pickup(0);pickup(1);route(0);route(1);}
 pthread_mutex_destroy(&f.lock);printf("RESULT minimal checks=%u failures=%u\n",checks,failures);return failures?1:0;
}

static unsigned extended_cases;
static void extended_fader(int d,float value){
 view.route_up=1;xz_deck_select_feedback(&select_side,&view);
 struct xz_ds_key k={.side=d,.code=XZ_DS_KEY_TEMPO,.op=XZ_DS_OP_FADER,.value=value};
 struct xz_ds_outcome o=xz_deck_select_global_player(&select_side,&k,clock_ms);
 if(o.verdict==XZ_DS_DELIVER)assert(input(o.event));step();
}
static void extended_pickup(void){
 for(int d=0;d<2;d++)for(int promotion=0;promotion<2;promotion++)for(int switching=0;switching<4;switching++){
  extended_cases++;begin(0);xz_deck_select_init(&select_side);xz_deck_select_route_instance(&select_side,1,fake_link(NULL));
  xz_deck_select_toggle(&select_side,d,NULL);extended_fader(d,0);press(d,XZ_EXTRA_IN_SYNC);advance(100);extended_fader(d,.5f);
  EXPECT(view.deck[d].tempo_x100==500);
  if(switching==2)xz_deck_select_toggle(&select_side,d,NULL);
  if(promotion){
   press(d,XZ_EXTRA_IN_MASTER);
   actual=(struct xz_master_observation){.deck=d+2,.revision=2,.operator_epoch=1,.request_epoch=view.master_intent.epoch,.lease_generation=9,
    .route_epoch=view.deck[d].timeline.route_epoch,.load_revision=view.deck[d].timeline.load_revision,.transport_generation=view.deck[d].timeline.transport_generation};step();
   EXPECT(view.deck[d].master);
  }else press(d,XZ_EXTRA_IN_SYNC);
  view.route_up=1;xz_deck_select_feedback(&select_side,&view);
  if(switching==1||switching==3)xz_deck_select_toggle(&select_side,d,NULL);
  if(switching==3){extended_fader(d,-.2f);extended_fader(d,.5f);}
  if(switching)xz_deck_select_toggle(&select_side,d,NULL);
  extended_fader(d,.5f);extended_fader(d,.6f);advance(100);
  printf("EXT PICKUP side=%d promotion=%d switching=%d tempo=%d speed=%.8f\n",d,promotion,switching,view.deck[d].tempo_x100,debug(d).speed);
  EXPECT(view.deck[d].tempo_x100==500);EXPECT(fabs(debug(d).speed-1.05)<.00001);
  extended_fader(d,.7f);EXPECT(view.deck[d].tempo_x100==500);
  extended_fader(d,.5f);extended_fader(d,.6f);EXPECT(view.deck[d].tempo_x100==600);EXPECT(view.deck[1-d].tempo_x100==0);
  xz_xp_destroy(p);p=NULL;
 }
}
static int extended_arm,extended_stage,extended_observations,extended_mutated;
static int extended_observe(struct xz_master_observation *out){
 *out=actual;out->observed_ms=clock_ms;
 if(extended_arm){
  extended_observations++;
  if(extended_stage>1&&extended_observations==extended_stage){fake_route(fake_link(NULL)+1);extended_mutated=1;}
 }
 return 1;
}
static int extended_native(struct xz_sync_native_snapshot *out){
 int ok=copy_sim_native(out);
 if(extended_arm&&extended_stage==1){extended_arm=0;fake_route(fake_link(NULL)+1);extended_mutated=1;}
 return ok;
}
static void extended_routes(void){
 const int stages[]={0,1,3,4,5};
 for(int d=0;d<2;d++)for(int quantize=0;quantize<2;quantize++)for(unsigned si=0;si<5;si++){
  int stage=stages[si];if(!quantize&&stage==5)continue;
  extended_cases++;begin(quantize);extended_arm=0;extended_observations=extended_mutated=0;extended_stage=stage;
  xz_xp_set_master_observer(p,extended_observe);xz_xp_set_native_sync_clock(p,extended_native);
  press(0,XZ_EXTRA_IN_PLAY);press(1,XZ_EXTRA_IN_PLAY);seek(d,805);
  sim_native.playing=0;sim_native.anchor_phase=1.;press(d,XZ_EXTRA_IN_SYNC);clock_ms+=10;step();EXPECT(view.deck[d].tempo_x100==500);
  press(d,XZ_EXTRA_IN_SYNC);seek(d,805);press(d,XZ_EXTRA_IN_SYNC);
  uint64_t before=debug(d).position;uint32_t gen=debug(d).generation,old_route=fake_link(NULL);
  sim_native.bpm=129.6;clock_ms+=10;extended_observations=0;extended_arm=stage!=0;
  stepping_side=d;xz_xp_step(p,d);extended_arm=0;xz_xp_read(p,&view);
  printf("EXT ROUTE side=%d quantize=%d stage=%d mutated=%d old=%u current=%u source=%llu/%llu tempo=%d gen=%u/%u\n",d,quantize,stage,extended_mutated,old_route,fake_link(NULL),(unsigned long long)before,(unsigned long long)debug(d).position,view.deck[d].tempo_x100,gen,debug(d).generation);
  EXPECT(extended_mutated==(stage!=0));
  if(stage){
   EXPECT(fake_link(NULL)==old_route+1);EXPECT(debug(d).position==before);EXPECT(debug(d).generation==gen);
   /* The phase-gate case retires after the already valid BPM commit. */
   EXPECT(view.deck[d].tempo_x100==(stage==5?800:500));
  }else{
   EXPECT(fake_link(NULL)==old_route);EXPECT(view.deck[d].tempo_x100==800);
   EXPECT(quantize?debug(d).position!=before:debug(d).position==before);
  }
  xz_xp_destroy(p);p=NULL;
 }
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 extended_pickup();extended_routes();
 pthread_mutex_destroy(&f.lock);printf("RESULT expanded cases=%u checks=%u failures=%u\n",extended_cases,checks,failures);return failures?1:0;
}
