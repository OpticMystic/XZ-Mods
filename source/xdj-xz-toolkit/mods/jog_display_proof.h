/* Optional own-source telemetry only. Compile with -DXZ_JOG_DISPLAY_PROOF. */
#ifndef XZ_JOG_DISPLAY_PROOF_H
#define XZ_JOG_DISPLAY_PROOF_H
#include <stdint.h>
#include <stdatomic.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2 && sizeof(unsigned)==4,"proof requires lock-free 32-bit unsigned");
enum xz_jdp_reason { XZ_JDP_NATIVE, XZ_JDP_NO_PUBLICATION, XZ_JDP_EMPTY,
 XZ_JDP_LENGTH, XZ_JDP_STALE, XZ_JDP_FUTURE, XZ_JDP_VALID };
struct xz_jdp_side {
 _Atomic unsigned calls, publications, published_seq, published_ms;
 /* One assembler writer per side, as required by the existing adapter.
  * Reader: acquire seq, fields, acquire fence, relaxed seq. Require same even seq.
  * Global counters/publication fields are independent, not one transaction. */
 _Atomic unsigned seq, channel, control, desired, snapshot_reason, select_result,
  step_result, phase, encoded_frame, encoded_packet, changed, fallback, read_seq;
};
struct xz_jdp {
 unsigned version;
 _Atomic unsigned start_attempts, install_ok, install_failed, installed, test_installs,
  calls, disabled_calls, null_self, invalid_channel, last_invalid_channel,
  snapshot_calls, snapshot_no_players, selections, selection_control;
 struct xz_jdp_side side[2];
};
extern struct xz_jdp xz_mods_jog_proof_v1;
#define JDP_INC(x) ((void)atomic_fetch_add_explicit(&(x),1u,memory_order_relaxed))
#define JDP_SET(x,v) atomic_store_explicit(&(x),(unsigned)(v),memory_order_relaxed)
#endif
