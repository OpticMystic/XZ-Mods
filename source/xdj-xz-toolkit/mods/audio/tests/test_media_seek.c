/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error "Seek acceptance requires active assertions"
#endif
#include "../stem_media.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
 assert(argc==2);
 struct xz_stem_media *reference=xz_stem_media_open(argv[1],0),*seekable=xz_stem_media_open(argv[1],0);
 assert(reference&&seekable&&xz_stem_media_rate(reference)==44100);
 uint64_t frames=xz_stem_media_frames(reference);
 assert(frames==xz_stem_media_frames(seekable)&&frames<1000000);
 float *linear=calloc((size_t)frames*2,sizeof *linear);assert(linear);
 for(uint64_t at=0;at<frames;){
  size_t n=(size_t)(frames-at);if(n>65536)n=65536;
  assert(!xz_stem_media_read(reference,(int64_t)at,n,linear+at*2));at+=n;
 }
 const int64_t positions[]={0,441000,113,220500,440999,0,44100,529000,100};
 float a[2048],b[2048];float worst=0;
 for(unsigned k=0;k<sizeof positions/sizeof *positions;k++){
  for(unsigned n=0;n<1024;n++)for(unsigned ch=0;ch<2;ch++){
   uint64_t at=(uint64_t)positions[k]+n;
   a[n*2+ch]=at<frames?linear[at*2+ch]:0;
  }
  int result=xz_stem_media_read(seekable,positions[k],1024,b);
  if(result)fprintf(stderr,"Seek/read failed at frame %lld\n",(long long)positions[k]);
  assert(!result);
  for(unsigned n=0;n<2048;n++){float error=fabsf(a[n]-b[n]);if(error>worst)worst=error;}
 }
 assert(worst<1e-5f);
 free(linear);xz_stem_media_close(seekable);xz_stem_media_close(reference);
 printf("PASS MP3 seeks match sequential reference PCM, forward/backward/start/EOF, max error %.8f\n",worst);
 return 0;
}
