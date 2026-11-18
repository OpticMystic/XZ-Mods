/* SPDX-License-Identifier: MIT */
#ifndef XZ_RECORDER_POLL_H
#define XZ_RECORDER_POLL_H
/* One poller of the stock recorder (recorder_state.h) for every renderer: the MODERN shell runtime
   and the MODS / CLASSIC runtime both ask it, so both show the same session clock, and the seam is read
   at most every XZ_REC_POLL_MS whoever asks (SEAM.md: 2 to 10 Hz is fine).

   Callers live in different clock domains (the shell's model clock is wall time plus the scan-out lead;
   the MODS model's is CLOCK_MONOTONIC), so the poller keeps its own monotonic time and hands each caller
   the last reading with read_at_ms moved into the caller's clock (the same instant), ready for
   xz_rec_view_at (ui/rec_mark.h), which runs the clock on from it.

   UI, display or worker threads only, never the audio thread (the seam makes syscalls). One
   priority-inheriting mutex; the critical section is one seam read (about ten guarded reads) at most
   four times a second, otherwise a struct copy. */
#include "ui/rec_reading.h"
#include "ui/rec_mark.h"
#include <stdint.h>

enum { XZ_REC_POLL_MS=250u };

struct xz_recorder_poller {
 struct xz_rec_tracker tracker;
 struct xz_rec_reading reading;      /* stamped in the poller's monotonic clock */
 uint32_t polled_at_ms;
 uint8_t polled;
};
typedef int (*xz_recorder_read_fn)(struct xz_recorder_state *);
/* Pure core: polls through read when due at mono_now_ms, then restamps for caller_now_ms. */
struct xz_rec_reading xz_recorder_poll_at(struct xz_recorder_poller *, xz_recorder_read_fn read,
                                          uint32_t mono_now_ms, uint32_t caller_now_ms);
/* The runtime's: xz_recorder_state through the shared poller, under its mutex. */
struct xz_rec_reading xz_recorder_reading(uint32_t caller_now_ms);
#endif
