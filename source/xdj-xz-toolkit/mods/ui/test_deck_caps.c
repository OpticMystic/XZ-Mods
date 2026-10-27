/* SPDX-License-Identifier: MIT */
/* xz_deck_caps is the only source of the 4 DECKS grid, the WORKS ON chips and the "left out" reasons. Each bit
   follows what the runtime can carry out (merged-evidence/REPORT.md, capability table): engines this runtime
   does not run never light, whatever ready bits a model carries; decks 3 and 4 have the parity features once
   their players run and nothing while they do not. */
#include "ui.h"
#include "ui_sections.h"
#include "pictures.h"
#include "deck_colour.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Capability acceptance requires active assertions
#endif
enum { ABSENT=XZ_DECK_CAN_CUE_PREVIEW|XZ_DECK_CAN_XPAD|XZ_DECK_CAN_GROOVE,
       PARITY=XZ_DECK_CAN_BEAT_JUMP|XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE };
static const unsigned all[]={XZ_DECK_CAN_BEAT_JUMP,XZ_DECK_CAN_GATE_CUE,XZ_DECK_CAN_SMART_CUE,XZ_DECK_CAN_CUE_PREVIEW,
 XZ_DECK_CAN_PLAYING_KEY,XZ_DECK_CAN_KEY_SYNC,XZ_DECK_CAN_STEMS,XZ_DECK_CAN_XPAD,XZ_DECK_CAN_GROOVE,XZ_DECK_CAN_RAM,XZ_DECK_CAN_DISPLAY};
/* Every feature a deck lacks has one short reason; one it has has none. */
static void reasons(const struct xz_ui_model *m){
 for(int d=0;d<4;d++)for(size_t f=0;f<sizeof all/sizeof *all;f++){
  const char *why=xz_deck_cap_reason(m,d,all[f]);
  if(xz_deck_caps(m,d)&all[f])assert(!why);else assert(why&&*why&&strlen(why)<=48);
 }
}
/* The WORKS ON chips and the grid's column heads name decks the way the four-deck screens do: a deck it works on
   gets its badge (deck_colour.h), one it does not a plain outline; on a light theme the box has an ink edge. */
static uint16_t chip_px[800*300];
static int has(uint32_t c,int x0,int x1){
 uint16_t want=(uint16_t)(((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31));int n=0;
 for(int y=0;y<40;y++)for(int x=x0;x<x1;x++)n+=chip_px[y*800+x]==want;return n;
}
static void chips(void){
 for(int theme=0;theme<2;theme++){
  struct xz_pic_style st=theme?xz_pic_style_classic(1):xz_pic_style_modern();
  for(int i=0;i<800*300;i++)chip_px[i]=0;
  struct xz_canvas c={chip_px,800,800,300,theme?1:0};
  int x=xz_pic_works_on(c,&st,0,4,16,3u,0);assert(x==4*19);
  static const int order[4]={2,0,1,3};
  /* Decks 1 and 2 work: their grey badges with the white digit; 3 and 4 do not: no white badge, only an outline. */
  for(int k=0;k<4;k++){int d=order[k],x0=k*19,lit=has(xz_deck_box_fill(d,1,0),x0,x0+16);assert(d<2?lit>100:lit==0);}
  if(theme)assert(has(st.ink,19,35)>=40);
  /* The grid heads: four badges over each half, both badge fills present in the header row. */
  for(int i=0;i<800*300;i++)chip_px[i]=0;
  struct xz_pic_grid_geo g;xz_pic_grid_geometry(10,20,780,24,&g);unsigned caps[4]={0};
  xz_pic_grid_draw(c,&st,&g,caps,-1);
  for(int d=0;d<4;d++){int n=0;uint32_t k=xz_deck_box_fill(d,1,0);uint16_t want=(uint16_t)(((k>>19)&31)<<11|((k>>10)&63)<<5|((k>>3)&31));
   for(int y=g.header_y-2;y<g.header_y+11;y++)for(int x=0;x<800;x++)n+=chip_px[y*800+x]==want;assert(n>=2*60);}
 }
}
int main(void){
 chips();
 struct xz_ui_model m;memset(&m,0,sizeof m);
 /* Every ready bit a loader could report, both native decks, the RAM engine, decks 3 and 4 connected. */
 m.deck[0].ready=m.deck[1].ready=0xffffffffu;m.deck[0].ram.state=XZ_UI_RAM_EMPTY;m.route.state=XZ_UI_ROUTE_READY;
 for(int d=0;d<4;d++)assert(!(xz_deck_caps(&m,d)&ABSENT));
 for(size_t f=0;f<3;f++){unsigned a=(unsigned[]){XZ_DECK_CAN_CUE_PREVIEW,XZ_DECK_CAN_XPAD,XZ_DECK_CAN_GROOVE}[f];
  assert(xz_deck_cap_absent(a));assert(!strcmp(xz_deck_cap_reason(&m,0,a),"Not in this version, on any deck."));}
 assert(!xz_ui_has_engine(&m,XZ_UI_PREVIEW)&&!xz_ui_has_engine(&m,XZ_UI_SAMPLE)&&!xz_ui_has_engine(&m,XZ_UI_GROOVE));
 for(int d=0;d<2;d++)assert((xz_deck_caps(&m,d)&(PARITY|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_KEY_SYNC|XZ_DECK_CAN_RAM|XZ_DECK_CAN_DISPLAY))==
  (PARITY|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_KEY_SYNC|XZ_DECK_CAN_RAM|XZ_DECK_CAN_DISPLAY));
 /* Decks 3 and 4: the shared beat jump, Gate Cue and Smart Cue, and the display; not the playing key, key
    sync, stems or RAM. The same while the route is down or starting: the players run. */
 for(int s=XZ_UI_ROUTE_NOT_DETECTED;s<XZ_UI_ROUTE_STATES;s++){
  m.route.state=s;
  for(int d=2;d<4;d++)assert(xz_deck_caps(&m,d)==(PARITY|XZ_DECK_CAN_DISPLAY|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_KEY_SYNC));
  reasons(&m);
 }
 assert(!xz_deck_cap_reason(&m,2,XZ_DECK_CAN_PLAYING_KEY));
 /* Players not running right now: the same features, since WORKS ON says what works on a deck when it plays;
    whether decks 3 and 4 are connected is the 4 DECKS page's to say. */
 m.route.state=XZ_UI_ROUTE_NO_ENGINE;
 for(int d=2;d<4;d++){assert(xz_deck_caps(&m,d)==(PARITY|XZ_DECK_CAN_DISPLAY|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_KEY_SYNC));assert(!xz_deck_cap_reason(&m,d,XZ_DECK_CAN_BEAT_JUMP));}
 reasons(&m);
 /* Native decks: each engine-backed bit follows the runtime's report on that deck. */
 memset(&m,0,sizeof m);m.route.state=XZ_UI_ROUTE_NOT_DETECTED;
 assert(xz_deck_caps(&m,0)==(XZ_DECK_CAN_BEAT_JUMP|XZ_DECK_CAN_PLAYING_KEY|XZ_DECK_CAN_DISPLAY));
 m.deck[1].ready=XZ_UI_GATE|XZ_UI_SMART;
 assert(!(xz_deck_caps(&m,0)&(XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE))&&(xz_deck_caps(&m,1)&(XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE))==(XZ_DECK_CAN_GATE_CUE|XZ_DECK_CAN_SMART_CUE));
 m.deck[0].ready=XZ_UI_KEY;assert(!(xz_deck_caps(&m,1)&XZ_DECK_CAN_KEY_SYNC));
 m.deck[1].ram.state=XZ_UI_RAM_READY;assert(xz_deck_caps(&m,0)&XZ_DECK_CAN_RAM);
 assert(!(xz_deck_caps(&m,0)&XZ_DECK_CAN_STEMS)&&!strcmp(xz_deck_cap_reason(&m,0,XZ_DECK_CAN_STEMS),"This USB loader has no stem engine."));
 m.deck[0].ready|=XZ_UI_STEM;assert(xz_deck_caps(&m,0)&XZ_DECK_CAN_STEMS);
 reasons(&m);
 /* The grid rows and the chips read the same function: the picture's 4 DECKS grid never shows a check
    xz_deck_caps does not give. */
 struct xz_ui u;xz_ui_init(&u);m.deck[0].ready=m.deck[1].ready=0xffffffffu;m.route.state=XZ_UI_ROUTE_READY;
 struct xz_ui_info info;struct xz_ui_widget w[XZ_UI_WIDGETS];xz_ui_navigate(&u,XZ_UI_FOUR_DECKS);size_t n=xz_ui_layout(&u,&m,w);int rows=0;
 for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_INFO&&w[i].index>=XZ_UI_INFO_GRID&&xz_ui_describe_widget(&u,&m,&w[i],&info)){
  rows++;unsigned on=0;for(int d=0;d<4;d++)if(xz_deck_caps(&m,d)&info.feature)on|=1u<<d;
  assert(info.works_on==on);
  if(info.feature&ABSENT)assert(!on&&strstr(info.body,"not in this version"));
 }
 assert(rows==XZ_UI_GRID_ROWS);
 puts("PASS deck caps: no preview/X-PAD/groove engine on any deck whatever the ready bits, decks 3/4 parity and display only, native bits follow the runtime, one short reason per missing feature, grid = caps, WORKS ON chips as deck badges");
 return 0;
}
