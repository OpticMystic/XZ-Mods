/* The REC mark on the shell's screens (ui/rec_mark.h). PLAY's top bar has one layout, recording or not:
   BEAT FX strip ..497 | REC slot 502..571 | DECKS 576..629 | INFO 630..673 | STEMS | MODS.
   For every PLAY variant (two and four decks, every stem layout, mode lamps hidden, BEAT FX lit and off,
   a popup open) this checks:
   - no recording (idle or unknown reading): the frame is byte for byte the frame without a reading;
   - recording, calm or long, at any clock up to the 99:59:59 hold, in either half of the blink: the
     BEAT FX strip's rectangle and every pixel in it are identical to the idle frame; the frame changes
     only inside the REC slot (nothing below the top bar, no control's pixels); the time is drawn;
   - no touch target moves, and a touch anywhere in the top band (the REC slot included) does exactly
     what it does without a recording.
   Every other screen changes only the header slot left of X. */
#ifdef NDEBUG
#error REC mark acceptance requires assertions
#endif
#include "four_deck_fixtures.h"
#include "browse_fixtures.h"
#include "shell_info_fixtures.h"
#include "shell_settings.h"
#include "../ui/rec_mark.h"
#include "../ui/deck_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SLOT_X0=502, SLOT_X1=572, TOP_H=26, FX_END=498 };

static struct fx_track track;
static struct bx_library lib;
static uint16_t base[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],before[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],after[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE];
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static struct xz_shell_settings_model settings_zero;
static const struct xz_canvas measure={NULL,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP};
/* Calm, the last calm second, the long state, the widest clock and the hold past it. */
static const uint32_t CLOCKS[]={3000u,59u*60000u+59000u,(72u*60u+40u)*1000u,10u*3600000u,99u*3600000u+59u*60000u+59000u,120u*3600000u};

static struct xz_rec_reading recording(const struct xz_shell_model *m, uint32_t elapsed, int timed) {
 return (struct xz_rec_reading){1,1,(uint8_t)timed,elapsed,m->clock_ms,7};
}
struct box { int x0,y0,x1,y1; };
static struct box diff(const uint16_t *a, const uint16_t *b) {
 struct box r={XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,-1,-1};
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++)if(a[y*XZ_SHELL_WIDTH+x]!=b[y*XZ_SHELL_WIDTH+x]){
  if(x<r.x0)r.x0=x;if(y<r.y0)r.y0=y;if(x>r.x1)r.x1=x;if(y>r.y1)r.y1=y;
 }
 return r;
}
static void render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *f) { assert(xz_shell_render(u,m,f,XZ_SHELL_WIDTH)); }
static int same_rect(struct xz_shell_rect a, struct xz_shell_rect b) { return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h; }
static int rect_pixels_equal(const uint16_t *a, const uint16_t *b, struct xz_shell_rect r) {
 for(int y=r.y;y<r.y+r.h;y++)if(memcmp(a+y*XZ_SHELL_WIDTH+r.x,b+y*XZ_SHELL_WIDTH+r.x,(size_t)r.w*sizeof *a))return 0;
 return 1;
}

/* What a touch does: the actions, and the state a DJ would see change. */
struct outcome { size_t n; struct xz_shell_action act[2*XZ_SHELL_ACTIONS]; int screen, open, deck, popup, panel; };
static struct outcome touch(const struct xz_shell *u0, const struct xz_shell_model *m, int x, int y) {
 struct xz_shell u=*u0;struct outcome o;memset(&o,0,sizeof o);
 struct xz_shell_action a[XZ_SHELL_ACTIONS];
 size_t n=xz_shell_input(&u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1},a);
 for(size_t i=0;i<n;i++)o.act[o.n++]=a[i];
 n=xz_shell_input(&u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1},a);
 for(size_t i=0;i<n;i++)o.act[o.n++]=a[i];
 o.screen=(int)xz_shell_screen(&u);o.open=u.deck_view.open;o.deck=u.deck;o.popup=(int)u.popup.kind;o.panel=(int)u.panel.kind;
 return o;
}
static int same_outcome(const struct outcome *a, const struct outcome *b) {
 if(a->n!=b->n||a->screen!=b->screen||a->open!=b->open||a->deck!=b->deck||a->popup!=b->popup||a->panel!=b->panel)return 0;
 for(size_t i=0;i<a->n;i++)if(a->act[i].kind!=b->act[i].kind||a->act[i].deck!=b->act[i].deck||a->act[i].value!=b->act[i].value||a->act[i].id!=b->act[i].id)return 0;
 return 1;
}

static void play_case(struct xz_shell *u, struct xz_shell_model *m, const char *what) {
 const struct xz_rec_reading zero={0},idle={1,0,0,0,m->clock_ms,3};
 enum xz_shell_play_region regions[]={XZ_SHELL_R_TOP_BAR,XZ_SHELL_R_FX,XZ_SHELL_R_INFO_BUTTON,XZ_SHELL_R_VIEW_TWO,XZ_SHELL_R_VIEW_FOUR};
 enum { NREG=sizeof regions/sizeof *regions };
 struct xz_shell_rect idle_rect[NREG];
 m->recorder=zero;render(u,m,base);
 for(int i=0;i<NREG;i++)idle_rect[i]=xz_shell_play_rect(m,regions[i],0);
 struct xz_shell_rect fx=idle_rect[1];
 /* The strip itself: today's geometry, which no state changes. */
 assert(fx.x+fx.w==FX_END&&fx.y==2&&fx.h==22);
 m->recorder=idle;render(u,m,after);assert(diff(base,after).x1<0);
 /* A held-out reading that never saw a clock: the dot and REC, still inside the slot. */
 for(size_t k=0;k<=sizeof CLOCKS/sizeof *CLOCKS;k++)for(int half=0;half<2;half++){
  int timed=k<sizeof CLOCKS/sizeof *CLOCKS;
  m->recorder=recording(m,timed?CLOCKS[k]:0,timed);
  uint32_t keep=m->clock_ms;m->clock_ms+=half?600u:0u;m->recorder.read_at_ms=m->clock_ms;
  render(u,m,after);
  struct box b=diff(base,after);
  if(b.x1<0||b.x0<SLOT_X0||b.x1>=SLOT_X1||b.y0<0||b.y1>=TOP_H){
   fprintf(stderr,"FAIL %s: REC (clock %u, half %d) changes x %d..%d y %d..%d, outside the slot %d..%d\n",
    what,timed?CLOCKS[k]:0u,half,b.x0,b.x1,b.y0,b.y1,SLOT_X0,SLOT_X1-1);
   assert(0);
  }
  /* The BEAT FX strip: the same rectangle and the same pixels, lit or not. */
  assert(same_rect(fx,xz_shell_play_rect(m,XZ_SHELL_R_FX,0))&&rect_pixels_equal(base,after,fx));
  /* No control moves. */
  for(int i=0;i<NREG;i++)assert(same_rect(idle_rect[i],xz_shell_play_rect(m,regions[i],0)));
  /* The time is always there once known: the pill drawn is the dot and the clock, right-aligned in the slot. */
  struct xz_rec_view v=xz_rec_view_at(&m->recorder,m->clock_ms);
  unsigned parts=xz_rec_parts(measure,SLOT_X1-SLOT_X0,XZ_REC_SHELL_H,&v);
  if(timed){
   assert(parts&XZ_REC_PART_TIME);
   int w=xz_rec_width(measure,parts,XZ_REC_SHELL_H,v.elapsed_ms);
   assert(b.x1==SLOT_X1-1&&b.x0>=SLOT_X1-w);
  }else assert(parts==XZ_REC_PART_WORD);
  m->clock_ms=keep;
 }
 /* Taps: every point of the top band, the REC slot included, does what it does without a recording. */
 struct xz_shell_model rec=*m;rec.recorder=recording(m,CLOCKS[2],1);
 struct xz_shell_model none=*m;none.recorder=zero;
 int points=0;
 for(int y=0;y<XZ_SHELL_HEADER_HIT_H;y+=7)for(int x=1;x<XZ_SHELL_WIDTH;x+=5){
  struct outcome a=touch(u,&none,x,y),c=touch(u,&rec,x,y);
  if(!same_outcome(&a,&c)){fprintf(stderr,"FAIL %s: a touch at %d,%d acts differently while recording\n",what,x,y);assert(0);}
  points++;
 }
 /* The REC slot answers no touch of its own. */
 for(int x=SLOT_X0;x<SLOT_X1;x+=3){struct outcome c=touch(u,&rec,x,12);assert(c.n==0&&!c.open);}
 m->recorder=zero;
 printf("PASS %s: idle frame unchanged; REC only in x %d..%d, FX strip identical, time shown, no control moved, %d touches alike\n",
  what,SLOT_X0,SLOT_X1-1,points);
}

int main(void) {
 setvbuf(stdout,NULL,_IONBF,0);
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;
 struct xz_shell_info_model im;struct xz_shell_source_model sm;
 (void)bx_folders;
 fx_track_build(&track,7);bx_build(&lib);

 /* The layout: DECKS and INFO where the slot needs them, STEMS and MODS where they always were. */
 xz_shell_init(&u);fx_model_full(&m,&track);
 {
  struct xz_shell_rect d=xz_shell_play_rect(&m,XZ_SHELL_R_VIEW_TWO,0),i=xz_shell_play_rect(&m,XZ_SHELL_R_INFO_BUTTON,0);
  assert(d.x==SLOT_X1+4&&d.w==54&&i.x==d.x+d.w&&i.x+i.w==XZ_SHELL_STEMS_X&&i.w>=44);
  assert(XZ_SHELL_STEMS_X==674&&XZ_SHELL_MODS_X==744);
 }
 play_case(&u,&m,"PLAY two decks");
 fx_beat_fx(&m.fx,1,"VINYL BRAKE",64,1,29999);m.fx.tap=1;play_case(&u,&m,"PLAY two decks, BEAT FX lit, longest name with TAP");
 fx_beat_fx(&m.fx,1,"ECHO",1,2,234);play_case(&u,&m,"PLAY two decks, BEAT FX lit, ECHO 1/2");
 fx_beat_fx(&m.fx,0,"ECHO",1,2,234);play_case(&u,&m,"PLAY two decks, BEAT FX off");
 xz_shell_init(&u);fx_model_four(&m,&track);play_case(&u,&m,"PLAY four decks");
 fx_beat_fx(&m.fx,1,"VINYL BRAKE",64,1,29999);m.fx.tap=1;play_case(&u,&m,"PLAY four decks, BEAT FX lit");
 xz_shell_init(&u);u.display.wave_color=XZ_SHELL_WAVE_STEMS;fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN;m.stems_known=m.stems_on=1;
 m.stem_layout_known=1;
 for(int layout=0;layout<XZ_SHELL_STEM_LAYOUTS;layout++){
  char name[64];snprintf(name,sizeof name,"PLAY stem layout %d",layout);
  m.stem_layout=(enum xz_shell_stem_layout)layout;play_case(&u,&m,name);
 }
 /* The mode lamps hidden: the strip starts at the bar's left edge and still ends at 497. */
 xz_shell_init(&u);u.display.hidden|=1u<<XZ_SHELL_IND_MODE_LAMPS;fx_model_full(&m,&track);play_case(&u,&m,"PLAY, mode lamps hidden");
 fx_beat_fx(&m.fx,1,"VINYL BRAKE",64,1,29999);m.fx.tap=1;play_case(&u,&m,"PLAY, mode lamps hidden, BEAT FX lit");
 /* A popup open over PLAY: the top bar's pill only, never a second one in the header slot. */
 xz_shell_init(&u);fx_model_full(&m,&track);u.popup.kind=XZ_SHELL_POPUP_RECENT;
 m.recorder=(struct xz_rec_reading){0};render(&u,&m,before);m.recorder=recording(&m,CLOCKS[0],1);render(&u,&m,after);
 {struct box d=diff(before,after);assert(d.x0>=SLOT_X0&&d.x1<SLOT_X1&&d.y1<TOP_H);}
 puts("PASS PLAY with a popup: one pill, in the REC slot");

 /* Every other screen: only the header slot left of X, x 674..741 y 0..23. */
 fx_model_full(&m,&track);
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SOURCE;
 m.browse=&b;bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 fxi_artwork(art);fxi_info_full(&im,art);fxi_source_full(&sm);m.info=&im;m.source=&sm;m.settings_screen=&settings_zero;
 static const struct { enum xz_shell_key key; const char *name; } screens[]={
  {XZ_SHELL_KEY_BROWSE,"BROWSE"},{XZ_SHELL_KEY_SEARCH,"SEARCH"},{XZ_SHELL_KEY_INFO,"INFO"},{XZ_SHELL_KEY_SOURCE,"SOURCE"},
  {XZ_SHELL_KEY_SHORTCUT,"SHORTCUT"},{XZ_SHELL_KEY_MENU_HOLD,"UTILITY"}};
 for(size_t i=0;i<sizeof screens/sizeof *screens;i++){
  xz_shell_init(&u);xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,screens[i].key,-1},out);
  if(xz_shell_screen(&u)==XZ_SHELL_PLAY){fprintf(stderr,"FAIL %s did not open\n",screens[i].name);assert(0);}
  m.recorder=(struct xz_rec_reading){0};render(&u,&m,before);
  for(int level=0;level<2;level++){
   m.recorder=recording(&m,level?CLOCKS[2]:CLOCKS[0],1);render(&u,&m,after);
   struct box d=diff(before,after);
   if(d.x1<0||d.x0<XZ_SHELL_STEMS_X||d.x1>=XZ_SHELL_STEMS_X+XZ_SHELL_STEMS_W||d.y1>=XZ_SHELL_HEADER_BUTTON_H){
    fprintf(stderr,"FAIL %s: the REC frame changes %d..%d x %d..%d\n",screens[i].name,d.x0,d.x1,d.y0,d.y1);assert(0);
   }
  }
  printf("PASS %s: REC changes only the header slot left of X\n",screens[i].name);
 }
 return 0;
}
