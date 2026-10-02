#ifndef XZ_NATIVE_TITLE_THEME_H
#define XZ_NATIVE_TITLE_THEME_H
#include "themes.h"

static inline int xz_native_title_deck(unsigned id,unsigned width,unsigned height){
 if(width!=400)return -1;
 if(height==172)return id==649?0:id==687?1:-1;
 if(height==70)return id==725?0:id==726?1:-1;
 return -1;
}
static inline int xz_native_title_colors(int theme,int deck,uint32_t *paper,uint32_t *ink){
 if(deck<0||deck>1)return 0;
 if(theme==XZ_THEME_LCARS){*paper=deck?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER;*ink=0;return 1;}
 if(theme<7||theme>18)return 0;
 const struct xz_theme *t=xz_theme(theme);*paper=t->title;*ink=t->title_ink;return 1;
}
/* The argb16 callback returns the original white-title coverage in RGB565. */
static inline uint16_t xz_native_title_pixel(uint16_t pixel,uint32_t paper,uint32_t ink,int keyed,uint16_t key){
 if(keyed&&pixel==key)return pixel;
 uint32_t rgb=xz_theme_rgb888(pixel);unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255,coverage=r>g?r:g;if(b>coverage)coverage=b;
 uint16_t result=xz_theme_rgb565(xz_theme_mix(paper,ink,coverage*256/255));
 return keyed&&result==key?(uint16_t)(result^1):result;
}
static inline uint32_t xz_native_title_rgba(uint32_t rgba,uint32_t ink,int key_state,uint16_t key){
 uint16_t source=(uint16_t)(((rgba&255)>>3)<<11|(((rgba>>8)&255)>>2)<<5|(((rgba>>16)&255)>>3));
 if(key_state<0||(key_state>0&&source==key))return rgba;
 uint16_t target=xz_theme_rgb565(ink);
 if(key_state>0&&target==key){target^=1;ink=xz_theme_rgb888(target);}
 return (rgba&0xff000000u)|((ink>>16)&255)|(ink&0xff00)|((ink&255)<<16);
}
#endif
