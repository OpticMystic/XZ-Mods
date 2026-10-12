/* SPDX-License-Identifier: MIT */
#ifndef XZ_GLYPH_RUN_H
#define XZ_GLYPH_RUN_H
#include <stddef.h>
#include "game_feel.h"
#include <stdint.h>
#define XZ_GLYPH_SCROLL 120.f
#define XZ_GLYPH_GRAVITY 670.f
#define XZ_GLYPH_JUMP_SPEED 348.f
struct xz_glyph_player { struct xz_game_effects effects; float landing_impact,landing_quality,launch_strength; float height,velocity,buffer,flash; unsigned jumps,landings,collisions,gates; double safe_until,jump_at,landing_at,party_at; };
struct xz_rhythm_lab;
void xz_glyph_begin(struct xz_rhythm_lab *);
void xz_glyph_jump(struct xz_rhythm_lab *,unsigned);
void xz_glyph_step(struct xz_rhythm_lab *,float source_dt);
int xz_glyph_render(const struct xz_rhythm_lab *,uint16_t *,size_t count,size_t stride);
struct xz_game_targets;
int xz_glyph_render_targets(const struct xz_rhythm_lab *,uint16_t *,size_t count,size_t stride,struct xz_game_targets *);
#endif
