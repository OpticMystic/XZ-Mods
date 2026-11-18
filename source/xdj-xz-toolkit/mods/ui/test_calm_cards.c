#ifdef NDEBUG
#error Calm card fixtures require assertions
#endif
/* ram_notice.h: the RAM card on CLASSIC's wave surface and the calm card in the player's own hub window. */
#include "ui.h"
#include "ram_notice.h"
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define KEY 0xf81fu
static uint16_t px[XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT];
static void key_fill(void){for(size_t i=0;i<sizeof px/sizeof *px;i++)px[i]=KEY;}
/* Every drawn pixel lies in rows [top,bottom) and columns [2,534); returns how many. */
static int drawn_within(int top,int bottom){
 int n=0;
 for(int y=0;y<XZ_WAVE_HEIGHT;y++)for(int x=0;x<XZ_WAVE_WIDTH;x++)if(px[y*XZ_WAVE_WIDTH+x]!=KEY){
  assert(y>=top&&y<bottom&&x>=2&&x<XZ_WAVE_WIDTH-2);n++;
 }
 return n;
}
int main(void){
 /* CLASSIC's RAM card: inside the lane, clear of its ten cue rows at each edge, in every stem layout. */
 struct xz_canvas s={px,XZ_WAVE_WIDTH,XZ_WAVE_WIDTH,XZ_WAVE_HEIGHT,0};
 int layouts=0;
 for(int mode=0;mode<XZ_STEM_LAYOUT_COUNT;mode++){
  struct xz_wave_layout g;assert(xz_wave_layout_get(mode,&g));
  for(int d=0;d<2;d++){
   key_fill();xz_ram_notice_two_lane(s,(int)g.lane_y[d],(int)g.lane_height);
   assert(drawn_within((int)g.lane_y[d]+10,(int)(g.lane_y[d]+g.lane_height)-10)>8000);layouts++;
  }
 }
 /* 2 DECKS with a side on DECK 3 or 4: that deck's 132-row half. */
 for(int side=0;side<2;side++){key_fill();xz_ram_notice_two_lane(s,side?136:0,132);assert(drawn_within((side?136:0)+10,(side?136:0)+122)>8000);}
 /* Four decks: below the lane's deck tag (rows 2..14), inside the 64-row lane. */
 for(int order=0;order<XZ_FOUR_ORDER_COUNT;order++){
  struct xz_four_layout g;assert(xz_four_layout_get(order,&g));
  for(int d=0;d<XZ_FOUR_DECKS;d++){
   key_fill();xz_ram_notice_four_lane(s,(int)g.lane_y[d]);
   assert(drawn_within((int)g.lane_y[d]+16,(int)g.lane_y[d]+XZ_FOUR_LANE_HEIGHT)>6000);layouts++;
  }
 }
 /* The headline is white and the edge teal: the card reads as a status, not an error (no red). */
 key_fill();xz_ram_notice_two_lane(s,0,132);
 int white=0,teal=0,red=0;
 for(size_t i=0;i<sizeof px/sizeof *px;i++){white+=px[i]==0xffff;teal+=px[i]==(uint16_t)(((XZ_CALM_EDGE>>8)&0xf800)|((XZ_CALM_EDGE>>5)&0x7e0)|((XZ_CALM_EDGE>>3)&31));
  red+=(px[i]>>11)>24&&((px[i]>>5)&63)<20&&(px[i]&31)<10;}
 assert(white>300&&teal>200&&!red);

 /* The player's hub window, any size it comes in: the calm card fills it all. Too small: left alone. */
 static const int sizes[][2]={{304,60},{420,100},{200,30},{120,40},{536,240}};
 for(size_t k=0;k<sizeof sizes/sizeof *sizes;k++){
  key_fill();xz_hub_card_draw((struct xz_canvas){px,(size_t)sizes[k][0],sizes[k][0],sizes[k][1],0});
  int left=0,ink=0;
  for(int y=0;y<sizes[k][1];y++)for(int x=0;x<sizes[k][0];x++){uint16_t v=px[y*sizes[k][0]+x];left+=v==KEY;ink+=(v>>11)>=24&&((v>>5)&63)>=48&&(v&31)>=24;}
  assert(!left&&ink>60);
  assert(px[sizes[k][0]*sizes[k][1]]==KEY); /* nothing past the window */
 }
 key_fill();xz_hub_card_draw((struct xz_canvas){px,40,40,10,0});
 for(size_t i=0;i<sizeof px/sizeof *px;i++)assert(px[i]==KEY);
 printf("PASS calm cards: %d RAM card lanes in bounds, no red, hub window card fills any size\n",layouts);
 return 0;
}
