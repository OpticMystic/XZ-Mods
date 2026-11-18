#define main existing_main
#include "test_overcue_seek.c"
#undef main
int main(void){
 struct xz_oc_stream s;init(&s);window(&s,0,20000,.125f);s.published=0;float pcm[BLOCK*2];uint32_t t[16];block(&s,20000,pcm,t);s.window[0].valid=0;
 unsigned silent=0;for(unsigned used=0;used<9275;){unsigned n=9275-used;if(n>BLOCK)n=BLOCK;for(unsigned i=0;i<n*2;i++)pcm[i]=.75f;
 xz_oc_stream_mix(&s,pcm,n,1000000,levels);for(unsigned i=0;i<n;i++)silent+=pcm[i*2]==0;used+=n;}
 struct xz_oc_status st;xz_oc_stream_status(&s,&st);assert(st.starve_runs==1&&st.starve_last_frames==9275&&st.starve_last_ms==210&&st.starvation==XZ_OC_STARVE_HELD);assert(silent==9275-(BLOCK-1));
 window(&s,1,1000000,.375f);s.published=1;block(&s,1000000,pcm,t);assert(fabsf(pcm[0]-.375f/BLOCK)<1e-6&&pcm[(BLOCK-1)*2]==.375f);
 xz_oc_stream_status(&s,&st);assert(st.starvation==XZ_OC_STARVE_NONE&&st.starve_last_frames==9275&&st.starve_fallback==XZ_OC_STARVE_HELD);
 printf("mute-plan starvation: callback_frames=9275 reported_ms=210 exactly_zero_frames=%u recovered_first=%.8f last=%.3f stale_metrics_retained=1\n",silent,pcm[0],pcm[(BLOCK-1)*2]);cleanup(&s);
 init(&s);float attenuated[3]={1,.6f,.25f};memcpy(s.previous_levels,attenuated,sizeof(attenuated));
 for(unsigned blockno=0;blockno<3;blockno++){for(unsigned i=0;i<BLOCK;i++)pcm[i*2]=pcm[i*2+1]=(float)(blockno*BLOCK+i)/512;
 xz_oc_stream_mix(&s,pcm,BLOCK,1000000,attenuated);for(unsigned i=0;i<BLOCK;i++)assert(pcm[i*2]==(float)(blockno*BLOCK+i)/2048);}
 puts("attenuated fallback follows current native samples times held0.25, no PCM replay");cleanup(&s);return 0;
}
