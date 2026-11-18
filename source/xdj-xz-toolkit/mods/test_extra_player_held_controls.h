// SPDX-License-Identifier: GPL-2.0-only
/* Public input -> actual FIFO/worker/decoder -> synthetic PCM transport. */
#include "deck_select.h"
#ifdef XZ_XP_SERVICE_PROOF
#include "xp_service_proof.h"
#endif
static unsigned hc_checks,hc_failures,hc_cases;
static int hc_side,hc_capture;
static uint32_t hc_frames;
static uint64_t hc_after;
static float hc_pcm[XZ_USB_BLOCK_FRAMES*2];
#define HC(test) do { hc_checks++; if(!(test)){hc_failures++;printf("RED held case=%u line=%d check=%s\n",hc_cases,__LINE__,#test);} } while(0)
static void hc_record(int d,const float *pcm,uint32_t n,uint64_t after){
 if(hc_capture&&d==hc_side){hc_capture=0;hc_frames=n;hc_after=after;memcpy(hc_pcm,pcm,n*2*sizeof(float));}
}
static xz_usb_result hc_submit(void *ctx,uint32_t epoch,int d,uint32_t generation,const float *pcm,uint32_t n,uint64_t after){
 xz_usb_result r=fake_submit(ctx,epoch,d,generation,pcm,n,after);if(r==XZ_USB_OK)hc_record(d,pcm,n,after);return r;
}
static xz_usb_result hc_transition(void *ctx,uint32_t epoch,int d,uint32_t expected,const float *pcm,uint32_t n,uint64_t after,uint32_t *generation){
 xz_usb_result r=fake_transition(ctx,epoch,d,expected,pcm,n,after,generation);if(r==XZ_USB_OK)hc_record(d,pcm,n,after);return r;
}
static void hc_setup(int d,int playing){
 hc_cases++;hc_side=d;hc_capture=hc_frames=0;clock_ms=1000;
 wire_horizon=0;position_interval=position_noise=position_blocked=0;threaded_clock=0;hold_flush=0;
 struct xz_xp_env env={{NULL,fake_link,hc_submit,fake_flush,fake_position,hc_transition},now_ms,resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);fake_route(3000+hc_cases);step();load(d,d?2:1);load(1-d,d?1:2);seek(d,700);seek(1-d,1000);
 if(playing)press(d,XZ_EXTRA_IN_PLAY);
}
static void hc_close(void){xz_xp_destroy(p);p=NULL;}
static struct xz_extra_deck_event hc_pad(int d,int pad,int down){
 struct xz_extra_deck_event e=event(d,XZ_EXTRA_IN_HOT_CUE,down);e.hot_cue=pad;return e;
}
static void hc_fill(int d){for(unsigned i=0;i<XZ_XP_QUEUE;i++)assert(input(event(d,XZ_EXTRA_IN_JOG_TURN,1)));}
static void hc_cue(int d,int pad){
 mode(d,XZ_EXTRA_PAD_HOT_CUE,0);struct xz_extra_deck_event e=hc_pad(d,pad,1);e.shift=1;assert(input(e));step();pad_key(d,pad,0);
 pad_key(d,pad,1);pad_key(d,pad,0); /* Store a cue without starting playback. */
}
static void hc_begin(int d,int pad,int hot){
 if(hot){press(d,XZ_EXTRA_IN_SLIP);mode(d,XZ_EXTRA_PAD_HOT_CUE,0);}else mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);
 pad_key(d,pad,1);
}
static void hc_pcm_forward(int d,uint64_t origin){
 HC(hc_frames>=32);double error=0,rate=debug(d).source_rate;
 for(unsigned n=0;n<hc_frames;n++)for(unsigned ch=0;ch<2;ch++)
  error=fmax(error,fabs(hc_pcm[n*2+ch]-.5*sin(6.283185307179586*440*(origin+rate/XZ_USB_RATE*n)/rate)));
 HC(error<.001);HC(near_frames(hc_after,origin+(uint64_t)(hc_frames*rate/XZ_USB_RATE),1));
}
static void hc_modes(void){
 for(int d=0;d<2;d++)for(int hot=0;hot<2;hot++)for(int pad=0;pad<XZ_EXTRA_HOT_CUES;pad++)
 for(int playing=0;playing<2;playing++)for(int dest=0;dest<4;dest++){
  hc_setup(d,0);if(hot)hc_cue(d,pad);seek(d,700);if(playing)press(d,XZ_EXTRA_IN_PLAY);
  hc_begin(d,pad,hot);if(playing)assert(view.deck[d].slip_active);else assert(!view.deck[d].slip_active);
  advance(100);mode(d,dest==0?XZ_EXTRA_PAD_HOT_CUE:dest==1?XZ_EXTRA_PAD_BEAT_JUMP:dest==2?XZ_EXTRA_PAD_BEAT_LOOP:XZ_EXTRA_PAD_SLIP_LOOP,dest==0);
  unsigned other_gen=debug(1-d).generation,other_flush=f.flushes[1-d],before=f.flushes[d],submits=f.submits[d];
  hc_capture=playing;hc_frames=0;HC(input(hc_pad(d,pad,0)));step();
  HC(!view.deck[d].slip_active);if(!hot)HC(!debug(d).loop_active&&view.deck[d].active_pad==-1);
  HC(debug(d).state==((playing||hot)?XZ_XP_PLAYING:XZ_XP_CUED));
  HC(debug(1-d).generation==other_gen&&f.flushes[1-d]==other_flush);
  if(playing){HC(f.flushes[d]==before+1);hc_pcm_forward(d,ms_frames(d,800));advance(60);HC(completed_tag(d)>=ms_frames(d,800));}
  else if(hot)HC(f.flushes[d]==before);else HC(f.submits[d]==submits);
  hc_close();
 }
}
static void hc_queue(void){
 for(int d=0;d<2;d++)for(int hot=0;hot<2;hot++)for(int pad=0;pad<XZ_EXTRA_HOT_CUES;pad++){
  hc_setup(d,0);if(hot)hc_cue(d,pad);seek(d,700);press(d,XZ_EXTRA_IN_PLAY);hc_begin(d,pad,hot);
  mode(d,XZ_EXTRA_PAD_HOT_CUE,1);hc_fill(d);HC(!input(event(d,XZ_EXTRA_IN_JOG_TURN,1)));
  HC(input(hc_pad(d,pad,0)));step();HC(!view.deck[d].slip_active&&!debug(d).loop_active);hc_close();
 }
 for(int d=0;d<2;d++){
  hc_setup(d,1);press(d,XZ_EXTRA_IN_SLIP);hold(d,XZ_EXTRA_IN_REVERSE,1);assert(view.deck[d].reverse&&view.deck[d].slip_active);
  advance(100);uint64_t reverse_tag=completed_tag(d);HC(reverse_tag<ms_frames(d,700));hc_fill(d);
  hc_capture=1;hc_frames=0;HC(input(event(d,XZ_EXTRA_IN_REVERSE,0)));step();
  HC(!view.deck[d].reverse&&!view.deck[d].slip_active);hc_pcm_forward(d,ms_frames(d,800));hc_close();
 }
 /* Every distinct hold may be accepted together. Exactly 64 ordinary + 12 owed ups fit. */
 for(int d=0;d<2;d++)for(int rotation=0;rotation<12;rotation++){
  hc_setup(d,1);hold(d,XZ_EXTRA_IN_CUE,1);hold(d,XZ_EXTRA_IN_JOG_TOUCH,1);hold(d,XZ_EXTRA_IN_LOOP_IN,1);
  hold(d,XZ_EXTRA_IN_REVERSE,1);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);
  for(int pad=0;pad<XZ_EXTRA_HOT_CUES;pad++)pad_key(d,pad,1);
  hc_fill(d);HC(!input(hc_pad(d,7,1)));
  for(int i=0;i<12;i++){
   int k=(i+rotation)%12;struct xz_extra_deck_event e=k<8?hc_pad(d,k,0):event(d,k==8?XZ_EXTRA_IN_REVERSE:k==9?XZ_EXTRA_IN_CUE:k==10?XZ_EXTRA_IN_JOG_TOUCH:XZ_EXTRA_IN_LOOP_IN,0);
   HC(input(e));
  }
#ifdef XZ_XP_SERVICE_PROOF
  struct xp_sp_input_block *latest=&xz_mods_xp_service_proof_v1.deck[d].latest;
  HC(atomic_load(&latest->word[XP_SP_INPUT_RELEASE_OWED])==0);
  HC(atomic_load(&latest->word[XP_SP_INPUT_QUEUE_DEPTH])==XZ_XP_QUEUE+12);
#endif
  step();step();HC(!debug(d).loop_active&&!view.deck[d].slip_active);hc_fill(d);HC(!input(event(d,XZ_EXTRA_IN_JOG_TURN,1)));step();hc_close();
 }
}
static void hc_ownership(void){
 for(int d=0;d<2;d++)for(int first=0;first<8;first++)for(int second=0;second<8;second++)if(first!=second){
  hc_setup(d,1);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);pad_key(d,first,1);pad_key(d,second,1);
  uint64_t in=debug(d).loop_in,out=debug(d).loop_out;unsigned gen=debug(d).generation;
  pad_key(d,first,0);HC(view.deck[d].slip_active&&debug(d).loop_active&&view.deck[d].active_pad==second);
  HC(debug(d).generation==gen&&debug(d).loop_in==in&&debug(d).loop_out==out);
  pad_key(d,second,0);HC(!view.deck[d].slip_active&&!debug(d).loop_active);hc_close();
 }
 for(int d=0;d<2;d++)for(int first=0;first<8;first++)for(int second=0;second<8;second++)if(first!=second){
  hc_setup(d,0);hc_cue(d,first);hc_cue(d,second);seek(d,700);press(d,XZ_EXTRA_IN_PLAY);press(d,XZ_EXTRA_IN_SLIP);
  pad_key(d,first,1);pad_key(d,second,1);unsigned gen=debug(d).generation;
  mode(d,XZ_EXTRA_PAD_HOT_CUE,1);hc_fill(d);HC(input(hc_pad(d,first,0)));step();
  HC(view.deck[d].slip_active&&debug(d).generation==gen);pad_key(d,second,0);HC(!view.deck[d].slip_active);hc_close();
 }
 for(int d=0;d<2;d++)for(int oldhot=0;oldhot<2;oldhot++)for(int newer=0;newer<3;newer++){
  hc_setup(d,0);hc_cue(d,0);seek(d,700);press(d,XZ_EXTRA_IN_PLAY);hc_begin(d,0,oldhot);
  if(newer==0){mode(d,XZ_EXTRA_PAD_BEAT_LOOP,0);pad_key(d,1,1);pad_key(d,1,0);}
  if(newer==1){mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);pad_key(d,1,1);}
  if(newer==2){if(!view.deck[d].slip)press(d,XZ_EXTRA_IN_SLIP);hold(d,XZ_EXTRA_IN_REVERSE,1);}
  unsigned gen=debug(d).generation;uint64_t in=debug(d).loop_in,out=debug(d).loop_out;
  hc_fill(d);HC(input(hc_pad(d,0,0)));step();
  HC(debug(d).generation==gen&&debug(d).loop_in==in&&debug(d).loop_out==out);
  if(newer<2)HC(debug(d).loop_active&&view.deck[d].active_pad==1);
  else HC(view.deck[d].reverse&&view.deck[d].slip_active);
  if(newer==1)pad_key(d,1,0);if(newer==2)hold(d,XZ_EXTRA_IN_REVERSE,0);
  hc_close();
 }
 for(int d=0;d<2;d++){
  hc_setup(d,1);press(d,XZ_EXTRA_IN_SLIP);hold(d,XZ_EXTRA_IN_REVERSE,1);
  mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);pad_key(d,3,1);unsigned gen=debug(d).generation;
  HC(!view.deck[d].reverse);hold(d,XZ_EXTRA_IN_REVERSE,0);
  HC(debug(d).generation==gen&&view.deck[d].slip_active&&debug(d).loop_active);
  pad_key(d,3,0);HC(!debug(d).loop_active&&!view.deck[d].slip_active);hc_close();
 }
}
static void hc_refused_takeover(void){
 for(int d=0;d<2;d++){
  hc_setup(d,1);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);pad_key(d,2,1);
  mode(d,XZ_EXTRA_PAD_BEAT_LOOP,0);press(d,XZ_EXTRA_IN_SLIP);pad_key(d,7,1);pad_key(d,7,0);
  HC(view.deck[d].slip_active&&view.deck[d].active_pad==2);pad_key(d,2,0);HC(!view.deck[d].slip_active&&!debug(d).loop_active);hc_close();
 }
}
static void hc_repeats_cancel(void){
 for(int d=0;d<2;d++)for(int rev=0;rev<2;rev++){
  hc_setup(d,1);press(d,XZ_EXTRA_IN_SLIP);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);
  struct xz_extra_deck_event down=rev?event(d,XZ_EXTRA_IN_REVERSE,1):hc_pad(d,5,1),up=down;up.pressed=0;
  assert(input(down));step();unsigned gen=debug(d).generation;uint64_t in=debug(d).loop_in,out=debug(d).loop_out;
  HC(input(down));step();down.held=1;HC(input(down));step();
  HC(debug(d).generation==gen&&debug(d).loop_in==in&&debug(d).loop_out==out&&view.deck[d].slip_active);
  HC(input(up));step();gen=debug(d).generation;HC(input(up));step();HC(input(down));step();
  HC(debug(d).generation==gen&&!view.deck[d].slip_active&&!view.deck[d].reverse&&!debug(d).loop_active);
  hc_close();
 }
 for(int d=0;d<2;d++)for(int rev=0;rev<2;rev++)for(int cancel=0;cancel<5;cancel++){
  hc_setup(d,1);press(d,XZ_EXTRA_IN_SLIP);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);
  struct xz_extra_deck_event down=rev?event(d,XZ_EXTRA_IN_REVERSE,1):hc_pad(d,6,1),up=down;up.pressed=0;
  assert(input(down));step();
  if(cancel==0)press(d,XZ_EXTRA_IN_CUE);
  if(cancel==1)load(d,d?1:2);
  if(cancel==2)load(d,3);
  if(cancel==3){fake_route(9000+hc_cases);step();}
  if(cancel==4)press(d,XZ_EXTRA_IN_SLIP);
  HC(!view.deck[d].slip_active&&!view.deck[d].reverse);unsigned gen=debug(d).generation;
  HC(input(up));step();HC(debug(d).generation==gen&&!view.deck[d].slip_active&&!view.deck[d].reverse);hc_close();
 }
 /* A queued old-route press/up pair must never affect a freshly accepted route gesture. */
 for(int d=0;d<2;d++){
  hc_setup(d,1);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);assert(input(hc_pad(d,1,1)));assert(input(hc_pad(d,1,0)));
  fake_route(10000+hc_cases);assert(input(hc_pad(d,2,1)));step();HC(debug(d).loop_active&&view.deck[d].active_pad==2);
  hc_fill(d);HC(input(hc_pad(d,2,0)));step();HC(!debug(d).loop_active&&!view.deck[d].slip_active);hc_close();
 }
 for(int d=0;d<2;d++)for(int index=-2;index<=40;index++)if(index<0||index>=8){
  hc_setup(d,1);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);unsigned gen=debug(d).generation;
  (void)input(hc_pad(d,index,1));(void)input(hc_pad(d,index,0));step();HC(debug(d).generation==gen&&!view.deck[d].slip_active&&!debug(d).loop_active);hc_close();
 }
}
static void hc_coast_decoder(void){
 for(int d=0;d<2;d++)for(int playing=0;playing<2;playing++){
  hc_setup(d,playing);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);pad_key(d,2,1);press(d,XZ_EXTRA_IN_VINYL);hold(d,XZ_EXTRA_IN_JOG_TOUCH,1);
  struct xz_extra_deck_event joge=event(d,XZ_EXTRA_IN_JOG_TURN,1);joge.jog.delta=8;joge.jog.speed=.2f;joge.jog.speed_known=1;
  assert(input(joge));step();hold(d,XZ_EXTRA_IN_JOG_TOUCH,0);assert(debug(d).state==XZ_XP_SCRATCHING);
  mode(d,XZ_EXTRA_PAD_HOT_CUE,1);pad_key(d,2,0);HC(!debug(d).loop_active&&!view.deck[d].slip_active&&debug(d).state==XZ_XP_SCRATCHING);
  unsigned gen=debug(d).generation;uint64_t target=debug(d).scratch_target;
  pad_key(d,4,1);pad_key(d,4,0);HC(debug(d).state==XZ_XP_SCRATCHING&&debug(d).generation==gen&&debug(d).scratch_target==target);
  joge.jog.delta=0;joge.jog.speed=0;assert(input(joge));step();advance(70);HC(debug(d).state==(playing?XZ_XP_PLAYING:XZ_XP_CUED));hc_close();
 }
 for(int d=0;d<2;d++)for(int pad=0;pad<8;pad++){
  hc_setup(d,1);mode(d,XZ_EXTRA_PAD_SLIP_LOOP,0);struct xz_deck_select selector;xz_deck_select_init(&selector);xz_deck_select_route(&selector,1);xz_deck_select_toggle(&selector,d,NULL);
  for(int edge=0;edge<2;edge++){
   unsigned char bytes[24]={0};uint16_t code=(uint16_t)(0x4119+pad);memcpy(bytes+8,&code,2);bytes[10]=(unsigned char)(d+1);bytes[11]=edge?2:0;
   struct xz_ds_key key;assert(xz_ds_key_decode_global(bytes,&key));struct xz_ds_outcome o=xz_deck_select_global_player(&selector,&key,clock_ms);
   assert(o.verdict==XZ_DS_DELIVER&&o.event.deck==d&&o.event.hot_cue==pad);
   if(edge){mode(d,XZ_EXTRA_PAD_HOT_CUE,1);hc_fill(d);}HC(input(o.event));step();
  }
  HC(!debug(d).loop_active&&!view.deck[d].slip_active);hc_close();
 }
}
/* A successful loop move supersedes held REV just like a non-loop BEAT JUMP. */
static void hc_reverse_loop_move(void){
 for(int d=0;d<2;d++)for(int forward=0;forward<2;forward++){
  hc_setup(d,0);load(d,1);seek(d,700);press(d,XZ_EXTRA_IN_PLAY);
  mode(d,XZ_EXTRA_PAD_BEAT_LOOP,0);pad_key(d,0,1);pad_key(d,0,0);
  press(d,XZ_EXTRA_IN_SLIP);hold(d,XZ_EXTRA_IN_REVERSE,1);
  assert(view.deck[d].reverse&&view.deck[d].slip_active&&debug(d).loop_active);
  uint64_t old=debug(d).loop_in;mode(d,XZ_EXTRA_PAD_BEAT_JUMP,0);
  hc_capture=1;hc_frames=0;pad_key(d,forward,1);pad_key(d,forward,0);
  HC(debug(d).loop_in==(forward?old+ms_frames(d,500):old-ms_frames(d,500)));
  HC(!view.deck[d].reverse);hc_pcm_forward(d,debug(d).loop_in);
  unsigned generation=debug(d).generation;uint64_t in=debug(d).loop_in,out=debug(d).loop_out;
  hc_fill(d);HC(input(event(d,XZ_EXTRA_IN_REVERSE,0)));step();
  HC(debug(d).generation==generation&&debug(d).loop_active&&debug(d).loop_in==in&&debug(d).loop_out==out);
  hc_close();
 }
}
static int test_held_controls(void){
 hc_modes();hc_queue();hc_ownership();hc_refused_takeover();hc_repeats_cancel();hc_coast_decoder();hc_reverse_loop_move();
 printf("RESULT held_controls_permanent cases=%u checks=%u failures=%u profile=%d\n",hc_cases,hc_checks,hc_failures,XZ_XP_JOG_TICKS_PER_REV);
 return hc_failures==0;
}
#undef HC
