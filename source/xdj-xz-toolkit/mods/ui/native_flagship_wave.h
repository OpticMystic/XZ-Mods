#ifndef XZ_NATIVE_FLAGSHIP_WAVE_H
#define XZ_NATIVE_FLAGSHIP_WAVE_H
#include "native_palette.h"
/* Three-band look on the stock waveform: each source colour is classed by its
   dominant channel (blue lows, warm mids, green/white highs) and scaled by its
   strongest channel. Run during snapshot preparation, never per frame. */
static inline uint16_t xz_flagship_wave_pixel(uint16_t source){
 uint32_t rgb=xz_theme_rgb888(source);unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255,hi=r>g?r:g,lo=r<g?r:g;
 if(b>hi)hi=b;if(b<lo)lo=b;
 /* The stock lane ground is a dim grey gradient; the flagship lane is black. */
 if(hi<40||(hi<64&&hi-lo<=28))return 0;
 uint32_t band;
 if(hi-lo<=28||(g>=r&&g>=b))band=XZ_FLAGSHIP_WAVE_HIGH;
 else if(b>=r)band=hi<128?XZ_FLAGSHIP_WAVE_LOW_DEEP:XZ_FLAGSHIP_WAVE_LOW;
 else band=XZ_FLAGSHIP_WAVE_MID;
 return xz_theme_rgb565(xz_theme_mix(0,band,hi*256/255));
}
/* One deck-independent 65536-entry map. */
static inline void xz_flagship_wave_maps(uint16_t *maps){
 for(unsigned i=0;i<65536;i++)maps[i]=xz_flagship_wave_pixel((uint16_t)i);
}
static inline void xz_flagship_wave_mapped(uint16_t *pixels,unsigned width,unsigned height,unsigned stride,int deck,const uint16_t *standard,const uint16_t *maps){
 for(unsigned y=0;y<height;y++){
  unsigned row=deck<0?(y>=136?y-136:y):y;
  const uint16_t *lut=deck<0&&(row<10||row>=122)?standard:maps;
  uint16_t *line=pixels+(size_t)y*stride;uint16_t head0=0,head1=0;
  if(deck<0&&width==536){head0=line[268];head1=line[269];}
  for(unsigned x=0;x<width;x++)line[x]=lut[line[x]];
  /* 1.26 draws its two-pixel yellow playhead at x 268/269. */
  if(deck<0&&width==536){if(head0==0xffe0)line[268]=0xffff;if(head1==0xffe0)line[269]=0xffff;}
 }
}
/* Scalar reference for equivalence tests only. */
static inline void xz_flagship_wave_pixels(uint16_t *pixels,unsigned width,unsigned height,unsigned stride,int deck){
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
  unsigned row=deck<0?(y>=136?y-136:y):y;uint16_t p=pixels[y*stride+x];
  if(deck<0&&width==536&&(x==268||x==269)&&p==0xffe0){pixels[y*stride+x]=0xffff;continue;}
  pixels[y*stride+x]=deck<0&&(row<10||row>=122)?xz_native_palette_pixel(XZ_THEME_FLAGSHIP,p):xz_flagship_wave_pixel(p);
 }
}
#endif
