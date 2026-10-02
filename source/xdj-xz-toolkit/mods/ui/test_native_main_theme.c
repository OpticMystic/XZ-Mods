#ifdef NDEBUG
#error Main-screen acceptance requires assertions
#endif
#include "native_asset_theme.h"
#include "native_palette.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t normal_deck[124*41];
static uint16_t source[564*172],saved[564*172],dest[564*172+2],lut[65536];
int main(void){
 const unsigned ids[]={649,687,650,688,1026,1027,1028,1487,725,726,969};
 const int widths[]={400,400,400,400,120,120,120,560,400,400,120},heights[]={172,172,30,30,41,41,41,92,70,70,31};
 for(int theme=XZ_THEME_LCARS;theme<=XZ_THEME_ANALOG;theme++){
  for(unsigned p=0;p<65536;p++)lut[p]=xz_native_palette_pixel(theme,(uint16_t)p);
  for(unsigned k=0;k<sizeof(ids)/sizeof(ids[0]);k++){
   int w=widths[k],h=heights[k],stride=w+4;size_t count=(size_t)stride*h;
   memset(source,0,sizeof(source));source[0]=source[(h-1)*stride+w-1]=0xf81f;memcpy(saved,source,sizeof(source));
   for(size_t p=0;p<count+2;p++)dest[p]=0x1234;
   assert(xz_native_asset_theme_render(ids[k],theme,source,w,h,stride,dest+1,stride,count,lut,1,0xf81f,0)==2);
   assert(dest[0]==0x1234&&dest[count+1]==0x1234&&!memcmp(source,saved,sizeof(source)));
   int owned=theme==XZ_THEME_LCARS&&xz_lcars_asset_owned(ids[k],w,h);
   if(!owned)assert(dest[1]==0xf81f&&dest[1+(h-1)*stride+w-1]==0xf81f);
   unsigned artwork=0;
   for(int y=0;y<h;y++)for(int x=0;x<stride;x++){
    if(x>=w)assert(dest[1+y*stride+x]==0x1234);
    else artwork+=dest[1+y*stride+x]!=lut[source[y*stride+x]];
   }
   if(!owned)assert(artwork>100); /* Proves structural artwork beyond the palette LUT. */
   if(ids[k]==649||ids[k]==687){
    assert(dest[1+75*stride+180]==xz_theme_rgb565(xz_theme_palette(theme)->bg)); /* Native time digits. */
    assert(dest[1+128*stride+160]==xz_theme_rgb565(xz_theme_palette(theme)->bg)); /* Native overview. */
    if(theme==XZ_THEME_LCARS){
     uint16_t rail=xz_theme_rgb565(ids[k]==687?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER);
     assert(dest[1+32*stride+160]==rail); /* The title and elbow remain connected. */
     assert(dest[1+169*stride+160]==rail&&dest[1+169*stride+36]==0);
     /* Full tempo extent and overview/cue gutters cannot be narrowed by rails. */
     for(int y=43;y<98;y++)for(int x=310;x<400;x++)assert(dest[1+y*stride+x]==0);
     for(int y=110;y<150;y++)for(int x=0;x<12;x++)assert(dest[1+y*stride+x]==0);
    }
   }
   if(theme==XZ_THEME_LCARS&&(ids[k]==725||ids[k]==726))
    assert(dest[1+8*stride+80]==xz_theme_rgb565(ids[k]==726?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER));
   if(ids[k]==1026)memcpy(normal_deck,dest+1,sizeof(normal_deck));
   if(ids[k]==1028&&theme!=XZ_THEME_LCARS)assert(memcmp(normal_deck,dest+1,sizeof(normal_deck)));
   /* The same dimensions at an unqualified ID must remain plain LUT. */
   assert(xz_native_asset_theme_render(500,theme,source,w,h,stride,dest+1,stride,count,lut,1,0xf81f,0)==1);
   for(int y=0;y<h;y++)for(int x=0;x<w;x++)assert(dest[1+y*stride+x]==(source[y*stride+x]==0xf81f?0xf81f:lut[source[y*stride+x]]));
  }
 }
 puts("PASS main-screen structural artwork, exact asset gates, text/overview wells, keys, strides and immutable source");
}
