#ifndef XZ_NATIVE_LED_H
#define XZ_NATIVE_LED_H
#include <stddef.h>
#include <stdint.h>
#include "stem_colors.h"
#include "stem_pads.h"
#include "../extra_decks.h"

#define XZ_LED_RECORD_BYTES 0x2cu
#define XZ_LED_MAX_RECORDS 256u

enum xz_led_color_mode { XZ_LED_STOCK=0, XZ_LED_REPLACE=1, XZ_LED_HUE=2 };
enum xz_led_id {
 XZ_LED_ID_CUE=1,
 XZ_LED_ID_PLAY_PAUSE=2,
 XZ_LED_ID_VINYL=3,
 XZ_LED_ID_BEAT_SYNC=4,
 XZ_LED_ID_MASTER=5,
 XZ_LED_ID_MASTER_TEMPO=6,
 XZ_LED_ID_TEMPO_RESET=7,
 XZ_LED_ID_LOOP_IN=8,
 XZ_LED_ID_LOOP_OUT=9,
 XZ_LED_ID_RELOOP_EXIT=10,
 XZ_LED_ID_REVERSE=11,
 XZ_LED_ID_SLIP=12,
 XZ_LED_ID_PAD_HOT_CUE=14,
 XZ_LED_ID_PAD_BEAT_LOOP=15,
 XZ_LED_ID_PAD_SLIP_LOOP=16,
 XZ_LED_ID_PAD_BEAT_JUMP=17,
 XZ_LED_ID_HOT_CUE_1=18,
 XZ_LED_ID_HOT_CUE_8=25,
 XZ_LED_ID_JOG_RING_POS=26,
 XZ_LED_ID_JOG_RING_LAMP=27,
 XZ_LED_ID_JOG_RING_AUX=29
};
enum xz_led_state {
 XZ_LED_STATE_OFF=0,
 XZ_LED_STATE_ON=1,
 XZ_LED_STATE_BLINK=2,
 XZ_LED_STATE_SYNC_BLINK=3
};

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
/* Updates only qualified on/off state in existing same-channel, non-forced
   transport records. No insertion, index writes, blink/timer or loop fields.
   Missing native records remain unchanged; hot cue colors use apply(). */
size_t xz_native_led_extra_deck(void *records,size_t capacity_bytes,
                                uint16_t *count_io,uint16_t *index_table,
                                unsigned max_rows,unsigned num_channels,
                                uint32_t dimmer,uint32_t now_ms,
                                unsigned channel,const struct xz_extra_deck *deck);
/* Disabled until both the record id and native slow-blink words are captured. */
struct xz_led_keysync_spec { uint32_t enabled,id,mask[8],blink[8]; };
extern const struct xz_led_keysync_spec xz_led_keysync_mapping;
size_t xz_native_led_keysync(void *,size_t,unsigned,unsigned,int,const struct xz_led_keysync_spec *);
#define XZ_LED_PROBE_IDS 64u
struct xz_led_probe_entry { uint32_t sequence,seen,id,changes,words[8]; };
struct xz_led_probe { uint32_t version,overflow[2]; struct xz_led_probe_entry entry[2][XZ_LED_PROBE_IDS]; };
extern struct xz_led_probe xz_mods_led_probe_v1;
void xz_native_led_probe(const void *,size_t,unsigned,unsigned);
/* A DECK 3 / DECK 4 hot-cue pad (pad 0..7 = A..H): a stored cue lit in its colour, an empty pad or
   unknown cues off. Always XZ_LED_REPLACE so no native pad state shows through. */
#define XZ_EXTRA_PAD_DEFAULT_RGB 0x00ff00u
int xz_extra_pad_led(const struct xz_extra_deck *deck,int pad,uint32_t *rgb,int *enabled);
int xz_native_led_start(void);
void xz_native_led_stop(void);
#endif
