/* Offline preview: load a track's waveform from a USB folder with the production
 * loader and draw the native 536x268 surface with the production renderer.
 * usage: preview_layered_wave <usb root> </Contents/... path> <3band|stems> <theme> <out.ppm> */
#include "layered_wave.h"
#include "layered_wave_source.h"
#include "themes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31));}
int main(int argc,char **argv){
 if(argc!=6)return 2;
 char source[2048];snprintf(source,sizeof(source),"%s%s",argv[1],argv[2]);
 struct xz_wave_load w;
 int stems=!strcmp(argv[3],"stems");
 if((stems?xz_wave_load_stems(source,&w):xz_wave_load_three_band(source,&w))){fprintf(stderr,"load failed\n");return 3;}
 const struct xz_theme_palette *p=xz_theme_palette(atoi(argv[4]));
 struct xz_layered_data data={w.bands,w.count,w.normalization,w.kind,{rgb565(p->stem[0]),rgb565(p->stem[1]),rgb565(p->stem[2])}};
 printf("kind=%s samples=%u normalization=%u\n",w.kind==XZ_LAYERED_STEMS?"stems":"3-band",w.count,w.normalization);
 static uint16_t pixels[536*268];
 /* Deck 1 at native zoom (1 sample per column), deck 2 zoomed out (4 per column). */
 for(unsigned deck=0;deck<2;deck++)for(unsigned x=0;x<536;x++){
  unsigned per=deck?4:1,first=w.count*3/10+x*per;
  if(first+per>w.count)continue;
  if(!xz_layered_column(pixels,536,536,268,x,65+deck*136,52,&data,first,first+per,0,0,NULL))return 4;
 }
 /* The native caller mirrors the upper half, then adds cues and playheads. */
 for(unsigned deck=0;deck<2;deck++)for(unsigned d=0;d<52;d++)
  memcpy(pixels+(65+deck*136+1+d)*536,pixels+(65+deck*136-d)*536,536*2);
 for(unsigned deck=0;deck<2;deck++)for(unsigned y=14;y<118;y++)pixels[(deck*136+y)*536+268]=0xffff;
 FILE *f=fopen(argv[5],"wb");if(!f)return 5;fprintf(f,"P6\n536 268\n255\n");
 for(unsigned i=0;i<536*268;i++){unsigned q=pixels[i];unsigned char b[3]={((q>>11)&31)*255/31,((q>>5)&63)*255/63,(q&31)*255/31};fwrite(b,1,3,f);}
 fclose(f);xz_wave_load_free(&w);return 0;
}
