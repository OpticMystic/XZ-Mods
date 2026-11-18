/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#error "AIFF acceptance requires active assertions"
#endif
#include "../aiff_reader.h"
#include <assert.h>
#include <stdlib.h>

int main(int argc,char **argv) {
 assert(argc==2);
 const char *valid[]={"pcm8.aiff","pcm16.aiff","pcm24.aiff","pcm32.aiff","sowt.aifc","float.aifc","offset.aiff","reordered.aiff"};
 const float want[]={-.5f,.25f,0,-.25f,.5f,-1};
 char path[1024];
 for(unsigned n=0;n<sizeof valid/sizeof *valid;n++){
  snprintf(path,sizeof path,"%s/%s",argv[1],valid[n]);
  struct xz_aiff a;
  assert(xz_aiff_open(&a,path)&&a.rate==48000&&a.channels==2&&a.frames==3);
  float pcm[6];assert(xz_aiff_read(&a,3,pcm)==3);
  for(unsigned i=0;i<6;i++)assert(fabsf(pcm[i]-want[i])<1e-6f);
  assert(xz_aiff_seek(&a,1)&&xz_aiff_read(&a,1,pcm)==1);
  assert(pcm[0]==0&&pcm[1]==-.25f);
  assert(!xz_aiff_seek(&a,4));
  fclose(a.file);
 }
 const char *bad[]={"truncated.aiff","oversized.aiff","bad-rate.aiff","bad-channels.aiff","bad-offset.aiff",
                    "duplicate.aiff","compressed.aifc","no-version.aifc","bad-version.aifc","bad-name.aifc"};
 for(unsigned n=0;n<sizeof bad/sizeof *bad;n++){
  snprintf(path,sizeof path,"%s/%s",argv[1],bad[n]);struct xz_aiff a;
  assert(!xz_aiff_open(&a,path));if(a.file)fclose(a.file);
 }
 puts("PASS AIFF 8/16/24/32-bit PCM, AIFC sowt/float, chunk order/padding/offset, seek and malformed files");
 return 0;
}
