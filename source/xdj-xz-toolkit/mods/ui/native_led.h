#ifndef XZ_NATIVE_LED_H
#define XZ_NATIVE_LED_H
#include <stddef.h>
#include <stdint.h>
#include "stem_colors.h"
#include "stem_pads.h"

#define XZ_LED_RECORD_BYTES 0x2cu
#define XZ_LED_MAX_RECORDS 256u
enum xz_led_color_mode { XZ_LED_STOCK=0, XZ_LED_REPLACE=1, XZ_LED_HUE=2 };
static inline uint32_t xz_jump_page2_color(int pad){
 return pad<2||pad>=6?0xff3000:0xffa300;
}
typedef int (*xz_led_color_fn)(int deck,int pad,uint32_t *rgb,int *enabled);
/* Bass on four-stem decks: two primaries, apart from blue, red, green and the
   white bypass. Unverified on the physical LEDs; amber 0xffa300 is the fallback. */
#define XZ_STEM_HARDWARE_BASS 0xff00ffu
/* Native PadColorCode primaries; display RGB mixtures wash out physical LEDs.
   0-2 drums/harmonics/vocals, 3 bypass (white), 4 bass. Bypass keeps 3 because
   older callers pass 3 for it. */
static inline uint32_t xz_stem_hardware_color(int stem){
 static const uint32_t colors[5]={0x0000ff,0xff0000,0x00ff00,0xffffff,XZ_STEM_HARDWARE_BASS};
 return colors[stem>=0&&stem<5?stem:3];
}

/* One stem pad's LED. role is xz_stem_pad_role(); stem_colors are the three
   saved choices (THEME keeps the hardware primary). A stem is lit while audible;
   bypass is lit while on. Returns 0 for a role no pad carries. */
static inline int xz_stem_pad_led(int role,const int stem_colors[3],unsigned muted,
                                  const float levels[4],int bypass,uint32_t *rgb,int *lit){
 if(role==XZ_STEM_PAD_BYPASS){*rgb=xz_stem_hardware_color(3);*lit=bypass!=0;return 1;}
 if(role<0||role>XZ_STEM_BASS)return 0;
 *rgb=role==XZ_STEM_BASS?XZ_STEM_HARDWARE_BASS:xz_stem_palette_color(stem_colors[role],xz_stem_hardware_color(role));
 *lit=!(muted&(1u<<role))&&levels[role]>0&&!bypass;
 return 1;
}

/* Existing records only. REPLACE sets rgb and brightness; HUE changes only RGB, scaled by native intensity; on/off/blink stay native. */
size_t xz_native_led_apply(void *records,size_t bytes,unsigned count,
                           unsigned channel,xz_led_color_fn color);
int xz_native_led_start(void);
void xz_native_led_stop(void);
#endif
