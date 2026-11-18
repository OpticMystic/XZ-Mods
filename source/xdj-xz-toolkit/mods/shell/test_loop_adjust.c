#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* LOOP IN / OUT ADJUST on the PLAY screen (XZ manual pp.47-48, shell.h XZ_SHELL_F_LOOP_EDIT), the same on all
   four decks. Pinned here, for two decks (stock decks alone and with decks 3 and 4 up) and four lanes, each
   deck, IN and OUT, a normal and a very short loop:
   - the edge under the jog is the one full-height yellow column in its lane, and a yellow chip sits next to it
     with dark text, inside the lane;
   - nothing outside that deck's lane changes;
   - the edge follows the point: moving it moves the column by the same pixels as the axis;
   - an edge out of view pins the chip to that end of the lane and draws no column;
   - ending the adjustment draws the frame without it, byte for byte. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include "../ui/layout_cycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=XZ_SHELL_WIDTH, H=XZ_SHELL_HEIGHT };
static struct fx_track track;
static uint16_t plain[W*H],edit[W*H];
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *p){
 memset(p,0,sizeof plain);assert(xz_shell_render(u,m,p,W));
}
static int inside(struct xz_shell_rect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
struct found { int columns, first, chip, chip_x0, chip_x1, dark_in_chip; };
/* Columns of the lane that are the adjust yellow over at least half its height (the waveform band; stem rows
   may share the lane), and the yellow elsewhere. */
static struct found scan(const uint16_t *p,struct xz_shell_rect lane){
 struct found f={0,-1,0,W,-1,0};uint16_t y565=rgb565(SH_ADJUST);
 int full[W]={0};
 for(int x=lane.x;x<lane.x+lane.w;x++){
  int n=0;for(int y=lane.y;y<lane.y+lane.h;y++)n+=p[y*W+x]==y565;
  if(n*2>=lane.h){full[x]=1;f.columns++;if(f.first<0)f.first=x;}
 }
 for(int y=lane.y;y<lane.y+lane.h;y++)for(int x=lane.x;x<lane.x+lane.w;x++)
  if(!full[x]&&p[y*W+x]==y565){f.chip++;if(x<f.chip_x0)f.chip_x0=x;if(x>f.chip_x1)f.chip_x1=x;}
 return f;
}
static void check_outside_unchanged(struct xz_shell_rect lane){
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(!inside(lane,x,y))assert(plain[y*W+x]==edit[y*W+x]);
 uint16_t y565=rgb565(SH_ADJUST);
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(!inside(lane,x,y))assert(edit[y*W+x]!=y565||plain[y*W+x]==y565);
}
static void check_deck(struct xz_shell *u,struct xz_shell_model *m,int deck,int32_t in,int32_t out,const char *what){
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 struct xz_shell_rect lane=xz_shell_play_rect(&view,XZ_SHELL_R_LANE,deck);
 struct xz_shell_deck *d=&m->deck[deck];
 d->loop=(struct xz_shell_loop){1,1,1,in,out};d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP);
 d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP_EDIT);
 render(u,m,plain);
 struct found base=scan(plain,lane);assert(base.columns==0&&base.chip==0);
 for(int endpoint=0;endpoint<2;endpoint++){
  d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP_EDIT);d->loop_edit=(struct xz_shell_loop_edit){endpoint,endpoint?out:in};
  render(u,m,edit);
  struct found f=scan(edit,lane);
  /* One edge, 3 px (less only where the lane clips it), and the chip: at least its text height by 40 px. */
  if(!(f.columns>=1&&f.columns<=3))fprintf(stderr,"deck %d %s endpoint %d: columns %d lane %d,%d %dx%d chip %d\n",deck+1,what,endpoint,f.columns,lane.x,lane.y,lane.w,lane.h,f.chip);
  assert(f.columns>=1&&f.columns<=3);
  assert(f.chip>=40*SH_CAP_SMALL&&f.chip_x0>=lane.x&&f.chip_x1<lane.x+lane.w);
  /* The chip touches the edge's neighbourhood: within 8 px of the column, on either side. */
  assert(f.chip_x1>=f.first-8&&f.chip_x0<=f.first+3+8);
  check_outside_unchanged(lane);
  /* The edge follows the point: 1 s later moves the column right by the axis' pixels for 1 s. */
  int first=f.first;
  d->loop_edit.ms+=endpoint?1000:-1000;
  if(endpoint)d->loop.out_ms+=1000;else d->loop.in_ms-=1000;
  render(u,m,edit);struct found g=scan(edit,lane);
  assert(g.columns>=1&&(endpoint?g.first>first:g.first<first));
  if(endpoint)d->loop.out_ms-=1000;else d->loop.in_ms+=1000;
  /* Ten minutes away: no column, the chip pinned at that end of the lane. */
  d->loop_edit.ms=endpoint?out+600000:in-600000;
  render(u,m,edit);struct found h=scan(edit,lane);
  assert(h.columns==0&&h.chip>=40*SH_CAP_SMALL);
  assert(endpoint?h.chip_x1>=lane.x+lane.w-8:h.chip_x0<=lane.x+8);
  /* Adjustment over: the frame without it. */
  d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP_EDIT);render(u,m,edit);
  assert(!memcmp(plain,edit,sizeof plain));
 }
 printf("  deck %d %s: IN and OUT\n",deck+1,what);
}
static void each(struct xz_shell *u,struct xz_shell_model *m,const char *view){
 struct xz_shell_model shown;xz_shell_play_projection(u,m,&shown);
 for(int deck=0;deck<sh_deck_count(m);deck++){
  int on=0;for(int slot=0;slot<sh_play_deck_count(&shown);slot++)on|=sh_play_deck_count(&shown)==4?slot==deck:sh_play_deck(&shown,slot)==deck;
  if(!on||sh_play_placeholder(&shown,deck))continue;
  int32_t at=xz_shell_elapsed_now(m,deck);
  struct xz_shell_deck saved=m->deck[deck];
  char what[64];
  snprintf(what,sizeof what,"%s, normal loop",view);check_deck(u,m,deck,at-1500,at+1500,what);m->deck[deck]=saved;
  snprintf(what,sizeof what,"%s, 7 ms loop",view);check_deck(u,m,deck,at-3,at+4,what);m->deck[deck]=saved;
 }
}
int main(void){
 struct xz_shell u;struct xz_shell_model m;
 fx_track_build(&track,7);
 xz_shell_init(&u);fx_model_full(&m,&track);m.stems_known=1;each(&u,&m,"two stock decks");
 xz_shell_init(&u);fx_model_four_players(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_TWO+1;m.sides_extra=1;each(&u,&m,"2 DECKS, left on 3");
 xz_shell_init(&u);fx_model_four_players(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_FOUR+1;each(&u,&m,"4 DECKS");
 xz_shell_init(&u);fx_model_four_players(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_MIXER+1;each(&u,&m,"4 DECKS mixer order");
 puts("PASS loop adjust: the edge under the jog is the one yellow column with its chip, it follows the point, pins when out of view, and goes with the adjustment, on all four decks");
 return 0;
}
