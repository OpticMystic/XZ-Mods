/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_SECTIONS_H
#define XZ_UI_SECTIONS_H
/* The MODS sections (HOME, PLAY, STEMS, SCREEN, 4 DECKS, CONNECT, EXTRAS) and the picture pages they open
   (BEAT JUMP PADS, STEM PADS, BUTTON NEXT TO LINK, KEYS): which controls each page has, where the CLASSIC panel
   puts them, and what it draws over the skin. One menu for both interfaces: MODERN draws the same pages in
   the FLAGSHIP skin and adds its own play-screen parts (xz_ui_modern_menu). Internal to ui/. */
#include "ui_skin.h"
#include "pictures.h"

int xz_ui_section_page(int page);
size_t xz_ui_section_controls(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget *, size_t n);
void xz_ui_section_place(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget *, size_t n);
void xz_ui_section_render(const struct xz_ui_scene *);
/* The value a row shows at its right ("1 2 4 8 | 16 32 64 128"), or NULL. */
const char *xz_ui_section_value(const struct xz_ui_model *, const struct xz_ui_widget *, char *buf, size_t cap);
/* HOME: what a section is for, and its live state; returns 1 when it needs the DJ (one SET UP button). tone:
   0 plain, 1 good, 2 needs you. */
const char *xz_ui_section_purpose(int section);
int xz_ui_home_status(const struct xz_ui_model *, int section, char *out, size_t cap, int *tone);
/* The pictures' data from the panel state. focus_pair -1 uses the panel's own edit pair. */
void xz_ui_jump_picture(const struct xz_ui *, const struct xz_ui_model *, int focus_pair, struct xz_pic_jump *);
void xz_ui_stem_picture(const struct xz_ui_model *, int focus, struct xz_pic_stem_pads *);
void xz_ui_home_picture(const struct xz_ui_model *, struct xz_pic_home *);
/* 4 DECKS: the route card's title and its line. */
const char *xz_ui_route_title(int state);
const char *xz_ui_route_line(const struct xz_ui_model *);
/* KEY NOTATION: a key written in format (enum xz_key_format order). */
const char *xz_ui_key_example(int format);
/* CLASSIC geometry of the picture pages, shared by place and render. */
enum { XZ_UI_JUMP_X=138, XZ_UI_JUMP_Y=50, XZ_UI_JUMP_W=652, XZ_UI_JUMP_BOTTOM=400, XZ_UI_STEM_X=138, XZ_UI_STEM_W=652 };
#endif
