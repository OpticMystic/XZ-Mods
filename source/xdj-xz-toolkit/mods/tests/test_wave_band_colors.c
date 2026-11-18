/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Band colour acceptance requires active assertions
#endif
#include "../shell/shell_internal.h"
#include "../ui/layered_wave.h"
#include "../ui/band_colors.h"
#include "../ui/native_flagship_wave.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT],reference[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static uint16_t c565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
int main(void){
 struct xz_shell_model m={0};struct xz_shell shell;xz_shell_init(&shell);
 struct xz_canvas c={pixels,800,800,480,XZ_THEME_FLAGSHIP};
 struct xz_shell_column bands[3]={{255,0,0},{0,255,0},{0,0,255}};
 struct xz_shell_wave wave={bands,3,3000};
 const uint32_t standard[]={SH_WAVE_LOW,SH_WAVE_MID,SH_WAVE_HIGH};
 const uint32_t classic[]={0x2053d9,0xf2aa3c,0xffffff};
 struct xz_layered_data look={0};struct xz_layered_col col={{3,2,1},1};
 sh_wave_colors(&m);sh_wave_overview(c,(struct xz_shell_rect){0,0,30,30},&wave,XZ_SHELL_WAVE_3BAND,0);
 memcpy(reference,pixels,sizeof pixels);
 /* Explicit STANDARD is byte-identical to the old palette, including greys on the played set. */
 for(int k=0;k<3;k++)assert(pixels[29*800+k*10+5]==c565(standard[k]));
 sh_wave_colors(&m);sh_wave_overview(c,(struct xz_shell_rect){0,0,30,30},&wave,XZ_SHELL_WAVE_3BAND,0);
 assert(!memcmp(reference,pixels,sizeof pixels));
 for(int choice=0;choice<XZ_BAND_COLOR_CHOICES;choice++){
  uint32_t rgb[3];for(int k=0;k<3;k++)rgb[k]=m.wave_band_rgb[k]=xz_band_palette_color(choice,0);
  xz_layered_band_configure(rgb);xz_layered_band_colors(look.colors);
  sh_wave_colors(&m);memset(pixels,0,sizeof pixels);
  sh_wave_overview(c,(struct xz_shell_rect){0,0,30,30},&wave,XZ_SHELL_WAVE_3BAND,0);
  for(int k=0;k<3;k++){
   assert(pixels[29*800+k*10+5]==c565(choice?rgb[k]:standard[k]));
   assert(xz_layered_pixel(&look,&col,2-k)==c565(choice?rgb[k]:classic[k]));
  }
  sh_wave_overview(c,(struct xz_shell_rect){0,0,30,30},&wave,XZ_SHELL_WAVE_3BAND,30);
  assert(pixels[29*800]==c565(0x343434)&&pixels[29*800+15]==c565(0x5c5c5c)&&pixels[29*800+25]==c565(0x8c8c8c));
  uint32_t custom=xz_band_palette_color(choice,0);
  assert(xz_flagship_wave_band_pixel(0x001f,rgb)==c565(choice?custom:XZ_FLAGSHIP_WAVE_LOW));
  assert(xz_flagship_wave_band_pixel(0xf800,rgb)==c565(choice?custom:XZ_FLAGSHIP_WAVE_MID));
  assert(xz_flagship_wave_band_pixel(0x07e0,rgb)==c565(choice?custom:XZ_FLAGSHIP_WAVE_HIGH));
  if(choice)assert(xz_flagship_wave_band_pixel(0x000f,rgb)==c565(xz_theme_mix(0,xz_theme_mix(0,custom,160),(15*255/31)*256/255)));
 }
 /* Every stock RGB565 source maps exactly as it used to when STANDARD is chosen. */
 uint32_t zero[3]={0};for(unsigned p=0;p<65536;p++)assert(xz_flagship_wave_band_pixel((uint16_t)p,zero)==xz_flagship_wave_pixel((uint16_t)p));
 puts("PASS all band choices: MODERN and layered pixels, STANDARD defaults, played greys, native low deep shade and all 65536 source colours");
 return 0;
}
