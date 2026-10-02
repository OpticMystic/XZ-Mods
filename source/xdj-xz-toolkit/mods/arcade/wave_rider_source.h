/* SPDX-License-Identifier: MIT */
#ifndef XZ_WAVE_RIDER_SOURCE_H
#define XZ_WAVE_RIDER_SOURCE_H
#include "../audio/native_reader.h"

#define XZ_WAVE_RIDER_SAMPLES 1800u
#define XZ_WAVE_RIDER_PAST 300u
#define XZ_WAVE_RIDER_HZ 150u
#define XZ_WAVE_RIDER_FRESH_MS 250u

enum xz_wave_rider_source_error {
    XZ_WAVE_RIDER_SOURCE_OK, XZ_WAVE_RIDER_NO_TRACK,
    XZ_WAVE_RIDER_NO_ANALYSIS, XZ_WAVE_RIDER_POSITION_UNAVAILABLE
};
struct xz_wave_rider_snapshot {
    uint8_t bands[XZ_WAVE_RIDER_SAMPLES * 3];
    uint32_t first, count, total, normalization, generation, observed_ms;
    uint64_t track_hash;
    double position; /* Stock source position expressed in 150 Hz waveform samples. */
    unsigned kind; /* xz_layered_kind: 0 = low/mid/high, 1 = drums/harmonics/vocals. */
    int deck, playing, valid, loading, error;
    char title[96];
};
struct xz_wave_rider_position { int32_t ticks; int playing, audible_pause; };

/* Worker starts once; no hooks, audio writes, or transport commands. */
int xz_wave_rider_source_start(void);
void xz_wave_rider_source_select(int deck); /* 0/1, or -1 to release analysis. */
/* Never waits. A zero result means retry next frame; do not use stale data. */
int xz_wave_rider_source_read(struct xz_wave_rider_snapshot *);

/* Pure/read-only boundaries used by the worker and host/ARM acceptance tests. */
int xz_wave_rider_position_read(xz126_read_fn, void *context, uint32_t player,
                                struct xz_wave_rider_position *);
int xz_wave_rider_window(struct xz_wave_rider_snapshot *, const uint8_t *bands,
                         uint32_t total, double position);
#endif
