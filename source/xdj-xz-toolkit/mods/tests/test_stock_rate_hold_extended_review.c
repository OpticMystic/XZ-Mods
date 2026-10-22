#include "rate_hold_fixture.inc"
#include "deck_select.h"

static struct xz_deck_select selector;
static void feedback(void){view.route_up=1;xz_deck_select_feedback(&selector,&view);}
static enum xz_ds_verdict physical(int d,float value){
 feedback();
 struct xz_ds_key key={.side=d,.code=XZ_DS_KEY_TEMPO,.op=XZ_DS_OP_FADER,.value=value};
 struct xz_ds_outcome out=xz_deck_select_global_player(&selector,&key,clock_ms);
 if(out.verdict==XZ_DS_DELIVER)CHECK(input(out.event));
 step();feedback();return out.verdict;
}
static void begin_hold(const char *name){
 begin(name);
 for(int d=0;d<2;d++){
  load(d,d+1);seek(d,100);press(d,XZ_EXTRA_IN_LOOP_IN);seek(d,1100);press(d,XZ_EXTRA_IN_LOOP_OUT);seek(d,350);
  CHECK(debug(d).loop_active);CHECK(debug(d).loop_in==ms_frames(d,100));CHECK(debug(d).loop_out==ms_frames(d,1100));
 }
 sim_native_set_bpm(131.25);
 xz_deck_select_init(&selector);xz_deck_select_route_instance(&selector,1,fake_link(NULL));
}
static void rate_is(int d,double rate,int bpm){
 CHECK(fabs(debug(d).speed-rate)<1e-10);CHECK(view.deck[d].bpm_x100==bpm);
}
static void follow_exact(int d){press(d,XZ_EXTRA_IN_SYNC);advance(200);rate_is(d,1.09375,13125);}
static void release_exact(int d,int promote){
 if(promote){
  press(d,XZ_EXTRA_IN_MASTER);CHECK(!view.deck[d].master);rate_is(d,1.09375,13125);
  confirm(d,view.master_intent.epoch);step();CHECK(view.deck[d].master);
 }else{press(d,XZ_EXTRA_IN_SYNC);CHECK(!view.deck[d].sync.requested);}
 feedback();rate_is(d,1.09375,13125);
}
static void pickup_matrix(void){
 for(int d=0;d<2;d++)for(int promote=0;promote<2;promote++)
 for(int switching=0;switching<4;switching++)for(int at_target=0;at_target<2;at_target++){
  begin_hold("exact-rate-through-switching-until-fresh-physical-pickup");
  xz_deck_select_toggle(&selector,d,NULL);physical(d,0);follow_exact(d);
  float stationary=at_target?.938f:.5f;
  CHECK(physical(d,stationary)==XZ_DS_SWALLOW);rate_is(d,1.09375,13125);
  if(switching==2)xz_deck_select_toggle(&selector,d,NULL);
  release_exact(d,promote);
  CHECK(selector.side[d].fresh_pickup[XZ_DS_EXTRA]);
  if(switching==1||switching==3)xz_deck_select_toggle(&selector,d,NULL);
  if(switching==3){
   CHECK(physical(d,.98f)!=XZ_DS_DELIVER);CHECK(physical(d,.2f)!=XZ_DS_DELIVER);
   CHECK(physical(d,stationary)!=XZ_DS_DELIVER);rate_is(d,1.09375,13125);
  }
  if(switching)xz_deck_select_toggle(&selector,d,NULL);
  for(int n=0;n<10;n++){advance(100);feedback();rate_is(d,1.09375,13125);}
  CHECK(physical(d,stationary)==XZ_DS_SWALLOW);rate_is(d,1.09375,13125);
  CHECK(physical(d,at_target?.8f:.6f)==XZ_DS_SWALLOW);rate_is(d,1.09375,13125);
  CHECK(physical(d,.98f)==XZ_DS_DELIVER);rate_is(d,1.098,13176);
  CHECK(!selector.side[d].fresh_pickup[XZ_DS_EXTRA]);
  CHECK(physical(d,.7f)==XZ_DS_DELIVER);advance(100);rate_is(d,1.07,12840);
  rate_is(1-d,1.,12000);end();
 }
}
static void direct_manual_commands(void){
 for(int d=0;d<2;d++)for(int promote=0;promote<2;promote++)for(int command_kind=0;command_kind<3;command_kind++){
  begin_hold("accepted-manual-command-replaces-exact-retained-rate");follow_exact(d);
  tempo(d,7000);press(d,XZ_EXTRA_IN_TEMPO_RANGE);press(d,XZ_EXTRA_IN_TEMPO_RESET);
  rate_is(d,1.09375,13125);CHECK(view.deck[d].tempo_range==10);CHECK(!debug(d).tempo_reset);
  release_exact(d,promote);
  if(command_kind==0)tempo(d,7000);
  if(command_kind==1)press(d,XZ_EXTRA_IN_TEMPO_RANGE);
  if(command_kind==2)press(d,XZ_EXTRA_IN_TEMPO_RESET);
  double wanted=command_kind==0?1.07:command_kind==1?1.15:1.;
  int bpm=command_kind==0?12840:command_kind==1?13800:12000;
  rate_is(d,wanted,bpm);advance(300);rate_is(d,wanted,bpm);end();
 }
}
static void no_input_retention(void){
 for(int d=0;d<2;d++)for(int promote=0;promote<2;promote++)for(int below=0;below<2;below++){
  begin_hold("no-input-retains-rate-despite-old-master-updates");
  sim_native_set_bpm(below?108.75:131.25);press(d,XZ_EXTRA_IN_SYNC);advance(200);
  double wanted=below?.90625:1.09375;int bpm=below?10875:13125;
  rate_is(d,wanted,bpm);
  if(promote){press(d,XZ_EXTRA_IN_MASTER);confirm(d,view.master_intent.epoch);step();CHECK(view.deck[d].master);}
  else press(d,XZ_EXTRA_IN_SYNC);
  for(int n=0;n<40;n++){
   sim_native_set_bpm(n%2?125.:130.);advance(100);rate_is(d,wanted,bpm);
  }
  end();
 }
}
static void software_follower_retained_clock(void){
 for(int d=0;d<2;d++){
  begin_hold("promoted-exact-clock-drives-other-software-follower");follow_exact(d);release_exact(d,1);
  press(1-d,XZ_EXTRA_IN_SYNC);
  for(int n=0;n<12;n++){advance(100);rate_is(d,1.09375,13125);rate_is(1-d,1.09375,13125);}
  CHECK(view.deck[d].master);CHECK(view.deck[1-d].sync.requested);
  tempo(d,7000);advance(200);rate_is(d,1.07,12840);rate_is(1-d,1.07,12840);end();
 }
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 pickup_matrix();direct_manual_commands();no_input_retention();software_follower_retained_clock();
 pthread_mutex_destroy(&f.lock);printf("RESULT rate-hold-expanded cases=%u checks=%u failures=%u\n",cases,checks,failed);return failed?1:0;
}
