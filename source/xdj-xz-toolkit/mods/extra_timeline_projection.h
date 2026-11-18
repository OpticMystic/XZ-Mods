#ifndef XZ_EXTRA_TIMELINE_PROJECTION_H
#define XZ_EXTRA_TIMELINE_PROJECTION_H
#include "extra_decks.h"
#include <math.h>
#include <limits.h>
/* at_ms remains the observation time, not the time of the earlier completed tag.
   Callers retain their freshness, generation and discontinuity qualifications. */
static inline double xz_extra_project_frames(double frame,unsigned source_rate,double rate,
 uint32_t age,uint64_t sink,uint64_t wire,int wire_valid){
 double ms=age;
 if(wire_valid&&sink&&wire>=sink)ms+=(double)(wire-sink)*1000./44100.;
 return frame+ms*rate*source_rate/1000.;
}
/* When the completed (heard) audio reached timeline source_frame, in monotonic microseconds: the observation
   time less the wire gap between the tagged block end and the observed wire frame. 0 without a fresh exact
   completion coordinate (older than 90 ms, gap over 100 ms) or an applied rate. */
static inline int xz_extra_timeline_heard_us(const struct xz_extra_timeline *t,uint64_t now_mono_us,uint64_t *captured_us){
 const uint32_t req_flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_SINK_FRAME|
  XZ_EXTRA_TIMELINE_APPLIED_RATE;
 if((t->flags&req_flags)!=req_flags||t->source_rate<8000||!isfinite(t->completed_rate)||t->completed_rate<=0||
    !t->route_epoch||!t->load_revision||!t->transport_generation||!t->sink_frame||
    t->observed_wire_frame<t->sink_frame)
  return 0;
 uint32_t now_ms=(uint32_t)(now_mono_us/1000u);
 uint32_t age_ms=now_ms-t->at_ms;
 if((int32_t)age_ms<0||age_ms>90u)return 0;
 uint64_t cap_us=(uint64_t)t->at_ms*1000u+(now_mono_us%1000u);
 if(cap_us>now_mono_us)cap_us=now_mono_us;
 /* The source tag belongs to the completed block end, not the later observation.
    Keep the existing monotonic mapping and subtract only the known wire gap. */
 uint64_t gap=t->observed_wire_frame-t->sink_frame;
 if(gap>4410u)return 0; /* 100 ms: refuse stale or inconsistent completion coordinates */
 uint64_t lag_us=gap*1000000ULL/44100u;
 if(lag_us>cap_us)return 0;
 *captured_us=cap_us-lag_us;
 return 1;
}
static inline int xz_extra_project_ms_limit(const struct xz_extra_deck *d,uint32_t now,uint32_t horizon,double *out){
 const struct xz_extra_timeline *t=&d->timeline;
 uint32_t age=now-t->at_ms;
 if(!(t->flags&XZ_EXTRA_TIMELINE_ANCHOR)||!t->source_rate||!t->at_ms||age>250u||
    t->source_frame>(uint64_t)INT32_MAX*t->source_rate/1000u)return 0;
 double base=(double)t->source_frame*1000./t->source_rate,p=base;
 int moving=t->flags&XZ_EXTRA_TIMELINE_MOTION?t->moving:
  ((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING))&&d->playing);
 if(moving&&(t->flags&XZ_EXTRA_TIMELINE_RATE)){
  if(age>horizon)age=horizon;
  p=xz_extra_project_frames((double)t->source_frame,t->source_rate,t->rate_ppm/1000000.,
    age,t->sink_frame,t->observed_wire_frame,t->flags&XZ_EXTRA_TIMELINE_SINK_FRAME)*1000./t->source_rate;
  if((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_LOOP))&&d->loop.active&&d->loop.out_ms>d->loop.in_ms&&
     base>=d->loop.in_ms&&base<d->loop.out_ms&&(p<d->loop.in_ms||p>=d->loop.out_ms)){
   double span=(double)d->loop.out_ms-d->loop.in_ms;
   p=d->loop.in_ms+fmod(p-d->loop.in_ms,span);if(p<d->loop.in_ms)p+=span;
  }
 }
 if(p<0)p=0;
 if(p>INT32_MAX)p=INT32_MAX;
 if((d->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL))&&d->total_ms>0&&p>d->total_ms)p=d->total_ms;
 *out=p;return 1;
}
static inline int xz_extra_project_ms_precise(const struct xz_extra_deck *d,uint32_t now,double *out){
 return xz_extra_project_ms_limit(d,now,50u,out);
}
static inline int xz_extra_project_ms(const struct xz_extra_deck *d,uint32_t now,int32_t *out){
 double p;if(!xz_extra_project_ms_precise(d,now,&p))return 0;*out=(int32_t)p;return 1;
}
#endif
