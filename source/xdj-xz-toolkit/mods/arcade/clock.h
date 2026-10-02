/* SPDX-License-Identifier: MIT */
#ifndef XZ_ARCADE_CLOCK_H
#define XZ_ARCADE_CLOCK_H
#include <stddef.h>
#include <stdint.h>
struct xz_arcade_beat { float bpm; uint32_t at,sequence; unsigned deck,bar; };
int xz_arcade_decode_beat(const void *,size_t,struct xz_arcade_beat *);
int xz_arcade_clock_read(unsigned deck,struct xz_arcade_beat *);
#endif
