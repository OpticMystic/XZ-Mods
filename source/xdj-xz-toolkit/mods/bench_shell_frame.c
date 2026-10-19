/* SPDX-License-Identifier: MIT */
/* Host benchmark for the FLAGSHIP shell's 60 Hz PLAY frame (never touches a device).
   1. Render cost: both decks playing, the clock advanced one 60 Hz frame per render as the runtime
      does (model refreshed every second frame, elapsed_at_ms stamped), at every zoom level.
   2. Playhead smoothness: the frame-to-frame advance of the published playhead for raw per-reading
      extrapolation versus the runtime's smoother, from a coarse 10 ms stat-frame reading refreshed at
      30 Hz. Usage: bench_shell_frame [frames-per-zoom] */
#include "shell_runtime.h"
#include "shell/fixtures.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static struct fx_track track;
static uint16_t frame[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static double now_us(void) { struct timespec t;timespec_get(&t,TIME_UTC);return (double)t.tv_sec*1e6+(double)t.tv_nsec/1e3; }
static int by_value(const void *a, const void *b) { double x=*(const double *)a,y=*(const double *)b;return x<y?-1:x>y; }

int main(int argc, char **argv) {
 int frames=argc>1?atoi(argv[1]):600;if(frames<10)frames=10;
 double *samples=malloc(sizeof *samples*(size_t)frames);if(!samples)return 1;
 struct xz_shell u;struct xz_shell_model m;
 fx_track_build(&track,7);fx_model_full(&m,&track);xz_shell_init(&u);
 m.fx.known=(1u<<XZ_SHELL_FX_F_ON)|(1u<<XZ_SHELL_FX_F_NAME)|(1u<<XZ_SHELL_FX_F_BEAT)|(1u<<XZ_SHELL_FX_F_TIME)|(1u<<XZ_SHELL_FX_F_BPM);
 m.fx.on=1;m.fx.name="SPIRAL";m.fx.beat_num=1;m.fx.beat_den=1;m.fx.time_ms=500;m.fx.bpm_x10=1022;
 m.stems_known=1;m.stems_on=1;
 unsigned check=0;double all=0,worst=0;
 for(int pass=0;pass<2;pass++){
 /* Pass 0: the theme's 3-band lanes; pass 1: the STEMS waveform (WAVEFORM COLOR = STEMS). */
 u.display.wave_color=pass?XZ_SHELL_WAVE_STEMS:XZ_SHELL_WAVE_3BAND;all=0;worst=0;
 printf("render (host -O2), %s, both decks playing, clock +16.67 ms per frame:\n",pass?"STEMS waveform":"3-BAND waveform");
 for(int zoom=0;zoom<=XZ_SHELL_ZOOM_MAX;zoom++){
  u.zoom=zoom;
  uint32_t clock=100000;int32_t base[2]={m.deck[0].elapsed_ms,m.deck[1].elapsed_ms};
  for(int i=0;i<frames;i++){
   clock=100000+(uint32_t)((int64_t)i*50/3);
   if(i%2==0)for(int d=0;d<2;d++){m.deck[d].elapsed_ms=base[d]+(int32_t)(clock-100000);m.deck[d].elapsed_at_ms=clock;}
   m.clock_ms=clock;
   double t0=now_us();xz_shell_render(&u,&m,frame,XZ_SHELL_WIDTH);samples[i]=now_us()-t0;
   check+=frame[(size_t)(i%480)*XZ_SHELL_WIDTH+(size_t)(i%800)];
  }
  qsort(samples,(size_t)frames,sizeof *samples,by_value);
  double sum=0;for(int i=0;i<frames;i++)sum+=samples[i];
  printf("  zoom %d: median %.0f us, mean %.0f us, p99 %.0f us, max %.0f us\n",zoom,samples[frames/2],sum/frames,
   samples[frames*99/100],samples[frames-1]);
  all+=samples[frames/2];if(samples[frames-1]>worst)worst=samples[frames-1];
 }
 printf("  all zooms: median of medians %.0f us, worst frame %.0f us (checksum %u)\n",all/(XZ_SHELL_ZOOM_MAX+1),worst,check);
 }

 /* Playhead: true position advances 1.02 ms per ms; the reading is truncated to 10 ms and taken every
    second frame. Raw: extrapolate from the latest reading. Smoothed: xz_shrt_position_step. */
 struct xz_shrt_position p;xz_shrt_position_reset(&p);
 int32_t raw_ms=0,smooth_ms=0;uint32_t raw_at=0,smooth_at=0;
 double raw_prev=-1,smooth_prev=-1,raw_dev=0,smooth_dev=0,ideal=50.0/3*1.02;
 for(int i=0;i<1200;i++){
  uint32_t now=100+(uint32_t)((int64_t)i*50/3);double truth=5000+(now-100)*1.02;
  if(i%2==0){
   int32_t reading=(int32_t)(truth/10)*10;
   raw_ms=reading;raw_at=now;
   xz_shrt_position_step(&p,1,reading,now,xz_shrt_rate_from_tempo(200),0,&smooth_ms,&smooth_at);
  }
  double raw=raw_ms+(double)(now-raw_at)*1.02,smooth=smooth_ms+(double)(int32_t)(now-smooth_at)*1.02;
  if(i>120){
   double a=raw-raw_prev-ideal,b=smooth-smooth_prev-ideal;if(a<0)a=-a;if(b<0)b=-b;
   if(a>raw_dev)raw_dev=a;if(b>smooth_dev)smooth_dev=b;
  }
  raw_prev=raw;smooth_prev=smooth;
 }
 printf("playhead frame-to-frame advance, worst deviation from the ideal %.2f ms: raw extrapolation %.2f ms, smoothed %.2f ms\n",
  ideal,raw_dev,smooth_dev);
 free(samples);return 0;
}
