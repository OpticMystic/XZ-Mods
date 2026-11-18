/* Optional own-code HISTORICAL diagnostics. Enabled only by XZ_JOG_DISPLAY_PROOF.
 * Independent counters are lifetime modulo32 observations, never a transaction.
 * Each bin keeps its first outcome per observed context, replacing lazily.
 * Unrelated bins survive session/control/route/source changes as HISTORICAL.
 * The copied bin admits only normal loaded EXTRA INFO copies.
 * Position, publication cadence, packet family and phase do not renew a slot. */
#ifndef XZ_JOG_DISPLAY_LATCH_H
#define XZ_JOG_DISPLAY_LATCH_H
#include <stdint.h>
#include <stdatomic.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2&&sizeof(unsigned)==4,"latch requires lock-free32-bit unsigned");
#define XZ_JDL_REASON_COUNT 7u
#define XZ_JDL_RESULT_COUNT 6u
#define XZ_JDL_SLOTS (XZ_JDL_REASON_COUNT+XZ_JDL_RESULT_COUNT+2u)
/* All fields are32-bit atomic words. No native pointer, packing or host ABI. */
#define XZ_JDL_EVENT_FIELDS(X) \
 X(valid) X(context) X(session) X(control) X(route_epoch) \
 X(at_ms_lo) X(at_ms_hi) X(source_present) X(source_session) \
 X(source_route_epoch) X(source_revision) X(source_timeline_route) \
 X(source_load_revision) X(source_media_lo) X(source_media_hi) \
 X(source_transport_generation) X(source_device) X(publication_seq) \
 X(publication_ms) X(snapshot_reason) X(select_result) X(step_result) \
 X(phase_before) X(phase) X(desired) X(encoded_frame) X(encoded_packet) X(packet_command) \
 X(candidate_changed) X(final_copied) X(fallback) X(read_result)
struct xz_jdl_event {
 _Atomic unsigned seq;
#define XZ_JDL_ATOMIC(name) _Atomic unsigned name;
 XZ_JDL_EVENT_FIELDS(XZ_JDL_ATOMIC)
#undef XZ_JDL_ATOMIC
};
struct xz_jdl_side {
 _Atomic unsigned callbacks,contention,context_changes,session;
 _Atomic unsigned reason[XZ_JDL_REASON_COUNT],select[XZ_JDL_RESULT_COUNT],step[XZ_JDL_RESULT_COUNT],final[2];
 struct xz_jdl_event event[XZ_JDL_SLOTS];
};
struct xz_jdl {
 unsigned version,size,sides,slots,event_bytes,counter_words;
 struct xz_jdl_side side[2];
};
_Static_assert(sizeof(struct xz_jdl_event)==132,"event metadata ABI");
_Static_assert(sizeof(struct xz_jdl_side)==2080,"side metadata ABI");
_Static_assert(sizeof(struct xz_jdl)==4184,"latch metadata ABI");
extern struct xz_jdl xz_mods_jog_latch_v2;
#endif
