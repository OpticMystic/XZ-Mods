/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../stem_media.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef XZ_TEST_ALLOC_FAILURES
extern unsigned xz_test_failed_index_allocations,xz_test_failed_cache_allocations;
#endif
#define N 192
#define MAX 8192
static uint32_t rng=431;static uint32_t rand32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char**argv){if(argc!=3)return 2;int raw=atoi(argv[2]);struct xz_stem_media *m=xz_stem_media_open(argv[1],raw),*r=xz_stem_media_open(argv[1],raw);if(!m||!r)return 3;uint64_t total=xz_stem_media_frames(m),at[N];size_t counts[N];float *expected=calloc((size_t)N*MAX*2,sizeof(float)),*buf=malloc(MAX*2*sizeof(float));if(!expected||!buf)return 4;
for(unsigned i=0;i<N;i++){at[i]=((uint64_t)rand32()*total)>>32;counts[i]=1+rand32()%MAX;}
const uint64_t early[]={0,1,112,113,1104,1105,1151,1152,2047,2048,3071,3072,3873,3874,4095,4096,8191,8192,16383,16384};for(unsigned i=0;i<20;i++){at[i]=early[i];counts[i]=4096;}at[20]=total-1;at[21]=total;at[22]=total+10;
for(uint64_t p=0;p<total;){size_t n=total-p;if(n>MAX)n=MAX;if(xz_stem_media_read(r,p,n,buf))return 5;for(int i=0;i<N;i++){uint64_t lo=p>at[i]?p:at[i],hi=p+n<at[i]+counts[i]?p+n:at[i]+counts[i];if(lo<hi)memcpy(expected+(size_t)i*MAX*2+(lo-at[i])*2,buf+(lo-p)*2,(hi-lo)*2*sizeof(float));}p+=n;}
uint32_t hash=2166136261u;unsigned mismatches=0;for(int i=0;i<N;i++){if(xz_stem_media_read(m,at[i],counts[i],buf))return 6;for(size_t j=0;j<counts[i]*2;j++){float want=expected[(size_t)i*MAX*2+j];if(!isfinite(buf[j])||buf[j]!=want){if(mismatches++==0)fprintf(stderr,"FIRST_DIFFERENT read=%d position=%llu sample=%zu expected=%.9g actual=%.9g\n",i,(unsigned long long)at[i],j,want,buf[j]);}uint32_t b;memcpy(&b,buf+j,4);hash=(hash^b)*16777619u;}}
float z[32];if(xz_stem_media_read(m,-8,16,z))return 7;for(int i=0;i<16;i++)if(z[i])return 8;if(xz_stem_media_read(m,0,262145,z)!=-1)return 9;float one[2];if(xz_stem_media_read(m,INT64_MIN,1,one)||one[0]||one[1])return 10;
xz_stem_media_close(m);xz_stem_media_close(r);for(int i=0;i<12;i++){m=xz_stem_media_open(argv[1],raw);if(!m||xz_stem_media_read(m,113,1,one))return 11;xz_stem_media_close(m);}free(expected);free(buf);
#ifdef XZ_TEST_ALLOC_FAILURES
if(!xz_test_failed_index_allocations||!xz_test_failed_cache_allocations)return 77;
#endif
printf("{\"reads\":%d,\"untrimmed\":%d,\"mismatches\":%u,\"hash\":%u}\n",N,raw,mismatches,hash);return mismatches?12:0;}
