#define main existing_main
#include "test_overcue_seek.c"
#undef main
int main(void){
 struct xz_oc_stream s;init(&s);s.capture_state=2;s.capture_count=2048;s.capture_position=1000000;s.previous_position=990000;s.confirmations=1;
 for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<2;c++)s.capture[i*2+c]=reference_sample(1000000+i,c)/32768.0f;
 s.desired=1000000;s.request_masks=1|2<<4;s.loop_start=300000;s.running=1;test_hold_loop=1;
 assert(!pthread_create(&s.worker,NULL,stream_worker,&s));
 for(int i=0;i<10000&&!__atomic_load_n(&test_loop_parked,__ATOMIC_ACQUIRE);i++)delay();
 int parked=__atomic_load_n(&test_loop_parked,__ATOMIC_ACQUIRE);struct xz_oc_plan p;stream_plan(&s,levels,&p);
 struct window *w=find_window(&s,&p,1000000,63,0);int covered=w!=NULL;if(w)window_release(w);
 __atomic_store_n(&test_hold_loop,0,__ATOMIC_RELEASE);__atomic_store_n(&s.running,0,__ATOMIC_RELEASE);pthread_join(s.worker,NULL);
 printf("parked_unrelated_loop=%d current_covered=%d\n",parked,covered);cleanup(&s);if(!parked||!covered)return 1;
 init(&s);single_frames_read=0;s.capture_state=2;s.capture_count=2048;s.capture_position=300000;s.previous_position=290000;s.confirmations=1;
 for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<2;c++)s.capture[i*2+c]=reference_sample(300000+i,c)/32768.0f;
 s.desired=s.loop_start=300000;s.request_masks=1|2<<4;s.running=1;test_hold_loop=1;test_loop_parked=0;
 assert(!pthread_create(&s.worker,NULL,stream_worker,&s));for(int i=0;i<10000&&!__atomic_load_n(&test_loop_parked,__ATOMIC_ACQUIRE);i++)delay();
 assert(__atomic_load_n(&test_loop_parked,__ATOMIC_ACQUIRE));__atomic_store_n(&test_hold_loop,0,__ATOMIC_RELEASE);
 __atomic_store_n(&s.running,0,__ATOMIC_RELEASE);pthread_join(s.worker,NULL);
 w=find_window(&s,&p,300000,63,0);assert(w);window_release(w);assert(single_frames_read==2*WINDOW);
 printf("current_loop_coverage_ready=1 decoded_frames=%llu expected_one_window=%u\n",(unsigned long long)single_frames_read,2*WINDOW);cleanup(&s);
 init(&s);s.capture_state=2;s.capture_count=2048;s.capture_position=300000;s.previous_position=290000;s.confirmations=1;
 for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<2;c++)s.capture[i*2+c]=reference_sample(300000+i,c)/32768.0f;
 s.desired=s.loop_start=300000;s.request_masks=1|2<<4;s.loop_published=2;s.window[3].access=1;s.running=1;
 assert(!pthread_create(&s.worker,NULL,stream_worker,&s));covered=0;
 for(int i=0;i<2000&&!covered;i++){w=find_window(&s,&p,300000,63,0);if(w){covered=1;window_release(w);}else delay();}
 __atomic_store_n(&s.running,0,__ATOMIC_RELEASE);pthread_join(s.worker,NULL);s.window[3].access=0;
 printf("loop_refill_slot_borrowed=1 current_free_slot_covered=%d\n",covered);cleanup(&s);return covered?0:1;
}
