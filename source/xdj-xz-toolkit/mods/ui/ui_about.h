/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_ABOUT_H
#define XZ_UI_ABOUT_H
#include "ui_skin.h"
enum { XZ_ABOUT_LINKS=7 };
struct xz_about_link { const char *label, *url, *line1, *line2; };
extern const struct xz_about_link xz_about_links[XZ_ABOUT_LINKS];
void xz_about_place(struct xz_ui_widget *,int index);
void xz_about_render(struct xz_canvas,int selected,int focused,int lit);
#endif
