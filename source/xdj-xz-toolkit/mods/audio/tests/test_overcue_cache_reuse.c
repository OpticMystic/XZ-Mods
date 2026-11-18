/* SPDX-License-Identifier: MIT */
#include "../overcue_stream.c"
#include "../stem_pool.c"
#include <assert.h>
int main(void){
 struct xz_oc_stream s={0};s.role_count=3;
 struct window *w=&s.window[2];w->valid=1;w->masks=1;w->first=5429;
 w->pcm[0]=malloc(WINDOW*2*sizeof(float));assert(w->pcm[0]);
 for(unsigned i=0;i<WINDOW*2;i++)w->pcm[0][i]=0.25f;
 assert(fill_slot(&s,2,5429,1)==1);
 for(unsigned i=0;i<WINDOW*2;i++)assert(w->pcm[0][i]==0.25f);
 w->access=1;assert(fill_slot(&s,2,5429,1)==1);assert(w->access==1);
 free(w->pcm[0]);puts("PASS returning selection preserves valid cached audio, including active readers");return 0;
}
