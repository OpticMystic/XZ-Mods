#ifndef XZ_SHELL_AXIS_H
#define XZ_SHELL_AXIS_H
#include "shell_internal.h"
#include <limits.h>
/* Zoom fixes source-time pixels. Tempo, jog bend and SYNC change motion,
   never the size of the waveform, beat grid or cue/loop markers. */
struct sh_wave_axis { int pps; uint32_t rate; };
static inline struct sh_wave_axis sh_axis(int pps,const struct xz_shell_deck *d){
 (void)d;
 return (struct sh_wave_axis){pps,10000u};
}
static inline int64_t sh_axis_floor(int64_t a,int64_t b){int64_t q=a/b;return q-((a%b)!=0&&a<0);}
static inline int64_t sh_axis_px(struct sh_wave_axis a,int32_t ms){
 int64_t den=(int64_t)a.rate*1000;
 return sh_axis_floor((int64_t)ms*a.pps*10000+den/2,den);
}
static inline int32_t sh_axis_ms(struct sh_wave_axis a,int64_t px){
 int64_t value=sh_axis_floor(px*1000*a.rate,(int64_t)a.pps*10000);
 return value<INT32_MIN?INT32_MIN:value>INT32_MAX?INT32_MAX:(int32_t)value;
}
void sh_deck_lane_axis(struct xz_canvas,struct xz_shell_rect,int amp,const struct xz_shell *,const struct xz_shell_model *,const struct xz_shell_deck *,int64_t first_px,struct sh_wave_axis);
#endif
