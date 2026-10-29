/* SPDX-License-Identifier: MIT */
/* The real master observer and native-sync bridge (sync_link_runtime.c) on
 * simulated time. Stock entry points arrive through its existing test seam. */
#define _GNU_SOURCE
#include "sync_master_sim_clock.h"
#include "../sync_link_runtime.c"
void sim_bridge_reset(void){
 xz_sync_link_runtime_stop();atomic_store(&rt_initialized,0);atomic_store(&rt_global_mode,0);atomic_store(&rt_legacy_mode,0);
}
#include <stdio.h>
void sim_bridge_debug(char *out,unsigned n){
 snprintf(out,n,"lease=%d/src%u/commit%d/prev%d/opchoice%llu va_active=%u src=%u gen=%u valid=%u pending=%u op=%u ack=%u req=%u reqop=%u observed_id=%d ready=%d restore=%d/in%d/exp%d/op%u absent=%d/%d",
  rt_lease.active,rt_lease.claim.source_id,rt_lease.master_committed,rt_lease.claim.previous_master_id,(unsigned long long)rt_lease.claim.master_choice_epoch,
  atomic_load(&rt_va.active),atomic_load(&rt_va.source_id),atomic_load(&rt_va.lease_generation),atomic_load(&rt_va.valid_generation),
  atomic_load(&rt_va.pending),atomic_load(&rt_va.operator_epoch),atomic_load(&rt_ack_generation),atomic_load(&rt_request_epoch),
  atomic_load(&rt_request_operator),atomic_load(&rt_observed_master_id),atomic_load(&rt_observed_ready),
  atomic_load(&rt_restore_pending),atomic_load(&rt_restore_input_id),atomic_load(&rt_restore_expected_master),atomic_load(&rt_restore_operator_epoch),
  atomic_load(&rt_slot_absent[0]),atomic_load(&rt_slot_absent[1]));
}
