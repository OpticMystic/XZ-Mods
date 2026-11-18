#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* Owner 2026-11-17: no notices over the waveforms. On PLAY the notice sits in the top bar's BEAT FX strip in
   every deck view and stem layout: the frame with a notice differs from the frame without it only inside that
   box, so lanes, deck columns, strips and the other top-bar controls keep every pixel; it widens left over the
   mode chips only for a long sentence and breaks onto two lines only past that; it goes after
   XZ_SHELL_NOTICE_MS. NEEDLE LOCK draws no sentence: the refused deck's own mark lights, no other deck's. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define STRIDE 804
static uint16_t with[STRIDE*XZ_SHELL_HEIGHT], without[STRIDE*XZ_SHELL_HEIGHT];
static struct fx_track track;
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x07e0)|((c&0xff)>>3)); }
static int inside(struct xz_shell_rect r, int x, int y) { return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h; }
static int overlap(struct xz_shell_rect a, struct xz_shell_rect b) { return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h; }
static void render(const struct xz_shell *u, const struct xz_shell_model *m, uint16_t *f) {
 memset(f,0x5a,sizeof with);assert(xz_shell_render(u,m,f,STRIDE));
}
/* Pixels that differ between the two frames, and how many fall outside r. */
static int diff(struct xz_shell_rect r, int *outside) {
 int n=0;*outside=0;
 for(int y=0;y<XZ_SHELL_HEIGHT;y++)for(int x=0;x<XZ_SHELL_WIDTH;x++)
  if(with[y*STRIDE+x]!=without[y*STRIDE+x]){n++;if(!inside(r,x,y))(*outside)++;}
 return n;
}
static int count(const uint16_t *f, struct xz_shell_rect r, uint32_t color) {
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=f[y*STRIDE+x]==rgb565(color);
 return n;
}
static const char *const sentences[]={
 "DECK 1 DID NOT LOAD THE TRACK","DECK 3/4 ROUTE LOST","USB STARTING / DECK 1 LOADS WHEN READY",
 "DECK 3 AND 4 CANNOT LOAD AT A MEMORY POINT",                              /* the widest runtime sentence: strip */
 "DECKS 3 AND 4 ARE READY. TAP DECKS AT THE TOP FOR FOUR DECKS.",           /* wider: over the chips, one line */
 "THE TRACK ON USB 2 COULD NOT BE READ BECAUSE THE DEVICE WAS REMOVED WHILE IT WAS LOADING"}; /* two lines */
static const int expect_lines[]={1,1,1,1,1,2};
static const int expect_wide[]={0,0,0,0,1,1};

static void check_view(struct xz_shell_model *m, const char *what) {
 struct xz_shell u;xz_shell_init(&u);
 struct xz_shell_model view;xz_shell_play_projection(&u,m,&view);
 struct xz_shell_rect fx=xz_shell_play_rect(m,XZ_SHELL_R_FX,0);
 for(size_t t=0;t<sizeof sentences/sizeof *sentences;t++){
  struct xz_shell_model said=*m;said.notice=sentences[t];said.notice_at_ms=said.clock_ms;
  m->notice=NULL;
  render(&u,m,without);render(&u,&said,with);
  int lines;struct xz_shell_rect r=sh_notice_place(&u,&said,sentences[t],&lines);
  assert(lines==expect_lines[t]);
  /* In the top bar, inside the strip's rows; the strip itself, or the strip widened left to the bar's padding. */
  assert(r.y==fx.y&&r.h==fx.h&&r.y+r.h<=26&&r.x+r.w==fx.x+fx.w);
  assert(expect_wide[t]?r.x==8:r.x==fx.x);
  /* Clear of the DECK VIEW button, INFO and the STEMS / MODS header buttons. */
  assert(!overlap(r,xz_shell_play_rect(m,XZ_SHELL_R_VIEW_TWO,0)));
  assert(!overlap(r,xz_shell_play_rect(m,XZ_SHELL_R_INFO_BUTTON,0)));
  assert(r.x+r.w<=XZ_SHELL_HEADER_X);
  /* No lane, deck column, pad row or strip under it, in any deck of the view. */
  for(int slot=0;slot<sh_play_deck_count(&view);slot++){
   int deck=sh_play_deck(&view,slot);
   static const enum xz_shell_play_region regions[]={XZ_SHELL_R_LANE,XZ_SHELL_R_INFO_COLUMN,XZ_SHELL_R_STRIP,
    XZ_SHELL_R_PAD_ROW,XZ_SHELL_R_OVERVIEW};
   for(size_t k=0;k<sizeof regions/sizeof *regions;k++){
    struct xz_shell_rect g=xz_shell_play_rect(&view,regions[k],deck);
    if(g.w>0&&g.h>0)assert(!overlap(r,g));
   }
  }
  /* Every changed pixel is in the box; the box is the notice's colours. */
  int outside,n=diff(r,&outside);
  assert(n>0&&outside==0);
  assert(count(with,r,0x2b1d00)>r.w*r.h/3);
  /* Gone after XZ_SHELL_NOTICE_MS, the frame then exactly the plain one. */
  said.clock_ms+=XZ_SHELL_NOTICE_MS;m->clock_ms=said.clock_ms;
  render(&u,m,without);render(&u,&said,with);
  assert(!memcmp(with,without,sizeof with));
  m->clock_ms-=XZ_SHELL_NOTICE_MS;
 }
 printf("PASS notice on PLAY, %s: top bar only, lanes and strips untouched, gone after %d ms\n",what,XZ_SHELL_NOTICE_MS);
}

static void test_views(void) {
 struct xz_shell_model m;
 fx_model_full(&m,&track);check_view(&m,"2 DECKS");
 fx_model_full(&m,&track);m.stem_layout_known=1;
 for(int s=0;s<XZ_SHELL_STEM_LAYOUTS;s++){
  m.stem_layout=(enum xz_shell_stem_layout)s;
  char what[40];snprintf(what,sizeof what,"2 DECKS stem layout %d",s);check_view(&m,what);
 }
 fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;check_view(&m,"4 DECKS 1 2 3 4");
 fx_model_four(&m,&track);m.layout_preference=XZ_LAYOUT_MIXER+1;m.wave_order=1;check_view(&m,"4 DECKS 3 1 2 4");
 fx_model_full(&m,&track);m.layout_preference=XZ_LAYOUT_MIXER+1;check_view(&m,"4 DECKS while decks 3 and 4 are down");
 fx_model_four_players(&m,&track);m.layout_preference=XZ_LAYOUT_TWO+1;check_view(&m,"2 DECKS, left side on DECK 3");
}

/* The other screens keep the centred box: the brief is the play screen. */
static void test_other_screens(void) {
 struct xz_shell u;struct xz_shell_model m;fx_model_full(&m,&track);
 xz_shell_init(&u);u.depth=2;u.stack[1]=(struct xz_shell_view){XZ_SHELL_INFO,0,0,0};
 struct xz_shell_rect a=sh_notice_place(&u,&m,sentences[0],NULL),b=sh_notice_rect(sentences[0],NULL);
 assert(a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h);
 puts("PASS other screens keep the centred notice box");
}

static void test_needle_lock(void) {
 struct xz_shell u;struct xz_shell_model m;fx_model_full(&m,&track);
 m.settings.needle_lock_known=1;m.settings.needle_lock=1;
 for(int d=0;d<2;d++){m.deck[d].playing=1;m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYING);}
 xz_shell_init(&u);
 render(&u,&m,without);
 struct xz_shell_rect ov=xz_shell_play_rect(&m,XZ_SHELL_R_OVERVIEW,0),other=xz_shell_play_rect(&m,XZ_SHELL_R_OVERVIEW,1);
 struct xz_shell_action out[XZ_SHELL_ACTIONS];
 struct xz_shell_event down={XZ_SHELL_TOUCH_DOWN,ov.x+ov.w/3,ov.y+ov.h/2,0,0,-1},up=down;up.kind=XZ_SHELL_TOUCH_UP;
 size_t n=xz_shell_input(&u,&m,&down,out);
 for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_SEEK);
 n=xz_shell_input(&u,&m,&up,out);
 for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_SEEK);
 assert(u.notice&&!strcmp(u.notice,SH_NEEDLE_NOTICE)&&u.notice_deck==0);
 render(&u,&m,with);
 /* Only deck 1's overview changed, and only its mark: lit amber there, deck 2's mark still dark. */
 int outside,changed=diff(ov,&outside);
 assert(changed>0&&outside==0);
 assert(count(with,ov,SH_KEY_AMBER)>100&&count(without,ov,SH_KEY_AMBER)==0);
 assert(count(with,other,SH_KEY_AMBER)==0);
 int lines;struct xz_shell_rect r=sh_notice_place(&u,&m,u.notice,&lines);assert(r.w==0&&lines==0);
 /* The mark goes dark with the notice's time. */
 m.clock_ms+=XZ_SHELL_NOTICE_MS;render(&u,&m,with);
 struct xz_shell u0;xz_shell_init(&u0);render(&u0,&m,without);
 assert(!memcmp(with,without,sizeof with));
 puts("PASS NEEDLE LOCK: a refused touch lights that deck's mark, no sentence, nothing else changes");
}

int main(void) {
 fx_track_build(&track,7);
 test_views();
 test_other_screens();
 test_needle_lock();
 return 0;
}
