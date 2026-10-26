#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* Header buttons, BEAT FX readout, playhead extrapolation, stable scrolling,
   key notation and the SHORTCUT display items. */
#include "fixtures.h"
#include "shell_browse.h"
#include "shell_settings.h"
#include "shell_info.h"
#include "shell_source.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct xz_shell_browse_model browse_zero;static struct xz_shell_settings_model settings_zero;static struct xz_shell_info_model info_zero;static struct xz_shell_source_model source_zero;

#define STRIDE 804
#define GUARD 64
#define PIXELS (STRIDE*(XZ_SHELL_HEIGHT+2))
static uint16_t buffer_a[GUARD+PIXELS+GUARD], buffer_b[GUARD+PIXELS+GUARD];
static struct fx_track track;

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
static uint16_t at(const uint16_t *f, int x, int y) { return f[(size_t)y*STRIDE+(size_t)x]; }
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)); }
static void render_checked(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *buffer) {
 fill(buffer,0xa55a);
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
static void enable_all_screens(struct xz_shell_model *m) {
 m->browse=&browse_zero;m->settings_screen=&settings_zero;
 m->source=&source_zero;m->info=&info_zero;
 m->capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_SOURCE;
}

/* The mod header buttons answer on every screen, padded to 44 px, and their touch stays theirs. */
static void test_header_buttons(void) {
 struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_full(&m,&track);enable_all_screens(&m);
 assert(XZ_SHELL_HEADER_HIT_H>=44&&XZ_SHELL_STEMS_X+XZ_SHELL_STEMS_W<=XZ_SHELL_MODS_X&&XZ_SHELL_MODS_X+XZ_SHELL_MODS_W==XZ_SHELL_WIDTH);
 for(int screen=0;screen<XZ_SHELL_SCREEN_COUNT;screen++)for(int popup=0;popup<2;popup++){
  struct xz_shell u;xz_shell_init(&u);
  if(screen!=XZ_SHELL_PLAY){u.depth=2;u.stack[1]=(struct xz_shell_view){(enum xz_shell_screen)screen,0,0,0};}
  u.popup.kind=popup?XZ_SHELL_POPUP_RECENT:XZ_SHELL_POPUP_NONE;
  struct { int x, y; enum xz_shell_action_kind kind; } hits[]={
   {XZ_SHELL_STEMS_X,0,XZ_SHELL_ACT_TOGGLE_STEMS},{XZ_SHELL_STEMS_X+XZ_SHELL_STEMS_W/2,12,XZ_SHELL_ACT_TOGGLE_STEMS},
   {XZ_SHELL_STEMS_X+XZ_SHELL_STEMS_W,XZ_SHELL_HEADER_HIT_H-1,XZ_SHELL_ACT_TOGGLE_STEMS},
   {XZ_SHELL_MODS_X,12,XZ_SHELL_ACT_OPEN_MODS},{XZ_SHELL_WIDTH-1,XZ_SHELL_HEADER_HIT_H-1,XZ_SHELL_ACT_OPEN_MODS}};
  for(size_t i=0;i<sizeof hits/sizeof *hits;i++){
   struct xz_shell before=u;
   assert(xz_shell_render_frame(&u,&m,frame(buffer_a),STRIDE));
   int closing=screen!=XZ_SHELL_PLAY||popup;
   size_t n=touch(&u,&m,XZ_SHELL_TOUCH_DOWN,hits[i].x,hits[i].y,out);
   if(closing){
    assert(!n);
    if(hits[i].kind==XZ_SHELL_ACT_OPEN_MODS){
     if(popup)assert(u.popup.kind==XZ_SHELL_POPUP_NONE&&u.depth==before.depth);
     else assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
    }else assert(u.depth==before.depth&&u.popup.kind==before.popup.kind);
   }else assert(n==1&&out[0].kind==hits[i].kind&&out[0].deck==-1);
   assert(touch(&u,&m,XZ_SHELL_TOUCH_MOVE,200,300,out)==0);
   assert(touch(&u,&m,XZ_SHELL_TOUCH_UP,200,300,out)==0);
   assert(!u.down);u=before;
  }
  /* Just outside the buttons the screen gets the touch. */
  assert(sh_header_hit(XZ_SHELL_STEMS_X-1,10)==XZ_SHELL_ACT_NONE&&sh_header_hit(XZ_SHELL_MODS_X,XZ_SHELL_HEADER_HIT_H)==XZ_SHELL_ACT_NONE);
  /* Both are drawn on top, in the stock place and size; STEMS lights from the model. */
  m.stems_known=1;m.stems_on=screen&1;
  render_checked(&u,&m,buffer_a);
  assert(at(frame(buffer_a),XZ_SHELL_STEMS_X+2,2)==rgb565(screen!=XZ_SHELL_PLAY||popup?SH_CHROME:m.stems_on?SH_BLUE:XZ_FLAGSHIP_TILE));
  assert(at(frame(buffer_a),XZ_SHELL_MODS_X+2,2)==rgb565(XZ_FLAGSHIP_TILE));
  assert(at(frame(buffer_a),XZ_SHELL_MODS_X+2,XZ_SHELL_HEADER_BUTTON_H-1)==rgb565(SH_BLACK));
 }
 /* An unknown stems state never lights the button. */
 struct xz_shell u;xz_shell_init(&u);m.stems_known=0;m.stems_on=1;
 render_checked(&u,&m,buffer_a);assert(at(frame(buffer_a),XZ_SHELL_STEMS_X+2,2)==rgb565(XZ_FLAGSHIP_TILE));
 /* On PLAY, the header controls left of the buttons are padded the same way; deck 1's tag keeps its touch. */
 struct xz_shell_rect info=xz_shell_play_rect(&m,XZ_SHELL_R_INFO_BUTTON,0);
 assert(info.x+info.w<=XZ_SHELL_HEADER_X&&info.w>=44);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,info.x+info.w/2,XZ_SHELL_HEADER_HIT_H-2,out)==0&&xz_shell_screen(&u)==XZ_SHELL_INFO);
 touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 xz_shell_init(&u);u.deck=1;
 struct xz_shell_rect tag=xz_shell_play_rect(&m,XZ_SHELL_R_DECK_TAG,0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,tag.x+20,tag.y+4,out)==1&&out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==0);
 puts("PASS header MODS and STEMS: every screen and popup, 44 px band, drawn on top");
}
/* Every pixel of the play frame is drawn each time: two different prefills give one image. */
static void test_full_coverage(void) {
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);fx_model_full(&m,&track);
 fill(buffer_a,0x0000);assert(xz_shell_render(&u,&m,frame(buffer_a),STRIDE));check_guards(buffer_a);
 fill(buffer_b,0xffff);assert(xz_shell_render(&u,&m,frame(buffer_b),STRIDE));check_guards(buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 puts("PASS play frame covers every pixel");
}
static int fx_strip_count(const uint16_t *f, uint32_t color) {
 struct xz_shell_rect r=xz_shell_play_rect(NULL,XZ_SHELL_R_FX,0);
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=at(f,x,y)==rgb565(color);
 return n;
}
static void test_fx_readout(void) {
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);fx_model_full(&m,&track);
 struct xz_shell_rect r=xz_shell_play_rect(&m,XZ_SHELL_R_FX,0),info=xz_shell_play_rect(&m,XZ_SHELL_R_INFO_BUTTON,0);
 assert(r.w>=240&&r.y>=0&&r.y+r.h<=xz_shell_play_rect(&m,XZ_SHELL_R_TOP_BAR,0).h&&r.x+r.w<info.x);
 /* Nothing read: a quiet strip, no red. */
 render_checked(&u,&m,buffer_a);
 assert(fx_strip_count(frame(buffer_a),SH_RED)==0);
 /* A known name bit with a NULL name, and garbage in every field whose bit is clear. */
 m.fx.known=1u<<XZ_SHELL_FX_F_NAME;m.fx.name=NULL;m.fx.on=1;m.fx.beat_num=-9;m.fx.time_ms=-1;m.fx.channel=(enum xz_shell_fx_channel)77;
 render_checked(&u,&m,buffer_b);
 assert(fx_strip_count(frame(buffer_b),SH_RED)==0);
 m.fx.known=(1u<<XZ_SHELL_FX_FIELD_COUNT)-1;m.fx.name="";m.fx.bpm_x10=-5;m.fx.level_pct=900;
 render_checked(&u,&m,buffer_b);
 /* ON fills the strip with the FX red; OFF does not. */
 fx_beat_fx(&m.fx,1,"ECHO",1,2,234);
 render_checked(&u,&m,buffer_a);
 assert(fx_strip_count(frame(buffer_a),SH_RED)>r.w*r.h/2);
 fx_beat_fx(&m.fx,0,"ECHO",1,2,234);
 render_checked(&u,&m,buffer_b);
 assert(fx_strip_count(frame(buffer_b),SH_RED)==0&&memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* An overlong name stays inside the strip; a beatless effect draws too. */
 static char wide[300];memset(wide,'W',sizeof wide-1);
 fx_beat_fx(&m.fx,1,wide,64,1,99999999);m.fx.tap=1;
 render_checked(&u,&m,buffer_a);
 for(int y=r.y;y<r.y+r.h;y++)assert(at(frame(buffer_a),r.x-1,y)==rgb565(SH_CHROME)&&at(frame(buffer_a),r.x+r.w,y)==rgb565(SH_CHROME));
 fx_beat_fx(&m.fx,1,"REVERB",0,0,-1);render_checked(&u,&m,buffer_a);
 puts("PASS FX readout: unknown blanks, NULL name, loud ON, bounds");
}
static void test_extrapolation(void) {
 struct xz_shell_model m;fx_model_full(&m,&track);
 struct xz_shell_deck *d=&m.deck[0];
 d->elapsed_ms=60000;d->tempo_x100=0;
 m.clock_ms=10000;d->elapsed_at_ms=10000;
 assert(xz_shell_elapsed_now(&m,0)==60000);
 /* Paused: no movement however long ago it was read. */
 d->playing=0;m.clock_ms=10400;
 assert(xz_shell_elapsed_now(&m,0)==60000);
 /* Playing at 0 %: 1 ms per ms; at +10 %: 1.1 ms per ms; at -50 %: 0.5. */
 d->playing=1;
 assert(xz_shell_elapsed_now(&m,0)==60400);
 d->tempo_x100=1000;assert(xz_shell_elapsed_now(&m,0)==60440);
 m.clock_ms=10100;assert(xz_shell_elapsed_now(&m,0)==60110);
 d->tempo_x100=-5000;assert(xz_shell_elapsed_now(&m,0)==60050);
 /* Tempo unknown counts as 0 %; PLAYING unknown, no stamp, or a clock before the stamp: no movement. */
 d->tempo_x100=1000;d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO);assert(xz_shell_elapsed_now(&m,0)==60100);
 d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO);
 d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYING);assert(xz_shell_elapsed_now(&m,0)==60000);
 d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYING);
 d->elapsed_at_ms=0;assert(xz_shell_elapsed_now(&m,0)==60000);
 d->elapsed_at_ms=10200;assert(xz_shell_elapsed_now(&m,0)==60000);
 /* A stale reading stops after the cap instead of running on. */
 d->elapsed_at_ms=10000;d->tempo_x100=0;m.clock_ms=10000+5000;
 assert(xz_shell_elapsed_now(&m,0)==60000+XZ_SHELL_EXTRAPOLATE_MAX_MS);
 /* The clock wrapping past 2^32 still advances. */
 d->elapsed_at_ms=0xfffffff0u;m.clock_ms=0x10;assert(xz_shell_elapsed_now(&m,0)==60032);
 /* Clamped to the track, and wrapped inside an active loop. */
 d->elapsed_at_ms=10000;m.clock_ms=10300;d->elapsed_ms=FX_TOTAL_MS-100;assert(xz_shell_elapsed_now(&m,0)==FX_TOTAL_MS);
 d->elapsed_ms=1900;d->loop=(struct xz_shell_loop){1,1,1,1000,2000};assert(xz_shell_elapsed_now(&m,0)==1200);
 d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED);assert(xz_shell_elapsed_now(&m,0)==-1);
 assert(xz_shell_elapsed_now(&m,2)==-1&&xz_shell_elapsed_now(NULL,0)==-1);
 /* The render follows the clock while the model stays put; paused, it does not move. */
 struct xz_shell u;xz_shell_init(&u);
 fx_model_full(&m,&track);m.deck[0].elapsed_at_ms=m.clock_ms;
 render_checked(&u,&m,buffer_a);
 m.clock_ms+=17;render_checked(&u,&m,buffer_b);
 assert(memcmp(buffer_a,buffer_b,sizeof buffer_a));
 m.deck[0].playing=0;render_checked(&u,&m,buffer_b);
 m.clock_ms-=17;render_checked(&u,&m,buffer_a);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 puts("PASS playhead extrapolation: paused holds, tempo scales, stale caps, loops wrap");
}
/* Moving n track pixels moves the lane picture by exactly n pixels: no column shimmer. */
static void test_scroll_is_stable(void) {
 static const int pps[XZ_SHELL_ZOOM_MAX+1]={32,48,64,96,128,192,256};
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);fx_model_full(&m,&track);
 m.deck[0].known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|XZ_SHELL_KNOWN(XZ_SHELL_F_HOT_CUES)|XZ_SHELL_KNOWN(XZ_SHELL_F_MEMORY_CUES)|
  XZ_SHELL_KNOWN(XZ_SHELL_F_CUE_POINT)|XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP));
 struct xz_shell_rect lane=xz_shell_play_rect(&m,XZ_SHELL_R_LANE,0);
 for(int color=0;color<4;color++)for(int zoom=0;zoom<=XZ_SHELL_ZOOM_MAX;zoom++)for(int step=1;step<=9;step+=4){
  u.zoom=zoom;u.display.wave_color=(enum xz_shell_wave_color)color;
  int32_t from=90000+zoom*7;
  m.deck[0].elapsed_ms=from;render_checked(&u,&m,buffer_a);
  int32_t to=from+(step*1000+pps[zoom]/3)/pps[zoom];
  /* Without a grid, the known original 128 BPM fixes scale. */
  int shift=(int)(((int64_t)to*pps[zoom]*FX_BPM_X100+6000000)/12000000-
    ((int64_t)from*pps[zoom]*FX_BPM_X100+6000000)/12000000);
  m.deck[0].elapsed_ms=to;render_checked(&u,&m,buffer_b);
  int ph=-1;
  for(int x=lane.x;x<lane.x+lane.w;x++)if(at(frame(buffer_a),x,lane.y+1)==rgb565(SH_PLAYHEAD)){ph=x;break;}
  assert(ph>0&&shift>=0);
  for(int y=lane.y;y<lane.y+lane.h;y++)for(int x=lane.x+4+shift;x<lane.x+lane.w-4;x++){
   if(x==ph||x-shift==ph)continue;
   assert(at(frame(buffer_b),x-shift,y)==at(frame(buffer_a),x,y));
  }
 }
 puts("PASS lane scroll is whole-pixel stable at every zoom and colour");
}
static void test_key_display(void) {
 char b[8];
 static const struct { const char *in; enum xz_shell_key_display mode; const char *out; } cases[]={
  {"Am",XZ_SHELL_KEY_ALPHANUMERIC,"8A"},{"C",XZ_SHELL_KEY_ALPHANUMERIC,"8B"},{"Abm",XZ_SHELL_KEY_ALPHANUMERIC,"1A"},
  {"G#m",XZ_SHELL_KEY_ALPHANUMERIC,"1A"},{"B",XZ_SHELL_KEY_ALPHANUMERIC,"1B"},{"F#m",XZ_SHELL_KEY_ALPHANUMERIC,"11A"},
  {"E",XZ_SHELL_KEY_ALPHANUMERIC,"12B"},{"Fm",XZ_SHELL_KEY_ALPHANUMERIC,"4A"},
  {"9A",XZ_SHELL_KEY_CLASSIC,"Em"},{"12A",XZ_SHELL_KEY_CLASSIC,"Dbm"},{"10b",XZ_SHELL_KEY_CLASSIC,"D"},{"1B",XZ_SHELL_KEY_CLASSIC,"B"},
  {"9A",XZ_SHELL_KEY_ALPHANUMERIC,"9A"},{"Em",XZ_SHELL_KEY_CLASSIC,"Em"},{"F\xe2\x99\xafm",XZ_SHELL_KEY_ALPHANUMERIC,"11A"},
  {"Abmaj7/Eb",XZ_SHELL_KEY_ALPHANUMERIC,"Abmaj7/Eb"},{"13A",XZ_SHELL_KEY_CLASSIC,"13A"},{"",XZ_SHELL_KEY_CLASSIC,""},
  {"Am",XZ_SHELL_KEY_AS_DECK,"Am"}};
 for(size_t i=0;i<sizeof cases/sizeof *cases;i++)assert(!strcmp(sh_key_text(cases[i].in,cases[i].mode,b,sizeof b),cases[i].out));
 assert(sh_key_text(NULL,XZ_SHELL_KEY_CLASSIC,b,sizeof b)==NULL);
 /* Every alphanumeric key survives a round trip through classic. */
 for(int n=1;n<=12;n++)for(int l=0;l<2;l++){
  char key[4],c[8],a[8];snprintf(key,sizeof key,"%d%c",n,l?'B':'A');
  const char *classic=sh_key_text(key,XZ_SHELL_KEY_CLASSIC,c,sizeof c);
  assert(!strcmp(sh_key_text(classic,XZ_SHELL_KEY_ALPHANUMERIC,a,sizeof a),key));
 }
 puts("PASS key notation converts both ways and leaves other text alone");
}
/* SHORTCUT over the runtime's real table: display items present and settable, read-only items hand off. */
static void test_shortcut_items(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 static const char *const time_c[]={"REMAIN","TIME"},*const q_c[]={"1/8","1/4","1/2","1"},*const off_on[]={"OFF","ON"};
 static struct xz_shell_setting_item items[3];
 items[0]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"TIME MODE",time_c,2,0,1,1};
 items[1]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_SETTING_CAT_DJ_SETTING,"QUANTIZE BEAT VALUE",q_c,4,3,1,1};
 items[2]=(struct xz_shell_setting_item){XZ_SHELL_ITEM_AUTO_CUE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"AUTO CUE",off_on,2,1,1,0};
 static struct xz_shell_settings_model table={3,items,NULL};
 fx_model_full(&m,&track);m.settings_screen=&table;
 m.settings.writable=((uint64_t)1<<XZ_SHELL_SET_TIME_MODE)|((uint64_t)1<<XZ_SHELL_ITEM_QUANTIZE_BEAT);
 xz_shell_init(&u);
 assert(press(&u,&m,XZ_SHELL_KEY_SHORTCUT,out)==0&&xz_shell_screen(&u)==XZ_SHELL_SHORTCUT);
 /* Reading order: player settings, the four display items, then my settings. */
 static const int order[]={XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_ITEM_BEAT_JUMP_BEAT,XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD,
  XZ_SHELL_ITEM_WAVE_COLOR,XZ_SHELL_ITEM_WAVE_POSITION,XZ_SHELL_ITEM_KEY_DISPLAY,XZ_SHELL_ITEM_WAVE_ZOOM,
  XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_ITEM_PLAY_MODE,XZ_SHELL_ITEM_AUTO_CUE};
 for(size_t i=0;i<sizeof order/sizeof *order;i++){
  render_checked(&u,&m,buffer_a);
  assert(u.stack[1].cursor-1==order[i]||(i==0&&u.stack[1].cursor==0));
  struct xz_shell_event push={XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1};
  struct xz_shell before=u;
  size_t n=xz_shell_input(&u,&m,&push,out);
  switch(order[i]){
  case XZ_SHELL_ITEM_QUANTIZE_BEAT:assert(n==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==order[i]&&out[0].value==0);break;
  case XZ_SHELL_ITEM_TIME_MODE:assert(n==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].value==XZ_SHELL_TIME_ELAPSED);break;
  case XZ_SHELL_ITEM_WAVE_COLOR:   /* a local pick also stops the waveform following the sticks' colours */
   assert(n==2&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==order[i]&&out[0].value==XZ_SHELL_WAVE_STEMS&&u.display.wave_color==XZ_SHELL_WAVE_STEMS);
   assert(out[1].id==XZ_SHELL_ITEM_USB_COLORS&&out[1].value==XZ_SHELL_USB_COLORS_TILE);break;
  case XZ_SHELL_ITEM_WAVE_POSITION:assert(n==1&&out[0].id==order[i]&&u.display.wave_position==XZ_SHELL_POSITION_LEFT);break;
  case XZ_SHELL_ITEM_KEY_DISPLAY:assert(n==1&&out[0].id==order[i]&&u.display.key_display==XZ_SHELL_KEY_CLASSIC);break;
  case XZ_SHELL_ITEM_WAVE_ZOOM:assert(n==1&&out[0].kind==XZ_SHELL_ACT_ZOOM&&u.zoom==XZ_SHELL_ZOOM_DEFAULT+1);break;
  default:  /* read only: the stock screen takes over and the shell changes nothing */
   assert(n==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_SHORTCUT);
   assert(!memcmp(&before.display,&u.display,sizeof u.display)&&before.zoom==u.zoom);
  }
  struct xz_shell_event next={XZ_SHELL_ENCODER_TURN,0,0,1,0,-1};
  xz_shell_input(&u,&m,&next,out);
 }
 /* Without the capability the runtime items go read only; the display items stay live. */
 struct xz_shell_model no_set=m;no_set.capabilities&=~(uint32_t)XZ_SHELL_CAP_SET_SETTING;
 xz_shell_init(&u);press(&u,&no_set,XZ_SHELL_KEY_SHORTCUT,out);
 struct xz_shell_event push={XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1},three={XZ_SHELL_ENCODER_TURN,0,0,3,0,-1};
 assert(xz_shell_input(&u,&no_set,&push,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF);
 xz_shell_input(&u,&no_set,&three,out);
 assert(xz_shell_input(&u,&no_set,&push,out)==2&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_COLOR);
 /* The display settings drive the play screen: BLUE paints no amber in the lanes. */
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_BLUE;
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect lane=xz_shell_play_rect(&m,XZ_SHELL_R_LANE,0);
 int amber=0,lit=0;
 for(int y=lane.y;y<lane.y+lane.h;y++)for(int x=lane.x+4;x<lane.x+lane.w;x++){amber+=at(frame(buffer_a),x,y)==rgb565(SH_WAVE_MID);lit+=at(frame(buffer_a),x,y)!=0;}
 assert(amber==0&&lit>1000);
 /* Restoring persisted values: only shell-owned ids, only in range. */
 assert(xz_shell_restore_setting(&u,XZ_SHELL_SET_WAVE_COLOR,XZ_SHELL_WAVE_RGB)==1&&u.display.wave_color==XZ_SHELL_WAVE_RGB);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_SET_WAVE_COLOR,4)==0&&u.display.wave_color==XZ_SHELL_WAVE_RGB);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_SET_WAVE_COLOR,XZ_SHELL_WAVE_STEMS)==1&&u.display.wave_color==XZ_SHELL_WAVE_STEMS);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_KEY_DISPLAY,XZ_SHELL_KEY_ALPHANUMERIC)==1&&u.display.key_display==XZ_SHELL_KEY_ALPHANUMERIC);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_WAVE_ZOOM,XZ_SHELL_ZOOM_MAX)==1&&u.zoom==XZ_SHELL_ZOOM_MAX);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_WAVE_ZOOM,-1)==0&&xz_shell_restore_setting(&u,XZ_SHELL_SET_TIME_MODE,0)==0);
 assert(xz_shell_restore_setting(NULL,XZ_SHELL_SET_WAVE_COLOR,0)==0);
 /* A corrupt display value draws the defaults instead of reading past a table. */
 u.display.wave_color=(enum xz_shell_wave_color)99;u.display.key_display=(enum xz_shell_key_display)-3;
 u.display.wave_position=(enum xz_shell_wave_position)7;render_checked(&u,&m,buffer_a);
 puts("PASS SHORTCUT items: display settings shell-owned and live, read-only items hand off");
}

/* Runtime notices: shown for XZ_SHELL_NOTICE_MS from notice_at_ms, on every screen, inside their box,
   never over the header buttons. */
static int differs_outside(const uint16_t *a, const uint16_t *b, struct xz_shell_rect r) {
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++){
  if(x>=r.x-1&&x<r.x+r.w+1&&y>=r.y-1&&y<r.y+r.h+1)continue;
  if(at(a,x,y)!=at(b,x,y))return 1;
 }
 return 0;
}
static void test_notice(void) {
 struct xz_shell u;struct xz_shell_model m,plain;
 fx_model_full(&m,&track);enable_all_screens(&m);
 m.clock_ms=50000;m.notice="DECK 1 DID NOT LOAD THE TRACK";m.notice_at_ms=50000;
 assert(sh_notice_shown(&m));
 m.clock_ms=50000+XZ_SHELL_NOTICE_MS-1;assert(sh_notice_shown(&m));
 m.clock_ms=50000+XZ_SHELL_NOTICE_MS;assert(!sh_notice_shown(&m));
 m.clock_ms=49999;assert(!sh_notice_shown(&m));                 /* raised after this frame's clock */
 m.notice_at_ms=0xffffff00u;m.clock_ms=0x100;assert(sh_notice_shown(&m));   /* across the clock wrap */
 m.notice=NULL;assert(!sh_notice_shown(&m));
 m.notice="";assert(!sh_notice_shown(&m));
 /* Drawn over every screen, inside its box, below the header buttons. */
 static char wide[400];memset(wide,'W',sizeof wide-1);
 static char words[400];
 for(int i=0;i<(int)sizeof words-1;i++)words[i]=i%9==8?' ':'A'+i%7;words[sizeof words-1]=0;
 const char *texts[]={"DECK 1 DID NOT LOAD THE TRACK","SELECT A TRACK, THEN PRESS LOAD",
  "THE TRACK ON USB 2 COULD NOT BE READ BECAUSE THE DEVICE WAS REMOVED WHILE IT WAS LOADING",wide,words,"OK",
  "\xc3\x89T\xc3\x89 \xff\xfe BAD BYTES"};
 for(size_t t=0;t<sizeof texts/sizeof *texts;t++){
  int lines;struct xz_shell_rect r=sh_notice_rect(texts[t],&lines);
  assert(r.x>=0&&r.x+r.w<=XZ_SHELL_WIDTH&&r.y>=XZ_SHELL_HEADER_HIT_H&&r.y+r.h<=XZ_SHELL_HEIGHT&&r.w>=200&&(lines==1||lines==2));
  for(int screen=0;screen<XZ_SHELL_SCREEN_COUNT;screen++){
   xz_shell_init(&u);
   if(screen!=XZ_SHELL_PLAY){u.depth=2;u.stack[1]=(struct xz_shell_view){(enum xz_shell_screen)screen,0,0,0};}
   m.notice=texts[t];m.notice_at_ms=m.clock_ms=7000;
   plain=m;plain.notice=NULL;
   render_checked(&u,&plain,buffer_b);
   render_checked(&u,&m,buffer_a);
   assert(!differs_outside(frame(buffer_a),frame(buffer_b),r));
   assert(memcmp(buffer_a,buffer_b,sizeof buffer_a));
   /* Text stays inside the box: its edge columns are border, never ink. */
   for(int y=r.y+3;y<r.y+r.h-1;y++)for(int x=r.x+1;x<r.x+12;x++)assert(at(frame(buffer_a),x,y)==rgb565(SH_ROW_ALT));
   for(int y=r.y+3;y<r.y+r.h-1;y++)for(int x=r.x+r.w-12;x<r.x+r.w-1;x++)assert(at(frame(buffer_a),x,y)==rgb565(SH_ROW_ALT));
   m.clock_ms=7000+XZ_SHELL_NOTICE_MS;plain.clock_ms=m.clock_ms;
   render_checked(&u,&m,buffer_a);render_checked(&u,&plain,buffer_b);
   assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
  }
 }
 /* The two runtime sentences each read on one line. */
 int lines;sh_notice_rect("DECK 1 DID NOT LOAD THE TRACK",&lines);assert(lines==1);
 sh_notice_rect("SELECT A TRACK, THEN PRESS LOAD",&lines);assert(lines==1);
 sh_notice_rect(texts[2],&lines);assert(lines==2);
 puts("PASS notice toast: timing, wrap, every screen, inside its box, under the header");
}
/* Held MENU opens MODS > UTILITY and remains there on another hold; a short
   MENU on PLAY does nothing. The runtime's key probe reads exactly this: an effect without HAND_OFF
   means SWALLOW, so a held MENU never reaches the stock menu while the shell draws. */
static void test_menu_hold(void) {
 struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_full(&m,&track);enable_all_screens(&m);
 for(int screen=0;screen<XZ_SHELL_SCREEN_COUNT;screen++){
  struct xz_shell u;xz_shell_init(&u);
  if(screen!=XZ_SHELL_PLAY){u.depth=2;u.stack[1]=(struct xz_shell_view){(enum xz_shell_screen)screen,0,0,0};}
  struct xz_shell before=u;
  size_t n=press(&u,&m,XZ_SHELL_KEY_MENU_HOLD,out);
  for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_HAND_OFF);
  if(screen==XZ_SHELL_UTILITY){
   assert(n==0&&memcmp(&u,&before,sizeof u)&&u.depth==2&&u.mods_view.tab==0&&u.mods_view.on_tabs);
   struct xz_shell at_row=u;
   assert(press(&u,&m,XZ_SHELL_KEY_MENU_HOLD,out)==0&&!memcmp(&u,&at_row,sizeof u)&&u.depth==2);
  }
  else assert(xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.depth==before.depth+1&&u.mods_view.tab==0&&u.mods_view.on_tabs);
  render_checked(&u,&m,buffer_a);
 }
 /* Short MENU on PLAY opens MODS (UTILITY here: this model has no mod tabs); short MENU there closes it. */
 struct xz_shell u;xz_shell_init(&u);
 assert(press(&u,&m,XZ_SHELL_KEY_MENU,out)==0&&xz_shell_screen(&u)==XZ_SHELL_UTILITY&&u.depth==2&&u.mods_view.tab==0&&u.mods_view.on_tabs);
 assert(press(&u,&m,XZ_SHELL_KEY_MENU,out)==0&&u.depth==1);
 /* Only without the settings slot does held MENU go to the stock UTILITY. */
 m.settings_screen=NULL;
 assert(press(&u,&m,XZ_SHELL_KEY_MENU_HOLD,out)==1&&out[0].kind==XZ_SHELL_ACT_HAND_OFF&&out[0].id==XZ_SHELL_UTILITY);
 puts("PASS held MENU opens MODS > UTILITY on every screen; short MENU on PLAY opens MODS");
}

/* STEMS: the stem layer in the stem pad colours, muted stems grey, 3BAND while a deck has no layer. */
static int lane_count(const uint16_t *f, int deck, uint32_t color) {
 struct xz_shell_rect lane=xz_shell_play_rect(NULL,XZ_SHELL_R_LANE,deck);
 int n=0;
 for(int y=lane.y;y<lane.y+lane.h;y++)for(int x=lane.x+4;x<lane.x+lane.w;x++)n+=at(f,x,y)==rgb565(color);
 return n;
}
static void test_stem_wave(void) {
 struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);fx_model_full(&m,&track);
 for(int d=0;d<2;d++)m.deck[d].known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP)|XZ_SHELL_KNOWN(XZ_SHELL_F_HOT_CUES));
 /* Deck 1 sits in a drop (drums, harmonics), deck 2 in the break, where the vocal sings. */
 m.deck[1].elapsed_ms=FX_BEAT_MS*4*8*3/100+FX_BEAT_MS/100;
 u.display.wave_color=XZ_SHELL_WAVE_STEMS;
 render_checked(&u,&m,buffer_a);
 const uint16_t *f=frame(buffer_a);
 /* Two tones per layer, as the stem buttons: a lighter interior fills, the bright colour outlines. */
 const uint32_t drums=sh_stem_bright(&m,0),harmonics=sh_stem_bright(&m,1),vocal=sh_stem_bright(&m,2);
 assert(drums==0x4b4ace&&harmonics==0xac1f12&&vocal==0x15b634);
 assert(lane_count(f,0,sh_stem_wave_fill(drums))>200&&lane_count(f,0,drums)>40);
 assert(lane_count(f,0,sh_stem_wave_fill(harmonics))>200&&lane_count(f,0,harmonics)>40);
 assert(lane_count(f,1,sh_stem_wave_fill(vocal))>100&&lane_count(f,1,vocal)>20);
 assert(lane_count(f,0,SH_WAVE_MID)==0&&lane_count(f,1,SH_WAVE_MID)==0);     /* not 3BAND */
 /* Colours the DJ picked win over the defaults; the runtime's other colours do not. */
 m.stem_rgb_known=1;m.stem_rgb[0]=0xffff00;m.stem_rgb[1]=0x00ffff;m.stem_rgb[2]=0xff00ff;
 render_checked(&u,&m,buffer_a);
 assert(lane_count(f,0,sh_stem_wave_fill(drums))>200&&lane_count(f,0,0xffff00)==0);
 m.stem_rgb_custom=5;
 render_checked(&u,&m,buffer_a);
 assert(lane_count(f,0,0xffff00)>40&&lane_count(f,0,sh_stem_wave_fill(0xffff00))>200&&lane_count(f,0,sh_stem_wave_fill(drums))==0);
 assert(lane_count(f,0,sh_stem_wave_fill(harmonics))>200&&lane_count(f,1,0xff00ff)>20);
 m.stem_rgb_known=0;m.stem_rgb_custom=0;
 /* Muted stems grey out; the mute bits mean nothing without their known bit. */
 m.deck[0].stem_muted=1;m.deck[1].stem_muted=4;
 struct xz_shell_model unknown_mute=m;
 for(int d=0;d<2;d++)unknown_mute.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE);
 render_checked(&u,&unknown_mute,buffer_b);
 assert(lane_count(frame(buffer_b),0,sh_stem_wave_fill(drums))>200);
 render_checked(&u,&m,buffer_a);
 /* A muted layer is a dim grey with no outline. */
 assert(lane_count(f,0,sh_stem_wave_fill(drums))==0&&lane_count(f,0,drums)==0&&lane_count(f,0,0x2a2a2a)>200);
 assert(lane_count(f,0,sh_stem_wave_fill(harmonics))>200&&lane_count(f,1,vocal)==0&&lane_count(f,1,sh_stem_wave_fill(vocal))==0);
 m.deck[0].stem_muted=m.deck[1].stem_muted=0;
 /* No stem layer: that deck draws 3BAND and says so; the other keeps its stems. */
 render_checked(&u,&m,buffer_a);
 int dim_with=lane_count(f,1,SH_DIM);
 struct xz_shell_model half=m;half.deck[1].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);
 render_checked(&u,&half,buffer_a);
 assert(lane_count(f,1,SH_WAVE_MID)>200&&lane_count(f,1,SH_DIM)>dim_with+20&&lane_count(f,0,sh_stem_wave_fill(drums))>200);
 half.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);half.deck[1].stems.columns=NULL;
 render_checked(&u,&half,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* With neither layer the lane is empty but the frame still draws. */
 half.deck[1].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE);render_checked(&u,&half,buffer_b);
 assert(lane_count(frame(buffer_b),1,SH_WAVE_MID)==0);
 /* Overviews follow the mode: stem colours in the strip, played part grey. */
 struct xz_shell_rect ov=xz_shell_play_rect(&m,XZ_SHELL_R_OVERVIEW,0);
 render_checked(&u,&m,buffer_a);
 int stem=0,amber=0;
 for(int y=ov.y;y<ov.y+ov.h;y++)for(int x=ov.x;x<ov.x+ov.w;x++){
  uint16_t p=at(f,x,y);stem+=p==rgb565(sh_stem_wave_fill(drums))||p==rgb565(drums)||p==rgb565(sh_stem_wave_fill(harmonics))||p==rgb565(harmonics);amber+=p==rgb565(SH_WAVE_MID);
 }
 assert(stem>200&&amber==0);
 /* Wild stem data is tolerated: huge counts with a short span, all-zero columns, a one-column track. */
 static struct xz_shell_column silent[3],loud[1]={{255,255,255}};
 m.deck[0].stems=(struct xz_shell_wave){silent,3,FX_TOTAL_MS};m.deck[1].stems=(struct xz_shell_wave){loud,1,1};
 render_checked(&u,&m,buffer_a);
 m.deck[0].stems=(struct xz_shell_wave){track.stems,FX_LANE_COLUMNS,1};
 for(int zoom=0;zoom<=XZ_SHELL_ZOOM_MAX;zoom++){u.zoom=zoom;render_checked(&u,&m,buffer_a);}
 puts("PASS STEMS: stem colours, runtime colours, muted grey, 3BAND fallback with a hint, overview, wild data");
}
/* Pixels that differ between two frames, and their bounding box. */
static int diff_box(const uint16_t *a, const uint16_t *b, struct xz_shell_rect *box) {
 int n=0,x0=XZ_SHELL_WIDTH,y0=XZ_SHELL_HEIGHT,x1=-1,y1=-1;
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++)if(at(a,x,y)!=at(b,x,y)){
  n++;x0=x<x0?x:x0;y0=y<y0?y:y0;x1=x>x1?x:x1;y1=y>y1?y:y1;
 }
 *box=n?(struct xz_shell_rect){x0,y0,x1-x0+1,y1-y0+1}:(struct xz_shell_rect){0,0,0,0};
 return n;
}
static int box_inside(struct xz_shell_rect in, struct xz_shell_rect out) {
 return in.x>=out.x&&in.y>=out.y&&in.x+in.w<=out.x+out.w&&in.y+in.h<=out.y+out.h;
}
static int count_in(const uint16_t *f, struct xz_shell_rect r, uint32_t color) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=at(f,x,y)==rgb565(color);
 return n;
}
static int ink_in(const uint16_t *f, struct xz_shell_rect r) { return r.w*r.h-count_in(f,r,SH_CHROME); }
/* The column's key block: everything under the source row. */
static struct xz_shell_rect key_block(int deck) {
 struct xz_shell_rect col=xz_shell_play_rect(NULL,XZ_SHELL_R_INFO_COLUMN,deck);
 return (struct xz_shell_rect){col.x,col.y+47,col.w,col.h-47};
}
/* The XZ has a pad bank for beat jumps and its QUANTIZE lamp in each deck strip: the column draws
   neither. The deck's live key takes their place, with cents, the analysed key and KEY SYNC. */
static void test_live_key(void) {
 struct xz_shell u;struct xz_shell_model m,other;
 xz_shell_init(&u);fx_model_full(&m,&track);
 /* Beat jump draws nowhere; quantize only in its strip, on the caption row over the time. */
 render_checked(&u,&m,buffer_a);
 other=m;for(int d=0;d<2;d++){other.deck[d].beat_jump_num=d?1:64;other.deck[d].beat_jump_den=d?2:1;}
 render_checked(&u,&other,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 struct xz_shell_rect box;
 for(int d=0;d<2;d++){
  other=m;other.deck[d].quantize_on=!m.deck[d].quantize_on;other.deck[d].quantize_den=8;
  render_checked(&u,&other,buffer_b);
  struct xz_shell_rect s=xz_shell_play_rect(&m,XZ_SHELL_R_STRIP,d),row={s.x,s.y+30,s.w,16};
  assert(diff_box(frame(buffer_a),frame(buffer_b),&box)>20&&box_inside(box,row));
  assert(box.x>xz_shell_play_rect(&m,XZ_SHELL_R_TIME,d).x+60);   /* right of REMAIN / TIME */
 }
 /* Deck 2 plays Bbm (KEY SYNC, from Am, +12 cents) against the master's Fm: amber, a wheel step away. */
 struct xz_shell_rect k1=key_block(0),k2=key_block(1);
 struct xz_shell_rect name2={k2.x,k2.y+22,60,20},name1={k1.x,k1.y+22,60,20};
 assert(count_in(frame(buffer_a),name2,SH_KEY_AMBER)>40&&count_in(frame(buffer_a),name1,SH_WHITE)>40);
 /* The KEY SYNC lamp replaces the KEY caption: blue on deck 2, none on deck 1. */
 struct xz_shell_rect cap2={k2.x,k2.y,k2.w,19},cap1={k1.x,k1.y,k1.w,19};
 assert(count_in(frame(buffer_a),cap2,SH_BLUE)>40&&count_in(frame(buffer_a),cap1,SH_BLUE)==0);
 /* The analysed key shows under the playing key only once they differ. */
 struct xz_shell_rect from2={k2.x,k2.y+50,k2.w,14},from1={k1.x,k1.y+50,k1.w,14};
 assert(count_in(frame(buffer_a),from2,SH_CHROME)<from2.w*from2.h-20&&count_in(frame(buffer_a),from1,SH_CHROME)==from1.w*from1.h);
 /* Every key change stays in the key block (KEY SYNC also blinks MASTER TEMPO, checked below). */
 other=m;other.deck[1].key="F#m";other.deck[1].key_cents=-37;other.deck[1].key_original="Abmaj7/Eb";
 render_checked(&u,&other,buffer_b);
 assert(diff_box(frame(buffer_a),frame(buffer_b),&box)>0&&box_inside(box,k2));
 /* Cents: the tail right of the name, gone on the semitone. */
 struct xz_shell_rect tail={k2.x+62,k2.y+20,k2.w-62,14};
 other=m;other.deck[1].key_cents=0;render_checked(&u,&other,buffer_b);
 assert(ink_in(frame(buffer_a),tail)>10&&ink_in(frame(buffer_b),tail)==0);
 /* A name the key face cannot draw, wild cents and a NULL analysed key stay in the column. */
 other=m;other.deck[1].key="Abmaj7/Eb";other.deck[1].key_cents=-999;other.deck[1].key_original=NULL;other.deck[0].key="";
 render_checked(&u,&other,buffer_b);
 for(int d=0;d<2;d++){struct xz_shell_rect k=key_block(d),edge={k.x+k.w-5,k.y,5,k.h};assert(count_in(frame(buffer_b),edge,SH_CHROME)==edge.w*edge.h);}
 /* An older runtime: no analysed key, no KEY SYNC. The key draws as before, with its key-shift step and
    no cents; nothing reads key_original, key_cents or key_synced, and MASTER TEMPO is steady. */
 struct xz_shell_model old=m;
 for(int d=0;d<2;d++)old.deck[d].known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_KEY_ORIGINAL)|XZ_SHELL_KNOWN(XZ_SHELL_F_KEY_SYNC));
 render_checked(&u,&old,buffer_b);
 assert(count_in(frame(buffer_b),name2,SH_KEY_AMBER)>40&&count_in(frame(buffer_b),cap2,SH_BLUE)==0);
 assert(count_in(frame(buffer_b),from2,SH_CHROME)==from2.w*from2.h&&ink_in(frame(buffer_b),tail)>10);  /* the step, not cents */
 other=old;for(int d=0;d<2;d++){other.deck[d].key_original=(const char *)1;other.deck[d].key_cents=77;other.deck[d].key_synced=1;}
 render_checked(&u,&other,buffer_a);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 other.clock_ms+=500;render_checked(&u,&other,buffer_a);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* No key read: the blank, dim. */
 other=m;other.deck[1].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_KEY);render_checked(&u,&other,buffer_b);
 assert(count_in(frame(buffer_b),name2,SH_KEY_AMBER)==0&&count_in(frame(buffer_b),name2,0x5a5a5a)>10);
 puts("PASS live key: no beat jump or quantize in the column, playing key, cents, analysed key, KEY SYNC, old runtime");
}
/* KEY SYNC blinks MASTER TEMPO from the model clock alone: 500 ms lit, 500 ms dark, only the chip changes. */
static void test_key_sync_blink(void) {
 assert(sh_key_sync_lit(0)&&sh_key_sync_lit(499)&&!sh_key_sync_lit(500)&&!sh_key_sync_lit(999)&&sh_key_sync_lit(1000));
 assert(sh_key_sync_lit(123456)&&!sh_key_sync_lit(123999));
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_rect box;
 xz_shell_init(&u);fx_model_full(&m,&track);
 struct xz_shell_rect s=xz_shell_play_rect(&m,XZ_SHELL_R_STRIP,1);
 m.clock_ms=40000;render_checked(&u,&m,buffer_a);
 m.clock_ms=40500;render_checked(&u,&m,buffer_b);
 int n=diff_box(frame(buffer_a),frame(buffer_b),&box);
 assert(n>20&&box_inside(box,s)&&box.w<=30&&box.h<=16);
 assert(count_in(frame(buffer_a),box,SH_RED)>20&&count_in(frame(buffer_b),box,SH_RED)==0);
 /* Every frame inside a phase is the same picture: the presenter redraws at 61.6 Hz without flicker. */
 m.clock_ms=40016;render_checked(&u,&m,buffer_b);assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 m.clock_ms=40499;render_checked(&u,&m,buffer_b);assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 m.clock_ms=41000;render_checked(&u,&m,buffer_b);assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* KEY SYNC off: MASTER TEMPO shows its own state and the clock changes nothing. */
 m.deck[1].key_synced=0;m.deck[1].master_tempo=1;
 m.clock_ms=40000;render_checked(&u,&m,buffer_a);
 m.clock_ms=40500;render_checked(&u,&m,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a)&&count_in(frame(buffer_a),box,SH_RED)>20);
 m.deck[1].master_tempo=0;render_checked(&u,&m,buffer_b);assert(count_in(frame(buffer_b),box,SH_RED)==0);
 /* KEY SYNC with MASTER TEMPO unread still blinks the chip. */
 m.deck[1].key_synced=1;m.deck[1].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER_TEMPO);
 m.clock_ms=40000;render_checked(&u,&m,buffer_a);
 m.clock_ms=40500;render_checked(&u,&m,buffer_b);
 assert(count_in(frame(buffer_a),box,SH_RED)>20&&count_in(frame(buffer_b),box,SH_RED)==0);
 puts("PASS KEY SYNC blinks MASTER TEMPO on the model clock, 500 ms phases, nothing else moves");
}
/* The stock indicators the DJ may hide: the mode lamps (and their touch) and each deck's QUANTIZE lamp. */
static void test_hidden_indicators(void) {
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_action out[XZ_SHELL_ACTIONS];struct xz_shell_rect box;
 xz_shell_init(&u);fx_model_full(&m,&track);
 assert(u.display.hidden==0);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_rect lamps={0,0,xz_shell_play_rect(&m,XZ_SHELL_R_FX,0).x-2,24};
 assert(count_in(frame(buffer_a),lamps,SH_RED)>20);   /* A. HOT CUE lights red for ON */
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_MODE_LAMPS,1)==1&&u.display.hidden==1u<<XZ_SHELL_IND_MODE_LAMPS);
 render_checked(&u,&m,buffer_b);
 /* The FX strip takes the room; no lamp ink is left, and the lamps no longer answer touch. */
 assert(count_in(frame(buffer_b),lamps,SH_RED)==0&&count_in(frame(buffer_b),lamps,SH_WHITE)==0);
 assert(at(frame(buffer_b),8,12)==rgb565(SH_RAIL));
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,20,12,out)==0);touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 assert(diff_box(frame(buffer_a),frame(buffer_b),&box)>0&&box_inside(box,xz_shell_play_rect(&m,XZ_SHELL_R_TOP_BAR,0)));
 /* QUANTIZE hidden: the strip draws as though quantize were unread. */
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_QUANTIZE_LAMP,1)==1);
 render_checked(&u,&m,buffer_a);
 struct xz_shell_model unread=m;
 for(int d=0;d<2;d++)unread.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_QUANTIZE);
 render_checked(&u,&unread,buffer_b);
 assert(!memcmp(buffer_a,buffer_b,sizeof buffer_a));
 /* Shown again, both come back; only 0 and 1 are values. */
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_MODE_LAMPS,0)==1&&xz_shell_restore_setting(&u,XZ_SHELL_ITEM_QUANTIZE_LAMP,0)==1&&u.display.hidden==0);
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_QUANTIZE_LAMP,2)==0&&xz_shell_restore_setting(&u,XZ_SHELL_ITEM_MODE_LAMPS,-1)==0&&u.display.hidden==0);
 assert(touch(&u,&m,XZ_SHELL_TOUCH_DOWN,20,12,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING);touch(&u,&m,XZ_SHELL_TOUCH_UP,0,0,out);
 /* The items read the shell's state, for the runtime's APPEARANCE tab and its store. */
 struct xz_shell_setting_item it;
 u.display.hidden=1u<<XZ_SHELL_IND_QUANTIZE;
 assert(sh_settings_item(&u,&m,XZ_SHELL_ITEM_QUANTIZE_LAMP,&it)&&it.known&&it.current==1&&it.writable&&it.choice_count==2);
 assert(sh_settings_item(&u,&m,XZ_SHELL_ITEM_MODE_LAMPS,&it)&&it.current==0);
 assert(sh_settings_set(&u,XZ_SHELL_ITEM_MODE_LAMPS,1,out)==1&&out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_MODE_LAMPS
  &&out[0].value==1&&out[0].deck==-1&&!sh_indicator_shown(&u,XZ_SHELL_IND_MODE_LAMPS));
 puts("PASS hidden indicators: mode lamps and their touch, QUANTIZE lamp, restore values, settings items");
}
static void same_key_blocks(const struct xz_shell_model *m){
 for(int deck=0;deck<(m->deck_count==4?4:2);deck++){
  struct xz_shell_rect b=xz_shell_play_rect(m,XZ_SHELL_R_INFO_COLUMN,deck);b.y+=47;b.h-=47;
  for(int y=b.y;y<b.y+b.h;y++)for(int x=b.x;x<b.x+b.w;x++)assert(at(frame(buffer_a),x,y)==at(frame(buffer_b),x,y));
 }
}
static void test_mods_key_policy(void){
 struct xz_shell u,ref;struct xz_shell_model m,expected;
 for(int count=2;count<=4;count+=2){
  xz_shell_init(&u);fx_model_full(&m,&track);m.deck_count=count;
  for(int deck=0;deck<count;deck++){
   m.deck[deck]=m.deck[0];m.deck[deck].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_KEY_ORIGINAL)|XZ_SHELL_KNOWN(XZ_SHELL_F_KEY_SYNC);
   m.deck[deck].master=0;m.deck[deck].key="Fm";m.deck[deck].key_original="Am";m.deck[deck].key_cents=12;m.deck[deck].key_synced=1;m.deck[deck].key_matches_master=0;
  }
  m.mods_settings_known=1;m.key_feedback=0;m.browse_traffic=0;m.stem_order=4;
  render_checked(&u,&m,buffer_a);expected=m;expected.mods_settings_known=0;
  for(int deck=0;deck<count;deck++){expected.deck[deck].key="Am";expected.deck[deck].key_cents=0;expected.deck[deck].key_shift=0;expected.deck[deck].key_synced=0;}
  render_checked(&u,&expected,buffer_b);same_key_blocks(&m);
  m.key_feedback=1;m.key_format=2;u.display.key_display=XZ_SHELL_KEY_CLASSIC;
  ref=u;ref.display.key_display=XZ_SHELL_KEY_ALPHANUMERIC;expected=m;expected.mods_settings_known=0;
  render_checked(&u,&m,buffer_a);render_checked(&ref,&expected,buffer_b);same_key_blocks(&m);
  m.key_format=1;u.display.key_display=XZ_SHELL_KEY_ALPHANUMERIC;ref=u;ref.display.key_display=XZ_SHELL_KEY_CLASSIC;
  render_checked(&u,&m,buffer_a);render_checked(&ref,&expected,buffer_b);same_key_blocks(&m);
 }
 m.deck[0].master=1;m.deck[0].key="Fm";
 assert(sh_key_ink(&m,"Fm",1,SH_WHITE)==SH_WHITE);
 m.browse_traffic=1;assert(sh_key_ink(&m,"Fm",1,SH_WHITE)==SH_KEY_GREEN);
 puts("PASS MODS key policy: original key with feedback off, shared notation on all four lanes, traffic colors off/on");
}

int main(void) {
 fx_track_build(&track,7);
 test_header_buttons();
 test_full_coverage();
 test_fx_readout();
 test_extrapolation();
 test_scroll_is_stable();
 test_mods_key_policy();
 test_key_display();
 test_shortcut_items();
 test_notice();
 test_menu_hold();
 test_stem_wave();
 test_live_key();
 test_key_sync_blink();
 test_hidden_indicators();
 puts("PASS play header, FX, extrapolation and shortcut");
 return 0;
}
