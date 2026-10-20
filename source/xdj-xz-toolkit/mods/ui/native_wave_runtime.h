/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WAVE_RUNTIME_H
#define XZ_NATIVE_WAVE_RUNTIME_H
#include "native_wave.h"
int xz_native_inline_start(int (*enabled)(void),xz_native_wave_render render,void *context);
int xz_native_inline_active(void);
/* The four-deck lane order the play screen shows (wave_viewport.h), or -1 for two decks. Touch
   threads map waveform contacts against it; it is revoked as soon as the play scene goes. */
int xz_native_four_order(void);
int xz_native_focus_deck(void);
int xz_native_play_visible(void);
#endif
