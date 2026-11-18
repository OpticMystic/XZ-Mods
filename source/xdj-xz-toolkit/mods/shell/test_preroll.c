#ifdef NDEBUG
#error Pre-start acceptance requires assertions
#endif
#include "shell_internal.h"
#include "fixtures.h"
#include <assert.h>
#include <stdio.h>
static struct fx_track track;
static uint16_t pixels[804*482];
static unsigned wave_pixels(struct xz_shell *u,struct xz_shell_model *m){
 for(unsigned i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0xa55a;
 assert(xz_shell_render(u,m,pixels+804,804));
 unsigned n=0;
 uint16_t low=(uint16_t)(((SH_WAVE_LOW>>8)&0xf800)|((SH_WAVE_LOW>>5)&0x7e0)|((SH_WAVE_LOW>>3)&31));
 for(int y=0;y<480;y++){
  for(int x=0;x<800;x++)n+=pixels[804+y*804+x]==low;
  for(int x=800;x<804;x++)assert(pixels[804+y*804+x]==0xa55a);
 }
 for(int x=0;x<804;x++)assert(pixels[x]==0xa55a&&pixels[804*481+x]==0xa55a);
 return n;
}
int main(void){
 struct xz_shell u;struct xz_shell_model m;fx_track_build(&track,1);fx_model_full(&m,&track);xz_shell_init(&u);
 for(int d=0;d<2;d++){
  m.deck[d].known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED)|XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED)|XZ_SHELL_KNOWN(XZ_SHELL_F_TOTAL)|XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE);
  m.deck[d].loaded=1;m.deck[d].elapsed_at_ms=0;
  for(unsigned i=0;i<FX_LANE_COLUMNS;i++)track.lane[i]=(struct xz_shell_column){200,0,0};
 }
 const int32_t positions[]={-1,-100,-400,-16000,-96000,0,100};
 for(unsigned i=0;i<sizeof positions/sizeof *positions;i++){
  m.deck[0].elapsed_ms=m.deck[1].elapsed_ms=positions[i];
  assert(xz_shell_elapsed_now(&m,0)==positions[i]);
  unsigned visible=wave_pixels(&u,&m);
  m.deck[0].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED);m.deck[1].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED);
  unsigned absent=wave_pixels(&u,&m);
  if(positions[i]>=-400)assert(visible>absent+200);
  m.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED);m.deck[1].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED);
 }
 puts("PASS signed pre-start position, waveform visible before zero, and full-frame bounds");return 0;
}
