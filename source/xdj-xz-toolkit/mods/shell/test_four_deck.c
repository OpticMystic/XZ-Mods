#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The four-deck view: PLAY geometry, every per-deck input for decks 3 and 4, the status strips under
   the other screens, refusal past the deck count, deck counts other than 2 and 4, and the frame's
   guard words. */
#include "four_deck_fixtures.h"
#include "browse_fixtures.h"
#include "settings_fixtures.h"
#include "shell_info.h"
#include "shell_source.h"
#include "shell_settings.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;
static struct bx_library lib;
static const int mixer_column[4]={1,2,0,3};

static uint16_t *frame(uint16_t *buffer) { return buffer+GUARD+STRIDE; }
static void fill(uint16_t *buffer, uint16_t inside) {
 for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a;
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++)frame(buffer)[y*STRIDE+x]=inside;
}
static void check_guards(uint16_t *buffer) {
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(frame(buffer)[y*STRIDE+x]==0xa55a);
}
/* Rendered over two different fills, the frame is the same: every pixel is drawn, nothing past it. */
static void render_both(const struct xz_shell *u, const struct xz_shell_model *m) {
 fill(buffer_a,0x0000);assert(xz_shell_render(u,m,frame(buffer_a),STRIDE));check_guards(buffer_a);
 fill(buffer_b,0xffff);assert(xz_shell_render(u,m,frame(buffer_b),STRIDE));check_guards(buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
}
static size_t tap(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event down={XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1},up={XZ_SHELL_TOUCH_UP,x,y,0,0,-1};
 struct xz_shell_action rest[XZ_SHELL_ACTIONS];
 size_t n=xz_shell_input(u,m,&down,out),k=xz_shell_input(u,m,&up,rest);
 for(size_t i=0;i<k&&n<XZ_SHELL_ACTIONS;i++)out[n++]=rest[i];
 return n;
}
static size_t tap_rect(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_rect r, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 return tap(u,m,r.x+r.w/2,r.y+r.h/2,out);
}
static size_t deck_key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, int deck, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,deck};
 return xz_shell_input(u,m,&e,out);
}
static int inside_frame(struct xz_shell_rect r) { return r.w>0&&r.h>0&&r.x>=0&&r.y>=0&&r.x+r.w<=XZ_SHELL_WIDTH&&r.y+r.h<=XZ_SHELL_HEIGHT; }
static int overlap(struct xz_shell_rect a, struct xz_shell_rect b) { return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h; }
static int within(struct xz_shell_rect in, struct xz_shell_rect out) { return in.x>=out.x&&in.y>=out.y&&in.x+in.w<=out.x+out.w&&in.y+in.h<=out.y+out.h; }
static struct xz_shell_rect rect(const struct xz_shell_model *m, enum xz_shell_play_region r, int deck) { return xz_shell_play_rect(m,r,deck); }
static void stems_on(struct xz_shell_model *m) {
 m->capabilities|=XZ_SHELL_CAP_STEM_MUTE;
 m->stems_known=m->stems_on=1;m->stem_layout_known=1;m->stem_layout=XZ_SHELL_STEMS_BELOW_FULL;
 for(int d=0;d<4;d++){m->deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);m->deck[d].stem_muted=0;}
}

static void test_layout(void) {
 struct xz_shell_model m;fx_model_four(&m,&track);
 assert(sh_deck_count(&m)==4);
 for(int d=0;d<4;d++){
  static const enum xz_shell_play_region per_deck[]={XZ_SHELL_R_INFO_COLUMN,XZ_SHELL_R_DECK_TAG,XZ_SHELL_R_LANE,XZ_SHELL_R_STRIP,
   XZ_SHELL_R_TIME,XZ_SHELL_R_OVERVIEW,XZ_SHELL_R_ALERT_BUTTON,XZ_SHELL_R_GRID_RESET,XZ_SHELL_R_GRID_SNAP,XZ_SHELL_R_GRID_SHIFT};
  for(size_t i=0;i<sizeof per_deck/sizeof *per_deck;i++)assert(inside_frame(rect(&m,per_deck[i],d)));
  assert(rect(&m,XZ_SHELL_R_PAD_ROW,d).h==0);
  struct xz_shell_rect lane=rect(&m,XZ_SHELL_R_LANE,d),col=rect(&m,XZ_SHELL_R_INFO_COLUMN,d),strip=rect(&m,XZ_SHELL_R_STRIP,d);
  assert(lane.h>=76&&lane.y>=rect(&m,XZ_SHELL_R_TOP_BAR,0).h&&col.y==lane.y&&col.h==lane.h&&col.x+col.w<=lane.x);
  assert(within(rect(&m,XZ_SHELL_R_DECK_TAG,d),col));
  /* The horizontal strips follow the XZ mixer: 3, 1, 2, 4. */
  assert(strip.x==mixer_column[d]*XZ_SHELL_WIDTH/4+2&&strip.w==XZ_SHELL_WIDTH/4-4&&strip.y>=rect(&m,XZ_SHELL_R_LANE,3).y+rect(&m,XZ_SHELL_R_LANE,3).h);
  struct xz_shell_rect time=rect(&m,XZ_SHELL_R_TIME,d),ov=rect(&m,XZ_SHELL_R_OVERVIEW,d);
  assert(within(time,strip)&&within(ov,strip)&&time.h>=44&&!overlap(time,ov));
  /* The overview answers 44 px tall without reaching the time toggle. */
  struct xz_shell_rect ov_hit={ov.x,ov.y+ov.h-44,ov.w,44};
  assert(ov.h<44&&within(ov_hit,strip)&&!overlap(ov_hit,time));
  for(int e=d+1;e<4;e++){
   assert(!overlap(lane,rect(&m,XZ_SHELL_R_LANE,e))&&!overlap(col,rect(&m,XZ_SHELL_R_INFO_COLUMN,e)));
   assert(!overlap(strip,rect(&m,XZ_SHELL_R_STRIP,e)));
  }
  for(int g=0;g<3;g++){
   struct xz_shell_rect b=rect(&m,(enum xz_shell_play_region)(XZ_SHELL_R_GRID_RESET+g),d);
   assert(b.h>=44&&within(b,lane)&&!overlap(b,rect(&m,XZ_SHELL_R_ZOOM,0)));
  }
  struct xz_shell_rect alert=rect(&m,XZ_SHELL_R_ALERT_BUTTON,d);
  assert(alert.h>=44&&within(alert,lane));
 }
 assert(within(rect(&m,XZ_SHELL_R_ZOOM,0),rect(&m,XZ_SHELL_R_LANE,3)));
 /* Stem rows: half rows under the lane, for decks with stem buttons only; buttons answer 44 px tall. */
 stems_on(&m);
 m.deck[2].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);
 for(int layout=XZ_SHELL_STEMS_BELOW_FULL;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  m.stem_layout=(enum xz_shell_stem_layout)layout;
  for(int d=0;d<4;d++){
   struct xz_shell_rect row=xz_shell_stem_row(&m,d),lane=rect(&m,XZ_SHELL_R_LANE,d);
   if(d==2){assert(row.h==0&&xz_shell_stem_button(NULL,&m,d,0).h==0);continue;}
   assert(row.h==24&&within(row,lane)&&row.y+row.h==lane.y+lane.h);
   for(int s=0;s<3;s++){
    struct xz_shell_rect b=xz_shell_stem_button(NULL,&m,d,s);
    assert(b.h==44&&within(b,lane));
    for(int t=s+1;t<3;t++)assert(!overlap(b,xz_shell_stem_button(NULL,&m,d,t)));
   }
  }
  /* ZOOM/GRID moves up by the lowest deck's stem row. */
  struct xz_shell_rect zoom=rect(&m,XZ_SHELL_R_ZOOM,0),row3=xz_shell_stem_row(&m,3);
  zoom.y-=row3.h;
  assert(!overlap(zoom,row3)&&within(zoom,rect(&m,XZ_SHELL_R_LANE,3)));
 }
 /* The 2 DECKS layouts do not apply to four lanes, HIDDEN included: the STEMS button hides them there. */
 m.stem_layout=XZ_SHELL_STEMS_HIDDEN;
 assert(xz_shell_stem_row(&m,0).h==24&&xz_shell_stem_button(NULL,&m,0,0).h==44);
 m.four_stems_hidden=1;
 for(int d=0;d<4;d++)assert(xz_shell_stem_row(&m,d).h==0&&xz_shell_stem_button(NULL,&m,d,1).h==0);
 m.four_stems_hidden=0;
 /* An unread layout still shows the buttons: the four-deck view has no pad row to hold them. */
 m.stem_layout_known=0;
 assert(xz_shell_stem_row(&m,0).h==24&&xz_shell_stem_button(NULL,&m,0,0).h==44);
 puts("PASS four-deck layout: lanes, quarter strips, 44 px targets, stem rows");
}

static void test_bypass_four(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(&u);fx_model_four(&m,&track);stems_on(&m);
 assert(!(XZ_SHELL_CAP_DECK_SELECT&XZ_SHELL_CAP_STEM_BYPASS));
 m.capabilities|=XZ_SHELL_CAP_STEM_BYPASS;
 for(int d=0;d<4;d++)m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_BYPASS);
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  /* Every layout draws the same; the HIDDEN pass also hides the buttons with the STEMS button. */
  m.stem_layout=(enum xz_shell_stem_layout)layout;m.four_stems_hidden=layout==XZ_SHELL_STEMS_HIDDEN;
  for(int d=0;d<4;d++){
   struct xz_shell_rect b=xz_shell_bypass_button(&u,&m,d);
   if(m.four_stems_hidden){assert(b.h==0);continue;}
   assert(b.h==44&&within(b,rect(&m,XZ_SHELL_R_LANE,d)));
   for(int s=0;s<3;s++)assert(!overlap(b,xz_shell_stem_button(&u,&m,d,s)));
   assert(tap_rect(&u,&m,b,out)==1&&out[0].kind==XZ_SHELL_ACT_STEM_BYPASS&&out[0].deck==d);
  }
  if(!m.four_stems_hidden){
   struct xz_shell_rect zoom=rect(&m,XZ_SHELL_R_ZOOM,0);zoom.y-=44;
   assert(!overlap(zoom,xz_shell_bypass_button(&u,&m,3)));
   u.deck=3;
   assert(tap_rect(&u,&m,zoom,out)==0&&u.grid_mode);
   u.grid_mode=0;
  }
  render_both(&u,&m);
 }
 /* An unsupported deck cannot expose BYPASS, even with stale known state. */
 m.deck[2].caps_off|=XZ_SHELL_CAP_STEM_BYPASS;
 assert(xz_shell_bypass_button(&u,&m,2).h==0);
 /* GRID owns its row while visible; touching the hidden BYPASS cannot toggle audio. */
 u.deck=3;u.grid_mode=1;m.capabilities|=XZ_SHELL_CAP_GRID_EDIT;
 struct xz_shell_rect b=xz_shell_bypass_button(&u,&m,3);
 size_t n=tap_rect(&u,&m,b,out);
 for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_STEM_BYPASS);
 render_both(&u,&m);
 puts("PASS four-deck BYPASS: unique capability, half-row targets, PADS fallback, grid priority");
}

static void test_deck_input(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_four(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_GRID_EDIT;
 for(int d=2;d<4;d++){
  xz_shell_init(&u);
  /* The DECK tag sets the active deck. */
  assert(tap_rect(&u,&m,rect(&m,XZ_SHELL_R_DECK_TAG,d),out)==1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==d&&u.deck==d);
  /* REMAIN / TIME toggles that deck's time mode. */
  int remain=m.deck[d].time_mode==XZ_SHELL_TIME_REMAIN;
  assert(tap_rect(&u,&m,rect(&m,XZ_SHELL_R_TIME,d),out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].deck==d
   &&out[0].id==XZ_SHELL_SET_TIME_MODE&&out[0].value==(remain?XZ_SHELL_TIME_ELAPSED:XZ_SHELL_TIME_REMAIN));
  /* Needle search on the overview, and on its padding above. */
  struct xz_shell_rect ov=rect(&m,XZ_SHELL_R_OVERVIEW,d);
  m.deck[d].playing=0;
  assert(tap(&u,&m,ov.x+ov.w/2,ov.y+ov.h/2,out)==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==d
   &&out[0].value==xz_shell_overview_ms(&m,FX_TOTAL_MS,ov.x+ov.w/2,d));
  assert(tap(&u,&m,ov.x+3,ov.y+ov.h-44,out)==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==d);
  assert(xz_shell_overview_ms(&m,FX_TOTAL_MS,ov.x+ov.w-1,d)==FX_TOTAL_MS&&xz_shell_overview_ms(&m,FX_TOTAL_MS,ov.x,d)==0);
  /* NEEDLE LOCK holds while that deck plays. */
  m.deck[d].playing=1;m.settings.needle_lock_known=m.settings.needle_lock=1;
  assert(tap(&u,&m,ov.x+ov.w/2,ov.y+ov.h/2,out)==0&&u.notice);
  m.settings.needle_lock=0;u.notice=NULL;
  /* Keys that carry the deck. */
  assert(deck_key(&u,&m,XZ_SHELL_KEY_TIME_MODE,d,out)==1&&out[0].deck==d&&out[0].id==XZ_SHELL_SET_TIME_MODE);
  assert(deck_key(&u,&m,XZ_SHELL_KEY_QUANTIZE,d,out)==1&&out[0].deck==d&&out[0].id==XZ_SHELL_SET_QUANTIZE&&out[0].value==!m.deck[d].quantize_on);
  /* GRID on the active deck: the encoder adjusts it and its buttons answer. */
  u.grid_mode=1;
  struct xz_shell_event turn={XZ_SHELL_ENCODER_TURN,0,0,2,0,-1};
  assert(xz_shell_input(&u,&m,&turn,out)==1&&out[0].kind==XZ_SHELL_ACT_GRID_ADJUST&&out[0].deck==d);
  for(int g=0;g<3;g++)
   assert(tap_rect(&u,&m,rect(&m,(enum xz_shell_play_region)(XZ_SHELL_R_GRID_RESET+g),d),out)==1&&out[0].kind==XZ_SHELL_ACT_GRID_EDIT&&out[0].deck==d&&out[0].id==g);
  u.grid_mode=0;
  /* The alert's CHOOSE TRACK makes the deck active and opens BROWSE for it. */
  m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);m.deck[d].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302",NULL};
  xz_shell_init(&u);
  size_t n=tap_rect(&u,&m,rect(&m,XZ_SHELL_R_ALERT_BUTTON,d),out);
  assert(n>=1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==d&&u.deck==d);
  m.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);
 }
 /* Stem buttons: a tap mutes that deck's stem on release. */
 stems_on(&m);
 for(int d=2;d<4;d++)for(int s=0;s<3;s++){
  xz_shell_init(&u);
  assert(tap_rect(&u,&m,xz_shell_stem_button(NULL,&m,d,s),out)==1&&out[0].kind==XZ_SHELL_ACT_STEM_MUTE&&out[0].deck==d&&out[0].id==s&&out[0].value==1);
 }
 /* GRID edit takes the active deck's stem row: its buttons answer where the stems were. */
 xz_shell_init(&u);u.deck=3;u.grid_mode=1;
 struct xz_shell_rect snap=rect(&m,XZ_SHELL_R_GRID_SNAP,3);
 assert(overlap(snap,xz_shell_stem_button(NULL,&m,3,2)));
 assert(tap_rect(&u,&m,snap,out)==1&&out[0].kind==XZ_SHELL_ACT_GRID_EDIT&&out[0].deck==3);
 assert(tap_rect(&u,&m,xz_shell_stem_button(NULL,&m,2,0),out)==1&&out[0].kind==XZ_SHELL_ACT_STEM_MUTE&&out[0].deck==2);
 puts("PASS four-deck input: tag, time, needle search, keys, GRID, alert and stems on decks 3 and 4");
}

/* Under BROWSE, INFO, SOURCE, SHORTCUT and UTILITY the strip holds four quarters; on BROWSE a quarter
   is that deck's LOAD target and the LOAD chip carries a cell per deck. */
static void test_strips(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 static struct xz_shell_info_model im;static struct xz_shell_source_model sm;static struct xz_shell_settings_model sets;
 fx_model_four(&m,&track);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 m.browse=&b;m.info=&im;m.source=&sm;m.settings_screen=&sets;
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SOURCE;
 xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_BROWSE,-1,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 for(int d=0;d<4;d++){
  struct xz_shell_rect q=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_STRIP,d);
  assert(q.x==mixer_column[d]*XZ_SHELL_WIDTH/4&&q.w==XZ_SHELL_WIDTH/4&&q.h>=44&&q.y+q.h==XZ_SHELL_HEIGHT);
  int was=u.deck;size_t n=tap_rect(&u,&m,q,out);
  assert(u.deck==d&&(was==d?n==0:n==1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==d));
 }
 assert(xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_STRIP,4).w==0);
 for(int d=0;d<4;d++){
  xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_BROWSE,-1,out);
  tap_rect(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,2),out);
  struct xz_shell_rect cell=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,d+1),load=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,0);
  assert(cell.w>0&&cell.x>=load.x+load.w&&xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,5).w==0);
  assert(tap_rect(&u,&m,cell,out)>=1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==d&&out[0].id==(int)lib.tracks[2].id);
 }
 render_both(&u,&m);
 /* A two-deck model keeps its halves and two LOAD cells. */
 struct xz_shell_model two=m;two.deck_count=2;
 xz_shell_init(&u);deck_key(&u,&two,XZ_SHELL_KEY_BROWSE,-1,out);
 tap_rect(&u,&two,xz_shell_browse_rect(&u,&two,XZ_SHELL_BR_ROW,2),out);
 assert(xz_shell_browse_rect(&u,&two,XZ_SHELL_BR_LOAD,2).w>0&&xz_shell_browse_rect(&u,&two,XZ_SHELL_BR_LOAD,3).w==0);
 assert(xz_shell_browse_rect(&u,&two,XZ_SHELL_BR_STRIP,1).w==XZ_SHELL_WIDTH/2&&xz_shell_browse_rect(&u,&two,XZ_SHELL_BR_STRIP,2).w==0);
 /* INFO: every mixer quarter shows the same logical deck's tab. */
 xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_INFO,-1,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_INFO);
 struct xz_shell_rect strip=xz_shell_info_rect(XZ_SHELL_INFO_R_STRIP,0);
 int y=strip.y+strip.h/2;
 tap(&u,&m,XZ_SHELL_WIDTH/2+20,y,out);assert(u.stack[1].focus==2);
 tap(&u,&m,XZ_SHELL_WIDTH/4+20,y,out);assert(u.stack[1].focus==1);
 assert(tap(&u,&m,20,y,out)==0&&u.stack[1].focus==3);
 assert(tap(&u,&m,XZ_SHELL_WIDTH*3/4+20,y,out)==0&&u.stack[1].focus==4);
 render_both(&u,&m);
 static const enum xz_shell_key screens[]={XZ_SHELL_KEY_SOURCE,XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_KEY_MENU_HOLD};
 for(size_t i=0;i<sizeof screens/sizeof *screens;i++){xz_shell_init(&u);deck_key(&u,&m,screens[i],-1,out);render_both(&u,&m);}
 /* Every quarter carries its deck: an unloaded deck says so in its own quarter only. */
 xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_BROWSE,-1,out);render_both(&u,&m);
 static uint16_t loaded[GUARD+PIXELS+GUARD];memcpy(loaded,buffer_a,sizeof loaded);
 for(int d=0;d<4;d++){
  struct xz_shell_model one=m;fx_deck_unloaded(&one.deck[d],XZ_SHELL_SRC_NONE);render_both(&u,&one);
  struct xz_shell_rect q=xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_STRIP,d);int changes=0;
  for(int yy=0;yy<XZ_SHELL_HEIGHT;yy++)for(int x=0;x<XZ_SHELL_WIDTH;x++){
   int changed=frame(loaded)[yy*STRIDE+x]!=frame(buffer_a)[yy*STRIDE+x];changes+=changed&&yy>=q.y;
   assert(!changed||yy<q.y||sh_inside(q,x,yy));
  }
  assert(changes>0);
 }
 puts("PASS four-deck status strips: quarters, LOAD target and cells, INFO tabs");
}

static void test_wave_order(void) {
 static const int numeric[4]={0,1,2,3},mixer[4]={2,0,1,3};
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_four(&m,&track);m.settings_screen=fxs_reset();xz_shell_init(&u);
 for(int order=0;order<2;order++){
  m.wave_order=order;
  const int *decks=order?mixer:numeric;
  for(int row=0;row<4;row++){
   int d=decks[row];struct xz_shell_rect lane=rect(&m,XZ_SHELL_R_LANE,d);
   assert(lane.y==26+row*78);
   assert(rect(&m,XZ_SHELL_R_STRIP,d).x==mixer_column[d]*200+2);
   tap(&u,&m,50,26+row*78+38,out);assert(u.deck==d);
   stems_on(&m);
   struct xz_shell_rect stem=xz_shell_stem_row(&m,d);
   assert(stem.y+stem.h==lane.y+lane.h);
  }
  render_both(&u,&m);
  deck_key(&u,&m,XZ_SHELL_KEY_SHORTCUT,-1,out);
  render_both(&u,&m);
  /* DECK VIEW replaced SHORTCUT's WAVE ORDER toggle: the title bar there sends nothing. */
  size_t n=tap(&u,&m,550,20,out);
  assert(n==0||out[0].kind!=XZ_SHELL_ACT_WAVE_ORDER);
  deck_key(&u,&m,XZ_SHELL_KEY_BACK,-1,out);
 }
 m.deck_count=2;m.wave_order=1;
 assert(rect(&m,XZ_SHELL_R_LANE,0).y==26&&rect(&m,XZ_SHELL_R_LANE,1).y==145);
 assert(rect(&m,XZ_SHELL_R_STRIP,0).x==2&&rect(&m,XZ_SHELL_R_STRIP,1).x==402);
 puts("PASS waveform order switches lanes and touches; mixer strips stay 3/1/2/4; two-deck view unchanged");
}

static void test_mixer_column_actions(void) {
 static const int deck_at_column[4]={2,0,1,3};
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_four(&m,&track);bx_tracks(&b,&lib,XZ_SHELL_BROWSE);m.browse=&b;
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;
 for(int column=0;column<4;column++){
  int deck=deck_at_column[column];xz_shell_init(&u);
  size_t n=tap(&u,&m,column*200+50,380,out);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].deck==deck&&out[0].id==XZ_SHELL_SET_TIME_MODE);
  n=tap(&u,&m,column*200+100,460,out);
  assert(n>=1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==deck);
  xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_BROWSE,-1,out);
  tap_rect(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_ROW,2),out);
  tap(&u,&m,column*200+100,450,out);assert(u.deck==deck);
  n=tap_rect(&u,&m,xz_shell_browse_rect(&u,&m,XZ_SHELL_BR_LOAD,deck+1),out);
  assert(n>=1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==deck&&out[0].id==(int)lib.tracks[2].id);
 }
 puts("PASS fixed mixer columns 3/1/2/4 route TIME, SEEK and BROWSE LOAD to the labelled deck");
}

/* A deck past deck_count is refused as an index past the model is today. */
static void test_refusal(void) {
 struct xz_shell u;struct xz_shell_model two,four;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_four(&four,&track);two=four;two.deck_count=2;
 stems_on(&two);
 for(int d=2;d<XZ_SHELL_DECKS;d++){
  assert(xz_shell_elapsed_now(&two,d)==-1&&xz_shell_elapsed_now(&four,d)>=0);
  assert(xz_shell_stem_row(&two,d).h==0&&xz_shell_stem_button(NULL,&two,d,0).h==0);
 }
 assert(xz_shell_elapsed_now(&four,4)==-1&&xz_shell_elapsed_now(&four,-1)==-1);
 /* A key for deck 3 on a two-deck model acts on the active deck, as a key for deck 9 always has. */
 xz_shell_init(&u);u.deck=1;
 assert(deck_key(&u,&two,XZ_SHELL_KEY_TIME_MODE,2,out)==1&&out[0].deck==1);
 assert(deck_key(&u,&two,XZ_SHELL_KEY_TIME_MODE,9,out)==1&&out[0].deck==1);
 assert(deck_key(&u,&four,XZ_SHELL_KEY_TIME_MODE,4,out)==1&&out[0].deck==1);
 /* An active deck left over from a four-deck model falls back to deck 1 on a two-deck one. */
 u.deck=3;
 assert(deck_key(&u,&two,XZ_SHELL_KEY_QUANTIZE,-1,out)==1&&out[0].deck==0);
 u.grid_mode=1;
 struct xz_shell_event turn={XZ_SHELL_ENCODER_TURN,0,0,1,0,-1};
 assert(xz_shell_input(&u,&two,&turn,out)==1&&out[0].kind==XZ_SHELL_ACT_GRID_ADJUST&&out[0].deck==0);
 /* LOAD for a deck past the count loads nothing. */
 struct xz_shell_browse_model b;memset(&b,0,sizeof b);
 static struct xz_shell_browse_row row;row.kind=XZ_SHELL_ROW_TRACK;row.id=42;
 b.known=(1u<<XZ_SHELL_BF_TOTAL)|(1u<<XZ_SHELL_BF_LIST);b.screen=XZ_SHELL_BROWSE;b.total=1;b.rows=&row;b.window_count=1;
 two.browse=four.browse=&b;two.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;four.capabilities=two.capabilities;
 for(int d=0;d<4;d++){
  xz_shell_init(&u);deck_key(&u,&four,XZ_SHELL_KEY_BROWSE,-1,out);
  size_t n=deck_key(&u,&four,XZ_SHELL_KEY_LOAD,d&1,out);assert(n==0&&u.load_choice.mode);
  n=deck_key(&u,&four,XZ_SHELL_KEY_LOAD,d>=2,out);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==d&&out[0].id==42);
  xz_shell_init(&u);deck_key(&u,&two,XZ_SHELL_KEY_BROWSE,-1,out);
  n=deck_key(&u,&two,XZ_SHELL_KEY_LOAD,d,out);
  assert(d<2?n==1&&out[0].deck==d:n==0);
 }
 /* Touching where a two-deck model draws its strips never reaches decks 3 and 4. */
 two.browse=NULL;
 for(int y=rect(&two,XZ_SHELL_R_STRIP,0).y;y<XZ_SHELL_HEIGHT;y+=7)for(int x=0;x<XZ_SHELL_WIDTH;x+=9){
  xz_shell_init(&u);size_t n=tap(&u,&two,x,y,out);
  for(size_t i=0;i<n;i++)assert(out[i].deck<2);
 }
 puts("PASS decks past deck_count are refused");
}

/* The software players (DECK 3, 4): per-deck capabilities keep their controls dark, LOAD and needle
   search answer, and SHORTCUT carries a DECK SELECT tile per side. */
static void test_players(void) {
 struct xz_shell u;struct xz_shell_model m,two;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_four_players(&m,&track);
 for(int d=0;d<4;d++){
  xz_shell_init(&u);m.deck[d].playing=0;
  struct xz_shell_rect ov=rect(&m,XZ_SHELL_R_OVERVIEW,d);
  size_t n=tap(&u,&m,ov.x+ov.w/2,ov.y+ov.h/2,out);
  assert(d>=2?n==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==d:n==0);
 }
 fx_model_four_players(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_TAG_TRACK|XZ_SHELL_CAP_GRID_EDIT;
 for(int d=2;d<4;d++){
  xz_shell_init(&u);
  assert(tap_rect(&u,&m,rect(&m,XZ_SHELL_R_TIME,d),out)==0);
  assert(deck_key(&u,&m,XZ_SHELL_KEY_TIME_MODE,d,out)==0&&deck_key(&u,&m,XZ_SHELL_KEY_QUANTIZE,d,out)==0);
  assert(deck_key(&u,&m,XZ_SHELL_KEY_TAG_TRACK,d,out)==0);
  u.deck=d;
  assert(deck_key(&u,&m,XZ_SHELL_KEY_TAG_TRACK,-1,out)==0);
  struct xz_shell_event hold={XZ_SHELL_ENCODER_HOLD,0,0,0,0,-1};
  xz_shell_input(&u,&m,&hold,out);assert(!u.grid_mode);
  u.grid_mode=1;
  struct xz_shell_event turn={XZ_SHELL_ENCODER_TURN,0,0,1,0,-1};
  assert(xz_shell_input(&u,&m,&turn,out)==1&&out[0].kind==XZ_SHELL_ACT_ZOOM);
  for(int g=0;g<3;g++){
   size_t n=tap_rect(&u,&m,rect(&m,(enum xz_shell_play_region)(XZ_SHELL_R_GRID_RESET+g),d),out);
   for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_GRID_EDIT);
  }
  u.grid_mode=0;
  /* The same keys still reach the native deck on that side. */
  assert(deck_key(&u,&m,XZ_SHELL_KEY_TIME_MODE,d-2,out)==1&&out[0].deck==d-2);
  assert(deck_key(&u,&m,XZ_SHELL_KEY_TAG_TRACK,d-2,out)==1&&out[0].kind==XZ_SHELL_ACT_TAG_TRACK&&out[0].deck==d-2);
 }
 /* Stem buttons stay dark on a player even with mute bits set. */
 stems_on(&m);m.capabilities|=XZ_SHELL_CAP_STEM_GAIN;
 for(int d=2;d<4;d++){
  m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_GAIN);
  assert(xz_shell_stem_button(NULL,&m,d,0).h==0);
 }
 render_both(&u,&m);
 fx_model_four_players(&m,&track);
 /* LOAD reaches every deck from the list. */
 struct xz_shell_browse_model b;memset(&b,0,sizeof b);
 static struct xz_shell_browse_row row;row.kind=XZ_SHELL_ROW_TRACK;row.id=42;
 b.known=(1u<<XZ_SHELL_BF_TOTAL)|(1u<<XZ_SHELL_BF_LIST);b.screen=XZ_SHELL_BROWSE;b.total=1;b.rows=&row;b.window_count=1;
 m.browse=&b;m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;
 for(int d=0;d<4;d++){
  xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_BROWSE,-1,out);
  assert(deck_key(&u,&m,XZ_SHELL_KEY_LOAD,d&1,out)==0&&u.load_choice.mode);
  assert(deck_key(&u,&m,XZ_SHELL_KEY_LOAD,d>=2,out)==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==d&&out[0].id==42);
 }
 /* An unavailable native target stays disabled while its extra partner remains selectable. */
 m.deck[0].caps_off|=XZ_SHELL_CAP_LOAD;
 xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_BROWSE,-1,out);
 assert(deck_key(&u,&m,XZ_SHELL_KEY_LOAD,0,out)==0&&u.load_choice.mode);
 assert(deck_key(&u,&m,XZ_SHELL_KEY_LOAD,0,out)==0&&!u.load_choice.mode);
 render_both(&u,&m);
 m.deck[0].caps_off&=~XZ_SHELL_CAP_LOAD;
 /* DECK SELECT: a tile per side, only in a four-deck model with the capability. */
 two=m;two.deck_count=2;
 for(int side=0;side<2;side++){
  struct xz_shell_rect t=xz_shell_shortcut_deck_select(&m,side),q=xz_shell_shortcut_quadrant(1);
  assert(xz_shell_shortcut_deck_select(&two,side).w==0&&xz_shell_shortcut_deck_select(NULL,side).w==0);
  assert(t.w>=60&&t.h>=30&&within(t,q));
  for(int l=0;l<XZ_SHELL_LAMP_COUNT;l++){
   struct xz_shell_rect a=xz_shell_shortcut_lamp(&m,side,l),plain=xz_shell_shortcut_lamp(&two,side,l),none=xz_shell_shortcut_lamp(NULL,side,l);
   assert(within(a,q)&&!overlap(a,t)&&a.w<plain.w&&!memcmp(&plain,&none,sizeof plain));
  }
  for(int j=0;j<XZ_SHELL_JUMP_COUNT;j++)assert(!overlap(t,xz_shell_shortcut_jump(j)));
  m.capabilities&=~XZ_SHELL_CAP_DECK_SELECT;assert(xz_shell_shortcut_deck_select(&m,side).w==0);
  m.capabilities|=XZ_SHELL_CAP_DECK_SELECT;
 }
 m.settings_screen=fxs_reset();
 for(int side=0;side<2;side++){
  xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_SHORTCUT,-1,out);
  assert(xz_shell_screen(&u)==XZ_SHELL_SHORTCUT);
  struct xz_shell_rect t=xz_shell_shortcut_deck_select(&m,side);
  uint8_t sides=m.sides_extra;
  /* The tile asks; the runtime publishes the switch in the next model. */
  assert(tap_rect(&u,&m,t,out)==1&&out[0].kind==XZ_SHELL_ACT_DECK_SELECT&&out[0].deck==side&&m.sides_extra==sides);
  /* A 44 px touch target. */
  assert(tap(&u,&m,t.x+t.w/2,t.y-7,out)==1&&out[0].kind==XZ_SHELL_ACT_DECK_SELECT);
  assert(tap(&u,&m,t.x+t.w/2,t.y+t.h+6,out)==1&&out[0].kind==XZ_SHELL_ACT_DECK_SELECT);
  for(m.sides_extra=0;m.sides_extra<4;m.sides_extra++)render_both(&u,&m);
  m.sides_extra=sides;
 }
 /* Without the capability that place answers nothing. */
 m.capabilities&=~XZ_SHELL_CAP_DECK_SELECT;
 {
  xz_shell_init(&u);deck_key(&u,&m,XZ_SHELL_KEY_SHORTCUT,-1,out);
  struct xz_shell_rect plain=xz_shell_shortcut_lamp(&m,0,XZ_SHELL_LAMP_TIME);
  size_t n=tap(&u,&m,plain.x+plain.w+20,plain.y+plain.h/2,out);
  for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_DECK_SELECT);
 }
 puts("PASS players: per-deck capabilities, LOAD and needle search, SHORTCUT DECK SELECT");
}

/* Only 4 means four decks; every other count draws and answers as two. */
static void test_other_counts(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action a[XZ_SHELL_ACTIONS],b[XZ_SHELL_ACTIONS];
 fx_model_four(&m,&track);
 xz_shell_init(&u);
 m.deck_count=2;render_both(&u,&m);
 static uint16_t two[GUARD+PIXELS+GUARD];memcpy(two,buffer_a,sizeof two);
 static const int counts[]={0,1,3,5,-1,8,-4,1<<20};
 for(size_t i=0;i<sizeof counts/sizeof *counts;i++){
  m.deck_count=counts[i];
  assert(sh_deck_count(&m)==2);
  render_both(&u,&m);
  assert(!memcmp(two,buffer_a,sizeof two));
  for(int r=XZ_SHELL_R_TOP_BAR;r<=XZ_SHELL_R_GRID_SHIFT;r++)for(int d=0;d<2;d++){
   struct xz_shell_rect p=rect(&m,(enum xz_shell_play_region)r,d),q=xz_shell_play_rect(NULL,(enum xz_shell_play_region)r,d);
   assert(!memcmp(&p,&q,sizeof p));
  }
  for(int y=30;y<XZ_SHELL_HEIGHT;y+=23)for(int x=5;x<XZ_SHELL_WIDTH;x+=31){
   struct xz_shell w,v;xz_shell_init(&w);xz_shell_init(&v);
   struct xz_shell_model twice=m;twice.deck_count=2;
   size_t n=tap(&w,&m,x,y,a),k=tap(&v,&twice,x,y,b);
   assert(n==k&&!memcmp(a,b,n*sizeof *a)&&w.deck==v.deck);
  }
 }
 puts("PASS deck counts other than 2 and 4 behave as 2");
}

/* Every four-deck state covers the frame and stays inside it. */
static void test_frames(void) {
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);fx_model_four(&m,&track);
 render_both(&u,&m);
 for(u.deck=0;u.deck<4;u.deck++)render_both(&u,&m);
 u.deck=3;u.grid_mode=1;m.capabilities|=XZ_SHELL_CAP_GRID_EDIT;render_both(&u,&m);
 stems_on(&m);
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){m.stem_layout=(enum xz_shell_stem_layout)layout;render_both(&u,&m);}
 u.grid_mode=0;
 m.deck[2].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);m.deck[2].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_EMERGENCY_LOOP,NULL,NULL};
 render_both(&u,&m);
 fx_deck_unloaded(&m.deck[2],XZ_SHELL_SRC_USB2);fx_deck_unloaded(&m.deck[3],XZ_SHELL_SRC_NONE);render_both(&u,&m);
 memset(&m.deck[3],0,sizeof m.deck[3]);render_both(&u,&m);
 m.deck[0].title=m.deck[1].title="WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
 m.deck[0].tempo_x100=-9999;m.deck[0].bpm_x100=99999;m.deck[1].tempo_range=100;
 render_both(&u,&m);
 for(int screen=XZ_SHELL_PLAY;screen<XZ_SHELL_SCREEN_COUNT;screen++){
  u.stack[0].screen=(enum xz_shell_screen)screen;u.depth=1;render_both(&u,&m);
 }
 puts("PASS four-deck frames: every pixel drawn, guard words intact");
}

int main(void) {
 fx_track_build(&track,7);
 bx_build(&lib);
 (void)bx_folders;(void)fxs_item;
 test_layout();
 test_bypass_four();
 test_deck_input();
 test_strips();
 test_mixer_column_actions();test_wave_order();
 test_refusal();
 test_players();
 test_other_counts();
 test_frames();
 puts("PASS four decks");
 return 0;
}
