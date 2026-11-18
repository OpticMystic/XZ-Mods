/* SPDX-License-Identifier: GPL-2.0-only
 * Worker-owned stereo WSOLA. Roelands/Verhelst, Eurospeech 1993:
 * https://www.isca-archive.org/eurospeech_1993/roelands93_eurospeech.html
 * Fixed memory and bounded correlation search; no allocation or device calls.
 * The reader supplies source-rate PCM; one shared alignment preserves stereo. */
#ifndef XZ_EXTRA_STRETCH_H
#define XZ_EXTRA_STRETCH_H
#include <math.h>
#include <stdint.h>
#include <string.h>

#define XZ_STRETCH_HOP 1024u
#define XZ_STRETCH_SEARCH 512u
#define XZ_STRETCH_INPUT (XZ_STRETCH_HOP * 2 + XZ_STRETCH_SEARCH * 2)
#define XZ_STRETCH_SOURCE 8192u
struct xz_stretch {
 float source[XZ_STRETCH_SOURCE * 2];
 float input[XZ_STRETCH_INPUT * 2];
 float tail[XZ_STRETCH_HOP * 2], output[XZ_STRETCH_HOP * 2];
 unsigned cursor, primed;
 int64_t source_first;
 uint32_t source_count;
};
typedef int (*xz_stretch_reader)(void *,int64_t,uint32_t,float *);
static void xz_stretch_reset(struct xz_stretch *s) { s->cursor = XZ_STRETCH_HOP; s->primed = 0; s->source_count = 0; }
static float xz_stretch_score(const struct xz_stretch *s,unsigned candidate,unsigned stride) {
 float dot=0,a=0,b=0;
 for(unsigned n=0;n<XZ_STRETCH_HOP;n+=stride)for(unsigned ch=0;ch<2;ch++){
  float x=s->tail[n*2+ch],y=s->input[(candidate+n)*2+ch];
  dot+=x*y;a+=x*x;b+=y*y;
 }
 if(a<1e-12f||b<1e-12f)return 0;
 float distance=fabsf((float)candidate-(float)XZ_STRETCH_SEARCH);
 return dot/sqrtf(a*b)-distance*1e-7f;
}
static int xz_stretch_grain(struct xz_stretch *s,double position,uint32_t rate,
                            xz_stretch_reader read,void *context) {
 double pitch_step=(double)rate/44100.0;
 int64_t first=(int64_t)floor(position-XZ_STRETCH_SEARCH*pitch_step);
 double last=position+(XZ_STRETCH_INPUT-XZ_STRETCH_SEARCH-1)*pitch_step;
 uint64_t count=(uint64_t)((int64_t)floor(last)-first)+2;
 if(count>XZ_STRETCH_SOURCE)return 0;
 uint32_t kept=0;
 if(s->source_count&&first>=s->source_first&&(uint64_t)(first-s->source_first)<s->source_count){
  uint32_t consumed=(uint32_t)(first-s->source_first);
  kept=s->source_count-consumed;
  memmove(s->source,s->source+consumed*2,kept*2*sizeof(float));
 }
 if(count>kept&&read(context,first+kept,(uint32_t)count-kept,s->source+kept*2))return 0;
 s->source_first=first;s->source_count=kept>count?kept:(uint32_t)count;
 for(unsigned i=0;i<XZ_STRETCH_INPUT;i++){
  double at=position+((int)i-(int)XZ_STRETCH_SEARCH)*pitch_step-(double)first;
  unsigned n=(unsigned)at;float fraction=(float)(at-n);
  for(unsigned ch=0;ch<2;ch++)
   s->input[i*2+ch]=s->source[n*2+ch]+fraction*(s->source[(n+1)*2+ch]-s->source[n*2+ch]);
 }
 unsigned chosen=XZ_STRETCH_SEARCH;
 if(s->primed){
  float best=xz_stretch_score(s,chosen,8);
  for(unsigned candidate=0;candidate<=XZ_STRETCH_SEARCH*2;candidate+=8){
   float score=xz_stretch_score(s,candidate,8);
   if(score>best){best=score;chosen=candidate;}
  }
  unsigned low=chosen>7?chosen-7:0,high=chosen+7;
  if(high>XZ_STRETCH_SEARCH*2)high=XZ_STRETCH_SEARCH*2;
  best=xz_stretch_score(s,chosen,4);
  for(unsigned candidate=low;candidate<=high;candidate++){
   float score=xz_stretch_score(s,candidate,4);
   if(score>best){best=score;chosen=candidate;}
  }
 }
 for(unsigned n=0;n<XZ_STRETCH_HOP;n++){
  float weight=((float)n+.5f)/(float)XZ_STRETCH_HOP;
  for(unsigned ch=0;ch<2;ch++){
   float value=s->input[(chosen+n)*2+ch];
   s->output[n*2+ch]=s->primed?s->tail[n*2+ch]+weight*(value-s->tail[n*2+ch]):value;
   s->tail[n*2+ch]=s->input[(chosen+n+XZ_STRETCH_HOP)*2+ch];
  }
 }
 s->cursor=0;s->primed=1;return 1;
}
static int xz_stretch_render(struct xz_stretch *s,double position,double step,uint32_t rate,
                             xz_stretch_reader read,void *context,float *out,unsigned frames) {
 if(!s||!read||!out||rate<32000||rate>96000||!isfinite(position)||!isfinite(step)||
    position<0||position>UINT32_MAX||step<=0||step>24||frames>1024)return 0;
 for(unsigned done=0;done<frames;){
  if(s->cursor==XZ_STRETCH_HOP&&!xz_stretch_grain(s,position+done*step,rate,read,context))return 0;
  unsigned count=XZ_STRETCH_HOP-s->cursor;
  if(count>frames-done)count=frames-done;
  memcpy(out+done*2,s->output+s->cursor*2,count*2*sizeof(float));
  s->cursor+=count;done+=count;
 }
 return 1;
}
#endif
