/* Synthetic 6600-count turn. This checks the configured travel model, not
   measured physical encoder counts or native hardware feel. */
#define main upstream_player_main
#include "../test_extra_player.c"
#undef main
#include "../deck_select.h"

static unsigned checks,failures,epoch=900;
static void check(const char *name,int pass){
 checks++;if(!pass){failures++;fprintf(stderr,"FAIL %s\n",name);}
}
static void tick(unsigned ms){while(ms--){clock_ms++;consume(0,44.1);consume(1,44.1);step();}}

int main(int argc,char **argv){
 assert(argc==2);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);
 for(int side=0;side<2;side++)for(int rate=44100;rate<=48000;rate+=3900)
 for(int playing=0;playing<2;playing++)for(int sign=-1;sign<=1;sign+=2){
  struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,NULL,0,0};
  p=xz_xp_create(&env);assert(p);clock_ms=1000;fake_route(++epoch);step();
  load(side,rate==44100?4:2);seek(side,sign>0?50:1950);
  press(side,XZ_EXTRA_IN_VINYL);if(playing)press(side,XZ_EXTRA_IN_PLAY);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,1);uint64_t start=debug(side).position;
  struct xz_deck_select selector;xz_deck_select_init(&selector);xz_deck_select_route(&selector,1);
  struct xz_ds_outcome selected;xz_deck_select_toggle(&selector,side,&selected);assert(selected.toggled);
  f.capture[side]=1;f.captured_frames[side]=0;f.peak[side]=0;
  uint32_t arrival=clock_ms+16;
  for(int packet=0;packet<100;packet++){
   while(clock_ms!=arrival)tick(1);
   unsigned char raw[24]={0};uint16_t code=0x4305;int32_t delta=sign*66;float speed=0;
   memcpy(raw+8,&code,2);raw[10]=(unsigned char)(side+1);raw[11]=4;
   memcpy(raw+12,&delta,4);memcpy(raw+16,&speed,4);
   struct xz_ds_key key;assert(xz_ds_key_decode_global(raw,&key));
   struct xz_ds_outcome result=xz_deck_select_global_player(&selector,&key,clock_ms);
   assert(result.verdict==XZ_DS_DELIVER&&result.event.jog.delta==delta);
   assert(input(result.event));step();
   if(!packet){
    /* A group at the current millisecond may wait until the next clock tick
       so later packets sharing that timestamp do not multiply its duration. */
    if(f.captured_frames[side]<32)tick(1);
    check("configured first segment produces PCM by the next clock tick",f.captured_frames[side]>=32);
    for(unsigned n=0;n<32&&n<f.captured_frames[side];n++){
     double source=(double)start+sign*1.125*rate/44100.*n;
     check("configured first segment traverses signed PCM at 1.125x",fabs(f.captured[side][n*2]-.5*sin(6.283185307179586*440*source/rate))<.001);
    }
   }
   arrival+=18;
  }
  tick(200);
  double moved=((double)debug(side).position-start)*1000./rate;
  printf("MODEL counts=6600 side=%d source_rate=%d playing=%d direction=%d travel_ms=%.6f\n",side,rate,playing,sign,moved);
  check("configured full turn retains signed 1800ms source travel",fabs(moved-sign*1800.)<=1000./rate+1e-6);
  hold(side,XZ_EXTRA_IN_JOG_TOUCH,0);
  int coasting=debug(side).state==XZ_XP_SCRATCHING;
  struct xz_extra_deck_event stopped=event(side,XZ_EXTRA_IN_JOG_TURN,1);
  stopped.jog.delta=0;stopped.jog.speed=0;stopped.jog.speed_known=1;
  assert(input(stopped));step();tick(70);
  check("released moving platter coasts until known stop then restores prior play state",
        coasting&&debug(side).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));
  xz_xp_destroy(p);p=NULL;
 }
 printf("RESULT configured-jog-model checks=%u failures=%u physical_calibration=unverified\n",checks,failures);
 return failures?1:0;
}
