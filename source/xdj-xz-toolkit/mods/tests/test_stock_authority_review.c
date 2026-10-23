/* Real player with an independently controlled native authority observation. */
#define main retained_player_main
#include "test_extra_player.c"
#undef main
#include "sync_master.h"
#include <stdatomic.h>
#pragma weak xz_xp_set_master_observer
#pragma weak xz_xp_native_master_selected
extern void xz_xp_set_master_observer(struct xz_xp_players *,xz_master_observer);
extern void xz_xp_native_master_selected(struct xz_xp_players *,unsigned);
static struct xz_master_observation observed;
static atomic_int observe_ok=1;
static int observe_fresh=1;
static pthread_mutex_t observation_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint32_t control_operator=1;
static unsigned checks,failed,cases;
static const char *label;
#define CHECK(x) do{checks++;if(!(x)){failed++;printf("RED authority case=%u %s line=%d %s\n",cases,label,__LINE__,#x);}}while(0)
static int observe(struct xz_master_observation *out){
 if(!atomic_load(&observe_ok)){*out=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN};return 0;}
 if(pthread_mutex_trylock(&observation_mutex)){
  *out=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN,.operator_epoch=atomic_load(&control_operator)};return out->operator_epoch!=0;
 }
 *out=observed;if(observe_fresh)out->observed_ms=now_ms();int ok=observe_ok;
 pthread_mutex_unlock(&observation_mutex);return ok;
}
static void begin(const char *name){
 cases++;label=name;clock_ms=1000;wire_horizon=882;observe_ok=observe_fresh=1;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 observed=(struct xz_master_observation){.deck=XZ_MASTER_DECK1,.revision=1,.operator_epoch=1};atomic_store(&control_operator,1);
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);
 if(xz_xp_set_master_observer)xz_xp_set_master_observer(p,observe);
 xz_xp_set_native_sync_clock(p,copy_sim_native);
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.bpm=126;
 sim_native.native_deck=0;sim_native.source_revision=11;sim_native.track_identity=123;
 sim_native.anchor_us=(uint64_t)clock_ms*1000;sim_native.anchor_phase=.3;
 fake_route(90000+cases);step();sync_loop_track(0,1,350);sync_loop_track(1,2,700);advance(50);
}
static void end(void){xz_xp_destroy(p);p=NULL;}
static void confirm(int d,uint32_t epoch){
 const struct xz_extra_timeline *t=&view.deck[d].timeline;
 atomic_store(&control_operator,observed.operator_epoch);
 observed=(struct xz_master_observation){.deck=d+2,.revision=observed.revision+1,.operator_epoch=observed.operator_epoch,
  .request_epoch=epoch,.lease_generation=7,.route_epoch=t->route_epoch,.load_revision=t->load_revision,
  .transport_generation=t->transport_generation};
}
static void pending(void){
 for(int d=0;d<2;d++){
  begin("pending-promotion-keeps-confirmed-native-clock");press(d,XZ_EXTRA_IN_SYNC);advance(200);
  CHECK(view.master_actual.deck==0&&view.master_actual.revision==1);
  CHECK(view.deck[d].tempo_x100==500);
  press(d,XZ_EXTRA_IN_MASTER);
  CHECK(view.master_intent.deck==d+2&&view.master_intent.epoch&&view.master_intent.explicit_choice);
  CHECK(view.master_actual.deck==0&&!view.deck[0].master&&!view.deck[1].master);
  CHECK(xz_xp_needs_native_sync(p));tempo(d,1000);CHECK(view.deck[d].tempo_x100==500);
  sim_native_set_bpm(129.6);advance(100);CHECK(abs(view.deck[d].tempo_x100-800)<=1);
  uint32_t wanted=view.master_intent.epoch;confirm(d,wanted+1);step();
  CHECK(!view.deck[d].master&&view.master_actual.deck==XZ_MASTER_UNKNOWN);
  confirm(d,wanted);step();CHECK(view.deck[d].master&&!view.deck[1-d].master);
  CHECK(view.master_actual.deck==d+2&&view.master_actual.request_epoch==wanted);
  CHECK((view.deck[d].master_scope&(XZ_EXTRA_MASTER_SCOPE_NATIVE|XZ_EXTRA_MASTER_SCOPE_SOFTWARE))==(XZ_EXTRA_MASTER_SCOPE_NATIVE|XZ_EXTRA_MASTER_SCOPE_SOFTWARE));
  CHECK(abs(view.deck[d].tempo_x100-800)<=1);end();
 }
}
static void unavailable(void){
 for(int d=0;d<2;d++)for(int kind=0;kind<8;kind++){
  begin("unknown-stale-foreign-source-never-falls-back-to-intent");press(d,XZ_EXTRA_IN_MASTER);
  uint32_t wanted=view.master_intent.epoch;confirm(d,wanted);step();press(1-d,XZ_EXTRA_IN_SYNC);advance(200);
  tempo(d,5000);advance(100);CHECK(view.deck[1-d].tempo_x100==500);
  if(kind==0)observe_ok=0;
  if(kind==1){observe_fresh=0;observed.observed_ms=clock_ms-151;}
  if(kind==2)observed.deck=XZ_MASTER_EXTERNAL;
  if(kind==3)observed.load_revision++;
  if(kind==4)observed.route_epoch++;
  if(kind==5)observed.transport_generation++;
  if(kind==6)observed.lease_generation=0;
  if(kind==7)observed.request_epoch=0;
  observed.revision++;unsigned gen=debug(1-d).generation;tempo(d,8000);advance(100);
  CHECK(!view.deck[0].master&&!view.deck[1].master);CHECK(view.deck[1-d].tempo_x100==500);
  CHECK(view.deck[1-d].sync.requested&&!view.deck[1-d].sync.locked);CHECK(debug(1-d).generation==gen);
  CHECK(view.master_actual.deck==(kind==2?XZ_MASTER_EXTERNAL:XZ_MASTER_UNKNOWN));end();
 }
}
static void concurrent(void){
 begin("concurrent-workers-use-confirmation-not-intent");
 observed.deck=XZ_MASTER_NONE;observed.revision++;xz_xp_set_native_sync_clock(p,NULL);
 threaded_clock=1;assert(!xz_xp_start(p));__atomic_store_n(&stop_usb,0,__ATOMIC_RELEASE);
 pthread_t wire;assert(!pthread_create(&wire,NULL,usb,NULL));
 CHECK(input(event(0,XZ_EXTRA_IN_SYNC,1)));CHECK(input(event(1,XZ_EXTRA_IN_SYNC,1)));
 int old=XZ_MASTER_NONE;
 for(int round=0;round<32;round++){
  int d=round&1;CHECK(input(event(d,XZ_EXTRA_IN_MASTER,1)));
  int requested=0;
  for(int tries=0;tries<300;tries++){
   xz_xp_read(p,&view);if(view.master_intent.deck==d+2){requested=1;break;}
   struct timespec pause={0,1000000};nanosleep(&pause,NULL);
  }
  CHECK(requested);CHECK(view.master_actual.deck==old||view.master_actual.deck==XZ_MASTER_UNKNOWN);
  CHECK(!view.deck[d].master);uint32_t epoch=view.master_intent.epoch;
  pthread_mutex_lock(&observation_mutex);confirm(d,epoch);pthread_mutex_unlock(&observation_mutex);
  int actual=0;
  for(int tries=0;tries<300;tries++){
   xz_xp_read(p,&view);CHECK(view.deck[0].master+view.deck[1].master<=1);
   if(view.master_actual.deck==d+2&&view.deck[d].master){actual=1;break;}
   struct timespec pause={0,1000000};nanosleep(&pause,NULL);
  }
  CHECK(actual);CHECK(!view.deck[1-d].master);old=d+2;
 }
 __atomic_store_n(&stop_usb,1,__ATOMIC_RELEASE);assert(!pthread_join(wire,NULL));xz_xp_stop(p);threaded_clock=0;end();
}
static void admission_order(void){
 for(int first=0;first<2;first++){
  begin("MASTER-choice-order-is-admission-order");
  CHECK(input(event(first,XZ_EXTRA_IN_MASTER,1)));CHECK(input(event(1-first,XZ_EXTRA_IN_MASTER,1)));
  stepping_side=1-first;xz_xp_step(p,1-first);stepping_side=first;xz_xp_step(p,first);xz_xp_read(p,&view);
  CHECK(view.master_intent.deck==3-first);CHECK(!view.deck[0].master&&!view.deck[1].master);end();
 }
}
static void operator_admission(void){
 for(int d=0;d<2;d++){
  begin("queued-old-operator-request-is-superseded");
  CHECK(input(event(d,XZ_EXTRA_IN_MASTER,1)));
  observed=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN,.operator_epoch=2};step();
  CHECK(view.master_intent.deck==XZ_MASTER_NONE);CHECK(!view.deck[0].master&&!view.deck[1].master);
  CHECK(input(event(d,XZ_EXTRA_IN_MASTER,1)));step();
  CHECK(view.master_intent.deck==d+2&&view.master_intent.operator_epoch==2);
  CHECK(view.master_actual.deck==XZ_MASTER_UNKNOWN);
  confirm(d,view.master_intent.epoch);step();CHECK(view.deck[d].master&&view.master_actual.deck==d+2);end();

  begin("new-request-after-operator-event-needs-no-second-press");
  press(d,XZ_EXTRA_IN_MASTER);
  observed=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN,.operator_epoch=2};
  CHECK(input(event(1-d,XZ_EXTRA_IN_MASTER,1)));step();
  CHECK(view.master_intent.deck==3-d&&view.master_intent.operator_epoch==2);
  unsigned epoch=view.master_intent.epoch;step();CHECK(view.master_intent.epoch==epoch);
  confirm(1-d,epoch);step();CHECK(view.deck[1-d].master&&view.master_actual.deck==3-d);end();

  begin("failed-admission-cannot-supersede-accepted-choice");
  CHECK(input(event(d,XZ_EXTRA_IN_MASTER,1)));
  for(int n=0;n<64;n++)assert(input(event(1-d,XZ_EXTRA_IN_JOG_TURN,1)));
  CHECK(!input(event(1-d,XZ_EXTRA_IN_MASTER,1)));step();CHECK(view.master_intent.deck==d+2);
  uint32_t epoch_before=view.master_intent.epoch;observe_ok=0;CHECK(!input(event(1-d,XZ_EXTRA_IN_MASTER,1)));
  observe_ok=1;step();CHECK(view.master_intent.deck==d+2&&view.master_intent.epoch==epoch_before);end();
 }
}
static void route_retires_admission(void){
 for(int d=0;d<2;d++){
  begin("stale-route-choice-cannot-block-later-handoff");
  CHECK(input(event(d,XZ_EXTRA_IN_MASTER,1)));fake_route(99000+cases);step();
  CHECK(view.master_intent.deck==XZ_MASTER_NONE);
  sync_loop_track(0,1,350);sync_loop_track(1,2,700);press(d,XZ_EXTRA_IN_SYNC);advance(100);
  sim_native.playing=0;advance(400);CHECK(view.master_intent.deck==XZ_MASTER_DECK3);
  CHECK(!view.deck[0].master&&!view.deck[1].master);end();
 }
}
static void age_boundary(void){
 for(int d=0;d<2;d++){
  begin("confirmed-authority-age-boundary");press(d,XZ_EXTRA_IN_MASTER);confirm(d,view.master_intent.epoch);step();
  observe_fresh=0;observed.observed_ms=clock_ms-XZ_MASTER_MAX_AGE_MS;xz_xp_read(p,&view);CHECK(view.deck[d].master);
  observed.observed_ms--;xz_xp_read(p,&view);CHECK(view.master_actual.deck==XZ_MASTER_UNKNOWN&&!view.deck[d].master);end();
 }
}
static void native_handoff_after_confirmation(void){
 begin("fulfilled-native-choice-does-not-block-later-authority");
 xz_xp_native_master_selected(p,0);press(1,XZ_EXTRA_IN_SYNC);advance(100);
 observed.deck=1;observed.revision++;observed.operator_epoch=2;atomic_store(&control_operator,2);
 sim_native.native_deck=1;sim_native.source_revision++;sim_native.track_identity++;advance(100);
 CHECK(view.master_actual.deck==1&&view.master_intent.deck==0);
 sim_native.playing=0;advance(400);CHECK(view.master_intent.deck==2&&view.master_intent.operator_epoch==2);
 CHECK(view.master_actual.deck==1&&!view.deck[0].master&&!view.deck[1].master);end();
}
static void explicit_native(void){
 begin("native-reselection-is-distinct-from-withdrawal");
 CHECK(xz_xp_native_master_selected!=NULL);
 if(xz_xp_native_master_selected){
  xz_xp_native_master_selected(p,1);step();uint32_t choice=view.master_intent.epoch;
  CHECK(view.master_intent.deck==1&&view.master_intent.explicit_choice&&choice);
  CHECK(view.master_actual.deck==0);
  CHECK(!xz_xp_use_native_master(p));step();CHECK(view.master_intent.deck==1&&view.master_intent.epoch==choice);
  xz_xp_native_master_selected(p,1);step();CHECK(view.master_intent.epoch>choice&&view.master_intent.deck==1);
  observed.deck=1;observed.revision++;observed.operator_epoch++;step();CHECK(view.master_actual.deck==1);
  /* The old deck-1 clock must not supply tempo for a confirmed deck-2 master. */
  press(0,XZ_EXTRA_IN_SYNC);advance(100);CHECK(view.deck[0].tempo_x100==0);
  sim_native.native_deck=1;sim_native.source_revision++;sim_native.track_identity++;advance(100);
  CHECK(view.deck[0].tempo_x100==500);
 }
 end();
}
static void rapid_native_choice(unsigned deck){
 observed.operator_epoch++;atomic_store(&control_operator,observed.operator_epoch);
 observed.deck=(int)deck;observed.revision++;observed.request_epoch=0;observed.lease_generation=0;
 sim_native.native_deck=deck;sim_native.source_revision++;sim_native.track_identity++;
 xz_xp_native_master_selected(p,deck);advance(80);
}
static void rapid_reacquisition(void){
 for(unsigned spacing=120;spacing<=1200;spacing+=1080){
  begin("MASTER-change-during-phase-cooldown-retains-request");press(0,XZ_EXTRA_IN_QUANTIZE);
  for(int d=0;d<2;d++){if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);press(d,XZ_EXTRA_IN_SYNC);}
  advance(2200);CHECK(view.deck[0].sync.locked&&view.deck[1].sync.locked);
  rapid_native_choice(1);advance(spacing);
  unsigned before[2]={flushes(0),flushes(1)};
  sim_native.anchor_phase+=.25;rapid_native_choice(0);
  if(spacing<1000)for(int d=0;d<2;d++)CHECK(flushes(d)==before[d]);
  advance(3000);
  for(int d=0;d<2;d++){
   CHECK(view.deck[d].sync.requested&&view.deck[d].playing);
   CHECK(view.deck[d].sync.locked&&fabs(actual_native_sync_error(d))<=.002);
   if(spacing<1000)CHECK(flushes(d)>before[d]);
  }
  end();
 }
}
static int pending_clock_fault;
static int pending_clock_sample(struct xz_sync_native_snapshot *out){
 int copied=copy_sim_native(out);
 switch(pending_clock_fault){
 case 1:return 0;
 case 2:out->manual=1;break;
 case 3:out->native_deck=1;out->master_id=1;break;
 case 4:out->captured_ms=clock_ms-151;break;
 case 5:out->captured_ms=clock_ms+1;break;
 case 6:out->track_identity=0;break;
 case 7:out->source_revision=0;break;
 case 8:out->bpm=NAN;break;
 case 9:out->reason=XZ_SYNC_NATIVE_EXTERNAL_MASTER;break;
 }
 return copied;
}
static void pending_output_tempo(void){
 for(int fault=0;fault<10;fault++){
  begin("BPM-follow-independent-of-output-anchor-with-source-fences");press(0,XZ_EXTRA_IN_QUANTIZE);
  for(int d=0;d<2;d++){if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);press(d,XZ_EXTRA_IN_SYNC);}
  advance(2200);CHECK(view.deck[0].sync.locked&&view.deck[1].sync.locked);
  position_blocked=1;advance(200);pending_clock_fault=fault;xz_xp_set_native_sync_clock(p,pending_clock_sample);
  sim_native_set_bpm(132.);advance(40);
  for(int d=0;d<2;d++){
   CHECK(view.deck[d].sync.requested&&!view.deck[d].sync.locked);
   CHECK(view.deck[d].tempo_x100==(fault?500:1000));
  }
  xz_xp_set_native_sync_clock(p,copy_sim_native);pending_clock_fault=0;position_blocked=0;advance(3000);
  for(int d=0;d<2;d++)CHECK(view.deck[d].sync.locked&&fabs(actual_native_sync_error(d))<=.002);
  end();
 }
}

#define XZ_LED_PORTABLE_TEST
#include "../ui/native_led.c"
static void master_tempo_feedback(void){
 for(int d=0;d<2;d++)for(int playing=0;playing<2;playing++){
  begin("master-tempo-feedback-is-state-not-popup");if(playing)press(d,XZ_EXTRA_IN_PLAY);
  uint32_t prior=view.notice_seq;int original=view.deck[d].master_tempo;
  for(int n=0;n<8;n++){
   press(d,XZ_EXTRA_IN_MASTER_TEMPO);
   CHECK(view.deck[d].master_tempo==(original^((n+1)&1)));
   CHECK(view.deck[d].known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER_TEMPO));
   CHECK(view.notice_seq==prior);
   uint8_t records[2*XZ_LED_RECORD_BYTES];memset(records,0xa5,sizeof records);
   uint32_t id=XZ_LED_ID_MASTER_TEMPO,ch=d+1,zero=0;memcpy(records,&id,4);memcpy(records+4,&ch,4);memcpy(records+0x18,&zero,4);
   uint8_t before[sizeof records];memcpy(before,records,sizeof records);uint16_t count=2;
   xz_native_led_extra_deck(records,sizeof records,&count,NULL,0,0,0,clock_ms,ch,&view.deck[d]);
   uint32_t state;memcpy(&state,records+0x10,4);CHECK(state==(uint32_t)view.deck[d].master_tempo);
   for(unsigned i=0;i<sizeof records;i++)if(i<0x10||i>=0x14)CHECK(records[i]==before[i]);
  }
  end();
 }
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"pending"))pending();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"unavailable"))unavailable();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"native"))explicit_native();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"concurrent"))concurrent();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"admission"))admission_order();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"operator"))operator_admission();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"route"))route_retires_admission();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"age"))age_boundary();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"native-handoff"))native_handoff_after_confirmation();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"rapid-recovery"))rapid_reacquisition();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"pending-output"))pending_output_tempo();
 if(!strcmp(argv[2],"all")||!strcmp(argv[2],"mt-feedback"))master_tempo_feedback();
 pthread_mutex_destroy(&f.lock);printf("RESULT authority cases=%u checks=%u failures=%u\n",cases,checks,failed);return failed?1:0;
}
