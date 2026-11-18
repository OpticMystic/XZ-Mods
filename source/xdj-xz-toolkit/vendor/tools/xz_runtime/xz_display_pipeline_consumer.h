#ifndef XZ_DISPLAY_PIPELINE_CONSUMER_H
#define XZ_DISPLAY_PIPELINE_CONSUMER_H
/* Event-age admission is deliberately conservative, not a claim that a
   userspace ioctl-return timestamp measures the panel's hardware blank. */
#define XZ_DP_EVENT_ADMISSION_US 250u
static void dp_note_copy(uint32_t sequence,uint32_t *presented_sequence){
 if(*presented_sequence&&sequence-*presented_sequence>1u&&sequence-*presented_sequence<120u)present_missed_vsync+=sequence-*presented_sequence-1u;
 *presented_sequence=sequence;
 struct timeval now;gettimeofday(&now,NULL);
 if(last_present_started.tv_sec){
  float interval=(float)((now.tv_sec-last_present_started.tv_sec)*1000L)+(float)(now.tv_usec-last_present_started.tv_usec)/1000.0f;
  if(interval>=0&&interval<1000){
   if(!present_interval_min_ms||interval<present_interval_min_ms)present_interval_min_ms=interval;
   if(interval>present_interval_max_ms)present_interval_max_ms=interval;
   if(interval>20)present_late_count++;
   present_hist[interval<14?0:interval<18?1:interval<26?2:interval<34?3:4]++;
  }
 }
 last_present_started=now;framebuffer_present_count++;
}
static void dp_consume(uint8_t *mapping,size_t pitch,int overlay,uint32_t *presented_sequence){
 dp_trace_begin(dp_blank.tick,dp_blank.mono);
 dp_count(1);
 if(xz_dp_load(&dp_prepare_declined)){dp_count(12);dp_cancel(0);dp_trace_end(DP_TRACE_CANCEL);return;}
 if(!overlay||!xz_dp_load(&dp_requested)||!mods_pipeline_eligible()||xz_dp_load(&dp_worker_state)!=1||!dp_blank.valid||!dp_blank.mono||__atomic_load_n(&vsync_state,__ATOMIC_ACQUIRE)!=VSYNC_LOCKED||xz_dp_load(&dp_waiter_applied)==UINT32_MAX){
  dp_count(9);dp_cancel(!overlay);dp_trace_end(DP_TRACE_CANCEL);return;
 }
 if(xz_dp_load(&dp_waiter_applied)!=3){dp_count(11);present_skipped++;dp_trace_end(DP_TRACE_WAITFAIL);return;}
 unsigned reason;int index=xz_dp_claim_reason(&dp_core,dp_blank.tick,&reason);
 if(index<0)dp_count(reason==XZ_DP_STALE?4:reason==XZ_DP_FUTURE?5:3);
 unsigned flags=DP_TRACE_NOFRAME;
 if(index>=0){
  struct xz_dp_slot *slot=&dp_core.slot[index];dp_trace_slot(&slot->meta);
  uint64_t at=xz_pt_mono();
  if(dp_trace_enabled)dp_trace64(34,at>=dp_blank.mono?at-dp_blank.mono:UINT64_MAX);
  if(!slot->meta.packed_mono||slot->meta.packed_mono>dp_blank.mono||at<dp_blank.mono||at-dp_blank.mono>XZ_DP_EVENT_ADMISSION_US||__atomic_load_n(&vsync_sequence,__ATOMIC_ACQUIRE)!=dp_blank.tick){
   /* Keep this immutable frame for the next genuine blank. */
   dp_count(slot->meta.packed_mono>dp_blank.mono?6:7);
   __atomic_store_n(&slot->state,XZ_DP_READY,__ATOMIC_RELEASE);flags=DP_TRACE_AFTERBLANK;
  }else{
   int accepted=mods_pipeline_accept(slot->meta.token,slot->meta.epoch,dp_blank.mono);
   /* Resolve only after accept/cancel completes. An old pending wake cannot
      overwrite the phase/geometry stage or acknowledgement timestamp. */
   dp_resolved=slot->meta.token;
   if(accepted){
    uint64_t cpu=0;if(dp_trace_enabled){dp_trace64(22,xz_pt_mono());xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&cpu);dp_trace64(26,cpu);}
    dp_notify(dp_blank.tick,dp_blank.wall+dp_blank.period);
    xz_frame_copy_present(mapping,pitch,slot->pixels);
    if(dp_trace_enabled){dp_trace64(24,xz_pt_mono());xz_pt_clock(CLOCK_THREAD_CPUTIME_ID,&cpu);dp_trace64(28,cpu);}
    dp_count(2);if(dp_blank.tick!=slot->meta.target_tick)dp_count(10);
    dp_note_copy(dp_blank.tick,presented_sequence);flags=DP_TRACE_COPIED;
   }else {dp_count(8);flags=DP_TRACE_REJECT;}
   xz_dp_release(&dp_core,(unsigned)index);
  }
 }
 if(!(flags&DP_TRACE_COPIED)){present_skipped++;dp_notify(dp_blank.tick,dp_blank.wall+dp_blank.period);}
 dp_trace_end(flags);
}
#endif
