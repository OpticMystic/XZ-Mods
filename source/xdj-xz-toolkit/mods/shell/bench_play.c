/* Host benchmark: microseconds per PLAY frame while both decks play with the
   Beat FX on, for each waveform colour across every zoom level, advancing the
   clock and the decks one 60 fps frame per render. Reports the median frame
   (robust against a busy host) and the mean. Host only. */
#include "fixtures.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { FRAMES=600 };
static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static double took[FRAMES];

static double now_us(void) {
 struct timespec t;timespec_get(&t,TIME_UTC);
 return (double)t.tv_sec*1e6+(double)t.tv_nsec/1e3;
}
static int by_value(const void *a, const void *b) { double x=*(const double *)a,y=*(const double *)b;return x<y?-1:x>y; }
int main(void) {
 static const char *const names[]={"BLUE","RGB","3BAND","STEMS"};
 struct xz_shell u;struct xz_shell_model m;
 fx_track_build(&track,7);
 fx_model_full(&m,&track);
 fx_beat_fx(&m.fx,1,"ECHO",1,2,234);
 /* Both decks show their stem mute buttons, deck 2 with a stem muted, and deck 1 slips with a ghost. */
 m.capabilities|=XZ_SHELL_CAP_STEM_MUTE;m.deck[1].stem_muted=4;
 m.deck[0].slip=1;m.deck[0].slip_ms=m.deck[0].elapsed_ms+1500;
 xz_shell_init(&u);
 unsigned check=0;
 for(int mode=XZ_SHELL_WAVE_BLUE;mode<=XZ_SHELL_WAVE_STEMS;mode++){
  u.display.wave_color=(enum xz_shell_wave_color)mode;
  double median_sum=0,mean_sum=0;
  for(int zoom=0;zoom<=XZ_SHELL_ZOOM_MAX;zoom++){
   u.zoom=zoom;
   double sum=0;
   for(int i=0;i<FRAMES;i++){
    m.clock_ms+=16;
    for(int d=0;d<XZ_SHELL_DECKS;d++){m.deck[d].elapsed_ms+=16;m.deck[d].elapsed_at_ms=m.clock_ms-8;}
    double t0=now_us();
    xz_shell_render(&u,&m,frame,XZ_SHELL_WIDTH);
    took[i]=now_us()-t0;sum+=took[i];
    check+=frame[(size_t)(i%480)*XZ_SHELL_WIDTH+(size_t)(i%800)];
   }
   qsort(took,FRAMES,sizeof *took,by_value);
   median_sum+=took[FRAMES/2];mean_sum+=sum/FRAMES;
  }
  printf("PLAY %-5s median %.0f us/frame, mean %.0f us/frame\n",names[mode],median_sum/(XZ_SHELL_ZOOM_MAX+1),mean_sum/(XZ_SHELL_ZOOM_MAX+1));
 }
 printf("checksum %u\n",check);
 return 0;
}
