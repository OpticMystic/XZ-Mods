#ifdef NDEBUG
#error Native retro tests require assertions
#endif
#include "native_retro_theme.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t pixels[804*174+2],saved[804*174+2],normal[128*41];
static void render(unsigned id,int theme,int w,int h){
 int stride=w+4;size_t count=(size_t)stride*h;
 for(size_t i=0;i<count+2;i++)pixels[i]=0x1234;
 struct xz_theme_surface s={pixels+1,(size_t)stride,w,h,0,0,w,h};
 assert(xz_native_retro_art(id,theme,xz_native_asset_role(id,w,h),s,w,h));
 assert(pixels[0]==0x1234&&pixels[count+1]==0x1234);
 for(int y=0;y<h;y++)for(int x=w;x<stride;x++)assert(pixels[1+y*stride+x]==0x1234);
}
static uint16_t at(int x,int y,int w){return pixels[1+y*(w+4)+x];}
int main(void){
 const unsigned ids[]={649,687,650,688,77,78,79,80,81,87,88,106,107,108,109,1316,1317,1318,1459,725,726,969,1026,1027,1028};
 const int widths[]={400,400,400,400,128,128,128,128,128,424,424,248,248,248,248,160,160,160,784,400,400,120,120,120,120};
 const int heights[]={172,172,30,30,36,36,36,36,36,36,36,36,36,36,36,55,55,55,30,70,70,31,41,41,41};
 _Static_assert(sizeof(ids)/sizeof(*ids)==sizeof(widths)/sizeof(*widths),"asset widths");
 _Static_assert(sizeof(ids)/sizeof(*ids)==sizeof(heights)/sizeof(*heights),"asset heights");
 for(int theme=7;theme<=18;theme++){
  for(unsigned n=0;n<sizeof(ids)/sizeof(ids[0]);n++){
   int w=widths[n],h=heights[n];render(ids[n],theme,w,h);
   if(ids[n]==649||ids[n]==687){
    uint16_t paper=xz_theme_rgb565(xz_theme_palette(theme)->bg);
    assert(at(150,14,w)==xz_theme_rgb565(xz_theme(theme)->title)); /* Untagged title. */
    assert(at(180,75,w)==paper); /* Time counter. */
    assert(at(160,128,w)==paper); /* Summary wave. */
    assert(at(396,84,w)==paper&&at(396,146,w)==paper); /* Tempo/pitch right edge. */
    assert(at(400-1,36,w)!=0x1234);
   }
   if(ids[n]==1026)memcpy(normal,pixels,sizeof(normal));
   if(ids[n]==1027||ids[n]==1028)assert(memcmp(normal,pixels,sizeof(normal)));
  }
  render(1316,theme,160,55);memcpy(saved,pixels,sizeof(pixels));
  render(1317,theme,160,55);assert(memcmp(saved,pixels,164*55*2));
  render(1318,theme,160,55);assert(memcmp(saved,pixels,164*55*2));
  render(649,theme,400,172);memcpy(saved,pixels,sizeof(pixels));
  struct xz_theme_surface s={pixels+1,404,400,172,0,0,400,172};
  assert(!xz_native_retro_art(500,theme,xz_native_asset_role(500,400,172),s,400,172));
  assert(!memcmp(saved,pixels,sizeof(pixels)));
  assert(!xz_native_retro_art(649,theme,xz_native_asset_role(649,400,172),s,399,172));
  assert(!memcmp(saved,pixels,sizeof(pixels)));
  /* A clipped dirty region must not overwrite adjacent native content. */
  for(unsigned n=0;n<sizeof(pixels)/sizeof(*pixels);n++)pixels[n]=0x1234;
  s.clip_x=140;s.clip_y=60;s.clip_w=20;s.clip_h=15;
  assert(xz_native_retro_art(649,theme,xz_native_asset_role(649,400,172),s,400,172));
  for(int y=0;y<172;y++)for(int x=0;x<404;x++)if(x<140||x>=160||y<60||y>=75)assert(at(x,y,400)==0x1234);
 }
 render(649,17,400,172);assert(at(30,30,400)==xz_theme_rgb565(xz_theme(17)->hi2));
 render(649,8,400,172);assert(at(326,168,400)==xz_theme_rgb565(xz_theme(8)->palette.stem[0]));
 render(77,9,128,36);assert(at(8,0,128)==xz_theme_rgb565(xz_theme(9)->hi));
 render(649,7,400,172);assert(at(128,75,400)==xz_theme_rgb565(xz_theme(7)->palette.ink));
 puts("PASS retro7-18 exact asset gates, clipped writes, guarded strides, native text wells and appearance contrast");
}
