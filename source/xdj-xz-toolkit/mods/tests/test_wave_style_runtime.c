#define _GNU_SOURCE
#include <assert.h>
#include "../shell_runtime.c"
#include "../shell/fixtures.h"
void xz_log(const char *text){(void)text;}
static struct fx_track track;
static uint16_t pixels[800*480];
#include "../shell/shell_internal.h"
static uint16_t pixel_color(uint32_t x){return (uint16_t)(((x>>8)&0xf800)|((x>>5)&0x7e0)|((x>>3)&31));}
static void save_frame(const char *directory,const char *name){
 char path[1200];snprintf(path,sizeof path,"%s/%s.ppm",directory,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n800 480\n255\n");
 for(unsigned i=0;i<800u*480u;i++){uint16_t v=pixels[i];unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv){
 fx_track_build(&track,7);fx_model_full(&model,&track);xz_shell_init(&shell);
 model.deck[2]=model.deck[0];model.deck[3]=model.deck[1];
 model.deck[2].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);model.deck[3].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);
 model.deck_count=4;shell.play_decks=4;shell.display.wave_color=XZ_SHELL_WAVE_RGB;
 for(int d=0;d<2;d++){
  model.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE_LOOK);
  model.deck[d].look.wave_color=XZ_SHELL_WAVE_RGB;
 }
 last_input.look_known=0;assert(menu_render_frame_locked(pixels,800)>0);if(argc>1)save_frame(argv[1],"stock-rgb");
 last_input.look_known=1;last_input.wave_mode=1;
 assert(menu_render_frame_locked(pixels,800)>0);
 if(argc>1)save_frame(argv[1],"global-three-band");
 unsigned failed=0;
 for(int d=0;d<4;d++){
  int selected=sh_deck_wave_color(&shell,&model.deck[d]);
  printf("deck=%d MODS=3BAND actual_wave_color=%d expected=%d\n",d+1,selected,XZ_SHELL_WAVE_3BAND);
  failed+=selected!=XZ_SHELL_WAVE_3BAND;
  uint16_t lane[200*100]={0};struct xz_canvas canvas={lane,200,200,100,0};
  /* A known low-dominant spectrum must show all three nested layers. A real
     fixture with no dominant lows legitimately has no visible blue region. */
  const struct xz_shell_column spectrum[2]={{210,130,50},{210,130,50}};
  struct xz_shell_deck probe=model.deck[d];probe.wave.columns=spectrum;probe.wave.count=2;probe.wave.span_ms=1000;
  probe.known|=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE);
  sh_deck_lane(canvas,(struct xz_shell_rect){0,0,200,100},45,&shell,&model,&probe,0,40);
  const uint16_t ink[3]={pixel_color(SH_WAVE_LOW),pixel_color(SH_WAVE_MID),pixel_color(SH_WAVE_HIGH)};unsigned counts[3]={0};
  for(unsigned i=0;i<20000;i++)for(unsigned k=0;k<3;k++)counts[k]+=lane[i]==ink[k];
  printf("layered pixels deck=%d low=%u mid=%u high=%u\n",d+1,counts[0],counts[1],counts[2]);
  failed+=!counts[0]||!counts[1]||!counts[2];
 }
 last_input.wave_mode=2;assert(menu_render_frame_locked(pixels,800)>0);
 for(int d=0;d<4;d++)failed+=sh_deck_wave_color(&shell,&model.deck[d])!=XZ_SHELL_WAVE_STEMS;
 last_input.wave_mode=0;assert(menu_render_frame_locked(pixels,800)>0);
 for(int d=0;d<4;d++)failed+=sh_deck_wave_color(&shell,&model.deck[d])!=XZ_SHELL_WAVE_RGB;
 last_input.look_known=0;assert(menu_render_frame_locked(pixels,800)>0);
 failed+=shell.display.wave_color_override!=XZ_SHELL_WAVE_BLUE;
 return failed?1:0;
}
