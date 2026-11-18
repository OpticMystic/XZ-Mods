#ifndef XZ_PRESENT_TIMING_H
#define XZ_PRESENT_TIMING_H
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
/* One presenter writer, external read-only observers. Entry word0 is a seqlock;
   header word4 is published after the complete entry. No file I/O or waiting. */
enum { XZ_PT_HEADER=8, XZ_PT_WORDS=64, XZ_PT_CAPACITY=128 };
__attribute__((visibility("default"))) uint32_t xz_present_timing_v1[XZ_PT_HEADER+XZ_PT_WORDS*XZ_PT_CAPACITY]={1,XZ_PT_HEADER,XZ_PT_WORDS,XZ_PT_CAPACITY};
/* [version, requested-enabled, presenter-cycle-latched-enabled, total cycles].
   The owner's normal-priority report worker may call the setter between cycles. */
__attribute__((visibility("default"))) uint32_t xz_present_timing_control_v1[4]={1,1,0,0};
__attribute__((visibility("default"))) void xz_receiver_present_trace_enable_v1(unsigned enabled){__atomic_store_n(&xz_present_timing_control_v1[1],!!enabled,__ATOMIC_RELEASE);}
static int xz_pt_enabled,xz_pt_initialized;
enum { PT_LOCKED=1u,PT_WOKE=2u,PT_COPIED=4u,PT_PREPARED=8u,PT_COPY_CPU=16u,PT_PREP_CPU=32u,PT_MONO=64u,PT_TARGET=128u,PT_OVERLAY=256u,PT_BACKGROUND=512u,PT_CONTIGUOUS=1024u,PT_TIMER=2048u };
struct xz_pt_target { uint64_t wall,query_wall,begin,end;uint32_t sequence,period,queries; };
struct xz_pt_frame { uint64_t begin,ready;struct xz_pt_target target;uint32_t id,ready_sequence; };
static uint32_t xz_pt_row[XZ_PT_WORDS],xz_pt_cycle;
static struct xz_pt_frame xz_pt_pending;
static __thread struct xz_pt_target xz_pt_query;
static __thread int xz_pt_capture;
static int xz_pt_copy_cpu_ok,xz_pt_prepare_cpu_ok;
#ifndef XZ_PT_TEST_CLOCK
static int xz_pt_clock(int id,uint64_t *out){
 struct {long sec,nsec;} t;
 if(syscall(SYS_clock_gettime,id,&t)||t.sec<0||t.nsec<0||t.nsec>=1000000000L){*out=0;return 0;}
 *out=(uint64_t)t.sec*1000000u+(uint64_t)t.nsec/1000u;return 1;
}
static uint32_t xz_pt_resolution(int id){
#ifdef SYS_clock_getres
 struct {long sec,nsec;} t;
 if(!syscall(SYS_clock_getres,id,&t)&&t.sec==0&&t.nsec>0&&t.nsec<1000000000L)return (uint32_t)t.nsec;
#endif
 return 0;
}
#endif
static uint64_t xz_pt_mono(void){uint64_t t;xz_pt_clock(CLOCK_MONOTONIC,&t);return t;}
static void xz_pt_u64(unsigned at,uint64_t value){xz_pt_row[at]=(uint32_t)value;xz_pt_row[at+1]=(uint32_t)(value>>32);}
static uint64_t xz_pt_get64(unsigned at){return (uint64_t)xz_pt_row[at]|(uint64_t)xz_pt_row[at+1]<<32;}
static void xz_pt_stamp(unsigned at){uint64_t t;if(!xz_pt_clock(CLOCK_MONOTONIC,&t))xz_pt_row[4]&=~PT_MONO;xz_pt_u64(at,t);}
static void xz_pt_begin(void){
 if(!++xz_pt_cycle)++xz_pt_cycle;
 xz_pt_enabled=__atomic_load_n(&xz_present_timing_control_v1[1],__ATOMIC_ACQUIRE)!=0;
 __atomic_store_n(&xz_present_timing_control_v1[2],(uint32_t)xz_pt_enabled,__ATOMIC_RELEASE);
 __atomic_store_n(&xz_present_timing_control_v1[3],xz_pt_cycle,__ATOMIC_RELEASE);
 if(!xz_pt_enabled){xz_pt_capture=0;memset(&xz_pt_pending,0,sizeof xz_pt_pending);return;}
 if(!xz_pt_initialized){
  xz_pt_initialized=1;xz_present_timing_v1[5]=xz_pt_resolution(CLOCK_MONOTONIC);xz_present_timing_v1[6]=xz_pt_resolution(CLOCK_THREAD_CPUTIME_ID);
  long tid=syscall(SYS_gettid);xz_present_timing_v1[7]=tid>0?(uint32_t)tid:0;
 }
 memset(xz_pt_row,0,sizeof xz_pt_row);xz_pt_row[1]=xz_pt_cycle;xz_pt_row[4]=PT_MONO;
}
static void xz_pt_wait_entry(void){if(!xz_pt_enabled)return;xz_pt_stamp(16);}
static void xz_pt_wait_register(uint32_t seq,uint64_t wall,uint64_t mono,uint32_t period){if(!xz_pt_enabled)return;
 xz_pt_row[4]|=PT_LOCKED;xz_pt_row[5]=seq;xz_pt_row[12]=period;xz_pt_stamp(18);xz_pt_u64(22,mono);xz_pt_u64(56,wall);
}
static void xz_pt_wait_observed(uint32_t seq,uint64_t wall,uint64_t mono,int woke){if(!xz_pt_enabled)return;
 xz_pt_row[6]=seq;if(woke)xz_pt_row[4]|=PT_WOKE;xz_pt_u64(24,mono);xz_pt_u64(58,wall);
}
static void xz_pt_wait_exit(int timer){if(!xz_pt_enabled)return;if(timer)xz_pt_row[4]|=PT_TIMER;xz_pt_stamp(20);}
static void xz_pt_target_query(uint64_t wall,uint64_t query_wall,uint64_t begin,uint32_t seq,uint32_t period){
 if(!xz_pt_capture)return;
 xz_pt_query.wall=wall;xz_pt_query.query_wall=query_wall;xz_pt_query.begin=begin;xz_pt_query.end=xz_pt_mono();
 xz_pt_query.sequence=seq;xz_pt_query.period=period;xz_pt_query.queries++;
}
static void xz_pt_copy_begin(uint32_t sequence,int contiguous){if(!xz_pt_enabled)return;
 xz_pt_row[4]|=PT_COPIED;if(contiguous)xz_pt_row[4]|=PT_CONTIGUOUS;xz_pt_row[2]=xz_pt_pending.id;xz_pt_row[7]=sequence;xz_pt_row[8]=xz_pt_pending.ready_sequence;
 xz_pt_row[10]=xz_pt_pending.target.sequence;xz_pt_row[11]=xz_pt_pending.target.queries;xz_pt_row[13]=xz_pt_pending.target.period;
 if(xz_pt_pending.target.queries)xz_pt_row[4]|=PT_TARGET;
 xz_pt_u64(34,xz_pt_pending.begin);xz_pt_u64(36,xz_pt_pending.ready);xz_pt_u64(38,xz_pt_pending.target.wall);xz_pt_u64(40,xz_pt_pending.target.query_wall);
 xz_pt_u64(42,xz_pt_pending.target.begin);xz_pt_u64(44,xz_pt_pending.target.end);xz_pt_stamp(26);
 uint64_t t;xz_pt_copy_cpu_ok=xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&t);xz_pt_u64(30,t);
}
static void xz_pt_copy_end(void){if(!xz_pt_enabled)return;uint64_t t;int ok=xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&t);xz_pt_u64(32,t);xz_pt_stamp(28);if(ok&&xz_pt_copy_cpu_ok)xz_pt_row[4]|=PT_COPY_CPU;}
static void xz_pt_prepare_begin(void){if(!xz_pt_enabled)return;
 memset(&xz_pt_query,0,sizeof xz_pt_query);xz_pt_capture=1;xz_pt_stamp(46);uint64_t t;xz_pt_prepare_cpu_ok=xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&t);xz_pt_u64(50,t);
}
static void xz_pt_prepare_end(int ready,uint32_t sequence){if(!xz_pt_enabled)return;
 uint64_t t;int ok=xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&t);xz_pt_u64(52,t);xz_pt_stamp(48);xz_pt_capture=0;
 if(ok&&xz_pt_prepare_cpu_ok)xz_pt_row[4]|=PT_PREP_CPU;
 xz_pt_u64(60,xz_pt_query.wall);xz_pt_row[62]=xz_pt_query.sequence;xz_pt_row[63]=xz_pt_query.queries;
 memset(&xz_pt_pending,0,sizeof xz_pt_pending);
 if(ready){xz_pt_row[4]|=PT_PREPARED;xz_pt_row[3]=xz_pt_cycle;xz_pt_pending=(struct xz_pt_frame){xz_pt_get64(46),xz_pt_get64(48),xz_pt_query,xz_pt_cycle,sequence};}
}
static void xz_pt_cancel(void){memset(&xz_pt_pending,0,sizeof xz_pt_pending);}
static void xz_pt_publish(uint32_t sequence){if(!xz_pt_enabled)return;
 xz_pt_row[9]=sequence;xz_pt_stamp(54);
 uint32_t *entry=xz_present_timing_v1+XZ_PT_HEADER+(xz_pt_cycle%XZ_PT_CAPACITY)*XZ_PT_WORDS;
 __atomic_store_n(entry,xz_pt_cycle*2u+1u,__ATOMIC_SEQ_CST); /* invalidate before any payload stores */
 memcpy(entry+1,xz_pt_row+1,(XZ_PT_WORDS-1)*sizeof(uint32_t));
 __atomic_store_n(entry,xz_pt_cycle*2u,__ATOMIC_RELEASE);
 __atomic_store_n(&xz_present_timing_v1[4],xz_pt_cycle,__ATOMIC_RELEASE);
}
#endif
