/* SPDX-License-Identifier: MIT */
#ifndef XZ_LAYOUT_CYCLE_H
#define XZ_LAYOUT_CYCLE_H
#include "ui_skin.h"
/* DECK VIEW, numbered as settings.h enum xz_deck_view and the saved deck_view. */
enum xz_deck_layout { XZ_LAYOUT_TWO, XZ_LAYOUT_FOUR, XZ_LAYOUT_MIXER, XZ_LAYOUT_COUNT };
/* The one seam for DECK VIEW. get reads the view the play screens draw. set changes it from a control (the
   play-screen button, the button next to LINK, MODS) and is saved: the UI runtime sees revision move and
   stores the new view in the settings. load applies a saved value and is not saved again. */
int xz_deck_layout_get(void);
void xz_deck_layout_set(int layout);
void xz_deck_layout_load(int layout);
unsigned xz_deck_layout_revision(void);
int xz_deck_layout_next(int layout);
/* "2 DECKS", "4 DECKS, NUMBER ORDER", "4 DECKS, MIXER ORDER"; and the deck numbers in screen order. */
const char *xz_deck_layout_name(int layout);
const char *xz_deck_layout_order(int layout);
/* SCREEN > DECK VIEW BUTTON (settings deck_view_button): the DECKS button on the play screen. */
int xz_deck_view_button_shown(void);
void xz_deck_view_button_load(int shown);
void xz_deck_layout_draw(struct xz_canvas,int x,int y,int width,int height,int layout,int classic);
enum { XZ_CLASSIC_LAYOUT_X=162, XZ_CLASSIC_LAYOUT_W=116, XZ_CLASSIC_LAYOUT_H=24 };
int xz_classic_layout_hit(int x,int y);
#endif
