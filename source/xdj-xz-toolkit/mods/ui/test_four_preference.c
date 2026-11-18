#ifdef NDEBUG
#error Four-deck preference acceptance requires active assertions
#endif
#include "native_wave_runtime.h"
#include "wave_viewport.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* The real preference and last-drawn accessor are linked from the runtime.
   No native scene exists in this offline fixture. */
int xz_read_memory(uint32_t address,void *out,size_t bytes){(void)address;(void)out;(void)bytes;return -1;}
int xz_mods_native_style_v1(void){return 0;}
int xz_layered_wave_active(void){return 0;}
int main(void){
 assert(XZ_FOUR_ORDER==XZ_FOUR_STACKED);
 assert(xz_native_four_mixer_order()==0);
 assert(xz_native_four_order()==-1);
 xz_native_four_set_mixer_order(1);
 assert(xz_native_four_mixer_order()==1&&xz_native_four_order()==-1);
 xz_native_four_set_mixer_order(42);
 assert(xz_native_four_mixer_order()==1);
 xz_native_four_set_mixer_order(0);
 assert(xz_native_four_mixer_order()==0&&xz_native_four_order()==-1);
 puts("PASS numeric default, shared mixer preference, no invented last-drawn geometry");
}
