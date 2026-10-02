#ifdef NDEBUG
#error LCARS acceptance requires assertions
#endif
#include "native_asset_theme.h"
#include "native_lcars_wave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t source[120*212],out[120*212+2],expected[120*212],lut[65536];
static uint16_t wave[540*268],original[540*268],fast[540*268],wave_maps[2*65536];
static void asset(unsigned id,int w,int h){
 for(int i=0;i<120*212;i++)source[i]=0xf81f;
 for(int i=0;i<120*212+2;i++)out[i]=0x1234;
 assert(xz_native_asset_theme_render(id,XZ_THEME_LCARS,source,w,h,w,out+1,w,w*h,lut,1,0xf81f,0)==2);
 assert(out[0]==0x1234&&out[1+w*h]==0x1234);
 for(int i=0;i<w*h;i++)assert(source[i]==0xf81f);
}
int main(void){
 /* Native saturation is independent of the established MODS menu palette. */
 assert(xz_theme_palette(XZ_THEME_LCARS)->ink==0xff7800);
 assert((XZ_LCARS_NATIVE_VIOLET&255)==255&&((XZ_LCARS_NATIVE_VIOLET>>8)&255)<100);
 assert((XZ_LCARS_NATIVE_AMBER>>16)==255&&(XZ_LCARS_NATIVE_AMBER&255)<32);
 unsigned char descriptor[68];memset(descriptor,0x55,sizeof(descriptor));xz_lcars_bitmap_mask(descriptor);
 assert(descriptor[24]==1&&!descriptor[25]&&!descriptor[26]&&!descriptor[27]);
 assert(descriptor[28]==255&&!descriptor[29]&&descriptor[30]==255);
 for(int i=0;i<68;i++)if(i<24||i>30)assert(descriptor[i]==0x55);
 assert(xz_lcars_title_pixel(0xffff,XZ_LCARS_NATIVE_AMBER,0,0)==0);
 assert(xz_lcars_title_pixel(0,XZ_LCARS_NATIVE_AMBER,0,0)==xz_theme_rgb565(XZ_LCARS_NATIVE_AMBER));
 assert(xz_lcars_title_pixel(0xf81f,XZ_LCARS_NATIVE_AMBER,1,0xf81f)==0xf81f);
 assert(xz_lcars_title_pixel(0xffff,XZ_LCARS_NATIVE_AMBER,1,0)!=0);
 assert(xz_lcars_mask_allowed(1,0,0));assert(xz_lcars_mask_allowed(0,1,0xf81f));
 assert(xz_lcars_mask_allowed(0,0,0)&&xz_lcars_mask_allowed(0,1,0)&&!xz_lcars_mask_allowed(1,-1,0));
 for(unsigned i=0;i<65536;i++)lut[i]=xz_native_palette_pixel(XZ_THEME_LCARS,(uint16_t)i);
 asset(640,64,14);assert(out[1]!=0xf81f); /* Original DECK ornaments no longer stencil new text. */
 assert(xz_native_asset_theme_render(640,0,source,64,14,64,out+1,64,64*14,NULL,1,0xf81f,0)==1);
 assert(!memcmp(source,out+1,64*14*2));
 assert(xz_native_asset_theme_render(640,XZ_THEME_LCARS,source,64,14,64,out+1,64,64*14,lut,1,0,0)==1);
 asset(732,16,13);memcpy(expected,out+1,16*13*2);asset(851,16,13);assert(!memcmp(expected,out+1,16*13*2));
 asset(765,16,18);memcpy(expected,out+1,16*18*2);asset(827,16,18);assert(!memcmp(expected,out+1,16*18*2));
 asset(849,16,11);asset(816,16,25);
 /* Regenerated figures stay in their native cells, with a clear outer gutter. */
 for(unsigned id=838;id<=847;id++){
  asset(id,24,35);
  for(int row=0;row<35;row++)assert(out[1+row*24]==0xf81f&&out[1+row*24+23]==0xf81f);
 }
 asset(890,88,10);struct xz_theme_surface s={expected,88,88,10,0,0,88,10};
 xz_lcars_label(s,"QUANTIZE 1",0,1,0x806020,0,0);assert(!memcmp(expected,out+1,88*10*2));
 asset(888,88,10);xz_lcars_label(s,"QUANTIZE 4",0,1,0x806020,0,0);assert(!memcmp(expected,out+1,88*10*2));
 asset(1068,88,27);s=(struct xz_theme_surface){expected,88,88,27,0,0,88,27};
 xz_lcars_label(s,"VINYL BRAKE",0,1,XZ_LCARS_NATIVE_AMBER,0,1);assert(!memcmp(expected,out+1,88*27*2));
 asset(970,120,212);for(int y=0;y<205;y++)assert(out[1+y*120+3]==xz_theme_rgb565(XZ_LCARS_NATIVE_VIOLET));
 assert(out[1+180*120+117]==xz_theme_rgb565(XZ_LCARS_NATIVE_AMBER));
 asset(969,120,31);for(int i=0;i<120*9;i++)assert(out[1+i]==0); /* Header clears overlay controls at y0..24. */
 int x,y;xz_lcars_asset_placement(849,400,172,383,72,&x,&y);assert(x==383&&x+16<=400&&y==72);
 xz_lcars_asset_placement(970,128,265,15,46,&x,&y);assert(x==8&&y==46);
 xz_lcars_asset_placement(966,128,295,57,277,&x,&y);assert(x==57&&y==277);
 xz_lcars_asset_placement(970,800,480,15,46,&x,&y);assert(x==15&&y==46);
 for(int yy=0;yy<268;yy++)for(int xx=0;xx<540;xx++)wave[yy*540+xx]=(uint16_t)((xx*137+yy*89)&65535);
 wave[10*540+268]=wave[146*540+269]=0xffe0;
 memcpy(original,wave,sizeof(wave));memcpy(fast,wave,sizeof(wave));xz_lcars_wave_maps(wave_maps);xz_lcars_wave_mapped(fast,536,268,540,-1,lut,wave_maps);xz_lcars_wave_pixels(wave,536,268,540,-1);
 assert(!memcmp(fast,wave,sizeof(wave)));assert(wave[10*540+268]==0xffff&&wave[146*540+269]==0xffff);
 for(int yy=0;yy<268;yy++)for(int xx=0;xx<540;xx++){
  int row=yy>=136?yy-136:yy;
  if(xx>=536)assert(wave[yy*540+xx]==original[yy*540+xx]);
  else if(row<10||row>=122)assert(wave[yy*540+xx]==xz_native_palette_pixel(XZ_THEME_LCARS,original[yy*540+xx]));
 }
 puts("PASS LCARS semantic sprites, exact values, alpha masks, header/FX clearances, pitch fit and protected cue bands");
}
