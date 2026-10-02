/* SPDX-License-Identifier: MIT */
#ifndef XZ_WAVE_RIDER_H
#define XZ_WAVE_RIDER_H
#include "wave_rider_source.h"
#include <stddef.h>

#define XZ_RIDER_WATER 342
#define XZ_RIDER_PLAYHEAD 190
#define XZ_RIDER_OBJECTS 96
enum xz_rider_phase { XZ_RIDER_SETUP, XZ_RIDER_TITLE, XZ_RIDER_RIDE, XZ_RIDER_HOLD, XZ_RIDER_RESULT };
enum xz_rider_key { XZ_RIDER_CUE, XZ_RIDER_PAD_A, XZ_RIDER_PAD_B, XZ_RIDER_PAD_C,
    XZ_RIDER_PAD_D, XZ_RIDER_PAD_E, XZ_RIDER_PAD_F, XZ_RIDER_PAD_G, XZ_RIDER_PAD_H };
enum xz_rider_object_kind { XZ_RIDER_SPIKE, XZ_RIDER_GEM };
struct xz_rider_object { uint32_t at; float altitude; unsigned kind; };
struct xz_rider_course {
    uint16_t swell[XZ_WAVE_RIDER_SAMPLES];
    uint8_t slip[XZ_WAVE_RIDER_SAMPLES];
    struct xz_rider_object objects[XZ_RIDER_OBJECTS];
    unsigned object_count, spikes, gems;
};
struct xz_rider {
    struct xz_wave_rider_snapshot wave;
    struct xz_rider_course course;
    enum xz_rider_phase phase;
    unsigned deck, setup_step, quiet, leave, initialized, valid, jog_seen, cue_seen;
    unsigned score, combo, best, perfects, hops, catches, scrapes, judged;
    unsigned spike_total, gem_total;
    float cruise, target, altitude, jog_direction;
    double position, previous, highwater, hop_at, shield_until, feedback_until;
    double trail_at[48]; float trail_alt[48]; unsigned trail_count, trail_head;
    uint32_t last_ms, feed_ms, jog_ms;
    uint64_t course_hash; uint32_t course_generation; unsigned course_kind;
    int touch_down, slipped, jumped, pending_spike;
    double pending_at; float pending_altitude;
    const char *feedback;
};

/* Window analysis is a deterministic function of actual 150 Hz samples.
   No allocations, random course or IO. Objects are absolute sample positions. */
void xz_rider_analyze(const struct xz_wave_rider_snapshot *,struct xz_rider_course *);
float xz_rider_swell(const struct xz_rider *,double position);
void xz_rider_init(struct xz_rider *,unsigned deck,uint32_t now);
void xz_rider_feed(struct xz_rider *,const struct xz_wave_rider_snapshot *,uint32_t now);
void xz_rider_step(struct xz_rider *,uint32_t now);
void xz_rider_begin(struct xz_rider *,uint32_t now);
void xz_rider_jog(struct xz_rider *,float pulses,uint32_t now);
void xz_rider_key(struct xz_rider *,enum xz_rider_key,uint32_t now);
void xz_rider_touch(struct xz_rider *,int x,int y,int down,uint32_t now);
int xz_rider_render(const struct xz_rider *,uint16_t *,size_t count,size_t stride);
#endif
