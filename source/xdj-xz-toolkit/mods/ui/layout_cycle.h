/* SPDX-License-Identifier: MIT */
#ifndef XZ_LAYOUT_CYCLE_H
#define XZ_LAYOUT_CYCLE_H
#include "ui_skin.h"
#include "deck_layout.h"
/* Session only. Neither route loss nor a different interface resets this preference. */
int xz_deck_layout_get(void);
void xz_deck_layout_set(int layout);
int xz_deck_layout_next(int layout);
void xz_deck_layout_draw(struct xz_canvas,int x,int y,int width,int height,int layout,int classic);
enum { XZ_CLASSIC_LAYOUT_X=162, XZ_CLASSIC_LAYOUT_W=116, XZ_CLASSIC_LAYOUT_H=24 };
int xz_classic_layout_hit(int x,int y);
#endif
