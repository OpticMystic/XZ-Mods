/* Stage1 own-C HISTORICAL observations, optional with XZ_JOG_DISPLAY_PROOF.
 * Frame bytes are the local assembler candidate, copied only if final_copied.
 * They say nothing about transmission, previous native buffers or acceptance.
 * All mutable exported words are atomic. Pair readers require identical whole
 * records and an even guard. Independent records are not one global snapshot.
 * Each side retains two three-record active banks and one latest observation.
 * Only the existing side serializer writes; lifecycle never resets these data.
 */
#ifndef XZ_JOG_POSITION_PROOF_H
#define XZ_JOG_POSITION_PROOF_H
#include <stdint.h>
#include <stdatomic.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2&&sizeof(unsigned)==4,"position proof needs lock-free32");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Position proof ABI requires little endian
#endif
#define XZ_JPOS_SLOTS 7u
#define XZ_JPOS_INTERVAL_MS 50u
#define XZ_JPOS_FIELDS(X) \
 X(valid) X(ordinal) X(context) X(session) X(control) X(route_epoch) X(physical_side) X(logical_id) \
 X(at_ms_lo) X(at_ms_hi) X(source_present) X(source_session) X(source_route_epoch) \
 X(source_ordered) X(source_revision) X(publication_seq) X(publication_ms) \
 X(source_known) X(source_loaded) X(source_device) X(source_elapsed_ms) X(source_elapsed_at_ms) \
 X(source_total_ms) X(source_playing) X(source_frame_lo) X(source_frame_hi) X(source_rate) \
 X(source_at_ms) X(source_flags) X(source_rate_ppm) X(source_moving) X(timeline_revision) \
 X(timeline_route_epoch) X(load_revision) X(media_lo) X(media_hi) X(transport_generation) \
 X(snapshot_valid) X(snapshot_generation_lo) X(snapshot_generation_hi) \
 X(snapshot_captured_lo) X(snapshot_captured_hi) X(snapshot_position_ms) X(snapshot_length_ms) \
 X(snapshot_paused) X(snapshot_status_known) X(snapshot_logical_id) \
 X(snapshot_reason) X(read_result) X(select_result) X(step_result) X(phase_before) X(phase) X(final_copied)
struct xz_jpos_record {
 _Atomic unsigned seq;
#define XZ_JPOS_ATOMIC(name) _Atomic unsigned name;
 XZ_JPOS_FIELDS(XZ_JPOS_ATOMIC)
#undef XZ_JPOS_ATOMIC
 _Atomic unsigned frame[37];
};
struct xz_jpos {
 unsigned version,size,sides,slots,record_bytes,frame_bytes,interval_ms;
 struct xz_jpos_record side[2][XZ_JPOS_SLOTS];
};
_Static_assert(sizeof(struct xz_jpos_record)==368,"position record ABI");
_Static_assert(sizeof(struct xz_jpos)==5180,"position export ABI");
_Static_assert(sizeof(struct xz_jpos)<=8192,"position export budget");
extern struct xz_jpos xz_mods_jog_position_v1;
#endif
