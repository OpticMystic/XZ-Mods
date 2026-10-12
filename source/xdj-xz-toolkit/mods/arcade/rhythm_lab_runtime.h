/* SPDX-License-Identifier: MIT */
#ifndef XZ_RHYTHM_LAB_RUNTIME_H
#define XZ_RHYTHM_LAB_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
void xz_rhythm_lab_open(uint32_t now);
void xz_rhythm_lab_open_glyph(uint32_t now);
void xz_rhythm_lab_close(void);
int xz_rhythm_lab_present(void);
int xz_rhythm_lab_leave_requested(void);
int xz_rhythm_lab_input(int deck,const void *event,uint32_t now);
int xz_rhythm_lab_touch(int x,int y,int down,uint32_t now);
/* Lists the frame's controls in targets (may be NULL) for encoder focus. */
struct xz_game_targets;
int xz_rhythm_lab_draw(uint16_t *,size_t count,size_t stride,uint32_t now,struct xz_game_targets *targets);
void xz_rhythm_lab_back(uint32_t now);
int xz_rhythm_lab_pad(int deck,int pad,unsigned *rgb,int *enabled);
struct xz_rhythm_lab_proof {
    uint32_t version,frames,inputs,phase,mode,deck,valid,playing,clock_kind;
    float position,elapsed,period,turn_pulses[2],cursor[2],angle[2];
    uint32_t held[2],hits[2],misses[2],score[2],cue_edges[2],jog_events[2];
    uint32_t render_us,worst_render_us,ratings[10][4],action_binding[2];
    float glyph_height[2];uint32_t glyph_collisions[2],glyph_gates[2];
};
extern struct xz_rhythm_lab_proof xz_rhythm_lab_proof_v1;
#endif
