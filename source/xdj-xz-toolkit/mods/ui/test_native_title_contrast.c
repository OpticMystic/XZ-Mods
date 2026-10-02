#include "native_asset_theme.h"
#include "native_palette.h"
#include "native_title_theme.h"
#include <assert.h>
#include <stdio.h>
static uint16_t source[400*172],dest[400*172],lut[65536];
static unsigned luma(uint16_t pixel){uint32_t c=xz_theme_rgb888(pixel);return ((c>>16)*299+((c>>8)&255)*587+(c&255)*114)/1000;}
int main(void){
 int failures=0;
 const unsigned ids[]={649,687,725,726};
 for(int t=1;t<XZ_THEME_COUNT;t++)for(unsigned k=0;k<4;k++){
  int h=k<2?172:70;
  for(unsigned i=0;i<400u*(unsigned)h;i++)source[i]=0x118c; /* Actual untagged title panel paper. */
  for(unsigned i=0;i<65536;i++)lut[i]=xz_native_palette_pixel(t,(uint16_t)i);
  assert(xz_native_asset_theme_render(ids[k],t,source,400,h,400,dest,400,400u*(unsigned)h,lut,1,0xf81f,0));
  uint32_t title_paper,title_ink;
  uint16_t bg=dest[8*400+80],ink=xz_native_title_colors(t,k&1,&title_paper,&title_ink)?xz_theme_rgb565(title_ink):lut[0xffff];
  unsigned a=luma(bg),b=luma(ink),difference=a>b?a-b:b-a;
  if(difference<70){printf("FAIL title contrast theme=%d asset=%u bg=%04x ink=%04x difference=%u\n",t,ids[k],bg,ink,difference);failures++;}
 }
 fflush(stdout);assert(!failures);puts("PASS untagged native title ink/paper contrast on both decks and layouts across all themes");
}
