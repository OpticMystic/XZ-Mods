/* SPDX-License-Identifier: MIT */
#include "../overcue_stream.c"
#include <assert.h>
int main(void){
    struct xz_oc_stream s={0};s.rate=44100;s.capture_state=3;s.published=s.loop_published=-1;s.state=XZ_OC_BUFFERING;
    float levels[3]={1,1,0};memcpy(s.previous_levels,levels,sizeof(levels));
    float pcm[8]={.2f,-.2f,.3f,-.3f,.4f,-.4f,.5f,-.5f},original[8];memcpy(original,pcm,sizeof(pcm));
    xz_oc_stream_mix(&s,pcm,4,15000,levels);
    if(memcmp(original,pcm,sizeof(pcm))){fprintf(stderr,"FAIL uncovered stem mix silenced or attenuated native audio: %f expected %f\n",pcm[0],original[0]);assert(0);}
    puts("PASS uncovered stem mix preserves native audio bit-exact while replacement data is unavailable");
}
