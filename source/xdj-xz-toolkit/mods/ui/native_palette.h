#ifndef XZ_NATIVE_PALETTE_H
#define XZ_NATIVE_PALETTE_H
#include "themes.h"

/* Map original native draw colors. Never feed a previously themed retained
   surface back through this mapping; callers own pristine input separately.
   Source black is the stock paper; source white is stock ink. The waveform's
   three hue classes (red/orange, green, other=blue) stay distinct on every map. */
static inline uint16_t xz_native_palette_pixel(int theme,uint16_t source){
 if(theme<=0||theme>=XZ_THEME_COUNT)return source;
 const struct xz_theme *t=xz_theme(theme);const struct xz_theme_palette *p=&t->palette;
 unsigned r=((source>>11)&31)*255/31,g=((source>>5)&63)*255/63,b=(source&31)*255/31;
 unsigned light=(77*r+150*g+29*b)>>8;
 uint32_t tone;
 if((theme==XZ_THEME_LCARS&&xz_theme_lcars_tone(r,g,b,light,&tone))||
 (theme==XZ_THEME_MATRIX&&xz_theme_matrix_tone(r,g,b,light,&tone))||
 (theme==XZ_THEME_ANALOG&&xz_theme_analog_tone(r,g,b,light,&tone)))return xz_theme_rgb565(tone);
 unsigned maximum=r>g?r:g;if(b>maximum)maximum=b;
 unsigned minimum=r<g?r:g;if(b<minimum)minimum=b;
 unsigned chroma=maximum-minimum;
 if(t->map==XZ_MAP_WIN95){
  if(maximum>=160&&chroma>=64){
   unsigned red=r*4>=maximum*3,green=g*4>=maximum*3,blue=b*4>=maximum*3;
   uint32_t level=t->dark?0xffu:0x80u;
   return xz_theme_rgb565((red?level<<16:0)|(green?level<<8:0)|(blue?level:0));
  }
  return xz_theme_rgb565(light>=192?t->tones[3]:light>=112?t->tones[2]:light>=56?t->tones[1]:t->tones[0]);
 }
 if(t->map==XZ_MAP_TONES){
  if(chroma>40)return xz_theme_rgb565(r>g&&r>b?t->tones[2]:g>r&&g>b?t->tones[3]:t->tones[1]);
  return xz_theme_rgb565(light>=176?t->tones[3]:light>=96?t->tones[2]:light>=40?t->tones[1]:t->tones[0]);
 }
 uint32_t ink=p->ink;unsigned amount=light;
 if(chroma>40){
  uint32_t accent=r>g&&r>b?p->stem[0]:g>r&&g>b?p->stem[2]:p->stem[1];
  ink=accent;
  /* Signal brightness follows its strongest channel. Mixing it with text ink
     washed out waveforms on light themes and dulled saturated cue colors. */
  amount=maximum;
 }
 return xz_theme_rgb565(xz_theme_mix(p->bg,ink,amount*256/255));
}

/* Native SetWindowColor role 1 uses reduced RGB bytes, not RGB888 or RGB565.
   Roles 2 (transparency key) and 3 (unverified semantics) are left unchanged. */
static inline uint32_t xz_native_palette_reduced(int theme,unsigned role,uint32_t source){
 if(role!=1||(source&255)>31||((source>>8)&255)>63||((source>>16)&255)>31)return source;
 uint16_t pixel=(uint16_t)((source&31)<<11|((source>>8)&63)<<5|((source>>16)&31));
 uint16_t mapped=xz_native_palette_pixel(theme,pixel);
 return (source&0xff000000u)|((mapped>>11)&31)|(((mapped>>5)&63)<<8)|((mapped&31)<<16);
}

/* The DirectFB text path instead takes full RGBA bytes. Alpha is never mapped. */
static inline uint32_t xz_native_palette_rgba(int theme,uint32_t source){
 if(theme<=0||theme>=XZ_THEME_COUNT)return source;
 unsigned r=source&255,g=(source>>8)&255,b=(source>>16)&255;
 uint16_t mapped=xz_native_palette_pixel(theme,(uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3)));
 return (source&0xff000000u)|(((mapped>>11)&31)*255/31)|((((mapped>>5)&63)*255/63)<<8)|(((mapped&31)*255/31)<<16);
}
#endif
