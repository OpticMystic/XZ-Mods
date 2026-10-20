/* SPDX-License-Identifier: GPL-2.0-only */
#ifdef NDEBUG
#error "Stretch acceptance requires active assertions"
#endif
#include "audio/stretch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static struct xz_stretch stretch;
static float output[44100*3*2];
static unsigned reads;
struct input { uint32_t rate; int silent, fail, started; int64_t next; unsigned backwards; };
static int source(void *context,int64_t first,uint32_t count,float *out) {
 struct input *in=context;reads++;
 assert(count<=XZ_STRETCH_SOURCE);
 if(in->fail)return -1;
 if(in->started&&first<in->next)in->backwards++;
 in->started=1;in->next=first+count;
 for(uint32_t i=0;i<count;i++)for(unsigned ch=0;ch<2;ch++){
  int64_t frame=first+i;
  out[i*2+ch]=frame<0||in->silent?0:(float)(.4*sin(6.283185307179586*(ch?660:440)*(double)frame/in->rate));
 }
 return 0;
}
static double frequency(unsigned channel,unsigned start,unsigned count) {
 unsigned crossings=0;double first=0,last=0;
 for(unsigned i=start+1;i<start+count;i++){
  double a=output[(i-1)*2+channel],b=output[i*2+channel];
  if(a<=0&&b>0){
   double at=(double)i-1-a/(b-a);
   if(!crossings)first=at;
   last=at;crossings++;
  }
 }
 assert(crossings>100);
 return (crossings-1)*44100.0/(last-first);
}
int main(void) {
 const double speeds[]={.1,.5,.84,1,1.16,2};
 clock_t began=clock();
 for(unsigned r=0;r<3;r++)for(unsigned t=0;t<sizeof speeds/sizeof *speeds;t++){
  struct input in={.rate=r==0?32000:r==1?44100:96000};
  double step=(double)in.rate/44100*speeds[t],position=0;
  xz_stretch_reset(&stretch);
  for(unsigned done=0;done<44100*3;){
   unsigned n=44100*3-done;if(n>256)n=256;
   assert(xz_stretch_render(&stretch,position,step,in.rate,source,&in,output+done*2,n));
   position+=n*step;done+=n;
  }
  assert(fabs(position-3.0*in.rate*speeds[t])<.001);
  assert(in.backwards==0); /* Overlap reuse keeps the decoder sequential. */
  for(unsigned ch=0;ch<2;ch++){
   double energy=0;
   for(unsigned i=44100;i<44100*3;i++){
    float x=output[i*2+ch];assert(isfinite(x)&&fabsf(x)<=.401f);energy+=x*x;
   }
   energy/=88200;
   double hz=frequency(ch,44100,88200),wanted=ch?660:440;
   printf("rate=%u tempo=%.2f channel=%u hz=%.3f energy=%.5f\n",in.rate,speeds[t],ch,hz,energy);
   assert(fabs(hz-wanted)<1.5&&energy>.05&&energy<.1);
   if(speeds[t]==1){
    double error=0;
    for(unsigned i=44100;i<44100*3;i++){
     double d=output[i*2+ch]-.4*sin(6.283185307179586*wanted*i/44100.0);
     error+=d*d;
    }
    assert(sqrt(error/88200)<.003);
   }
  }
 }
 struct input silent={.rate=44100,.silent=1};
 xz_stretch_reset(&stretch);
 assert(xz_stretch_render(&stretch,10000,1,44100,source,&silent,output,256));
 for(unsigned i=0;i<512;i++)assert(output[i]==0); /* A reset never carries old-track audio. */
 xz_stretch_reset(&stretch);silent.fail=1;
 assert(!xz_stretch_render(&stretch,10000,1,44100,source,&silent,output,256));
 assert(!xz_stretch_render(&stretch,0,NAN,44100,source,&silent,output,256));
 printf("PASS pitch-preserving stereo, duration tags, silence/reset, bounded reads=%u; host CPU %.3fs for 54s audio\n",
        reads,(double)(clock()-began)/CLOCKS_PER_SEC);
 return 0;
}
