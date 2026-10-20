#include "wave_viewport.h"
#include <string.h>
unsigned xz_wave_lane_row(unsigned row,unsigned height) {
 if(row<10)return row;
 if(row>=height-10)return 132-(height-row);
 return 10+(row-10)*112/(height-20);
}
unsigned xz_wave_layout_source_row(const struct xz_wave_layout *g,unsigned row) {
 int deck=xz_wave_layout_lane_at(g,(int)row);
 if(deck>=0)return (deck?136:0)+xz_wave_lane_row(row-g->lane_y[deck],g->lane_height);
 return 132;
}
int xz_wave_layout_pixels(uint16_t *pixels,size_t count,size_t stride,int mode) {
 struct xz_wave_layout g;
 if(!pixels||!xz_wave_layout_get(mode,&g)||stride<XZ_WAVE_WIDTH||
    stride>SIZE_MAX/sizeof(*pixels)/XZ_WAVE_HEIGHT||count<stride*XZ_WAVE_HEIGHT)return 0;
 if(mode==XZ_STEM_LAYOUT_HIDDEN)return 1;
 /* Below layouts move up. The outer top lane moves down and must copy bottom
    first; its last ten cue rows stay in place. No extra full-frame buffer. */
 for(int deck=0;deck<2;deck++){
  int downward=g.lane_y[deck]>(deck?136u:0u);
  for(unsigned i=0;i<g.lane_height;i++){
   unsigned y=g.lane_y[deck]+(downward?g.lane_height-1-i:i);
   unsigned src=xz_wave_layout_source_row(&g,y);
   memmove(pixels+y*stride,pixels+src*stride,XZ_WAVE_WIDTH*sizeof(*pixels));
  }
 }
 for(unsigned y=0;y<XZ_WAVE_HEIGHT;y++){
  uint16_t *row=pixels+y*stride;
  if(xz_wave_layout_lane_at(&g,(int)y)<0)memset(row,0,XZ_WAVE_WIDTH*sizeof(*pixels));
 }
 return 1;
}
int xz_four_layout_row(const struct xz_four_layout *g,unsigned row,unsigned *lane_row) {
 int deck=xz_four_layout_lane_at(g,(int)row);
 if(deck>=0&&lane_row)*lane_row=xz_wave_lane_row(row-g->lane_y[deck],XZ_FOUR_LANE_HEIGHT);
 return deck;
}
unsigned xz_four_layout_source_row(const struct xz_four_layout *g,unsigned row) {
 unsigned lane;int deck=xz_four_layout_row(g,row,&lane);
 return deck==0||deck==1?(unsigned)deck*136+lane:132;
}
int xz_four_layout_pixels(uint16_t *pixels,size_t count,size_t stride,int order) {
 struct xz_four_layout g;
 if(!pixels||!xz_four_layout_get(order,&g)||stride<XZ_WAVE_WIDTH||
    stride>SIZE_MAX/sizeof(*pixels)/XZ_WAVE_HEIGHT||count<stride*XZ_WAVE_HEIGHT)return 0;
 /* Every native row lands at or above its source, so one top-down pass reads each source row
    before anything overwrites it. No extra full-frame buffer. */
 for(unsigned y=0;y<XZ_WAVE_HEIGHT;y++){
  uint16_t *row=pixels+y*stride;
  unsigned src=xz_four_layout_source_row(&g,y);
  if(src==132)memset(row,0,XZ_WAVE_WIDTH*sizeof(*pixels));
  else if(src!=y)memmove(row,pixels+src*stride,XZ_WAVE_WIDTH*sizeof(*pixels));
 }
 return 1;
}
