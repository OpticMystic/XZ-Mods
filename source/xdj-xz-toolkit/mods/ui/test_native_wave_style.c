#ifdef NDEBUG
#error Native wave style tests require assertions
#endif
#include "native_wave_style.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static unsigned brightness(uint16_t p){
 uint32_t c=xz_theme_rgb888(p);return 77*(c>>16)+150*((c>>8)&255)+29*(c&255);
}
int main(void){
 for(unsigned p=0;p<65536;p++){
  assert(xz_native_wave_style_pixel(0,(uint16_t)p)==p);
  assert(xz_native_wave_style_pixel(-1,(uint16_t)p)==p);
  assert(xz_native_wave_style_pixel(XZ_THEME_COUNT,(uint16_t)p)==p);
  assert(xz_native_wave_style_pixel(17,(uint16_t)p)==xz_native_palette_pixel(7,(uint16_t)p));
  assert(xz_native_wave_style_pixel(18,(uint16_t)p)==xz_native_palette_pixel(12,(uint16_t)p));
  for(int theme=1;theme<XZ_THEME_COUNT;theme++)if(theme!=17&&theme!=18)
   assert(xz_native_wave_style_pixel(theme,(uint16_t)p)==xz_native_palette_pixel(theme,(uint16_t)p));
 }
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  uint16_t ink=xz_native_wave_style_pixel(theme,0xffff),red=xz_native_wave_style_pixel(theme,0xf800);
  uint16_t green=xz_native_wave_style_pixel(theme,0x07e0),blue=xz_native_wave_style_pixel(theme,0x001f);
  if(theme==17||theme==18)assert(red!=green&&green!=blue&&red!=blue);
  unsigned changed=0;
  for(int y=0;y<268;y++)for(int x=0;x<536;x++){
   uint16_t paper=xz_native_wave_style_paper_at(theme,x,y,536,268);
   assert(paper!=ink);
   if(theme==11||theme==19)assert(brightness(paper)>brightness(ink)+32000);
   if(theme==13||theme==16||theme==20)assert(brightness(ink)>brightness(paper)+30000);
   if(!xz_native_wave_style_has_paper(theme)||theme==17||theme==18)
    assert(paper==xz_native_wave_style_pixel(theme,0));
   changed+=paper!=xz_native_wave_style_paper_at(theme,0,0,536,268);
  }
  if(xz_native_wave_style_has_paper(theme)&&theme!=17&&theme!=18)assert(changed>1000);
  assert(xz_native_wave_style_paper_at(theme,2,2,0,268)==xz_native_wave_style_pixel(theme,0));
  assert(xz_native_wave_style_paper_at(theme,2,2,536,-1)==xz_native_wave_style_pixel(theme,0));
  (void)xz_native_wave_style_paper_at(theme,INT_MAX,INT_MAX,INT_MAX,INT_MAX);
  (void)xz_native_wave_style_paper_at(theme,INT_MIN,INT_MIN,1,1);
 }
 assert(!xz_native_wave_style_has_paper(-1)&&!xz_native_wave_style_has_paper(24));
 assert(xz_native_wave_style_position(INT_MAX,INT_MAX)==256);
 assert(xz_native_wave_style_position(INT_MIN,INT_MAX)==0);
 assert(xz_native_wave_style_position(1,1)==0);
 puts("PASS wave LUT equivalence, Original/unknown passthrough, DMG band separation, restrained paper contrast and bounded geometry");
}
