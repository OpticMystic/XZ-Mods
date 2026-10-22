/* SPDX-License-Identifier: MIT */
#ifndef XZ_SYNC_NATIVE_H
#define XZ_SYNC_NATIVE_H
#include <stddef.h>
#include <stdint.h>
enum xz_sync_native_reason {
 XZ_SYNC_NATIVE_OK, XZ_SYNC_NATIVE_NO_READER, XZ_SYNC_NATIVE_BAD_ABI,
 XZ_SYNC_NATIVE_MASTER_UNAVAILABLE, XZ_SYNC_NATIVE_EXTERNAL_MASTER,
 XZ_SYNC_NATIVE_MASTER_CHANGED, XZ_SYNC_NATIVE_CLOCK_UNAVAILABLE,
 XZ_SYNC_NATIVE_SOURCE_CHANGED, XZ_SYNC_NATIVE_STATE_CHANGED,
 XZ_SYNC_NATIVE_TIME_INVALID
};
#define XZ_SYNC_NATIVE_MAX_READ_US 20000u
#define XZ_SYNC_NATIVE_MAX_PHASE_UNCERTAINTY_US 1000u
/* Read evidence is independent of availability/reason. It is not serialized.
 * An unreadable code guard is uncertainty; successfully read unequal bytes are
 * an observed boundary. Master/track flags qualify partial identity only. */
enum xz_sync_native_evidence {
 XZ_SYNC_NATIVE_MASTER_OBSERVED=1u, XZ_SYNC_NATIVE_TRACK_OBSERVED=2u,
 XZ_SYNC_NATIVE_BOUNDARY_OBSERVED=4u
};
struct xz_sync_native_snapshot {
 int available;
 int phase_available;                /* 1 when grid phase and read window are qualified for beat sync */
 enum xz_sync_native_reason reason;
 int native_deck;                    /* 0/1 only; -1 when unavailable */
 int32_t master_id;                  /* native getter value; -1 absent, -2 unknown, 2..5 external */
 uint32_t track_identity;            /* native BeatPosition object token, not a rekordbox file ID */
 uint32_t position_ms;
 uint32_t position_frames;           /* native 44.1 kHz engine frame index */
 uint32_t source_revision;           /* publisher load generation; direct reader leaves this unknown (0) */
 unsigned evidence;                 /* occupies former 8-byte alignment padding; wire proof ABI unchanged */
 uint64_t captured_us, earliest_us, latest_us;
 uint32_t captured_ms;               /* same CLOCK_MONOTONIC domain as player env.now_ms */
 uint32_t uncertainty_us;            /* half-width of the guarded native read window */
 int32_t output_latency_us;          /* calibrated native engine-to-output offset in microseconds */
 float bpm,nominal_bpm,bar_phase,speed; /* bar_phase 0..4; engine/grid time, never an uncalibrated DAC timestamp */
 int playing;
 int manual;                         /* 1 when native deck is scratching or pitch-bending */
 unsigned read_count;
};
typedef int (*xz_sync_native_read_fn)(void *,uint32_t,void *,size_t);
typedef uint64_t (*xz_sync_native_now_us)(void *);
/* Caller supplies all I/O and time. Run on the UI/clock publisher; audio callbacks copy its cache.
   Identity is checked around two guarded engine/grid samples. Object-token reuse across loads is
   not a persistent source generation guarantee; the publisher must also invalidate on known loads. */
int xz_sync_native_read(xz_sync_native_read_fn,void *read_context,
 xz_sync_native_now_us,void *clock_context,struct xz_sync_native_snapshot *);
#endif
