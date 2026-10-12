#ifdef NDEBUG
#error Wave viewport acceptance requires active assertions
#endif
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
static uint16_t pixels[540*268+8];
static void fill(void) {
 for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++)pixels[y*540+x]=x<536?(uint16_t)(y*127+x):0xbeef;
 for(unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++)pixels[i]=0xabcd;
}
int main(void) {
 int mode=XZ_STEM_LAYOUT_BELOW;
 const unsigned heights[5]={0,48,24,48,24};
 const unsigned first_wave[5]={0,0,0,48,24},second_wave[5]={136,132,132,136,136};
 const unsigned first_button[5]={132,84,108,0,0},second_button[5]={264,216,240,220,244};
 for(int cycle=0;cycle<10;cycle++){
  struct xz_wave_layout g;assert(xz_wave_layout_get(mode,&g));
  assert(g.control_height==heights[mode]&&g.lane_height==132-heights[mode]);
  assert(g.lane_y[0]==first_wave[mode]&&g.lane_y[1]==second_wave[mode]);
  assert(g.control_y[0]==first_button[mode]&&g.control_y[1]==second_button[mode]);
  for(int deck=0;deck<2;deck++)for(unsigned y=0;y<10;y++){
   assert(xz_wave_layout_source_row(&g,g.lane_y[deck]+y)==(deck?136u:0u)+y);
   assert(xz_wave_layout_source_row(&g,g.lane_y[deck]+g.lane_height-10+y)==(deck?136u:0u)+122+y);
  }
  for(int repeat=0;repeat<3;repeat++){
   fill();assert(xz_wave_layout_pixels(pixels,540*268,540,mode));
   for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++){
    int deck=xz_wave_layout_lane_at(&g,(int)y);
    uint16_t expected=x>=536?0xbeef:mode==0?(uint16_t)(y*127+x):deck<0?0:
      (uint16_t)(xz_wave_layout_source_row(&g,y)*127+x);
    assert(pixels[y*540+x]==expected);
   }
   for(unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++)assert(pixels[i]==0xabcd);
  }
  mode=xz_wave_layout_next(mode);
 }
 assert(mode==XZ_STEM_LAYOUT_BELOW);
 assert(!xz_wave_layout_get(-1,&(struct xz_wave_layout){0}));
 assert(!xz_wave_layout_get(5,&(struct xz_wave_layout){0}));
 assert(!xz_wave_layout_pixels(pixels,10,540,1));
 assert(!xz_wave_layout_pixels(pixels,540*268,530,1));
 assert(!xz_wave_layout_pixels(pixels,SIZE_MAX,SIZE_MAX,1));
 assert(!xz_wave_layout_pixels(pixels,540*268,540,5));
 puts("PASS five layouts, larger half-height waves, downward relocation, fixed cue bands, inverse mapping and stride guards");
}
