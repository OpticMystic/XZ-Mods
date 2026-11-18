/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_ABOUT_H
#define XZ_UI_ABOUT_H
#include "ui_skin.h"
enum { XZ_ABOUT_LINKS=7 };
struct xz_about_link { const char *label, *url, *line1, *line2; };
extern const struct xz_about_link xz_about_links[XZ_ABOUT_LINKS];
/* Draws a side x side RGB565 logo into a size x size square. NULL pixels selects the built-in XZ Mods logo,
 * made for a 160 px square on black (side is then ignored); other sizes are scaled from it. */
void xz_ui_logo_draw(struct xz_canvas,const uint16_t *pixels,int side,int x,int y,int size);
void xz_about_place(struct xz_ui_widget *,int index);
/* loader: the ABOUT line naming this stick's loader ("Loader L-4F7K-2M9Q" or "Loader: not identified"). */
void xz_about_render(struct xz_canvas,int selected,int focused,int lit,const char *loader);
#endif
