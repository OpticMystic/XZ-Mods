/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WAVE_STYLE_H
#define XZ_NATIVE_WAVE_STYLE_H
#include "native_palette.h"

static inline int xz_native_wave_style_has_paper(int theme){
 return theme==11||theme==13||theme==16||theme==17||theme==18||theme==19||theme==20;
}

/* Build a foreground LUT once. The DMG shell surrounds a separate LCD with
 * its own paper and ink. Cue/index bands remain the caller's standard LUT. */
static inline uint16_t xz_native_wave_style_pixel(int theme,uint16_t source){
 int lcd=theme==17?7:theme==18?12:theme;
 return xz_native_palette_pixel(lcd,source);
}

static inline unsigned xz_native_wave_style_position(int coordinate,int extent){
 if(extent<=1||coordinate<=0)return 0;
 if(coordinate>=extent-1)return 256;
 return (unsigned)((uint64_t)(unsigned)coordinate*256u/(unsigned)(extent-1));
}

/* Snapshot preparation only. Store this paper once for the qualified wave
 * geometry; use it only for pristine source black, never retained output.
 * No frame-time gradients, allocations, or source-buffer writes are needed. */
static inline uint16_t xz_native_wave_style_paper_at(int theme,int x,int y,int width,int height){
 uint16_t flat=xz_native_wave_style_pixel(theme,0);
 if(width<=0||height<=0||!xz_native_wave_style_has_paper(theme))return flat;
 const struct xz_theme *t=xz_theme(theme);
 unsigned fx=xz_native_wave_style_position(x,width),fy=xz_native_wave_style_position(y,height);
 uint32_t color=t->palette.bg;
 if(theme==17||theme==18)return flat;
 if(theme==11||theme==16){
  /* Pinstripes remain quieter than native beat-grid and waveform marks. */
  uint32_t stripe=theme==11?0xdde2e8:0x383e47;
  color=xz_theme_mix(color,stripe,fy/16);
  if(y>=0&&((unsigned)y&3u)==0)color=xz_theme_mix(color,stripe,theme==11?38:32);
 }else if(theme==13){
  color=xz_theme_mix(0x101d51,0x070e29,fy);
 }else{
  /* Low-amplitude blue/lilac frost, not a full-saturation mesh wallpaper. */
  uint32_t left=theme==19?0xe4edf9:0x142233;
  uint32_t right=theme==19?0xeee7f7:0x211a31;
  uint32_t pool=xz_theme_mix(left,right,fx);
  unsigned center=fy<=128?fy:256-fy;
  color=xz_theme_mix(color,pool,48+center/2);
 }
 return xz_theme_rgb565(color);
}

/* Qualified main/overview geometries only. Tables and paper are immutable
 * snapshot data; the hot path does no drawing, allocation or gradient math. */
static inline int xz_native_wave_style_apply(uint16_t *pixels,unsigned width,unsigned height,
 unsigned stride,int deck,const uint16_t *standard,const uint16_t *signal,const uint16_t *paper){
 if(!pixels||!standard||!signal||stride<width||stride>UINT32_MAX/268u)return 0;
 if(!((width==536&&height==268&&deck==-1)||(width==300&&height==34&&(deck==0||deck==1))))return 0;
 for(unsigned y=0;y<height;y++){
  unsigned row=deck<0?(y>=136?y-136:y):y;
  int protected_band=deck<0&&(row<10||row>=122);
  uint16_t *line=pixels+(size_t)y*stride;
  uint16_t head0=deck<0?line[268]:0,head1=deck<0?line[269]:0;
  if(protected_band){
   for(unsigned x=0;x<width;x++)line[x]=standard[line[x]];
  }else if(paper){
   const uint16_t *background=paper+(size_t)y*width;
   for(unsigned x=0;x<width;x++){uint16_t source=line[x];line[x]=source?signal[source]:background[x];}
  }else for(unsigned x=0;x<width;x++)line[x]=signal[line[x]];
  if(deck<0){if(head0==0xffe0)line[268]=signal[0xffff];if(head1==0xffe0)line[269]=signal[0xffff];}
 }
 return 1;
}
#endif
