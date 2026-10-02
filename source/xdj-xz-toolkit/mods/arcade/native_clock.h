/* SPDX-License-Identifier: MIT */
#ifndef XZ_ARCADE_NATIVE_CLOCK_H
#define XZ_ARCADE_NATIVE_CLOCK_H
#include <stddef.h>
#include <stdint.h>
typedef int (*xz_arcade_memory_read)(void *,uint32_t,void *,size_t);
struct xz_arcade_track_clock {
    uint32_t track,position_ms;
    float bpm,bar_phase;
    unsigned playing;
};
int xz_arcade_native_clock_valid(xz_arcade_memory_read,void *);
int xz_arcade_native_clock_read(xz_arcade_memory_read,void *,unsigned deck,struct xz_arcade_track_clock *);
int xz_arcade_mixer_clock_read(xz_arcade_memory_read,void *,float *bpm,unsigned *tap_mode);
#endif
