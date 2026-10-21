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
struct xz_sync_native_snapshot {
 int available;
 enum xz_sync_native_reason reason;
 int native_deck;                    /* 0/1 only; -1 when unavailable */
 int32_t master_id;                  /* native getter value; -1 absent, -2 unknown, 2..5 external */
 uint32_t track_identity;            /* native BeatPosition object token, not a rekordbox file ID */
 uint32_t position_ms;
 uint32_t source_revision;           /* publisher load generation; direct reader leaves this unknown (0) */
 uint64_t captured_us, earliest_us, latest_us;
 uint32_t captured_ms;               /* same CLOCK_MONOTONIC domain as player env.now_ms */
 float bpm,bar_phase,speed;          /* bar_phase0..4; engine/grid time, never an actual DAC timestamp */
 int playing;
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
