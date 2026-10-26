/* SPDX-License-Identifier: MIT */
#ifndef XZ_CLASSIC_MIXER_RUNTIME_H
#define XZ_CLASSIC_MIXER_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
int xz_classic_mixer_runtime_start(void);
void xz_classic_mixer_runtime_stop(void);
/* The native decks' keys as the Classic key strip reads them (key_strip.h); valid 0 when unknown. */
struct xz_classic_mixer_key { int valid; char effective[8], original[8]; int cents; };
/* Called after stock/badges, never over MODS, a game, a native dialog or a takeover. In a four-deck DECK VIEW
   it owns the side column (x 0..129, y 22..308) and the channel band (y 309..479): four tags beside the lanes
   and four channels in mixer order 3 1 2 4, decks 3 and 4 NOT CONNECTED while their route is down. */
int xz_classic_mixer_runtime_frame(uint16_t *,size_t stride,size_t count,int eligible,int theme,uint32_t now_ms,
                                  const struct xz_classic_mixer_key keys[2]);
void xz_classic_mixer_runtime_hide(void);
int xz_classic_mixer_runtime_visible(void);
int xz_classic_mixer_runtime_layout(void);
/* Existing guarded native 300x34 overview capture, before the GR window unlock. Copies immediately. */
void xz_classic_mixer_native_overview(int deck,const uint16_t *pixels,size_t count,int x,int y);
int xz_classic_mixer_runtime_native_point(int deck,int local_x,int local_y,int *x,int *y);
#endif
