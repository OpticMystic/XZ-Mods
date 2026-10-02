#include "native_asset_theme.h"
#include "native_palette.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Native glass acceptance requires active assertions
#endif
static uint16_t source[400*172], target[404*172+2], lut[65536];
int main(void){
 for(int theme=19;theme<=20;theme++){
  for(unsigned n=0;n<65536;n++)lut[n]=xz_native_palette_pixel(theme,(uint16_t)n);
  memset(source,0,sizeof(source));
  for(int y=0;y<172;y++)source[y*400+399]=0xf81f;
  for(unsigned n=0;n<sizeof(target)/sizeof(*target);n++)target[n]=0x1234;
  int result=xz_native_asset_theme_render(649,theme,source,400,172,400,target+1,404,404*172,lut,1,0xf81f,0);
  assert(result==2); /* Main artwork must have a material, not only a color LUT. */
  assert(target[0]==0x1234&&target[404*172+1]==0x1234);
  for(int y=0;y<172;y++){
   assert(target[1+y*404+399]==0xf81f);
   for(int x=400;x<404;x++)assert(target[1+y*404+x]==0x1234);
  }
  uint16_t paper=xz_theme_rgb565(xz_theme_palette(theme)->bg);
  assert(target[1+65*404+170]==paper); /* time digits */
  assert(target[1+75*404+350]==paper); /* tempo */
  assert(target[1+120*404+150]==paper); /* overview and cues */
  assert(target[1+10*404+100]!=target[1+168*404+100]); /* top/bottom material light */
  assert(source[0]==0&&source[399]==0xf81f);
  uint16_t idle[128*36],focused[128*36],disabled[160*55];
  assert(xz_native_asset_theme_render(79,theme,source,128,36,400,idle,128,128*36,lut,0,0,0)==2);
  assert(xz_native_asset_theme_render(77,theme,source,128,36,400,focused,128,128*36,lut,0,0,0)==2);
  assert(memcmp(idle,focused,sizeof(idle))!=0);
  assert(xz_native_asset_theme_render(1318,theme,source,160,55,400,disabled,160,160*55,lut,0,0,0)==2);
  assert(xz_native_asset_theme_render(649,theme,source,400,171,400,target,404,404*172,lut,0,0,0)==1);
  assert(xz_native_asset_theme_render(22,theme,source,128,36,400,idle,128,128*36,lut,0,0,0)==1);
 }
 puts("PASS native glass structure, readable data wells, source keys, stride guards and focus states");
}
