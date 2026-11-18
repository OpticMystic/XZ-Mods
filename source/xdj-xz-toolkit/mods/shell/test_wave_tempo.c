#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { STRIDE=804 };
static struct fx_track track;
static uint16_t reference[STRIDE*480],changed[STRIDE*480];
static unsigned cases,failures;

static void render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *pixels){
 struct xz_shell old=*u;struct xz_shell_model original=*m;
 for(int i=0;i<STRIDE*480;i++)pixels[i]=0xa55a;
 assert(xz_shell_render(u,m,pixels,STRIDE));
 assert(!memcmp(u,&old,sizeof old)&&!memcmp(m,&original,sizeof original));
 for(int y=0;y<480;y++)for(int x=800;x<STRIDE;x++)assert(pixels[y*STRIDE+x]==0xa55a);
}

int main(void){
 static const int tempos[]={-9900,-5000,-600,0,600,5000,10000};
 fx_track_build(&track,7);
 for(int count=2;count<=4;count+=2)for(int sides=0;sides<4;sides+=3)
 for(int order=0;order<2;order++)for(int position=0;position<2;position++)
 for(int zoom=0;zoom<=6;zoom+=3)for(int mode=0;mode<=XZ_SHELL_WAVE_STEMS;mode++){
  struct xz_shell u;struct xz_shell_model m,view;
  xz_shell_init(&u);fx_model_four_players(&m,&track);
  u.play_decks=count;u.zoom=zoom;u.display.wave_color=(enum xz_shell_wave_color)mode;
  u.display.wave_position=(enum xz_shell_wave_position)position;
  m.sides_extra=(uint8_t)sides;m.wave_order=order;
  for(int d=0;d<4;d++){
   m.deck[d].elapsed_at_ms=0;m.deck[d].elapsed_ms=60000;m.deck[d].playing=0;
   m.deck[d].tempo_x100=0;
   m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO);
  }
  xz_shell_play_projection(&u,&m,&view);render(&u,&m,reference);
  for(unsigned t=0;t<sizeof tempos/sizeof tempos[0];t++){
   for(int d=0;d<4;d++)m.deck[d].tempo_x100=tempos[t];
   render(&u,&m,changed);
   for(int d=0;d<4;d++)if(sh_play_visible(&view,d)){
    struct xz_shell_rect lane=xz_shell_play_rect(&view,XZ_SHELL_R_LANE,d);
    int differences=0;
    for(int y=lane.y;y<lane.y+lane.h;y++)for(int x=lane.x+2;x<lane.x+lane.w-2;x++)
     differences+=reference[y*STRIDE+x]!=changed[y*STRIDE+x];
    cases++;
    if(differences){
     failures++;
     if(failures<=8)fprintf(stderr,"FAIL waveform resized: view=%d sides=%d deck=%d zoom=%d mode=%d tempo=%d changed_pixels=%d\n",count,sides,d+1,zoom,mode,tempos[t],differences);
    }
   }
  }
 }
 printf("RESULT fixed waveform geometry cases=%u failures=%u\n",cases,failures);
 return failures?1:0;
}
