#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "fixtures.h"
#include "shell_browse.h"
#include "shell_settings.h"
#include "shell_info.h"
#include "shell_source.h"
static struct xz_shell_browse_model browse_zero;static struct xz_shell_settings_model settings_zero;static struct xz_shell_info_model info_zero;static struct xz_shell_source_model source_zero;
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;

/* Frame origin one padded row in, so writes above, below, left of the first
   column and past column 799 all land on guard words. */
static uint16_t *frame(uint16_t *buffer) { return buffer+GUARD+STRIDE; }
static void fill(uint16_t *buffer) { for(size_t i=0;i<GUARD+PIXELS+GUARD;i++)buffer[i]=0xa55a; }
static void check_guards(uint16_t *buffer) {
 for(size_t i=0;i<GUARD+STRIDE;i++)assert(buffer[i]==0xa55a);
 for(size_t i=GUARD+STRIDE+(size_t)STRIDE*XZ_SHELL_HEIGHT;i<GUARD+PIXELS+GUARD;i++)assert(buffer[i]==0xa55a);
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=XZ_SHELL_WIDTH;x<STRIDE;x++)assert(frame(buffer)[y*STRIDE+x]==0xa55a);
}
static uint16_t at(const uint16_t *f, int x, int y) { return f[(size_t)y*STRIDE+(size_t)x]; }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static void render_checked(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *buffer) {
 fill(buffer);
 assert(xz_shell_render(u,m,frame(buffer),STRIDE));
 check_guards(buffer);
}
static size_t press(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,k,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t touch(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_event_kind kind, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={kind,x,y,0,0,-1};
 return xz_shell_input(u,m,&e,out);
}
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int delta, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_event e={XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1};
 return xz_shell_input(u,m,&e,out);
}
static void enable_all_screens(struct xz_shell_model *m) {
 m->browse=&browse_zero;m->settings_screen=&settings_zero;
 m->source=&source_zero;m->info=&info_zero;
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_SOURCE;
}

static void test_unknown_fields_read_nothing(void) {
 struct xz_shell u;struct xz_shell_model m,zero;
 xz_shell_init(&u);
 memset(&m,0xa5,sizeof m);                /* garbage everywhere ... */
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  m.deck[d].known=0;                      /* ... but nothing is known ... */
  m.deck[d].title=m.deck[d].artist=m.deck[d].key=NULL;  /* ... and every pointer is NULL */
  m.deck[d].grid=NULL;m.deck[d].wave.columns=NULL;m.deck[d].overview.columns=NULL;m.deck[d].artwork=NULL;
 }
 m.settings.known=0;m.link_known=0;m.capabilities=0;m.clock_ms=0;m.fx.known=0;m.stems_known=0;m.notice=NULL;
 m.browse=NULL;m.settings_screen=NULL;m.source=NULL;m.info=NULL;
 render_checked(&u,&m,buffer_a);
 memset(&zero,0,sizeof zero);
 render_checked(&u,&zero,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* A loaded deck whose other fields are unknown must not read them either. */
 for(int d=0;d<XZ_SHELL_DECKS;d++){m.deck[d].known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED);m.deck[d].loaded=1;}
 render_checked(&u,&m,buffer_a);
 /* Known fields with NULL pointers and wild counts are tolerated too. */
 for(int d=0;d<XZ_SHELL_DECKS;d++){m.deck[d].known=fx_all_known();m.deck[d].total_ms=300000;m.deck[d].elapsed_ms=1000;m.deck[d].grid_count=99999;m.deck[d].wave.count=99999;m.deck[d].wave.span_ms=300000;m.deck[d].overview.count=5;m.deck[d].memory_count=-7;}
 render_checked(&u,&m,buffer_a);
 puts("PASS unknown fields render without reading their data");
}
static void test_bounds_and_long_text(void) {
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);
 fx_model_full(&m,&track);
 static char wide[400];memset(wide,'W',sizeof wide-1);
 m.deck[0].title=wide;m.deck[1].title="\xc3\x89t\xc3\xa9 \xc3\xa0 Caf\xc3\xa9 \xe2\x99\xaa \xf0\x9f\x8e\xb5 odd bytes \xff\xfe";
 m.deck[0].key="Abmaj7/Eb";m.deck[1].key="12A";
 m.deck[0].track_no=12345;m.deck[1].tempo_x100=-10000;m.deck[1].tempo_range=100;m.deck[0].bpm_x100=99999;
 m.deck[0].elapsed_ms=99*60000+59999;m.deck[0].total_ms=99*60000+59999;
 m.deck[1].key_shift=-12;m.deck[1].beat_jump_num=64;m.deck[1].quantize_num=1;m.deck[1].quantize_den=8;
 m.deck[0].loop=(struct xz_shell_loop){1,1,16,0,FX_TOTAL_MS};
 for(int i=0;i<XZ_SHELL_HOT_CUES;i++)m.deck[1].hot_cue[i]=(struct xz_shell_cue){1,FX_TOTAL_MS-i,0xffffff,0};
 render_checked(&u,&m,buffer_a);
 /* The title stops before the transport glyph zone at the strip's right edge. */
 struct xz_shell_rect s=xz_shell_play_rect(XZ_SHELL_R_STRIP,0);
 for(int y=s.y+5;y<s.y+26;y++)for(int x=s.x+s.w-8-21;x<s.x+s.w-8-13;x++)assert(at(frame(buffer_a),x,y)==rgb565(SH_CHROME));
 /* Every region lies inside the frame and the lanes never overlap the strips. */
 for(int d=0;d<XZ_SHELL_DECKS;d++)for(int r=XZ_SHELL_R_TOP_BAR;r<=XZ_SHELL_R_FX;r++){
  struct xz_shell_rect q=xz_shell_play_rect((enum xz_shell_play_region)r,d);
  assert(q.w>0&&q.h>0&&q.x>=0&&q.y>=0&&q.x+q.w<=XZ_SHELL_WIDTH&&q.y+q.h<=XZ_SHELL_HEIGHT);
 }
 struct xz_shell_rect lane1=xz_shell_play_rect(XZ_SHELL_R_LANE,1),strip0=xz_shell_play_rect(XZ_SHELL_R_STRIP,0),strip1=xz_shell_play_rect(XZ_SHELL_R_STRIP,1);
 assert(lane1.y+lane1.h<=strip0.y&&strip0.x+strip0.w<=strip1.x);
 struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,1),time=xz_shell_play_rect(XZ_SHELL_R_TIME,1);
 assert(ov.x>=strip1.x&&ov.x+ov.w<=strip1.x+strip1.w&&time.y+time.h<=ov.y);
 /* Overlong zoom and a stale stack depth are refused, not drawn. */
 assert(!xz_shell_render(&u,&m,NULL,STRIDE));
 assert(!xz_shell_render(&u,&m,frame(buffer_a),XZ_SHELL_WIDTH-1));
 u.depth=0;assert(!xz_shell_render(&u,&m,frame(buffer_a),STRIDE));
 puts("PASS text and regions stay in bounds");
}
static void test_navigation(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_full(&m,&track);enable_all_screens(&m);
 static const struct { enum xz_shell_key key; enum xz_shell_screen screen; } lists[]={
  {XZ_SHELL_KEY_BROWSE,XZ_SHELL_BROWSE},{XZ_SHELL_KEY_TAG_LIST,XZ_SHELL_TAG_LIST},{XZ_SHELL_KEY_PLAYLIST,XZ_SHELL_PLAYLIST},
  {XZ_SHELL_KEY_SEARCH,XZ_SHELL_SEARCH},{XZ_SHELL_KEY_SOURCE,XZ_SHELL_SOURCE},{XZ_SHELL_KEY_INFO,XZ_SHELL_INFO},
  {XZ_SHELL_KEY_SHORTCUT,XZ_SHELL_SHORTCUT},{XZ_SHELL_KEY_MENU_HOLD,XZ_SHELL_UTILITY}};
 for(size_t i=0;i<sizeof lists/sizeof *lists;i++){
  /* BROWSE and PLAYLIST open over the SOURCE picker; BACK from their top reaches it, then PLAY. */
  int rooted=lists[i].screen==XZ_SHELL_BROWSE||lists[i].screen==XZ_SHELL_PLAYLIST;
  xz_shell_init(&u);
  assert(press(&u,&m,lists[i].key,out)==0&&xz_shell_screen(&u)==lists[i].screen&&u.depth==(rooted?3:2));
  render_checked(&u,&m,buffer_a);
  enum xz_shell_key again=lists[i].key==XZ_SHELL_KEY_MENU_HOLD?XZ_SHELL_KEY_MENU:lists[i].key;
  assert(press(&u,&m,again,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY&&u.depth==1);
  assert(press(&u,&m,lists[i].key,out)==0&&xz_shell_screen(&u)==lists[i].screen);
  if(rooted)assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SOURCE);
  assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
 }
 /* One list at a time: a second list key swaps. */
 xz_shell_init(&u);
 press(&u,&m,XZ_SHELL_KEY_BROWSE,out);press(&u,&m,XZ_SHELL_KEY_PLAYLIST,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_PLAYLIST&&u.depth==3);
 /* Overlays stack; BACK goes up one level, BACK_HOLD to the top. */
 press(&u,&m,XZ_SHELL_KEY_SHORTCUT,out);press(&u,&m,XZ_SHELL_KEY_MENU_HOLD,out);
 assert(xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.depth==5);
 assert(press(&u,&m,XZ_SHELL_KEY_SHORTCUT,out)==0&&u.depth==5); /* already on the stack: no change */
 assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SHORTCUT&&u.depth==4);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAYLIST&&u.depth==3);
 /* On a list INFO slides out the info panel instead of a screen. */
 press(&u,&m,XZ_SHELL_KEY_INFO,out);assert(u.depth==3&&u.panel.kind==XZ_SHELL_PANEL_INFO);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK_HOLD,out)==0&&xz_shell_screen(&u)==XZ_SHELL_PLAY&&u.depth==1&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 assert(press(&u,&m,XZ_SHELL_KEY_BACK,out)==0&&u.depth==1);
 /* A short MENU on PLAY does nothing, as on the stock deck; on lists it opens the MENU panel; on UTILITY it closes. */
 assert(press(&u,&m,XZ_SHELL_KEY_MENU,out)==0&&u.depth==1);
 press(&u,&m,XZ_SHELL_KEY_BROWSE,out);
 assert(press(&u,&m,XZ_SHELL_KEY_MENU,out)==0&&u.panel.kind==XZ_SHELL_PANEL_MENU&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 assert(press(&u,&m,XZ_SHELL_KEY_MENU,out)==0&&u.panel.kind==XZ_SHELL_PANEL_NONE);
 /* HAND_OFF when a slot is NULL or a capability is missing; the stack does not move. */
 struct xz_shell_model bare;fx_model_full(&bare,&track);
 xz_shell_init(&u);
 assert(press(&u,&bare,XZ_SHELL_KEY_BROWSE,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_BROWSE&&u.depth==1);
 assert(press(&u,&bare,XZ_SHELL_KEY_INFO,out)==1&&out[0].id==XZ_SHELL_INFO&&u.depth==1);
 assert(press(&u,&bare,XZ_SHELL_KEY_MENU_HOLD,out)==1&&out[0].id==XZ_SHELL_UTILITY&&u.depth==1);
 assert(press(&u,&bare,XZ_SHELL_KEY_SOURCE,out)==1&&out[0].id==XZ_SHELL_SOURCE&&u.depth==1);
 struct xz_shell_model partial=m;partial.capabilities&=~(uint32_t)XZ_SHELL_CAP_TAG_LIST;
 assert(press(&u,&partial,XZ_SHELL_KEY_TAG_LIST,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_TAG_LIST&&u.depth==1);
 assert(press(&u,&partial,XZ_SHELL_KEY_BROWSE,out)==0&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);
 /* The (i) button follows the same table as the INFO key. */
 xz_shell_init(&u);
 struct xz_shell_rect ib=xz_shell_play_rect(XZ_SHELL_R_INFO_BUTTON,0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,ib.x+ib.w/2,ib.y+ib.h/2,out)==0&&xz_shell_screen(&u)==XZ_SHELL_INFO);
 xz_shell_init(&u);
 assert(touch(&u,&bare,XZ_SHELL_TOUCH_DOWN,ib.x+ib.w/2,ib.y+ib.h/2,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_INFO);
 /* Keys out of range do nothing. */
 struct xz_shell_event bad={XZ_SHELL_KEY,0,0,0,(enum xz_shell_key)99,-1};
 assert(xz_shell_input(&u,&m,&bad,out)==0);
 /* TAG TRACK needs its capability. */
 assert(press(&u,&m,XZ_SHELL_KEY_TAG_TRACK,out)==0);
 struct xz_shell_model tagging=m;tagging.capabilities|=XZ_SHELL_CAP_TAG_TRACK;u.deck=1;
 assert(press(&u,&tagging,XZ_SHELL_KEY_TAG_TRACK,out)==1&&out[0].kind==XZ_SHELL_ACT_TAG_TRACK&&out[0].deck==1);
 puts("PASS navigation table round-trips, BACK semantics and hand-off");
}
static void test_seek_zoom_and_deck(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_full(&m,&track);
 xz_shell_init(&u);
 for(int d=0;d<XZ_SHELL_DECKS;d++){
  struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,d);
  int cy=ov.y+ov.h/2;
  assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,ov.x,cy,out)==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==d&&out[0].value==0);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_MOVE,ov.x+ov.w-1,cy,out)==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].value==FX_TOTAL_MS);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_MOVE,ov.x+ov.w+500,cy,out)==1&&out[0].value==FX_TOTAL_MS);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_MOVE,ov.x-500,cy,out)==1&&out[0].value==0);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_MOVE,ov.x+(ov.w-1)/2,cy,out)==1&&abs(out[0].value-FX_TOTAL_MS/2)<=FX_TOTAL_MS/(ov.w-1)+1);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_UP,ov.x,cy,out)==0);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_MOVE,ov.x,cy,out)==0); /* nothing held */
  assert(xz_shell_overview_ms(FX_TOTAL_MS,ov.x+ov.w-1,d)==FX_TOTAL_MS&&xz_shell_overview_ms(FX_TOTAL_MS,ov.x,d)==0&&xz_shell_overview_ms(0,ov.x+10,d)==0);
 }
 /* No SEEK capability, no track length, or no track: nothing. */
 struct xz_shell_rect ov=xz_shell_play_rect(XZ_SHELL_R_OVERVIEW,0);
 struct xz_shell_model no=m;no.capabilities&=~(uint32_t)XZ_SHELL_CAP_SEEK;
 assert(touch(&u,&no,XZ_SHELL_TOUCH_DOWN,ov.x+10,ov.y+10,out)==0);
 touch(&u,&no,XZ_SHELL_TOUCH_UP,0,0,out);
 no=m;no.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_TOTAL);
 assert(touch(&u,&no,XZ_SHELL_TOUCH_DOWN,ov.x+10,ov.y+10,out)==0);
 touch(&u,&no,XZ_SHELL_TOUCH_UP,0,0,out);
 no=m;no.deck[0].loaded=0;
 assert(touch(&u,&no,XZ_SHELL_TOUCH_DOWN,ov.x+10,ov.y+10,out)==0);
 touch(&u,&no,XZ_SHELL_TOUCH_UP,0,0,out);
 /* Zoom is shared shell state, clamped, reported once per change. */
 assert(u.zoom==XZ_SHELL_ZOOM_DEFAULT);
 assert(turn(&u,&m,100,out)==1&&out[0].kind==XZ_SHELL_ACT_ZOOM&&out[0].value==XZ_SHELL_ZOOM_MAX&&u.zoom==XZ_SHELL_ZOOM_MAX);
 assert(turn(&u,&m,1,out)==0&&u.zoom==XZ_SHELL_ZOOM_MAX);
 assert(turn(&u,&m,-100,out)==1&&out[0].value==0&&u.zoom==0);
 assert(turn(&u,&m,-1,out)==0&&turn(&u,&m,0,out)==0&&u.zoom==0);
 assert(turn(&u,&m,2,out)==1&&out[0].value==2);
 render_checked(&u,&m,buffer_a);
 /* GRID mode takes the encoder only when the runtime can adjust grids. */
 struct xz_shell_event hold={XZ_SHELL_ENCODER_HOLD,0,0,0,0,-1};
 assert(xz_shell_input(&u,&m,&hold,out)==0&&u.grid_mode==1);
 u.deck=1;
 assert(turn(&u,&m,-3,out)==1&&out[0].kind==XZ_SHELL_ACT_GRID_ADJUST&&out[0].deck==1&&out[0].value==-3&&u.zoom==2);
 render_checked(&u,&m,buffer_a);
 assert(xz_shell_input(&u,&m,&hold,out)==0&&u.grid_mode==0);
 struct xz_shell_model nogrid=m;nogrid.capabilities&=~(uint32_t)XZ_SHELL_CAP_GRID;
 assert(xz_shell_input(&u,&nogrid,&hold,out)==0&&u.grid_mode==0);
 u.grid_mode=1;assert(turn(&u,&nogrid,1,out)==1&&out[0].kind==XZ_SHELL_ACT_ZOOM);u.grid_mode=0;
 /* Touching a lane, tag or info column makes that deck active, once. */
 u.deck=0;
 struct xz_shell_rect lane=xz_shell_play_rect(XZ_SHELL_R_LANE,1);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,lane.x+lane.w/2,lane.y+lane.h/2,out)==1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==1&&u.deck==1);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,lane.x+5,lane.y+5,out)==0&&u.deck==1);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 struct xz_shell_rect tag=xz_shell_play_rect(XZ_SHELL_R_DECK_TAG,0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,tag.x+5,tag.y+5,out)==1&&out[0].deck==0&&u.deck==0);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 /* TIME MODE flips the shown mode only through an action; the shell itself holds no mode. */
 struct xz_shell_event tm={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_TIME_MODE,1};
 assert(xz_shell_input(&u,&m,&tm,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_SET_TIME_MODE&&out[0].deck==1&&out[0].value==XZ_SHELL_TIME_REMAIN);
 tm.deck=0;
 assert(xz_shell_input(&u,&m,&tm,out)==1&&out[0].deck==0&&out[0].value==XZ_SHELL_TIME_ELAPSED);
 struct xz_shell_rect time=xz_shell_play_rect(XZ_SHELL_R_TIME,0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,time.x+10,time.y+10,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_SET_TIME_MODE&&out[0].value==XZ_SHELL_TIME_ELAPSED);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 struct xz_shell_model readonly=m;readonly.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 assert(xz_shell_input(&u,&readonly,&tm,out)==0);
 assert(touch(&u,&readonly,XZ_SHELL_TOUCH_DOWN,time.x+10,time.y+10,out)==0);
 touch(&u,&readonly,XZ_SHELL_TOUCH_UP,0,0,out);
 struct xz_shell_event q={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_QUANTIZE,1};
 assert(xz_shell_input(&u,&m,&q,out)==1&&out[0].id==XZ_SHELL_SET_QUANTIZE&&out[0].deck==1&&out[0].value==1);
 /* Top-bar chips toggle device settings when the runtime can set them. */
 struct xz_shell_rect bar=xz_shell_play_rect(XZ_SHELL_R_TOP_BAR,0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,bar.x+20,bar.y+12,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_SET_PLAY_MODE&&out[0].value==XZ_SHELL_PLAY_CONTINUE);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 assert(touch(&u,&readonly,XZ_SHELL_TOUCH_DOWN,bar.x+20,bar.y+12,out)==0);
 touch(&u,&readonly,XZ_SHELL_TOUCH_UP,0,0,out);
 puts("PASS seek maths, zoom clamps, grid mode, active deck and time mode");
}
static void test_writable_settings(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 xz_shell_init(&u);fx_model_full(&m,&track);
 const struct xz_canvas measure={NULL,800,800,480,XZ_THEME_FLAGSHIP};
 int widths[]={sh_text_width(measure,"SINGLE",SH_SMALL)+sh_text_width(measure,"CONTINUE",SH_SMALL)+28,
  sh_text_width(measure,"A. HOT CUE",SH_SMALL)+12,sh_text_width(measure,"A. CUE",SH_SMALL)+12};
 const int ids[]={XZ_SHELL_SET_PLAY_MODE,XZ_SHELL_SET_HOT_CUE_AUTO_LOAD,XZ_SHELL_SET_AUTO_CUE};
 m.settings.writable=(uint64_t)1<<XZ_SHELL_SET_TIME_MODE;
 render_checked(&u,&m,buffer_a);
 int x=8;
 for(int i=0;i<3;i++){
  int grey=0;
  for(int y=6;y<20;y++)for(int px=x+1;px<x+widths[i]-1;px++){
   uint16_t ink=at(frame(buffer_a),px,y);
   grey+=ink==rgb565(0x8c8c8c);
   assert(ink!=rgb565(SH_BLUE)&&ink!=rgb565(SH_WHITE));
  }
  assert(grey>0);
  assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,x+widths[i]/2,12,out)==0);
  touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
  m.settings.writable|=(uint64_t)1<<ids[i];
  assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,x+widths[i]/2,12,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==ids[i]);
  touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
  m.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
  assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,x+widths[i]/2,12,out)==0);
  touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
  m.capabilities|=XZ_SHELL_CAP_SET_SETTING;
  m.settings.writable=(uint64_t)1<<XZ_SHELL_SET_TIME_MODE;
  x+=widths[i]+8;
 }
 struct xz_shell_event tm={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_TIME_MODE,1};
 struct xz_shell_rect time=xz_shell_play_rect(XZ_SHELL_R_TIME,1);
 assert(xz_shell_input(&u,&m,&tm,out)==1&&out[0].deck==1);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,time.x+10,time.y+10,out)==1&&out[0].id==XZ_SHELL_SET_TIME_MODE);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 m.settings.writable=0;
 assert(xz_shell_input(&u,&m,&tm,out)==0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,time.x+10,time.y+10,out)==0);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 assert(press(&u,&m,XZ_SHELL_KEY_QUANTIZE,out)==0);
 puts("PASS writable settings: per-chip mask and capability, grey read-only values, TIME MODE key/touch refusal");
}
static void test_load_ignored_outside_lists(void) {
 struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_full(&m,&track);enable_all_screens(&m);m.capabilities|=XZ_SHELL_CAP_LOAD;
 const enum xz_shell_screen screens[]={XZ_SHELL_PLAY,XZ_SHELL_INFO,XZ_SHELL_SHORTCUT,XZ_SHELL_UTILITY,XZ_SHELL_SOURCE};
 for(size_t i=0;i<sizeof screens/sizeof *screens;i++)for(int deck=0;deck<2;deck++){
  struct xz_shell u;xz_shell_init(&u);
  if(screens[i]!=XZ_SHELL_PLAY){u.depth=2;u.stack[1]=(struct xz_shell_view){screens[i],0,0,0};}
  struct xz_shell before=u;
  struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_LOAD,deck};
  assert(xz_shell_input(&u,&m,&e,out)==0&&!memcmp(&before,&u,sizeof u));
 }
 puts("PASS LOAD is inert on every non-list screen");
}
static void test_deterministic(void) {
 struct xz_shell u;struct xz_shell_model m,copy;
 xz_shell_init(&u);
 fx_model_full(&m,&track);
 copy=m;
 render_checked(&u,&m,buffer_a);
 render_checked(&u,&m,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 assert(!memcmp(&m,&copy,sizeof m));
 struct xz_shell before=u;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 touch(&u,&m,XZ_SHELL_TOUCH_DOWN,10,10,out);touch(&u,&m,XZ_SHELL_TOUCH_UP,10,10,out);
 assert(!memcmp(&m,&copy,sizeof m));
 u=before;
 /* Not loaded and LEFT playhead variants also render within bounds. */
 m.deck[1].loaded=0;render_checked(&u,&m,buffer_a);
 u.display.wave_position=XZ_SHELL_POSITION_LEFT;render_checked(&u,&m,buffer_b);u.display.wave_position=XZ_SHELL_POSITION_CENTER;
 assert(memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* The playhead sits where the setting says. */
 struct xz_shell_rect lane=xz_shell_play_rect(XZ_SHELL_R_LANE,0);
 int found=0;
 for(int x=lane.x;x<lane.x+lane.w;x++)if(at(frame(buffer_b),x,lane.y+lane.h/2)==rgb565(SH_PLAYHEAD)){found=x;break;}
 assert(found>lane.x&&found<lane.x+lane.w/3);
 /* The popup slot draws over the screen and BACK closes it. */
 u.popup.kind=XZ_SHELL_POPUP_RECENT;render_checked(&u,&m,buffer_a);
 struct xz_shell_event back={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BACK,-1};
 assert(xz_shell_input(&u,&m,&back,out)==0&&u.popup.kind==XZ_SHELL_POPUP_NONE&&u.depth==1);
 puts("PASS deterministic render, model untouched, playhead position, popup slot");
}
int main(void) {
 fx_track_build(&track,7);
 test_unknown_fields_read_nothing();
 test_bounds_and_long_text();
 test_navigation();
 test_seek_zoom_and_deck();
 test_writable_settings();
 test_load_ignored_outside_lists();
 test_deterministic();
 puts("PASS shell");
 return 0;
}
