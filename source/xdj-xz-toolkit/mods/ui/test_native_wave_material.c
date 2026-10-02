#ifdef NDEBUG
#error Native waveform material checks require assertions
#endif
#include "native_wave_style.h"
#include <assert.h>
#include <stdio.h>
#define STRIDE 540
static uint16_t standard[65536],signal[65536],paper[536*268],pixels[STRIDE*268+2];
int main(void){
 for(unsigned i=0;i<65536;i++){standard[i]=(uint16_t)(i^0x1111);signal[i]=(uint16_t)(i^0x2222);}
 for(unsigned i=0;i<536*268;i++)paper[i]=0x3456;
 for(unsigned i=0;i<STRIDE*268+2;i++)pixels[i]=0xabcd;
 for(unsigned y=0;y<268;y++)for(unsigned x=0;x<536;x++)pixels[1+y*STRIDE+x]=0;
 pixels[1+60*STRIDE+80]=0xf800;
 pixels[1+60*STRIDE+268]=pixels[1+60*STRIDE+269]=0xffe0;
 assert(xz_native_wave_style_apply(pixels+1,536,268,STRIDE,-1,standard,signal,paper));
 assert(pixels[1+60*STRIDE+80]==signal[0xf800]);
 assert(pixels[1+60*STRIDE+81]==paper[60*536+81]);
 assert(pixels[1+60*STRIDE+268]==signal[0xffff]);
 for(unsigned y=0;y<268;y++){
  unsigned row=y>=136?y-136:y;
  assert(pixels[1+y*STRIDE+10]==((row<10||row>=122)?standard[0]:paper[y*536+10]));
  for(unsigned x=536;x<STRIDE;x++)assert(pixels[1+y*STRIDE+x]==0xabcd);
 }
 assert(pixels[0]==0xabcd&&pixels[STRIDE*268+1]==0xabcd);
 assert(!xz_native_wave_style_apply(pixels+1,535,268,STRIDE,-1,standard,signal,paper));
 assert(!xz_native_wave_style_apply(pixels+1,536,268,535,-1,standard,signal,paper));
 pixels[1+60*STRIDE+80]=0;
 assert(xz_native_wave_style_apply(pixels+1,536,268,STRIDE,-1,standard,signal,NULL));
 assert(pixels[1+60*STRIDE+80]==signal[0]);
 puts("PASS native material lookup, pristine signals, protected cue bands, playhead and row guards");
}
