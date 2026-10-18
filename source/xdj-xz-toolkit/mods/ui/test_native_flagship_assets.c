#ifdef NDEBUG
#error FLAGSHIP acceptance requires assertions
#endif
#include "native_asset_theme.h"
#include "native_flagship_wave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t source[120*212],out[120*212+2],lut[65536];
static uint16_t wave[540*268],original[540*268],fast[540*268],wave_map[65536];
static const int sizes[][2]={{8,138},{120,41},{64,14},{120,31},{120,212},{104,22},{88,27},{16,26},{16,18},{16,23},
 {16,25},{24,35},{16,35},{8,25},{16,13},{16,11},{8,8},{8,6},{80,45},{88,10},{40,13},{48,10},{40,10},{400,172},{400,30}};
static void asset(unsigned id,int w,int h,uint16_t fill){
 for(int i=0;i<120*212;i++)source[i]=0xf81f;
 for(int i=0;i<w*h;i++)if(fill!=0xf81f&&(i%w)>=w/4&&(i%w)<w*3/4&&(i/w)>=h/4&&(i/w)<h*3/4)source[i]=fill;
 for(int i=0;i<120*212+2;i++)out[i]=0x1234;
 assert(xz_native_asset_theme_render(id,XZ_THEME_FLAGSHIP,source,w,h,w,out+1,w,w*h,lut,1,0xf81f,0)==2);
 assert(out[0]==0x1234&&out[1+w*h]==0x1234);
}
static unsigned count(int w,int h,uint16_t pixel){unsigned n=0;for(int i=0;i<w*h;i++)n+=out[1+i]==pixel;return n;}
static unsigned hue(uint16_t p){
 uint32_t c=xz_theme_rgb888(p);unsigned r=c>>16,g=(c>>8)&255,b=c&255;
 return b>r&&b>g?2:r>b&&g>b&&r-b<64?3:r>=g?0:1; /* blue, warm, green, cream */
}
int main(void){
 for(unsigned i=0;i<65536;i++)lut[i]=xz_native_palette_pixel(XZ_THEME_FLAGSHIP,(uint16_t)i);
 /* Same exact inventory as LCARS, so the shared ownership gates stay one list. */
 for(unsigned id=0;id<1600;id++)for(unsigned k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
  int w=sizes[k][0],h=sizes[k][1];
  assert(xz_flagship_asset_owned(id,w,h)==xz_lcars_asset_owned(id,w,h));
  assert(xz_native_asset_owned(XZ_THEME_FLAGSHIP,id,w,h)==xz_lcars_asset_owned(id,w,h));
  assert(!xz_native_asset_owned(XZ_THEME_ANALOG,id,w,h)&&!xz_native_asset_owned(0,id,w,h));
 }
 struct xz_theme_surface probe={out,16,16,26,0,0,16,26};
 assert(!xz_flagship_asset_draw(734,XZ_THEME_LCARS,probe)&&!xz_lcars_asset_draw(734,XZ_THEME_FLAGSHIP,probe));
 /* Every owned id renders, never touches its source, and keeps magenta where transparent. */
 for(unsigned id=600;id<1100;id++)for(unsigned k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
  int w=sizes[k][0],h=sizes[k][1];if(!xz_flagship_asset_owned(id,w,h))continue;
  asset(id,w,h,0xf81f);for(int i=0;i<w*h;i++)assert(source[i]==0xf81f);
  const struct xz_flagship_sprite *e=xz_flagship_sprite(id,w,h);
  int keyed=e->kind==XZ_FS_EDGE||e->kind==XZ_FS_DECK_LABEL||e->kind==XZ_FS_DIGIT||e->kind==XZ_FS_GLYPH||e->kind==XZ_FS_MARK||
   e->kind==XZ_FS_DOT||e->kind==XZ_FS_BPM_BOX||e->kind==XZ_FS_QUANTIZE||e->kind==XZ_FS_WORD;
  unsigned key=count(w,h,0xf81f);
  if(keyed)assert(key>0&&(e->kind==XZ_FS_EDGE?key==(unsigned)(w*h):key<(unsigned)(w*h)));
  else assert(key==0);
 }
 /* Digits keep a clear transparent gutter beside each numeral. */
 for(unsigned id=838;id<=847;id++){asset(id,24,35,0xf81f);for(int row=0;row<35;row++)assert(out[1+row*24]==0xf81f&&out[1+row*24+23]==0xf81f);}
 asset(734,16,26,0xf81f);assert(count(16,26,0xffff)>10);
 /* Ink follows the stock sprite's own state: grey stays dim, orange becomes the tempo red. */
 asset(734,16,26,0x8410);assert(!count(16,26,0xffff)&&count(16,26,xz_theme_rgb565(XZ_FLAGSHIP_DIM))>10);
 asset(734,16,26,0xfc00);assert(count(16,26,xz_theme_rgb565(XZ_FLAGSHIP_RED))>10);
 asset(956,40,13,0xf81f);{unsigned red=count(40,13,xz_theme_rgb565(XZ_FLAGSHIP_RED));assert(red>300&&520-red>15);}
 asset(895,88,10,0xf81f);assert(count(88,10,xz_theme_rgb565(XZ_FLAGSHIP_RED))>10);
 asset(887,88,10,0xf81f);assert(count(88,10,xz_theme_rgb565(XZ_FLAGSHIP_DIM))>10&&!count(88,10,xz_theme_rgb565(XZ_FLAGSHIP_RED)));
 asset(883,80,45,0xf81f);assert(out[1+0*80+10]==0xffff&&out[1+20*80+40]==0&&out[1+20*80+78]==0xf81f);
 asset(884,80,45,0xf81f);assert(out[1+0*80+10]==xz_theme_rgb565(XZ_FLAGSHIP_RED)&&out[1+38*80+3]==xz_theme_rgb565(XZ_FLAGSHIP_RED));
 asset(1027,120,41,0xf81f);assert(out[1]==0xffff&&out[1+20*120+60]==xz_theme_rgb565(XZ_FLAGSHIP_BLUE));
 asset(1026,120,41,0xf81f);assert(out[1]==0xffff&&out[1+20*120+60]==xz_theme_rgb565(XZ_FLAGSHIP_CHROME));
 asset(975,120,41,0xf81f);assert(count(120,41,0xffff)>20&&count(120,41,0xf81f)>3000);
 asset(969,120,31,0xf81f);for(int i=0;i<120*9;i++)assert(out[1+i]==0); /* Clear of overlay controls at y0..8. */
 assert(count(120,31,0xffff)>10);
 asset(970,120,212,0xf81f);assert(out[1+100*120+60]==xz_theme_rgb565(XZ_FLAGSHIP_CHROME)&&out[1+100*120]==xz_theme_rgb565(XZ_FLAGSHIP_RAIL));
 asset(1004,104,22,0xf81f);assert(out[1]==xz_theme_rgb565(XZ_FLAGSHIP_RAIL)&&count(104,22,0xffff)>10);
 /* Owned ids under another key, and unowned ids at an owned size, stay on the LUT. */
 for(int i=0;i<16*26;i++)source[i]=(uint16_t)(i*977);
 assert(xz_native_asset_theme_render(734,XZ_THEME_FLAGSHIP,source,16,26,16,out+1,16,16*26,lut,1,0,0)==1);
 for(int i=0;i<16*26;i++)assert(out[1+i]==(source[i]==0?0:lut[source[i]]));
 assert(xz_native_asset_theme_render(500,XZ_THEME_FLAGSHIP,source,16,26,16,out+1,16,16*26,lut,1,0xf81f,0)==1);
 for(int i=0;i<16*26;i++)assert(out[1+i]==(source[i]==0xf81f?0xf81f:lut[source[i]]));
 /* Three bands, three hues; the grey lane ground and black stay black. */
 uint16_t low=xz_flagship_wave_pixel(0x001f),mid=xz_flagship_wave_pixel(0xf800),high=xz_flagship_wave_pixel(0x07e0);
 assert(low==xz_theme_rgb565(XZ_FLAGSHIP_WAVE_LOW)&&mid==xz_theme_rgb565(XZ_FLAGSHIP_WAVE_MID)&&high==xz_theme_rgb565(XZ_FLAGSHIP_WAVE_HIGH));
 assert(hue(low)==2&&hue(mid)==0&&hue(high)==3);
 assert(xz_flagship_wave_pixel(0xffff)==xz_theme_rgb565(XZ_FLAGSHIP_WAVE_HIGH));
 assert(xz_flagship_wave_pixel(0x000f)==xz_theme_rgb565(xz_theme_mix(0,XZ_FLAGSHIP_WAVE_LOW_DEEP,(15*255/31)*256/255)));
 assert(!xz_flagship_wave_pixel(0)&&!xz_flagship_wave_pixel(0x3186));
 xz_flagship_wave_maps(wave_map);
 for(int deck=-1;deck<=1;deck++){
  unsigned w=deck<0?536:300,h=deck<0?268:34;
  for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++)wave[y*540+x]=(uint16_t)((x*137+y*89)&65535);
  wave[10*540+268]=wave[146*540+269]=wave[60*540+268]=0xffe0;
  memcpy(original,wave,sizeof(wave));memcpy(fast,wave,sizeof(wave));
  xz_flagship_wave_mapped(fast,w,h,540,deck,lut,wave_map);xz_flagship_wave_pixels(wave,w,h,540,deck);
  assert(!memcmp(fast,wave,sizeof(wave)));
  for(unsigned y=0;y<268;y++)for(unsigned x=0;x<540;x++){
   unsigned row=y>=136?y-136:y;uint16_t p=original[y*540+x],q=wave[y*540+x];
   if(x>=w||y>=h)assert(q==p);
   else if(deck<0&&(x==268||x==269)&&p==0xffe0)assert(q==0xffff);
   else if(deck<0&&(row<10||row>=122))assert(q==lut[p]);
   else assert(q==xz_flagship_wave_pixel(p));
  }
 }
 puts("PASS FLAGSHIP sprite inventory, keyed glyphs, state ink, deck tags, FX column, three-band wave and protected cue bands");
}
