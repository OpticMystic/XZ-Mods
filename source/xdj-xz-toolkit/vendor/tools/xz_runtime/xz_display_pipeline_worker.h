#ifndef XZ_DISPLAY_PIPELINE_WORKER_H
#define XZ_DISPLAY_PIPELINE_WORKER_H
#include <semaphore.h>
#include "xz_display_pipeline_core.h"
/* Included after the compositor. Only this worker may prepare while owned. */
static struct xz_dp_core dp_core;
static struct xz_dp_mailbox dp_mailbox;
static sem_t dp_semaphore;
static uint32_t dp_pending,dp_worker_state,dp_busy,dp_prepare_declined;
static uint32_t dp_resolved;
enum { DP_COMPOSITOR_SERIAL,DP_COMPOSITOR_ASYNC_IDLE,DP_COMPOSITOR_ASYNC_BUSY };
static uint32_t dp_compositor;
#ifndef XZ_DP_ENTRY_HOOK
#define XZ_DP_ENTRY_HOOK(point) ((void)0)
#endif
/* The CAS precedes every active/epoch read and every compositor access. */
static int dp_enter_producer(void){
 XZ_DP_ENTRY_HOOK(0);
 uint32_t idle=DP_COMPOSITOR_ASYNC_IDLE;
 if(!__atomic_compare_exchange_n(&dp_compositor,&idle,DP_COMPOSITOR_ASYNC_BUSY,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
 XZ_DP_ENTRY_HOOK(1);return 1;
}
static void dp_leave_producer(void){
 __atomic_store_n(&dp_busy,0u,__ATOMIC_RELEASE);
 __atomic_store_n(&dp_compositor,DP_COMPOSITOR_ASYNC_IDLE,__ATOMIC_RELEASE);
}
static unsigned char *dp_allocations[2];
static int dp_state; /* consumer: 0 serial, 1 pipeline, 2 draining */
static int dp_set_cpu(unsigned cpu){cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);return !pthread_setaffinity_np(pthread_self(),sizeof set,&set);}
static void dp_notify(uint32_t tick,uint64_t target){
 struct xz_dp_request r={xz_dp_load(&dp_core.epoch),tick,dp_resolved,target};xz_dp_mail(&dp_mailbox,&r);
 if(!__atomic_exchange_n(&dp_pending,1u,__ATOMIC_ACQ_REL))sem_post(&dp_semaphore);
}
static void *dp_worker(void *unused){
 (void)unused;syscall(SYS_prctl,PR_SET_NAME,"xz-prepare",0,0,0);
 struct sched_param scheduling={.sched_priority=20};
 if(!dp_set_cpu(2)||pthread_setschedparam(pthread_self(),SCHED_FIFO,&scheduling)){
  __atomic_store_n(&dp_worker_state,2u,__ATOMIC_RELEASE);return NULL;
 }
 __atomic_store_n(&dp_worker_state,1u,__ATOMIC_RELEASE);
 for(;;){
  while(sem_wait(&dp_semaphore)){if(errno!=EINTR){__atomic_store_n(&dp_worker_state,2u,__ATOMIC_RELEASE);return NULL;}}
  __atomic_exchange_n(&dp_pending,0u,__ATOMIC_ACQ_REL);
  if(!dp_enter_producer())continue;
  __atomic_store_n(&dp_busy,1u,__ATOMIC_RELEASE);
  struct xz_dp_request r;
  if(!xz_dp_receive(&dp_mailbox,&r)){dp_leave_producer();continue;}
  int index=xz_dp_begin(&dp_core,&r);
  if(index>=0){
   struct xz_dp_slot *slot=&dp_core.slot[index];
   unsigned offset=xz_frame_copy_render_offset()*2u;
   slot->pixels=dp_allocations[index]+offset;
   int trace=__atomic_load_n(&xz_present_timing_control_v1[1],__ATOMIC_ACQUIRE)!=0;
   slot->meta.begin_mono=trace?xz_pt_mono():0;
   slot->meta.cpu_valid=trace&&xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&slot->meta.cpu_begin);
   dp_render_request=(struct xz_pipeline_request_v1){1,sizeof dp_render_request,slot->meta.token,r.epoch,r.target_wall};
   const uint8_t *pixels=composite_for_present(NULL,(uint16_t *)slot->pixels);
   dp_render_request.token=0;
   slot->meta.finished_mono=xz_pt_mono();
   int complete=pixels==slot->pixels;
   if(trace)slot->meta.cpu_valid&=xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&slot->meta.cpu_end);
   slot->meta.packed_mono=slot->meta.finished_mono;slot->meta.ready_tick=__atomic_load_n(&vsync_sequence,__ATOMIC_ACQUIRE);
   if(!complete&&xz_dp_load(&dp_core.active)&&r.epoch==xz_dp_load(&dp_core.epoch))__atomic_store_n(&dp_prepare_declined,1u,__ATOMIC_RELEASE);
   xz_dp_publish(&dp_core,(unsigned)index,complete);
  }
  dp_leave_producer();
 }
}
static int dp_start(void){
 const char *enabled=getenv("XZ_DISPLAY_PIPELINE");
 if(!enabled||strcmp(enabled,"1")||diagnostic_primary_enabled()||!mods_pipeline_eligible||!mods_pipeline_epoch||!mods_pipeline_prepare||!mods_pipeline_accept)return 0;
 if(sem_init(&dp_semaphore,0,0))return 0;
 for(unsigned i=0;i<2;i++)if(posix_memalign((void **)&dp_allocations[i],64,XZ_FRAME_BYTES+64u)){
  free(dp_allocations[0]);free(dp_allocations[1]);sem_destroy(&dp_semaphore);return 0;
 }
 pthread_t worker;
 if(xz_display_thread_create(&worker,dp_worker,NULL)){
  free(dp_allocations[0]);free(dp_allocations[1]);sem_destroy(&dp_semaphore);__atomic_store_n(&dp_worker_state,2u,__ATOMIC_RELEASE);return 0;
 }
 pthread_detach(worker);return 1;
}
static void dp_cancel(int lost){
 uint32_t epoch=xz_dp_epoch(&dp_core,0);mods_pipeline_epoch(epoch,0,lost);
 dp_state=2;__atomic_store_n(&dp_waiter_requested,2u,__ATOMIC_RELEASE);
}
/* Consumer never waits for preparation. It may remain quiescent until the
   previous producer exits and its own CPU2 affinity restoration succeeds. */
static int dp_drain(void){
 uint32_t idle=DP_COMPOSITOR_ASYNC_IDLE;
 if(!__atomic_compare_exchange_n(&dp_compositor,&idle,DP_COMPOSITOR_SERIAL,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)&&idle!=DP_COMPOSITOR_SERIAL)return 0;
 if(!dp_set_cpu(2)||xz_dp_load(&dp_waiter_applied)!=2)return 0;
 __atomic_store_n(&dp_render_owned,0u,__ATOMIC_RELEASE);dp_state=0;return 1;
}
static int dp_activate(void){
 if(xz_dp_load(&dp_compositor)!=DP_COMPOSITOR_SERIAL||xz_dp_load(&dp_worker_state)!=1||!xz_dp_load(&dp_requested)||!mods_pipeline_eligible())return 0;
 __atomic_store_n(&dp_prepare_declined,0u,__ATOMIC_RELEASE);
 uint32_t epoch=xz_dp_epoch(&dp_core,1);
 if(!mods_pipeline_epoch(epoch,1,0)){xz_dp_epoch(&dp_core,0);return 0;}
 __atomic_store_n(&dp_render_owned,1u,__ATOMIC_RELEASE);dp_resolved=0;
 if(!dp_set_cpu(3)){dp_cancel(0);return 0;}
 __atomic_store_n(&dp_waiter_requested,3u,__ATOMIC_RELEASE);dp_state=1;
 __atomic_store_n(&dp_compositor,DP_COMPOSITOR_ASYNC_IDLE,__ATOMIC_RELEASE);return 1;
}
#endif
