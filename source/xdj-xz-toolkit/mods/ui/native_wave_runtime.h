/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WAVE_RUNTIME_H
#define XZ_NATIVE_WAVE_RUNTIME_H
#include "native_wave.h"
int xz_native_inline_start(int (*enabled)(void),xz_native_wave_render render,void *context);
int xz_native_inline_active(void);
/* The four-deck lane order the play screen shows (wave_viewport.h), or -1 for two decks. Touch
   threads map waveform contacts against it; it is revoked as soon as the play scene goes. */
int xz_native_four_order(void);
/* Session preference, shared by Classic and FLAGSHIP. Touch geometry continues
   to use four_order(), the last completed frame, never this pending preference. */
int xz_native_four_mixer_order(void);
void xz_native_four_set_mixer_order(int mixer);
/* The DECK 3/4 detail panel on the four-deck surface (extra_detail.h): 0 DECK 3, 1 DECK 4, -1 closed.
   The touch thread opens, switches and closes it; it also closes when the layout is revoked or the
   route goes down. notice raises one enum xz_extra_detail_notice in the panel's status row. */
int xz_native_four_detail(void);
void xz_native_four_set_detail(int deck);
void xz_native_four_detail_notice(int notice);
int xz_native_focus_deck(void);
int xz_native_play_visible(void);
#endif
