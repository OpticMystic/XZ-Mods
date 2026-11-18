#ifdef NDEBUG
#error Four-deck layout acceptance requires active assertions
#endif
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
static uint16_t pixels[540*268+8];
static uint16_t value(unsigned y,unsigned x){return (uint16_t)(y*127+x+1);}
static void fill(void){
 for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++)pixels[y*540+x]=x<536?value(y,x):0xbeef;
 for(unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++)pixels[i]=0xabcd;
}
int main(void){
 for(int order=0;order<XZ_FOUR_ORDER_COUNT;order++){
  struct xz_four_layout g;assert(xz_four_layout_get(order,&g));
  unsigned owner[268];for(unsigned y=0;y<268;y++)owner[y]=9;
  for(int d=0;d<4;d++){
   assert(g.lane_y[d]+XZ_FOUR_LANE_HEIGHT<=XZ_WAVE_HEIGHT);
   for(unsigned y=g.lane_y[d];y<g.lane_y[d]+XZ_FOUR_LANE_HEIGHT;y++){assert(owner[y]==9);owner[y]=(unsigned)d;}
  }
  unsigned gaps=0;for(unsigned y=0;y<268;y++)gaps+=owner[y]==9;
  assert(gaps==3*XZ_FOUR_GAP);
  for(unsigned y=0;y<268;y++){
   unsigned lane=999;int deck=xz_four_layout_row(&g,y,&lane);
   assert(deck==xz_four_layout_lane_at(&g,(int)y)&&(deck<0?owner[y]==9:owner[y]==(unsigned)deck));
   unsigned src=xz_four_layout_source_row(&g,y);
   if(deck==0||deck==1){
    assert(lane<132&&src==(unsigned)deck*136+lane);
    unsigned r=y-g.lane_y[deck];
    if(r<10)assert(lane==r);
    if(r>=XZ_FOUR_LANE_HEIGHT-10)assert(lane==132-(XZ_FOUR_LANE_HEIGHT-r));
   }else{
    assert(src==132);
    if(deck>=2)assert(lane<132);
   }
  }
  for(int d=0;d<4;d++){
   unsigned lane;xz_four_layout_row(&g,g.lane_y[d],&lane);assert(lane==0);
   xz_four_layout_row(&g,g.lane_y[d]+XZ_FOUR_LANE_HEIGHT-1,&lane);assert(lane==131);
  }
  for(int repeat=0;repeat<2;repeat++){
   fill();assert(xz_four_layout_pixels(pixels,540*268,540,order));
   for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++){
    unsigned src=xz_four_layout_source_row(&g,y);
    uint16_t expected=x>=536?0xbeef:src==132?0:value(src,x);
    assert(pixels[y*540+x]==expected);
   }
   for(unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++)assert(pixels[i]==0xabcd);
  }
 }
 struct xz_four_layout a;assert(xz_four_layout_get(XZ_FOUR_INTERLEAVED,&a));
 /* Native deck columns sit at screen y 18..159 and 161..302; the surface starts at y 18. */
 assert(a.lane_y[0]+XZ_FOUR_LANE_HEIGHT<=141&&a.lane_y[2]+XZ_FOUR_LANE_HEIGHT<=141);
 assert(a.lane_y[1]>=136&&a.lane_y[3]>=136&&a.lane_y[0]<a.lane_y[2]&&a.lane_y[1]<a.lane_y[3]);
 assert(a.lane_y[1]==136);
 struct xz_four_layout mixer;assert(xz_four_layout_get(XZ_FOUR_MIXER,&mixer));
 assert(mixer.lane_y[2]==0&&mixer.lane_y[0]==68&&mixer.lane_y[1]==136&&mixer.lane_y[3]==204);
 assert(XZ_FOUR_ORDER==XZ_FOUR_STACKED);
 struct xz_four_layout b;assert(xz_four_layout_get(XZ_FOUR_STACKED,&b));
 for(int d=0;d<3;d++)assert(b.lane_y[d]<b.lane_y[d+1]);
 assert(!xz_four_layout_get(-1,&a)&&!xz_four_layout_get(XZ_FOUR_ORDER_COUNT,&a)&&!xz_four_layout_get(0,NULL));
 assert(!xz_four_layout_pixels(pixels,10,540,0));
 assert(!xz_four_layout_pixels(pixels,540*268,530,0));
 assert(!xz_four_layout_pixels(pixels,SIZE_MAX,SIZE_MAX,0));
 assert(!xz_four_layout_pixels(pixels,540*268,540,XZ_FOUR_ORDER_COUNT));
 assert(!xz_four_layout_pixels(NULL,540*268,540,0));
 puts("PASS four-deck layout: all orders inside 536x268, no overlap, 4-row gaps, lane_at/source_row, in-place shrink, cleared DECK 3/4 lanes, guards");
}
