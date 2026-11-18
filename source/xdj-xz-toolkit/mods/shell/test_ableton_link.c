#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* PLAY's ABLETON LINK control in the top bar, between A. CUE and the BEAT FX strip (shell.h xz_shell_ableton_rect),
   and the three mode chips left of it. Pinned here, in two decks (also with a BELOW FULL and an OUTSIDE FULL stem
   row), four lanes, mixer order and a four-lane DECK VIEW while two decks play:
   - the chips: SINGLE over CONTINUE x 8..66, A. HOT CUE 73..134, A. CUE 141..180, each y 1..24; each answers in its
     own columns from y 0 to the header band's foot, the gaps between them answer nothing, and each shows its state;
   - without a Link runtime (ableton.known 0) the control is absent; the strip has its place before the control;
   - DISABLED adds exactly the control, x 187..244, y 1..24, the same in every view, and the BEAT FX strip keeps its
     place, size and drawing (x 251..497, y 2..23, the time readout included) in every Link state; every effect's
     name shows in full at its widest beat and time, the time giving way where all three do not fit;
   - every state changes nothing outside the control's rectangle;
   - each state's drawing: grey outline and grey LINK; blue outline, white LINK and a dash; blue fill with the count
     in the large face, a white badge from one other app on; dark red with a red edge and "!"; each count draws
     differently;
   - the touch target is the control's columns from y 0 to the header band's foot; the dead columns on both sides
     answer nothing, A. CUE still answers beside them, the strip answers nothing, and below the bar a stem button of
     the top lane wins;
   - with the mode lamps hidden it moves to the bar's left edge and the strip follows it. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include "shell_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=XZ_SHELL_WIDTH, H=XZ_SHELL_HEIGHT };
static struct fx_track track;
static uint16_t before[W*H],after[W*H];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *p){
 memset(p,0,sizeof before);assert(xz_shell_render(u,m,p,W));
}
static int inside(struct xz_shell_rect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
static int same_rect(struct xz_shell_rect a,struct xz_shell_rect b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}
/* The bounding box of the pixels that differ; w 0 when none. */
static struct xz_shell_rect diff(void){
 int x0=W,y0=H,x1=-1,y1=-1;
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(before[y*W+x]!=after[y*W+x]){
  if(x<x0)x0=x;if(y<y0)y0=y;if(x>x1)x1=x;if(y>y1)y1=y;
 }
 return x1<0?(struct xz_shell_rect){0,0,0,0}:(struct xz_shell_rect){x0,y0,x1-x0+1,y1-y0+1};
}
static int count(struct xz_shell_rect r,uint32_t c){
 int n=0;uint16_t v=rgb565(c);
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=after[y*W+x]==v;
 return n;
}
static uint16_t at(int x,int y){return after[y*W+x];}
/* Pixels at least three quarters lit in green (white ink, smoothed or not; none of the fills is). */
static int light(struct xz_shell_rect r){
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=((after[y*W+x]>>5)&63)>=48;
 return n;
}
static size_t tap(struct xz_shell *u,const struct xz_shell_model *m,int x,int y){
 size_t n=xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1},out);
 struct xz_shell_action up[XZ_SHELL_ACTIONS];
 xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1},up);
 return n;
}
static int link_asked(size_t n){for(size_t i=0;i<n;i++)if(out[i].kind==XZ_SHELL_ACT_ABLETON_LINK)return 1;return 0;}

/* The geometry the manual's pictures are re-accepted by. */
static const struct xz_shell_rect CONTROL={187,1,58,24},STRIP={251,2,247,22};
static const struct xz_shell_rect CHIPS[3]={{8,1,59,24},{73,1,62,24},{141,1,40,24}};

enum view { TWO, TWO_STEMS, TWO_OUTSIDE, FOUR, FOUR_MIXER, FOUR_VIEW_TWO, VIEWS };
static void setup(struct xz_shell *u,struct xz_shell_model *m,enum view v){
 xz_shell_init(u);
 if(v==FOUR_VIEW_TWO){fx_model_full(m,&track);m->layout_preference=2;}
 else if(v>=FOUR){fx_model_four_players(m,&track);m->layout_preference=v==FOUR?2:3;m->wave_order=v==FOUR_MIXER;}
 else fx_model_full(m,&track);
 m->stems_known=1;
 if(v==TWO_STEMS||v==TWO_OUTSIDE){
  m->stems_on=1;m->stem_layout_known=1;m->stem_layout=v==TWO_STEMS?XZ_SHELL_STEMS_BELOW_FULL:XZ_SHELL_STEMS_OUTSIDE_FULL;
  m->capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;   /* stem buttons in the rows, as test_stems.c */
  for(int d=0;d<2;d++)m->deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
 }
}
static void set(struct xz_shell_model *m,enum xz_shell_ableton_state s,unsigned peers){
 m->ableton=(struct xz_shell_ableton){1,s!=XZ_SHELL_ABLETON_OFF,s,peers};
}

static void test_off_rectangle(void){
 struct xz_shell u;struct xz_shell_model m;
 for(int v=0;v<VIEWS;v++){
  setup(&u,&m,(enum view)v);
  assert(!xz_shell_ableton_rect(&u,&m).w&&!xz_shell_ableton_rect(NULL,&m).w);
  /* No Link runtime: the strip's place before the control existed. */
  assert(same_rect(xz_shell_play_rect(&m,XZ_SHELL_R_FX,0),STRIP));
  render(&u,&m,before);
  set(&m,XZ_SHELL_ABLETON_OFF,0);
  struct xz_shell_rect r=xz_shell_ableton_rect(&u,&m);
  assert(same_rect(r,CONTROL)&&same_rect(xz_shell_ableton_rect(NULL,&m),CONTROL));
  assert(same_rect(xz_shell_play_rect(&m,XZ_SHELL_R_FX,0),STRIP));
  render(&u,&m,after);
  /* Exactly the control: its outline is on all four edges of the change. */
  struct xz_shell_rect d=diff();
  assert(same_rect(d,CONTROL));
  /* The dead columns beside the control stay the bar's chrome. */
  for(int y=0;y<XZ_SHELL_ABLETON_H;y++)for(int x=0;x<XZ_SHELL_ABLETON_GAP;x++){
   assert(at(r.x-1-x,r.y+y)==rgb565(SH_CHROME));
   assert(at(r.x+r.w+x,r.y+y)==rgb565(SH_CHROME));
  }
  /* A. CUE ends where the left gap starts; the strip's outline starts where the right one ends. */
  assert(r.x-XZ_SHELL_ABLETON_GAP==CHIPS[2].x+CHIPS[2].w&&at(r.x-XZ_SHELL_ABLETON_GAP-1,10)!=rgb565(SH_CHROME));
  assert(STRIP.x==r.x+r.w+XZ_SHELL_ABLETON_GAP&&at(STRIP.x,10)==rgb565(SH_RAIL));
 }
}

static void test_states(void){
 struct xz_shell u;struct xz_shell_model m;
 static const struct { enum xz_shell_ableton_state s; unsigned peers; } states[]={
  {XZ_SHELL_ABLETON_OFF,0},{XZ_SHELL_ABLETON_STARTING,0},{XZ_SHELL_ABLETON_ON,0},{XZ_SHELL_ABLETON_ON,1},
  {XZ_SHELL_ABLETON_ON,2},{XZ_SHELL_ABLETON_ON,12},{XZ_SHELL_ABLETON_ON,250},{XZ_SHELL_ABLETON_FAILED,0}};
 enum { N=sizeof states/sizeof *states, KW=22, KH=18 };
 static uint16_t cells[N][KW*KH];
 for(int v=0;v<VIEWS;v++){
  setup(&u,&m,(enum view)v);set(&m,XZ_SHELL_ABLETON_OFF,0);render(&u,&m,before);
  for(int i=0;i<N;i++){
   set(&m,states[i].s,states[i].peers);
   struct xz_shell_rect r=xz_shell_ableton_rect(&u,&m);
   assert(same_rect(r,CONTROL)&&same_rect(xz_shell_play_rect(&m,XZ_SHELL_R_FX,0),STRIP));
   render(&u,&m,after);
   /* Nothing outside the control changes, in any state: the strip never moves. */
   struct xz_shell_rect d=diff();
   assert(i==0?!d.w:(d.w&&inside(r,d.x,d.y)&&inside(r,d.x+d.w-1,d.y+d.h-1)));
   struct xz_shell_rect name={r.x+1,r.y+1,31,r.h-2};
   struct xz_shell_rect k={r.x+r.w-3-KW,r.y+3,KW,KH};
   switch(states[i].s){
   case XZ_SHELL_ABLETON_OFF:
    assert(at(r.x,r.y)==rgb565(0x5a5a5a)&&at(r.x+r.w-1,r.y+r.h-1)==rgb565(0x5a5a5a)&&at(r.x+2,r.y+2)==rgb565(SH_BLACK));
    assert(count(name,0x8c8c8c)>15&&!count(r,SH_WHITE)&&!count(r,SH_BLUE));
    assert(!light(k));
    break;
   case XZ_SHELL_ABLETON_STARTING:
    assert(at(r.x,r.y)==rgb565(SH_BLUE)&&at(r.x+2,r.y+2)==rgb565(SH_BLACK));
    assert(count(name,SH_WHITE)>15&&count(k,SH_BLACK)<k.w*k.h&&!light(k));  /* a grey dash */
    break;
   case XZ_SHELL_ABLETON_ON:
    assert(at(r.x+2,r.y+2)==rgb565(SH_BLUE)&&count(name,SH_WHITE)>15);
    {
     int corners=(at(k.x,k.y)==rgb565(SH_WHITE))+(at(k.x+k.w-1,k.y+k.h-1)==rgb565(SH_WHITE));
     assert(at(k.x-1,k.y)==rgb565(SH_BLUE)&&at(k.x+k.w,k.y+k.h-1)==rgb565(SH_BLUE));
     if(states[i].peers)assert(corners==2&&count(k,SH_WHITE)<k.w*k.h-3);   /* white badge, digits in it */
     else assert(!corners&&light(k)>12);                          /* a white 0 on the blue, in the large face */
    }
    break;
   case XZ_SHELL_ABLETON_FAILED:
    assert(at(r.x,r.y)==rgb565(SH_RED)&&at(r.x+2,r.y+2)==rgb565(0x5a0c0c));
    assert(count(name,SH_WHITE)>15&&light(k)>5); /* "!" is drawn smoothed: light, not all pure white */
    break;
   }
   if(v==TWO)for(int y=0;y<KH;y++)memcpy(&cells[i][y*KW],&after[(k.y+y)*W+k.x],KW*2);
  }
 }
 /* Each count draws differently; more than 99 shows 99 (states 5 and 6 are 12 and 250). */
 for(int i=1;i<N;i++)for(int j=i+1;j<N;j++)
  if(!(i==5&&j==6))assert(memcmp(cells[i],cells[j],sizeof cells[i]));
}

static void test_touch(void){
 struct xz_shell u;struct xz_shell_model m;
 for(int v=0;v<VIEWS;v++)for(int on=0;on<2;on++){
  setup(&u,&m,(enum view)v);set(&m,on?XZ_SHELL_ABLETON_ON:XZ_SHELL_ABLETON_OFF,2);
  struct xz_shell_rect r=xz_shell_ableton_rect(&u,&m);
  /* The corners of the target (x 187..244, y 0..43) and the middle of the drawing. Below the bar the top lane's
     stem buttons win in OUTSIDE layouts, so there only the bar's rows are tried. */
  int foot=v==TWO_OUTSIDE?XZ_SHELL_ABLETON_Y+XZ_SHELL_ABLETON_H:XZ_SHELL_ABLETON_HIT_H-1;
  const int px[5][2]={{r.x,0},{r.x+r.w-1,0},{r.x,foot},{r.x+r.w-1,foot},{r.x+r.w/2,r.y+r.h/2}};
  for(int p=0;p<5;p++){
   size_t n=tap(&u,&m,px[p][0],px[p][1]);
   assert(n==1&&out[0].kind==XZ_SHELL_ACT_ABLETON_LINK&&out[0].value==!on&&out[0].deck==-1);
  }
  /* The dead columns on both sides answer nothing at all; the strip answers nothing; under the band no Link. */
  for(int g=1;g<=XZ_SHELL_ABLETON_GAP;g++){
   assert(tap(&u,&m,r.x-g,10)==0);
   assert(tap(&u,&m,r.x+r.w-1+g,10)==0);
  }
  for(int x=STRIP.x;x<STRIP.x+STRIP.w;x+=16)assert(tap(&u,&m,x,10)==0);
  assert(!link_asked(tap(&u,&m,r.x+5,XZ_SHELL_ABLETON_HIT_H)));
  /* A. CUE still answers just past the left gap: a miss to the left lands in the gap or on A. CUE, never both. */
  size_t n=tap(&u,&m,r.x-XZ_SHELL_ABLETON_GAP-1,10);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_SET_AUTO_CUE);
 }
 /* OUTSIDE FULL: DECK 1's stem row is at the top of its lane; its button under the band wins below the bar. */
 setup(&u,&m,TWO_OUTSIDE);set(&m,XZ_SHELL_ABLETON_ON,2);
 {
  struct xz_shell_rect r=xz_shell_ableton_rect(&u,&m);
  int x=r.x+r.w/2,y=XZ_SHELL_ABLETON_HIT_H-4,hit=0;
  for(int s=0;s<3;s++)hit|=inside(xz_shell_stem_button(&u,&m,0,s),x,y);
  assert(hit);
  assert(!link_asked(tap(&u,&m,x,y)));
  assert(link_asked(tap(&u,&m,x,10)));
 }
 /* No Link runtime: nothing there answers, and the strip (which takes no touch) is back in its old place. */
 setup(&u,&m,TWO);
 assert(tap(&u,&m,CONTROL.x+5,10)==0&&!link_asked(tap(&u,&m,CONTROL.x+5,30)));
}

/* APPEARANCE hides the mode lamps: the control takes the bar's left edge and the strip follows it. */
static void test_lamps_hidden(void){
 struct xz_shell u;struct xz_shell_model m;
 setup(&u,&m,TWO);set(&m,XZ_SHELL_ABLETON_OFF,0);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_MODE_LAMPS,1)==1);
 struct xz_shell_rect r=xz_shell_ableton_rect(&u,&m);
 assert(same_rect(r,((struct xz_shell_rect){8,1,58,24})));
 render(&u,&m,after);
 assert(at(r.x,r.y)==rgb565(0x5a5a5a)&&at(r.x+r.w+XZ_SHELL_ABLETON_GAP-1,10)==rgb565(SH_CHROME));
 assert(at(r.x+r.w+XZ_SHELL_ABLETON_GAP,10)==rgb565(SH_RAIL));    /* the strip's outline */
 size_t n=tap(&u,&m,r.x+2,10);
 assert(n==1&&out[0].kind==XZ_SHELL_ACT_ABLETON_LINK&&out[0].value==1);
 assert(tap(&u,&m,r.x+r.w+2,10)==0);
}

/* The BEAT FX strip beside the control is the strip without it: for every effect at its widest (1/16, the longest
   TIME the stock manual gives, BPM with AUTO or TAP) and at 16 beats, FX on and off, in every Link state, the strip's
   pixels are those drawn with no Link runtime, and the TIME readout still shows (turning the knob changes it). */
static const struct { const char *name; int32_t ms; } effects[]={
 {"DELAY",4000},{"ECHO",4000},{"PING PONG",4000},{"SPIRAL",4000},{"REVERB",-1},{"TRANS",16000},{"FILTER",32000},
 {"FLANGER",32000},{"PHASER",32000},{"PITCH",-1},{"SLIP ROLL",4000},{"ROLL",4000},{"VINYL BRAKE",4000},{"HELIX",4000}};
static void fx_set(struct xz_shell_model *m,int on,const char *name,int num,int den,int32_t ms){
 fx_beat_fx(&m->fx,on,name,ms<0?0:num,den,ms);m->fx.bpm_x10=1296;
}
static int strip_same(void){
 for(int y=STRIP.y;y<STRIP.y+STRIP.h;y++)if(memcmp(&before[y*W+STRIP.x],&after[y*W+STRIP.x],STRIP.w*2))return 0;
 return 1;
}
static void test_strip_kept(void){
 struct xz_shell u;struct xz_shell_model m;
 static const enum xz_shell_ableton_state states[]={XZ_SHELL_ABLETON_OFF,XZ_SHELL_ABLETON_STARTING,XZ_SHELL_ABLETON_ON,XZ_SHELL_ABLETON_FAILED};
 for(unsigned e=0;e<sizeof effects/sizeof *effects;e++)for(int on=0;on<2;on++)for(int beat=0;beat<2;beat++){
  setup(&u,&m,e&1?FOUR:TWO);fx_set(&m,on,effects[e].name,beat?16:1,beat?1:16,effects[e].ms);m.fx.tap=e==12;
  render(&u,&m,before);
  for(unsigned s=0;s<sizeof states/sizeof *states;s++){set(&m,states[s],2);render(&u,&m,after);assert(strip_same());}
 }
 /* The TIME readout is there: another time redraws the strip, and only the strip, with Link on. */
 setup(&u,&m,TWO);set(&m,XZ_SHELL_ABLETON_ON,2);fx_set(&m,1,"ECHO",1,2,234);render(&u,&m,before);
 fx_set(&m,1,"ECHO",1,2,4000);render(&u,&m,after);
 {struct xz_shell_rect d=diff();assert(d.w&&inside(STRIP,d.x,d.y)&&inside(STRIP,d.x+d.w-1,d.y+d.h-1));}
 /* The name comes first: every effect's name is drawn in full at its widest beat and time (checked by drawing it over
    the strip's own fill and finding the frame unchanged), and where name, beat and time do not all fit the time is
    left out: VINYL BRAKE and PING PONG at 1/16 draw the same at any time, DELAY there still shows it. */
 for(unsigned e=0;e<sizeof effects/sizeof *effects;e++){
  setup(&u,&m,TWO);set(&m,XZ_SHELL_ABLETON_OFF,0);fx_set(&m,1,effects[e].name,1,16,effects[e].ms);render(&u,&m,after);
  const struct xz_canvas c={before,W,W,H,XZ_THEME_FLAGSHIP};
  int x=STRIP.x+6+sh_text_width(c,"FX",SH_SMALL)+8;
  memcpy(before,after,sizeof before);
  xz_rect(c,x,STRIP.y+1,sh_text_width(c,effects[e].name,SH_TEXT),STRIP.h-2,SH_RED);
  sh_text(c,x,STRIP.y+3,effects[e].name,SH_TEXT,SH_WHITE);
  assert(!memcmp(before,after,sizeof before));
 }
 static const struct { const char *name; int shows; } timed[]={{"VINYL BRAKE",0},{"PING PONG",0},{"DELAY",1}};
 for(unsigned i=0;i<sizeof timed/sizeof *timed;i++){
  setup(&u,&m,TWO);set(&m,XZ_SHELL_ABLETON_ON,2);fx_set(&m,1,timed[i].name,1,16,234);render(&u,&m,before);
  fx_set(&m,1,timed[i].name,1,16,4000);render(&u,&m,after);
  assert(!!diff().w==timed[i].shows);
 }
 /* With the mode lamps hidden the strip runs from 6 px after the control at x 8 and keeps the time too. */
 setup(&u,&m,TWO);xz_shell_restore_setting(&u,XZ_SHELL_ITEM_MODE_LAMPS,1);set(&m,XZ_SHELL_ABLETON_ON,2);
 fx_set(&m,1,"ECHO",1,2,234);render(&u,&m,before);fx_set(&m,1,"ECHO",1,2,4000);render(&u,&m,after);assert(diff().w);
}

/* The three mode chips: their boxes, their touch columns and dead gaps, and their state, with and without Link. */
static void test_chips(void){
 struct xz_shell u;struct xz_shell_model m;
 static const enum xz_shell_setting ids[3]={XZ_SHELL_SET_PLAY_MODE,XZ_SHELL_SET_HOT_CUE_AUTO_LOAD,XZ_SHELL_SET_AUTO_CUE};
 for(int known=0;known<2;known++)for(int v=0;v<VIEWS;v++){
  setup(&u,&m,(enum view)v);if(known)set(&m,XZ_SHELL_ABLETON_OFF,0);
  render(&u,&m,after);
  for(int i=0;i<3;i++){
   struct xz_shell_rect k=CHIPS[i];
   /* The outline on all four sides, chrome just outside it. */
   assert(at(k.x,k.y+k.h/2)!=rgb565(SH_CHROME)&&at(k.x+k.w-1,k.y+k.h/2)!=rgb565(SH_CHROME));
   assert(at(k.x+k.w/2,k.y)!=rgb565(SH_CHROME)&&at(k.x+k.w/2,k.y+k.h-1)!=rgb565(SH_CHROME));
   assert(at(k.x-1,k.y+k.h/2)==rgb565(SH_CHROME)&&at(k.x+k.w,k.y+k.h/2)==rgb565(SH_CHROME));
   assert(at(k.x+k.w/2,k.y-1)==rgb565(SH_CHROME)&&at(k.x+k.w/2,k.y+k.h)==rgb565(SH_BLACK));   /* the bar's rule under it */
   /* Its columns answer in all of the bar's rows; below the bar the deck's info column (x 0..95) answers first, as
      it always did, so the band reaches the foot only over the lane (OUTSIDE: not tried there, a stem row). */
   int foot=v!=TWO_OUTSIDE&&k.x+k.w-1>=96?XZ_SHELL_HEADER_HIT_H-1:25;
   const int px[4][2]={{k.x,0},{k.x+k.w-1,0},{k.x,25},{k.x+k.w-1,foot}};
   for(int p=0;p<4;p++){
    size_t n=tap(&u,&m,px[p][0],px[p][1]);
    assert(n==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==(int)ids[i]);
   }
   /* The gap after it answers nothing. */
   int gap_end=i<2?CHIPS[i+1].x:known?CONTROL.x:STRIP.x;
   for(int x=k.x+k.w;x<gap_end;x++)assert(tap(&u,&m,x,10)==0);
  }
 }
 /* State: SINGLE lit on the top line or CONTINUE on the bottom one; A. HOT CUE red for ON, white for the library,
    grey off; A. CUE white on, grey off. */
 setup(&u,&m,TWO);set(&m,XZ_SHELL_ABLETON_OFF,0);
 const struct xz_shell_rect k0=CHIPS[0],top={k0.x+1,k0.y+1,k0.w-2,11},bottom={k0.x+1,k0.y+12,k0.w-2,k0.h-13};
 render(&u,&m,after);
 assert(count(top,SH_WHITE)>15&&!count(bottom,SH_WHITE)&&count(bottom,0x5a5a5a)>15);
 assert(count(CHIPS[1],SH_RED)>15&&count(CHIPS[2],SH_WHITE)>10);
 m.settings.play_mode=XZ_SHELL_PLAY_CONTINUE;m.settings.hot_cue_auto_load=XZ_SHELL_HCAL_LIBRARY;m.settings.auto_cue=0;
 render(&u,&m,after);
 assert(!count(top,SH_WHITE)&&count(top,0x5a5a5a)>15&&count(bottom,SH_WHITE)>15);
 assert(!count(CHIPS[1],SH_RED)&&count(CHIPS[1],SH_WHITE)>15&&!count(CHIPS[2],SH_WHITE)&&count(CHIPS[2],0x5a5a5a)>10);
 m.settings.hot_cue_auto_load=XZ_SHELL_HCAL_OFF;render(&u,&m,after);
 assert(!count(CHIPS[1],SH_RED)&&!count(CHIPS[1],SH_WHITE)&&count(CHIPS[1],0x5a5a5a)>15);
 /* Every word stays inside its chip's outline: the bar's rows left of the control hold nothing else. */
 for(int y=0;y<25;y++)for(int x=0;x<CONTROL.x;x++){
  int in=0;for(int i=0;i<3;i++)in|=inside(CHIPS[i],x,y);
  if(!in)assert(at(x,y)==rgb565(SH_CHROME));
 }
}

int main(void){
 fx_track_build(&track,7);
 test_off_rectangle();
 test_states();
 test_touch();
 test_lamps_hidden();
 test_strip_kept();
 test_chips();
 puts("PASS LINK in the top bar: off change exactly x 187..244 y 1..24 in every view, nothing outside the control "
      "in any state, each state's drawing, touch target and dead columns, A. CUE and stem buttons kept, lamps "
      "hidden, the BEAT FX strip unchanged by Link with its time, names in full; the three chips' boxes, touch, gaps and states");
 return 0;
}
