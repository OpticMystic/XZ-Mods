#ifndef XZ_NATIVE_LCARS_WAVE_H
#define XZ_NATIVE_LCARS_WAVE_H
#include "native_palette.h"
/* Run during snapshot preparation, never in the live waveform pixel loop. */
static inline uint16_t xz_lcars_wave_pixel(uint16_t source,int deck){
 uint32_t rgb=xz_theme_rgb888(source);unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255,hi=r>g?r:g,lo=r<g?r:g;
 if(b>hi)hi=b;if(b<lo)lo=b;
 if(hi<40)return 0;
 if(hi-lo>28)return xz_theme_rgb565(xz_theme_mix(0,deck?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER,hi*256/255));
 return hi>210?0xffff:xz_native_palette_pixel(XZ_THEME_LCARS,source);
}
static inline void xz_lcars_wave_maps(uint16_t *maps){
 for(unsigned i=0;i<65536;i++){maps[i]=xz_lcars_wave_pixel((uint16_t)i,0);maps[65536+i]=xz_lcars_wave_pixel((uint16_t)i,1);}
}
static inline void xz_lcars_wave_mapped(uint16_t *pixels,unsigned width,unsigned height,unsigned stride,int deck,const uint16_t *standard,const uint16_t *maps){
 for(unsigned y=0;y<height;y++){
  unsigned row=deck<0?(y>=136?y-136:y):y;int lane=deck<0?(y>=136):deck;
  const uint16_t *lut=deck<0&&(row<10||row>=122)?standard:maps+(lane?65536:0);
  uint16_t *line=pixels+(size_t)y*stride;uint16_t head0=0,head1=0;
  if(deck<0&&width==536){head0=line[268];head1=line[269];}
  for(unsigned x=0;x<width;x++)line[x]=lut[line[x]];
  /* 1.26 draws its two-pixel yellow playhead at byte offset0x218. */
  if(deck<0&&width==536){if(head0==0xffe0)line[268]=0xffff;if(head1==0xffe0)line[269]=0xffff;}
 }
}
/* Scalar reference for equivalence tests only. */
static inline void xz_lcars_wave_pixels(uint16_t *pixels,unsigned width,unsigned height,unsigned stride,int deck){
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
  unsigned row=deck<0?(y>=136?y-136:y):y;int lane=deck<0?(y>=136):deck;uint16_t p=pixels[y*stride+x];
  if(deck<0&&width==536&&(x==268||x==269)&&p==0xffe0){pixels[y*stride+x]=0xffff;continue;}
  pixels[y*stride+x]=deck<0&&(row<10||row>=122)?xz_native_palette_pixel(XZ_THEME_LCARS,p):xz_lcars_wave_pixel(p,lane);
 }
}
#endif
