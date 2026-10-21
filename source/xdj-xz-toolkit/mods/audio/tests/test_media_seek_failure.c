/* SPDX-License-Identifier: MIT */
#include "../stem_media.h"
#include "../vendor/dr_libs/dr_mp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
drmp3_bool32 __real_drmp3_seek_to_pcm_frame(drmp3*,drmp3_uint64);
drmp3_bool32 __wrap_drmp3_seek_to_pcm_frame(drmp3 *m,drmp3_uint64 frame){drmp3_bool32 ok=__real_drmp3_seek_to_pcm_frame(m,frame);if(failures){--failures;return DRMP3_FALSE;}return ok;}
int main(int argc,char **argv){if(argc!=2)return 2;struct xz_stem_media *m=xz_stem_media_open(argv[1],0),*r=xz_stem_media_open(argv[1],0);if(!m||!r)return 3;float a[1024],b[1024];if(xz_stem_media_read(m,10000,512,a))return 4;failures=2;if(xz_stem_media_read(m,100000,512,a)!=-1||failures)return 5;if(xz_stem_media_read(m,12288,512,a)||xz_stem_media_read(r,12288,512,b))return 6;unsigned different=0;for(unsigned i=0;i<1024;i++)if(a[i]!=b[i]){if(!different)fprintf(stderr,"FIRST_DIFFERENT sample=%u actual=%.9g expected=%.9g\n",i,a[i],b[i]);++different;}xz_stem_media_close(m);xz_stem_media_close(r);printf("{\"mismatches\":%u,\"failed_seeks_injected\":2}\n",different);return different?7:0;}
