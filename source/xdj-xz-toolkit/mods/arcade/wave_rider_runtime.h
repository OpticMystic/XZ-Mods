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
/* Lists the frame's controls in targets (may be NULL) for encoder focus. */
struct xz_game_targets;
int xz_wave_rider_draw(uint16_t *pixels,size_t count,size_t stride,uint32_t now,struct xz_game_targets *targets);
void xz_wave_rider_back(uint32_t now);
int xz_wave_rider_pad(int deck,int pad,unsigned *rgb,int *enabled);
struct xz_wave_rider_runtime_proof {
    uint32_t version,frames,inputs,phase,mode,deck,kind,valid,generation;
    float position,zoom;
    uint32_t score[2],streak[2],grounded[2],charge[2];
    float altitude[2],pitch[2],vx[2],vy[2],tuck[2];
    uint32_t render_us,worst_render_us;
    uint32_t difficulty;
};
extern struct xz_wave_rider_runtime_proof xz_wave_rider_proof_v2;
#endif
