#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The STEMS button in four lanes: one press hides every stem button, the next shows them, and the 2 DECKS
   layout neither changes nor applies there. A deck whose track has no stems shows no stem row and keeps its
   whole lane, on every deck, and its row appears once a track with stems loads. Two lanes keep stepping. */
#include "four_deck_fixtures.h"
#include "../ui/wave_viewport.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],other[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static uint16_t px(const uint16_t *f, int x, int y) { return f[y*XZ_SHELL_WIDTH+x]; }
static void render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *f) { assert(xz_shell_render(u,m,f,XZ_SHELL_WIDTH)); }
static int same(const uint16_t *a, const uint16_t *b, struct xz_shell_rect r) {
 for(int y=r.y;y<r.y+r.h;y++)if(memcmp(a+y*XZ_SHELL_WIDTH+r.x,b+y*XZ_SHELL_WIDTH+r.x,(size_t)r.w*sizeof *a))return 0;
 return 1;
}
/* What the runtime's stem state says for a deck: its track has prepared stems (the stem mute bits are known). */
static void stems(struct xz_shell_model *m, int deck, int prepared) {
 const uint64_t bits=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
 if(prepared){m->deck[deck].known|=bits;m->deck[deck].stem_muted=0;}
 else m->deck[deck].known&=~bits;
}
static void model(struct xz_shell_model *m, enum xz_shell_stem_layout layout) {
 fx_model_four(m,&track);
 m->capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;
 m->stems_known=1;m->stems_on=layout!=XZ_SHELL_STEMS_HIDDEN;m->stem_layout_known=1;m->stem_layout=layout;
 for(int d=0;d<4;d++)stems(m,d,d<2);
}
/* The runtime's half (ui_runtime.c XZ_UI_STEMS_OVERLAY with XZ_UI_STEMS_BUTTON): one press of STEMS. */
static enum xz_stems_press press(const struct xz_shell *u, struct xz_shell_model *m) {
 enum xz_stems_press p=xz_stems_press(1,sh_play_four(u,m),m->four_stems_hidden);
 if(p==XZ_STEMS_PRESS_SHOW)m->four_stems_hidden=0;
 else if(p==XZ_STEMS_PRESS_HIDE)m->four_stems_hidden=1;
 else if(p==XZ_STEMS_PRESS_STEP){
  int at=0;while(at<XZ_SHELL_STEM_LAYOUTS&&xz_shell_stem_cycle[at]!=m->stem_layout)at++;
  m->stem_layout=xz_shell_stem_cycle[(at+1)%XZ_SHELL_STEM_LAYOUTS];
 }
 return p;
}
static int rows(const struct xz_shell_model *m) {
 int n=0;
 for(int d=0;d<4;d++)n+=xz_shell_stem_row(m,d).h>0;
 return n;
}
/* The header STEMS button: lit (blue) or not, and whether its indicator bar carries the six layout marks. */
static int header_lit(const uint16_t *f) { return px(f,XZ_SHELL_STEMS_X+3,3)==rgb565(SH_BLUE); }
static int header_marks(const uint16_t *f) {
 int y=XZ_SHELL_HEADER_BUTTON_H-5,changes=0;
 for(int x=XZ_SHELL_STEMS_X+9;x<XZ_SHELL_STEMS_X+XZ_SHELL_STEMS_W-8;x++)changes+=px(f,x,y)!=px(f,x-1,y);
 return changes>0;
}

static void test_one_press_each(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  model(&m,(enum xz_shell_stem_layout)layout);
  assert(sh_play_four(&u,&m));
  /* Shown by default, whatever the 2 DECKS layout, HIDDEN included: a half row on decks 1 and 2. */
  assert(rows(&m)==2&&xz_shell_stem_row(&m,0).h==24&&xz_shell_stem_button(NULL,&m,0,0).h==44);
  assert(xz_shell_render_frame(&u,&m,pixels,XZ_SHELL_WIDTH));assert(header_lit(pixels)&&!header_marks(pixels));
  /* The header button still sends the one STEMS request; the runtime decides what it does. */
  assert(xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,XZ_SHELL_STEMS_X+10,10,0,0,-1},out)==1&&
   out[0].kind==XZ_SHELL_ACT_TOGGLE_STEMS);
  xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,XZ_SHELL_STEMS_X+10,10,0,0,-1},out);
  /* One press: every stem button gone, the layout untouched, STEMS dark. */
  assert(press(&u,&m)==XZ_STEMS_PRESS_HIDE&&!strcmp(xz_stems_press_notice(XZ_STEMS_PRESS_HIDE),"STEM BUTTONS HIDDEN"));
  assert(rows(&m)==0&&m.stem_layout==(enum xz_shell_stem_layout)layout);
  for(int d=0;d<4;d++)for(int s=0;s<3;s++)assert(xz_shell_stem_button(&u,&m,d,s).h==0);
  render(&u,&m,pixels);assert(!header_lit(pixels)&&!header_marks(pixels));
  /* The next press: back, as they were. */
  assert(press(&u,&m)==XZ_STEMS_PRESS_SHOW&&!strcmp(xz_stems_press_notice(XZ_STEMS_PRESS_SHOW),"STEM BUTTONS SHOWN"));
  assert(rows(&m)==2&&m.stem_layout==(enum xz_shell_stem_layout)layout);
  render(&u,&m,pixels);assert(header_lit(pixels));
 }
 /* DECK VIEW 4 DECKS with decks 3 and 4 not connected is four lanes too. */
 fx_model_full(&m,&track);m.capabilities|=XZ_SHELL_CAP_STEM_MUTE;m.layout_preference=1+XZ_LAYOUT_FOUR;
 stems(&m,0,1);stems(&m,1,1);m.stem_layout_known=1;m.stem_layout=XZ_SHELL_STEMS_BELOW_FULL;
 struct xz_shell_model view;xz_shell_play_projection(&u,&m,&view);
 assert(sh_play_four(&u,&m)&&view.play_view_count==4&&xz_shell_stem_row(&view,1).h==24);
 assert(press(&u,&m)==XZ_STEMS_PRESS_HIDE);
 xz_shell_play_projection(&u,&m,&view);assert(xz_shell_stem_row(&view,1).h==0);
 puts("PASS four-lane STEMS: one press hides every stem button, one shows them; layout untouched; lamp follows, no marks");
}

static void test_no_stems_full_lane(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;
 for(int d=0;d<4;d++){
  /* Deck d's track has no prepared stems: no row, no buttons, no BYPASS, and its waveform keeps the whole lane,
     drawn exactly as when the STEMS button hid the buttons. Every other deck has stems here (the runtime gives
     decks 3 and 4 none today; the lanes treat all four alike). */
  model(&m,XZ_SHELL_STEMS_BELOW_HALF);
  for(int k=0;k<4;k++)stems(&m,k,1);
  m.capabilities|=XZ_SHELL_CAP_STEM_BYPASS;
  stems(&m,d,0);
  struct xz_shell_rect lane=xz_shell_play_rect(&m,XZ_SHELL_R_LANE,d);
  assert(xz_shell_stem_row(&m,d).h==0&&xz_shell_stem_button(NULL,&m,d,0).h==0&&xz_shell_bypass_button(&u,&m,d).h==0);
  render(&u,&m,pixels);
  m.four_stems_hidden=1;stems(&m,d,1);render(&u,&m,other);
  assert(same(pixels,other,lane));
  /* The decks that have stems still show theirs. */
  m.four_stems_hidden=0;stems(&m,d,0);
  for(int k=0;k<4;k++)if(k!=d)assert(xz_shell_stem_row(&m,k).h==24);
  /* A track with stems loads: the row appears at the foot of the lane, and the waveform gives it room. */
  stems(&m,d,1);
  struct xz_shell_rect row=xz_shell_stem_row(&m,d);
  assert(row.h==24&&row.y+row.h==lane.y+lane.h&&xz_shell_stem_button(NULL,&m,d,2).h==44);
  render(&u,&m,other);assert(!same(pixels,other,lane));
  /* ... and a track without stems after it takes the row away again. */
  stems(&m,d,0);render(&u,&m,other);assert(same(pixels,other,lane));
 }
 /* Decks 3 and 4 as the runtime publishes them (no stem caps): no rows, whole lanes. */
 model(&m,XZ_SHELL_STEMS_BELOW_HALF);
 for(int k=2;k<4;k++){stems(&m,k,1);m.deck[k].caps_off=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;}
 assert(xz_shell_stem_row(&m,0).h==24&&xz_shell_stem_row(&m,1).h==24&&!xz_shell_stem_row(&m,2).h&&!xz_shell_stem_row(&m,3).h);
 /* An empty deck shows no row either. */
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);
 m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED);m.deck[0].loaded=0;
 assert(xz_shell_stem_row(&m,0).h==0&&xz_shell_stem_row(&m,1).h==24);
 puts("PASS four lanes without stems: no row and the whole lane on every deck; a stems track brings the row, the next takes it");
}

static void test_two_decks_unchanged(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;
 static const int heights[XZ_SHELL_STEM_LAYOUTS]={0,48,24,48,24,0};
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;
 m.stems_known=1;m.stem_layout_known=1;m.stem_layout=XZ_SHELL_STEMS_PADS;
 stems(&m,0,1);stems(&m,1,1);
 assert(!sh_play_four(&u,&m));
 /* Two lanes: each press steps PADS, BELOW FULL, BELOW HALF, OUTER FULL, OUTER HALF, HIDDEN, PADS; the four-lane
    choice changes nothing there. */
 for(int hidden=0;hidden<2;hidden++){
  m.four_stems_hidden=hidden;m.stem_layout=XZ_SHELL_STEMS_PADS;
  for(int i=0;i<XZ_SHELL_STEM_LAYOUTS;i++){
   assert(m.stem_layout==xz_shell_stem_cycle[i]);
   m.stems_on=m.stem_layout!=XZ_SHELL_STEMS_HIDDEN;
   for(int d=0;d<2;d++)assert(xz_shell_stem_row(&m,d).h==heights[m.stem_layout]);
   render(&u,&m,pixels);assert(header_marks(pixels)&&header_lit(pixels)==(m.stem_layout!=XZ_SHELL_STEMS_HIDDEN));
   assert(press(&u,&m)==XZ_STEMS_PRESS_STEP&&m.four_stems_hidden==hidden);
  }
  assert(m.stem_layout==XZ_SHELL_STEMS_PADS);
 }
 /* Two lanes still say why a deck has no stem buttons. */
 m.stem_layout=XZ_SHELL_STEMS_BELOW_FULL;stems(&m,1,0);
 assert(xz_shell_stem_row(&m,1).h==48&&xz_shell_stem_button(NULL,&m,1,0).h==0);
 puts("PASS two lanes: STEMS steps the six layouts as before, untouched by the four-lane choice");
}

int main(void) {
 fx_track_build(&track,7);
 test_one_press_each();
 test_no_stems_full_lane();
 test_two_decks_unchanged();
 return 0;
}
