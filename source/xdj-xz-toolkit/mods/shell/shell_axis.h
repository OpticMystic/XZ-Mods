#ifndef XZ_SHELL_AXIS_H
#define XZ_SHELL_AXIS_H
#include "shell_internal.h"
#include <limits.h>
/* At the 120 BPM reference, pps/2 pixels are one beat. The source grid,
   never the fader or SYNC state, maps audio timestamps into that coordinate.
   Without a grid, original BPM supplies scale only, not a beat-phase claim. */
struct sh_wave_axis {
 int pps; uint32_t rate;
 const struct xz_shell_beat *grid;
 uint32_t count,lo,hi;
 int32_t regular,bpm;
};
static inline int64_t sh_axis_floor(int64_t a,int64_t b){int64_t q=a/b;return q-((a%b)!=0&&a<0);}
static inline struct sh_wave_axis sh_axis(int pps,const struct xz_shell_deck *d){
 struct sh_wave_axis a={.pps=pps,.rate=10000u};
 if(sh_has(d,XZ_SHELL_F_TRACK_BPM)&&d->track_bpm_x100>0&&d->track_bpm_x100<=1000000)a.bpm=d->track_bpm_x100;
 if(pps>=2&&sh_has(d,XZ_SHELL_F_GRID)&&d->grid&&d->grid_count>=2){a.grid=d->grid;a.count=d->grid_count;a.hi=a.count-1;}
 return a;
}
/* The model owns sorted source timestamps. Search and viewport validation are
   bounded even for a malformed model; there is no persistent pointer cache. */
static inline uint32_t sh_axis_lower(const struct xz_shell_beat *g,uint32_t n,int32_t ms){
 uint32_t lo=0,hi=n;
 while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(g[mid].ms<ms)lo=mid+1;else hi=mid;}
 return lo;
}
static inline uint32_t sh_axis_segment(struct sh_wave_axis a,int32_t ms){
 uint32_t i=sh_axis_lower(a.grid+a.lo,a.hi-a.lo+1,ms)+a.lo;
 if(i>a.lo)i--;
 return i>=a.hi?a.hi-1:i;
}
/* Validate only the beats that can reach this lane, with guard segments on
   both sides. This reads at most 112 adjacent intervals at any zoom.
   Invalid/duplicate timestamps or skipped beat labels use one linear fallback
   for the entire lane, including its peaks and markers. */
static inline struct sh_wave_axis sh_axis_window(struct sh_wave_axis a,int32_t ms,int width){
 if(!a.grid||a.pps<2)return a;
 uint32_t centre=sh_axis_lower(a.grid,a.count,ms);
 if(centre>=a.count)centre=a.count-1;
 unsigned radius=(unsigned)(width>0?width:0)/(unsigned)(a.pps/2)+4u;
 if(radius>56u)radius=56u;
 a.lo=centre>radius?centre-radius:0;
 a.hi=(uint64_t)centre+radius<a.count?centre+radius:a.count-1;
 if(a.lo==a.hi){if(a.lo)a.lo--;else a.hi++;}
 int good=a.grid[0].ms>=0&&a.grid[a.count-1].ms>a.grid[0].ms;
 int32_t step=0;int regular=1;
 for(uint32_t i=a.lo;i<=a.hi;i++){
  const struct xz_shell_beat *b=&a.grid[i];
  if(b->ms<0||b->beat_in_bar<1||b->beat_in_bar>4)good=0;
  if(i>a.lo){
   int64_t delta=(int64_t)b->ms-a.grid[i-1].ms;
   if(delta<=0||b->beat_in_bar!=a.grid[i-1].beat_in_bar%4+1)good=0;
   if(!step&&delta>0)step=(int32_t)delta;
   else if(delta!=step)regular=0;
  }
 }
 if((ms>=a.grid[0].ms&&ms<=a.grid[a.count-1].ms)&&
    (ms<a.grid[a.lo].ms||ms>a.grid[a.hi].ms))good=0;
 if(!good)a.grid=NULL;
 else a.regular=regular?step:0;
 return a;
}
static inline int64_t sh_axis_px(struct sh_wave_axis a,int32_t ms){
 if(a.pps<=0)return 0;
 if(a.grid){
  uint32_t i=a.regular?a.lo:sh_axis_segment(a,ms);
  int64_t span=a.regular?a.regular:(int64_t)a.grid[i+1].ms-a.grid[i].ms;
  if(span>0)return (int64_t)i*(a.pps/2)+sh_axis_floor(((int64_t)ms-a.grid[i].ms)*(a.pps/2)+span/2,span);
 }
 if(a.bpm)return sh_axis_floor((int64_t)ms*a.pps*a.bpm+6000000,12000000);
 int64_t den=(int64_t)(a.rate?a.rate:10000u)*1000;
 return sh_axis_floor((int64_t)ms*a.pps*10000+den/2,den);
}
static inline int32_t sh_axis_ms(struct sh_wave_axis a,int64_t px){
 if(a.pps<=0)return 0;
 if(px<=sh_axis_px(a,INT32_MIN))return INT32_MIN;
 if(px>=sh_axis_px(a,INT32_MAX))return INT32_MAX;
 int64_t value;
 if(a.grid){
  int64_t ordinal=sh_axis_floor(px,a.pps/2);
  uint32_t i=a.regular?a.lo:ordinal<(int64_t)a.lo?a.lo:ordinal>=(int64_t)a.hi?a.hi-1:(uint32_t)ordinal;
  int64_t span=a.regular?a.regular:(int64_t)a.grid[i+1].ms-a.grid[i].ms;
  value=a.grid[i].ms+sh_axis_floor((px-(int64_t)i*(a.pps/2))*span,a.pps/2);
 }else if(a.bpm)value=sh_axis_floor(px*12000000,(int64_t)a.pps*a.bpm);
 else value=sh_axis_floor(px*1000*(a.rate?a.rate:10000u),(int64_t)a.pps*10000);
 return value<INT32_MIN?INT32_MIN:value>INT32_MAX?INT32_MAX:(int32_t)value;
}
void sh_deck_lane_axis(struct xz_canvas,struct xz_shell_rect,int amp,const struct xz_shell *,const struct xz_shell_model *,const struct xz_shell_deck *,int64_t first_px,struct sh_wave_axis);
#endif
