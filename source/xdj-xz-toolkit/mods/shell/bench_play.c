/* Host benchmark: microseconds per PLAY frame while both decks play with the
   Beat FX on, at each zoom level, advancing the clock and the decks one 60 fps
   frame per render. Reports the median frame (robust against a busy host)
   and the mean. Host only. */
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
 struct xz_shell u;struct xz_shell_model m;
 fx_track_build(&track,7);
 fx_model_full(&m,&track);
 m.fx.known=1u|2u|4u|32u;m.fx.on=1;m.fx.name="ECHO";m.fx.beat_num=1;m.fx.beat_den=2;m.fx.time_ms=234;
 xz_shell_init(&u);
 double median_sum=0,mean_sum=0;
 unsigned check=0;
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
  printf("zoom %d: median %.0f us, mean %.0f us\n",zoom,took[FRAMES/2],sum/FRAMES);
  median_sum+=took[FRAMES/2];mean_sum+=sum/FRAMES;
 }
 printf("PLAY median %.0f us/frame, mean %.0f us/frame (checksum %u)\n",median_sum/(XZ_SHELL_ZOOM_MAX+1),mean_sum/(XZ_SHELL_ZOOM_MAX+1),check);
 return 0;
}
