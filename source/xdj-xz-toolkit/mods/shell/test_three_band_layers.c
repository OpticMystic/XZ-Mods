#ifdef NDEBUG
#error Three-band acceptance requires active assertions
#endif
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t pixels[101];
static uint16_t rgb(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void draw(struct xz_shell_column column){
 struct xz_shell_wave wave={.columns=&column,.count=1,.span_ms=1000};
 memset(pixels,0,sizeof pixels);
 sh_wave_lane((struct xz_canvas){pixels,1,1,101,0},(struct xz_shell_rect){0,0,1,101},50,&wave,XZ_SHELL_WAVE_3BAND,0,1);
}
int main(void){
 struct xz_shell_model model={0};sh_wave_colors(&model);
 draw((struct xz_shell_column){160,240,1});
 assert(pixels[50-34]==rgb(SH_WAVE_MID)); /* mids extend above lows */
 draw((struct xz_shell_column){192,128,0});
 assert(pixels[50-15]==rgb(0xa96b27)); /* low/mid overlap */
 assert(pixels[50-32]==rgb(SH_WAVE_LOW));
 draw((struct xz_shell_column){192,128,255});
 assert(pixels[50-5]==rgb(SH_WAVE_HIGH)); /* highs obscure both */
 draw((struct xz_shell_column){0,0,0});
 for(unsigned i=0;i<101;i++)assert(!pixels[i]);
 puts("PASS independent low/mid/high layers, low-mid overlap, high priority and silence");return 0;
}
