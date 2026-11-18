#ifdef NDEBUG
#error Wave viewport acceptance requires active assertions
#endif
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t pixels[540*268+8];
/* The STEMS button, both interfaces: two lanes step the layout as before; four lanes show or hide the stem
   buttons with one press each in MODERN and change nothing in CLASSIC, whose four lanes have no stem rows. */
static void stems_button(void) {
 int classic=XZ_STEM_LAYOUT_BELOW,modern=XZ_MODERN_STEMS_PADS,hidden=0;
 for(int i=1;i<=2*XZ_MODERN_STEMS_COUNT;i++){
  assert(xz_stems_press(1,0,hidden)==XZ_STEMS_PRESS_STEP&&!xz_stems_press_notice(XZ_STEMS_PRESS_STEP));
  modern=xz_modern_stems_next(modern);assert(modern==xz_modern_stems_order[i%XZ_MODERN_STEMS_COUNT]);
  assert(xz_stems_press(0,0,hidden)==XZ_STEMS_PRESS_STEP);
  classic=xz_wave_layout_next(classic);assert(classic==(XZ_STEM_LAYOUT_BELOW+i)%XZ_STEM_LAYOUT_COUNT);
 }
 for(int i=0;i<6;i++){
  enum xz_stems_press p=xz_stems_press(1,1,hidden);
  assert(p==(hidden?XZ_STEMS_PRESS_SHOW:XZ_STEMS_PRESS_HIDE));
  assert(!strcmp(xz_stems_press_notice(p),hidden?"STEM BUTTONS SHOWN":"STEM BUTTONS HIDDEN"));
  hidden=p==XZ_STEMS_PRESS_HIDE;
  assert(hidden==!(i&1));
  /* CLASSIC four lanes: no change, the same sentence every press, and a dark STEMS lamp. */
  assert(xz_stems_press(0,1,hidden)==XZ_STEMS_PRESS_TWO_ONLY);
  assert(!strcmp(xz_stems_press_notice(XZ_STEMS_PRESS_TWO_ONLY),"STEM BUTTONS SHOW IN 2 DECKS VIEW"));
 }
 for(int layout=0;layout<XZ_STEM_LAYOUT_COUNT;layout++){
  assert(xz_classic_stems_lamp(layout,0)==layout&&xz_classic_stems_lamp(layout,1)==XZ_STEM_LAYOUT_HIDDEN);
 }
}
static void fill(void) {
 for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++)pixels[y*540+x]=x<536?(uint16_t)(y*127+x):0xbeef;
 for(unsigned i=540*268;i<sizeof(pixels)/sizeof(*pixels);i++)pixels[i]=0xabcd;
}
int main(void) {
 stems_button();
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
  /* One deck only (2 DECKS with the other side on a software deck): its half matches the full transform,
     the other half is untouched. */
  static uint16_t full[540*268];
  fill();assert(xz_wave_layout_pixels(pixels,540*268,540,mode));memcpy(full,pixels,sizeof full);
  for(unsigned decks=0;decks<4;decks++){
   fill();assert(xz_wave_layout_pixels_decks(pixels,540*268,540,mode,decks));
   for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++){
    int deck=y>=132;
    assert(pixels[y*540+x]==((decks&(1u<<deck))?full[y*540+x]:x<536?(uint16_t)(y*127+x):0xbeef));
   }
  }
  mode=xz_wave_layout_next(mode);
 }
 assert(mode==XZ_STEM_LAYOUT_BELOW);
 assert(!xz_wave_layout_pixels_decks(pixels,10,540,1,1));
 assert(!xz_wave_layout_get(-1,&(struct xz_wave_layout){0}));
 assert(!xz_wave_layout_get(5,&(struct xz_wave_layout){0}));
 assert(!xz_wave_layout_pixels(pixels,10,540,1));
 assert(!xz_wave_layout_pixels(pixels,540*268,530,1));
 assert(!xz_wave_layout_pixels(pixels,SIZE_MAX,SIZE_MAX,1));
 assert(!xz_wave_layout_pixels(pixels,540*268,540,5));
 puts("PASS five layouts, larger half-height waves, downward relocation, fixed cue bands, inverse mapping and stride guards; one deck's half alone; STEMS steps two lanes, shows or hides four");
}
