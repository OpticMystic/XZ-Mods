/* SPDX-License-Identifier: MIT */
#ifndef XZ_WAVE_RIDER_RUNTIME_H
#define XZ_WAVE_RIDER_RUNTIME_H
#include <stddef.h>
#include <stdint.h>

/* Serialized by the existing UI mutex. Source IO lives on its own worker. */
void xz_wave_rider_open(uint32_t now);
void xz_wave_rider_close(void);
int xz_wave_rider_present(void);
int xz_wave_rider_leave_requested(void);
int xz_wave_rider_input(int deck,const void *event,uint32_t now);
int xz_wave_rider_touch(int x,int y,int down,uint32_t now);
int xz_wave_rider_draw(uint16_t *pixels,size_t count,size_t stride,uint32_t now);
int xz_wave_rider_pad(int deck,int pad,unsigned *rgb,int *enabled);
#endif
