/* SPDX-License-Identifier: MIT */
#ifndef XZ_ARCADE_NATIVE_CLOCK_H
#define XZ_ARCADE_NATIVE_CLOCK_H
#include <stddef.h>
#include <stdint.h>
typedef int (*xz_arcade_memory_read)(void *,uint32_t,void *,size_t);
enum xz_arcade_clock_observed {
 XZ_ARCADE_CLOCK_TRACK=1u, XZ_ARCADE_CLOCK_STATE=2u,
 XZ_ARCADE_CLOCK_MANUAL=4u, XZ_ARCADE_CLOCK_BOUNDARY=8u
};
struct xz_arcade_track_clock {
    uint32_t track,position_ms,position_frames;
    float bpm,nominal_bpm,bar_phase;
    unsigned playing,manual,phase_valid;
    float speed;   /* playback speed multiplier (pitch, sync), as used for bpm */
    unsigned observed; /* partial read evidence only; never a usable clock */
};
int xz_arcade_native_clock_valid(xz_arcade_memory_read,void *);
int xz_arcade_native_clock_read(xz_arcade_memory_read,void *,unsigned deck,struct xz_arcade_track_clock *);
/* Same structural/source guards and raw grid limits; no arcade effective-tempo policy.
   Positive finite representable outputs do not assert a hardware-supported tempo range. */
int xz_music_native_clock_read(xz_arcade_memory_read,void *,unsigned deck,struct xz_arcade_track_clock *);
int xz_arcade_mixer_clock_read(xz_arcade_memory_read,void *,float *bpm,unsigned *tap_mode);
#endif
