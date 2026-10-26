/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_WAVE_RUNTIME_H
#define XZ_NATIVE_WAVE_RUNTIME_H
#include "native_wave.h"
int xz_native_inline_start(int (*enabled)(void),xz_native_wave_render render,void *context);
int xz_native_inline_active(void);
/* The four-deck lane order the play screen shows (wave_viewport.h), or -1 for two decks. Touch
   threads map waveform contacts against it; it is revoked as soon as the play scene goes. */
int xz_native_four_order(void);
/* 2 DECKS: the sides whose half the last completed frame drew as DECK 3 or 4 (bit 0 left on DECK 3, bit 1 right
   on DECK 4), 0 when it drew the stock two decks or the play scene is gone. The side column, the bottom panels
   and touch follow this, so the whole half switches with the waveform. */
unsigned xz_native_two_pair(void);
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
/* DECK VIEW 4 DECKS while decks 3 and 4 are not up: the four lanes stay, decks 3 and 4 greyed, and the
   card (route_card.h) in their lanes, drawn inside the wave transaction after the theme pass.
   card: what the last completed four-deck frame drew, for touch: 0 none, else XZ_NATIVE_CARD_SHOWN with
   its lane order, route state and buttons (macros below). card_input: a play-screen touch or encoder
   input at now_ms (the pulse rests 60 s after the last one) and the encoder's focus on the card
   (XZ_ROUTE_CARD_HIT_PRIMARY / _TWO, or -1). card_success: while the READY hold and wipe after the route
   came up under the card last, its start (monotonic ms), else 0; decks 3 and 4 panels wait for it. */
#define XZ_NATIVE_CARD_SHOWN 1u
#define XZ_NATIVE_CARD_ORDER(w) ((int)(((w)>>1)&7u))
#define XZ_NATIVE_CARD_STATE(w) ((int)(((w)>>4)&15u))
#define XZ_NATIVE_CARD_SUCCESS 256u
uint32_t xz_native_four_card(void);
void xz_native_four_card_input(uint32_t now_ms,int focus);
uint32_t xz_native_four_card_success(uint32_t now_ms);
#endif
