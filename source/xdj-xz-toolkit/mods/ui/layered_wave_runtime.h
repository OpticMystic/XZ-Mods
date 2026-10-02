/* SPDX-License-Identifier: MIT */
#ifndef XZ_LAYERED_RUNTIME_H
#define XZ_LAYERED_RUNTIME_H
#include <stdint.h>

/* Scrolling waveform look. Stems falls back to 3-band on tracks without stems. */
enum xz_wave_mode {XZ_WAVE_STOCK=0,XZ_WAVE_THREE_BAND=1,XZ_WAVE_STEMS=2,XZ_WAVE_MODE_COUNT=3};

int xz_layered_wave_start(void);
int xz_layered_wave_enabled(void);
/* Started and showing a non-stock mode. */
int xz_layered_wave_active(void);
/* Any thread. stem_rgb is 0xRRGGBB per stem in drums, harmonics, vocals order. */
void xz_layered_wave_configure(int mode,const uint32_t stem_rgb[3]);
void xz_layered_wave_begin(uint16_t *pixels);
void xz_layered_wave_end(void);
/* Around a theme recolour of the 536x268 surface: keep drawn layer pixels exact
 * while cue, beat and playhead marks the stock drew over them stay themed. */
void xz_layered_wave_protect(const uint16_t *pixels);
void xz_layered_wave_restore(uint16_t *pixels);
/* Bottom overview (300x34, stride 300, drawn bottom-up by stock) for one deck, inside its
 lock. Returns 1 if layers were drawn; call _restore after any theme recolour. */
int xz_layered_overview(uint16_t *pixels,unsigned deck);
void xz_layered_overview_restore(uint16_t *pixels);
#endif
