#ifndef XZ_PIPELINE_TIMING_H
#define XZ_PIPELINE_TIMING_H
/* Consumer-only writer. Producer measurements travel in the immutable slot.
   Off skips diagnostic clocks/publication, never the semantic blank/ready clocks. */
enum { XZ_PIPE_WORDS=48,XZ_PIPE_CAPACITY=128 };
__attribute__((visibility("default"))) uint32_t xz_present_pipeline_timing_v2[8+XZ_PIPE_WORDS*XZ_PIPE_CAPACITY]={2,8,XZ_PIPE_WORDS,XZ_PIPE_CAPACITY};
/* version, cycles, copied, unready, stale epoch, future target, packed after
   blank, late admission, rejected publication, cancellation, late target,
   waiter unavailable, preparation declined (serial handback). Consumer-only lifetime counters, also when trace is off. */
__attribute__((visibility("default"))) uint32_t xz_present_pipeline_counts_v1[13]={1};
static void dp_count(unsigned n){__atomic_add_fetch(&xz_present_pipeline_counts_v1[n],1u,__ATOMIC_RELAXED);}
enum { DP_TRACE_COPIED=1,DP_TRACE_NOFRAME=2,DP_TRACE_CANCEL=4,DP_TRACE_AFTERBLANK=8,DP_TRACE_REJECT=16,DP_TRACE_WAITFAIL=32 };
static uint32_t dp_trace_cycle,dp_trace_row[XZ_PIPE_WORDS];
static int dp_trace_enabled;
static void dp_trace64(unsigned at,uint64_t n){dp_trace_row[at]=(uint32_t)n;dp_trace_row[at+1]=(uint32_t)(n>>32);}
static void dp_trace_begin(uint32_t tick,uint64_t blank){
 dp_trace_enabled=__atomic_load_n(&xz_present_timing_control_v1[1],__ATOMIC_ACQUIRE)!=0;
 if(!dp_trace_enabled)return;
 memset(dp_trace_row,0,sizeof dp_trace_row);dp_trace_row[1]=++dp_trace_cycle;dp_trace_row[2]=tick;dp_trace64(8,blank);
}
static void dp_trace_slot(const struct xz_dp_meta *m){
 if(!dp_trace_enabled)return;
 dp_trace_row[3]=m->epoch;dp_trace_row[4]=m->token;dp_trace_row[5]=m->target_tick;dp_trace_row[6]=m->ready_tick;dp_trace_row[7]=m->cpu_valid;
 dp_trace64(10,m->target_wall);dp_trace64(12,m->begin_mono);dp_trace64(14,m->finished_mono);dp_trace64(16,m->packed_mono);dp_trace64(18,m->cpu_begin);dp_trace64(20,m->cpu_end);
}
static void dp_trace_end(unsigned flags){
 if(!dp_trace_enabled)return;dp_trace_row[32]=flags;dp_trace64(30,xz_pt_mono());
 uint32_t *entry=xz_present_pipeline_timing_v2+8+(dp_trace_cycle%XZ_PIPE_CAPACITY)*XZ_PIPE_WORDS;
 __atomic_store_n(entry,dp_trace_cycle*2u+1u,__ATOMIC_SEQ_CST);
 memcpy(entry+1,dp_trace_row+1,(XZ_PIPE_WORDS-1)*sizeof *entry);
 __atomic_store_n(entry,dp_trace_cycle*2u,__ATOMIC_RELEASE);__atomic_store_n(&xz_present_pipeline_timing_v2[4],dp_trace_cycle,__ATOMIC_RELEASE);
}
#endif
