#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* Stem rows on PLAY: the five layouts shared with the classic interface, full rows as faders, the
   header STEMS button, and the two-tone stem colours. */
#include "fixtures.h"
#include "shell_internal.h"
#include "shell_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],other[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];

static size_t send(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y) {
 struct xz_shell_event e={kind,x,y,0,0,-1};
 memset(out,0,sizeof out);
 return xz_shell_input(u,m,&e,out);
}
static void render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *f) { assert(xz_shell_render(u,m,f,XZ_SHELL_WIDTH)); }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static int count(const uint16_t *f, struct xz_shell_rect r, uint32_t c) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=f[y*XZ_SHELL_WIDTH+x]==rgb565(c);
 return n;
}
static void model(struct xz_shell_model *m, enum xz_shell_stem_layout layout) {
 fx_model_full(m,&track);
 m->capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;
 m->stems_known=1;m->stems_on=layout!=XZ_SHELL_STEMS_HIDDEN;
 m->stem_layout_known=1;m->stem_layout=layout;
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  m->deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
  m->deck[d].stem_muted=0;m->deck[d].stem_gain[0]=80;m->deck[d].stem_gain[1]=50;m->deck[d].stem_gain[2]=100;
 }
}

static void test_layouts(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;
 struct xz_shell_rect lane[2]={xz_shell_play_rect(XZ_SHELL_R_LANE,0),xz_shell_play_rect(XZ_SHELL_R_LANE,1)};
 static const int heights[XZ_SHELL_STEM_LAYOUTS]={0,48,24,48,24,0};
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  model(&m,(enum xz_shell_stem_layout)layout);
  for(int deck=0;deck<2;deck++){
   struct xz_shell_rect row=xz_shell_stem_row(&m,deck);
   assert(row.h==heights[layout]);
   if(!row.h){
    /* PADS: the small buttons in the pad row, 44 px targets, DRUMS, VOCAL, INST from the left; HIDDEN: none. */
    struct xz_shell_rect pad=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,deck);
    for(int s=0;s<3;s++){
     struct xz_shell_rect b=xz_shell_stem_button(NULL,&m,deck,s);
     if(layout==XZ_SHELL_STEMS_PADS)assert(b.h==44&&b.x>=pad.x&&b.x+b.w<=pad.x+pad.w&&b.y<=pad.y&&b.y+b.h>=pad.y+pad.h);
     else assert(b.w==0);
    }
    if(layout==XZ_SHELL_STEMS_PADS)assert(xz_shell_stem_button(NULL,&m,deck,0).x<xz_shell_stem_button(NULL,&m,deck,2).x);
    continue;
   }
   /* Inside the deck's lane: BELOW at the foot of each deck, OUTSIDE above deck 1 and below deck 2. */
   int outside_top=deck==0&&(layout==XZ_SHELL_STEMS_OUTSIDE_FULL||layout==XZ_SHELL_STEMS_OUTSIDE_HALF);
   assert(row.y==(outside_top?lane[deck].y:lane[deck].y+lane[deck].h-row.h)&&row.x>=lane[deck].x&&row.x+row.w<=lane[deck].x+lane[deck].w);
   for(int s=0;s<3;s++){
    struct xz_shell_rect b=xz_shell_stem_button(NULL,&m,deck,s);
    assert(b.w>=150&&b.h>=44&&b.y>=lane[deck].y&&b.y+b.h<=lane[deck].y+lane[deck].h);
   }
   /* DRUMS, VOCAL, INST from the left, as the stock deck orders them. */
   assert(xz_shell_stem_button(NULL,&m,deck,0).x<xz_shell_stem_button(NULL,&m,deck,2).x&&xz_shell_stem_button(NULL,&m,deck,2).x<xz_shell_stem_button(NULL,&m,deck,1).x);
  }
  render(&u,&m,pixels);
  /* The pad row keeps only the hot-cue lamps unless the layout is PADS. */
  struct xz_shell_rect pad=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,0);
  assert((count(pixels,pad,sh_stem_bright(&m,0))==0)==(layout!=XZ_SHELL_STEMS_PADS));
 }
 /* A deck without stems keeps its row and says so; a row never shows on a hidden layout. */
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);
 m.deck[1].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);
 assert(xz_shell_stem_row(&m,1).h==48&&xz_shell_stem_button(NULL,&m,1,0).w==0);
 render(&u,&m,pixels);
 /* Not read: the pad row's small buttons, as before. */
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);m.stem_layout_known=0;
 assert(xz_shell_stem_row(&m,0).h==0&&xz_shell_stem_button(NULL,&m,0,0).h==44&&xz_shell_stem_button(NULL,&m,0,0).y>=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,0).y-8);
 m.stem_layout_known=1;m.stem_layout=(enum xz_shell_stem_layout)9;
 assert(xz_shell_stem_row(&m,0).h==0);render(&u,&m,pixels);
 puts("PASS stem rows: six layouts, rows inside each deck's lane, PADS in the pad row, 44 px buttons, pad-row fallback");
}

static void test_faders_and_taps(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;model(&m,XZ_SHELL_STEMS_BELOW_FULL);
 struct xz_shell_rect b=xz_shell_stem_button(NULL,&m,1,0);
 int cy=b.y+b.h/2;
 /* A tap toggles the mute on release, not on touch. */
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+40,cy)==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,b.x+40,cy)==1&&out[0].kind==XZ_SHELL_ACT_STEM_MUTE&&out[0].deck==1&&out[0].id==0&&out[0].value==1);
 /* A slide sets the level: half the button's width is half the range; no mute on release. */
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+40,cy);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,b.x+44,cy)==0);   /* under the slide threshold */
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,b.x+40-b.w/2,cy)==1&&out[0].kind==XZ_SHELL_ACT_STEM_GAIN&&out[0].deck==1&&out[0].id==0);
 assert(out[0].value>=28&&out[0].value<=32);
 /* Mid-drag the probe sees the touch in the struct. */
 struct xz_shell probe=u;
 assert(probe.down_region==XZ_SHELL_TOUCH_STEM&&probe.stem_touch.slid);
 m.deck[1].stem_gain[0]=(uint8_t)out[0].value;
 render(&u,&m,pixels);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,b.x+b.w*3,cy)==1&&out[0].value==XZ_SHELL_STEM_GAIN_MAX);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,b.x-b.w*3,cy)==1&&out[0].value==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,b.x-b.w*3,cy)==0);
 /* The level shows as a lighter fill from the left: 100 fills more of the button than 30. */
 uint32_t fill=xz_blend(sh_stem_dark(sh_stem_bright(&m,0)),sh_stem_bright(&m,0),90);
 m.deck[1].stem_gain[0]=30;render(&u,&m,pixels);int low=count(pixels,b,fill);
 m.deck[1].stem_gain[0]=100;render(&u,&m,other);int high=count(other,b,fill);
 assert(high>low*2&&low>0);
 /* Muted: grey, no level fill, no stem colour. */
 m.deck[1].stem_muted=1;render(&u,&m,pixels);
 assert(count(pixels,b,fill)==0&&count(pixels,b,sh_stem_bright(&m,0))==0);
 m.deck[1].stem_muted=0;
 /* Half rows are taps only; without the gain capability full rows are too. */
 model(&m,XZ_SHELL_STEMS_OUTSIDE_HALF);
 b=xz_shell_stem_button(NULL,&m,0,2);cy=b.y+b.h/2;
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+40,cy);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,b.x+140,cy)==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,b.x+140,cy)==1&&out[0].kind==XZ_SHELL_ACT_STEM_MUTE&&out[0].deck==0&&out[0].id==2);
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);m.capabilities&=~(uint32_t)XZ_SHELL_CAP_STEM_GAIN;
 b=xz_shell_stem_button(NULL,&m,0,1);cy=b.y+b.h/2;
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+40,cy);
 assert(send(&u,&m,XZ_SHELL_TOUCH_MOVE,b.x+140,cy)==0);
 send(&u,&m,XZ_SHELL_TOUCH_UP,b.x+140,cy);
 /* The 44 px half-row targets and the ZOOM box do not overlap. */
 for(int layout=1;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  model(&m,(enum xz_shell_stem_layout)layout);
  render(&u,&m,pixels);
 }
 puts("PASS stem faders: tap mutes on release, full rows slide the level, fill shows it, muted grey, half rows tap only");
}

static void test_header_toggle(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;
 struct xz_shell_rect marks={XZ_SHELL_STEMS_X,XZ_SHELL_HEADER_BUTTON_H-6,XZ_SHELL_STEMS_W,3};
 int lit[XZ_SHELL_STEM_LAYOUTS];
 /* In the order the runtime steps them: PADS, the four row layouts, HIDDEN. */
 assert(xz_shell_stem_cycle[0]==XZ_SHELL_STEMS_PADS&&xz_shell_stem_cycle[XZ_SHELL_STEM_LAYOUTS-1]==XZ_SHELL_STEMS_HIDDEN);
 for(int i=0;i<XZ_SHELL_STEM_LAYOUTS;i++){
  enum xz_shell_stem_layout layout=xz_shell_stem_cycle[i];
  model(&m,layout);
  /* The header button asks the runtime to step the layout, as the classic STEMS button does. */
  assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,XZ_SHELL_STEMS_X+10,10)==1&&out[0].kind==XZ_SHELL_ACT_TOGGLE_STEMS);
  send(&u,&m,XZ_SHELL_TOUCH_UP,XZ_SHELL_STEMS_X+10,10);
  render(&u,&m,pixels);
  /* Its six marks show the current choice: the white mark moves along. */
  int first=-1;
  for(int x=marks.x;x<marks.x+marks.w&&first<0;x++)for(int y=marks.y;y<marks.y+marks.h;y++)if(pixels[y*XZ_SHELL_WIDTH+x]==rgb565(SH_WHITE)){first=x;break;}
  lit[i]=first;
  assert(first>=0);
 }
 for(int i=1;i<XZ_SHELL_STEM_LAYOUTS;i++)assert(lit[i]>lit[i-1]);
 puts("PASS header STEMS: TOGGLE_STEMS on touch, six marks follow the layout in its cycle");
}

static void test_colours(void) {
 struct xz_shell_model m;model(&m,XZ_SHELL_STEMS_BELOW_FULL);
 /* The reference stem colours by default; the dark interior is 3/16 of the bright. */
 assert(sh_stem_bright(&m,0)==0x4b4ace&&sh_stem_bright(&m,1)==0xac1f12&&sh_stem_bright(&m,2)==0x15b634);
 assert(sh_stem_dark(0x4b4ace)==(((0x4b*3>>4)<<16)|((0x4a*3>>4)<<8)|(0xce*3>>4)));
 /* A runtime colour counts only when the DJ picked it. */
 m.stem_rgb_known=1;m.stem_rgb[1]=0x123456;
 assert(sh_stem_bright(&m,1)==0xac1f12);
 m.stem_rgb_custom=2;assert(sh_stem_bright(&m,1)==0x123456&&sh_stem_bright(&m,0)==0x4b4ace);
 m.stem_rgb_known=0;assert(sh_stem_bright(&m,1)==0xac1f12);
 assert(sh_stem_bright(&m,7)==SH_DIM);
 puts("PASS stem colours: reference defaults, 3/16 interiors, the DJ's picks win");
}

static int overlap(struct xz_shell_rect a, struct xz_shell_rect b) { return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h; }
static void test_bypass(void) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model m;
 /* Without the runtime's capability or the read state there is no BYPASS and the stems keep the row. */
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);
 struct xz_shell_rect wide=xz_shell_stem_button(&u,&m,0,1);
 assert(xz_shell_bypass_button(&u,&m,0).w==0);
 m.capabilities|=XZ_SHELL_CAP_STEM_BYPASS;m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_BYPASS);assert(xz_shell_bypass_button(&u,&m,0).w==0);
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++)for(int pad=0;pad<2;pad++){
  model(&m,(enum xz_shell_stem_layout)layout);m.stem_layout_known=!pad;
  m.capabilities|=XZ_SHELL_CAP_STEM_BYPASS;
  for(int d=0;d<XZ_SHELL_DECKS;d++){m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_BYPASS);m.deck[d].stem_bypass=0;}
  for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
   struct xz_shell_rect b=xz_shell_bypass_button(&u,&m,deck);
   if(layout==XZ_SHELL_STEMS_HIDDEN&&!pad){assert(b.w==0);continue;}
   /* After INST, 44 px tall, clear of every stem button and, in the pad row, of the hot-cue lamps. */
   struct xz_shell_rect inst=xz_shell_stem_button(&u,&m,deck,1);
   assert(b.h>=44&&b.w>=54&&b.x>inst.x+inst.w);
   for(int s=0;s<3;s++)assert(!overlap(b,xz_shell_stem_button(&u,&m,deck,s)));
   if(pad||layout==XZ_SHELL_STEMS_PADS){struct xz_shell_rect r=xz_shell_play_rect(XZ_SHELL_R_PAD_ROW,deck);assert(b.x+b.w<=r.x+r.w-8-8*14-7*3-10);}
   else{struct xz_shell_rect row=xz_shell_stem_row(&m,deck);assert(b.x+b.w==row.x+row.w);}
  }
  render(&u,&m,pixels);
 }
 /* BELOW / FULL: the stems narrow to make room; a touch asks the runtime to bypass, on touch-down. */
 model(&m,XZ_SHELL_STEMS_BELOW_FULL);m.capabilities|=XZ_SHELL_CAP_STEM_BYPASS;
 for(int d=0;d<XZ_SHELL_DECKS;d++)m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_BYPASS);
 assert(xz_shell_stem_button(&u,&m,0,1).w<wide.w&&xz_shell_stem_button(&u,&m,0,1).w>=150);
 struct xz_shell_rect b=xz_shell_bypass_button(&u,&m,1);
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,b.y+b.h/2)==1&&out[0].kind==XZ_SHELL_ACT_STEM_BYPASS&&out[0].deck==1&&out[0].value==1);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,b.x+b.w/2,b.y+b.h/2)==0);
 /* Lit: white, and every stem goes dark as the pads do, the mutes kept underneath. */
 render(&u,&m,pixels);assert(count(pixels,b,0xe6e6e6)==0);
 m.deck[1].stem_bypass=1;m.deck[1].stem_muted=2;render(&u,&m,pixels);
 assert(count(pixels,b,0xe6e6e6)>b.w*b.h/2);
 for(int s=0;s<3;s++)assert(count(pixels,xz_shell_stem_button(&u,&m,1,s),sh_stem_bright(&m,s))==0);
 assert(send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,b.y+b.h/2)==1&&out[0].value==0);send(&u,&m,XZ_SHELL_TOUCH_UP,0,0);
 /* A stem tap while bypassed still sets its mute, for when BYPASS lets go. */
 struct xz_shell_rect drums=xz_shell_stem_button(&u,&m,1,0);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,drums.x+20,drums.y+drums.h/2);
 assert(send(&u,&m,XZ_SHELL_TOUCH_UP,drums.x+20,drums.y+drums.h/2)==1&&out[0].kind==XZ_SHELL_ACT_STEM_MUTE);
 /* APPEARANCE > BYPASS BUTTON: hidden, the stems take the row back and the spot no longer answers. */
 struct xz_shell_setting_item it;
 assert(sh_settings_item(&u,&m,XZ_SHELL_ITEM_BYPASS_BUTTON,&it)&&it.known&&it.writable&&it.current==0&&it.choice_count==2);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_BYPASS_BUTTON,1)==1&&!sh_indicator_shown(&u,XZ_SHELL_IND_BYPASS));
 assert(sh_settings_item(&u,&m,XZ_SHELL_ITEM_BYPASS_BUTTON,&it)&&it.current==1);
 assert(xz_shell_bypass_button(&u,&m,1).w==0&&xz_shell_stem_button(&u,&m,1,1).w==wide.w);
 send(&u,&m,XZ_SHELL_TOUCH_DOWN,b.x+b.w/2,b.y+b.h/2);
 assert(out[0].kind!=XZ_SHELL_ACT_STEM_BYPASS);send(&u,&m,XZ_SHELL_TOUCH_UP,0,0);
 render(&u,&m,pixels);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_BYPASS_BUTTON,2)==0&&xz_shell_restore_setting(&u,XZ_SHELL_ITEM_BYPASS_BUTTON,0)==1);
 assert(sh_settings_set(&u,XZ_SHELL_ITEM_BYPASS_BUTTON,1,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_BYPASS_BUTTON&&out[0].value==1);
 puts("PASS stem BYPASS: after the stems in every layout, touch-down toggles, lit white darkens the stems, APPEARANCE hides it");
}

int main(void) {
 fx_track_build(&track,7);
 test_layouts();
 test_faders_and_taps();
 test_bypass();
 test_header_toggle();
 test_colours();
 puts("PASS stems");
 return 0;
}
