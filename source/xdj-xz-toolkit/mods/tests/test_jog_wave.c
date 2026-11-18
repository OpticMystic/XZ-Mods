/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../jog_display_wave.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned sum(struct xz_extra_column c){return (unsigned)c.low+c.mid+c.high;}
static void reduce_check(unsigned n,unsigned partition){
 struct xz_extra_column *in=malloc(n*sizeof *in);assert(in);
 for(unsigned i=0;i<n;i++)in[i]=(struct xz_extra_column){i*17u,i*37u,i*53u};
 in[n-1]=(struct xz_extra_column){255,255,255};
 struct xz_jd_overview s;assert(xz_jd_overview_begin(&s,n,0));
 for(unsigned first=0;first<n;){
  unsigned k=n-first;if(k>partition)k=partition;
  assert(xz_jd_overview_add(&s,first,in+first,k));first+=k;
 }
 assert(xz_jd_overview_complete(&s));
 for(unsigned cell=0;cell<300;cell++){
  unsigned lo=(uint64_t)cell*n/300,hi=(uint64_t)(cell+1)*n/300;if(hi<=lo)hi=lo+1;
  struct xz_extra_column best={0};
  for(unsigned i=lo;i<hi;i++)if(sum(in[i])>sum(best))best=in[i];
  assert(!memcmp(&s.column[cell],&best,sizeof best));
 }
 struct xz_jd_wave wave;assert(xz_jd_overview_encode(&s,&wave));
 assert(!wave.payload[0]);
 for(unsigned cell=0;cell<300;cell++){
  const uint8_t *p=wave.payload+1+cell*14;
  unsigned expected=sum(s.column[cell])*63u/765u;
  assert(p[4]==expected&&p[5]==expected&&!p[6]&&!memcmp(p,p+7,7));
 }
 free(in);
}
int main(int argc,char **argv){
 assert(argc==2);
 unsigned counts[]={1,2,299,300,301,511,512,513,1025,1000000};
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
 for(unsigned i=0;i<sizeof counts/sizeof counts[0];i++){
  reduce_check(counts[i],512);reduce_check(counts[i],37);
 }
 clock_gettime(CLOCK_MONOTONIC,&end);
 printf("PASS overview partitions, full-track peaks and terminal samples; test milliseconds %.3f; overview bytes %zu; asset bytes %zu\n",
        (end.tv_sec-begin.tv_sec)*1000.+(end.tv_nsec-begin.tv_nsec)/1e6,sizeof(struct xz_jd_overview),sizeof(struct xz_jd_wave));
 struct xz_jd_overview s;struct xz_extra_column col={1,2,3};
 assert(!xz_jd_overview_begin(&s,0,0)&&!xz_jd_overview_begin(&s,1000001,0));
 assert(xz_jd_overview_begin(&s,513,0));
 assert(!xz_jd_overview_add(&s,1,&col,1)&&!xz_jd_overview_add(&s,0,&col,513));
 assert(!xz_jd_overview_complete(&s));
 uint8_t input[1800],packet[64];struct xz_jd_wave wave;
 for(unsigned i=0;i<1800;i++)input[i]=(uint8_t)(i*37u+19u);
 FILE *out=fopen(argv[1],"wb");assert(out);
 for(unsigned side=1;side<=2;side++)for(unsigned reset=0;reset<=1;reset++){
  xz_jd_wave_encode_rgb(input,!reset,&wave);
  for(unsigned index=0;index<73;index++){
   assert(xz_jd_wave_packet(&wave,side,index,packet));assert(fwrite(packet,1,64,out)==64);
  }
 }
 assert(!fclose(out));
 assert(!xz_jd_wave_packet(&wave,0,0,packet)&&!xz_jd_wave_packet(&wave,3,0,packet));
 assert(!xz_jd_wave_packet(&wave,1,73,packet));
 puts("PASS 4 native RGB producer comparison streams ready");return 0;
}
