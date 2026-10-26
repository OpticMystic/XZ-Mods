/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_PICTURES_H
#define XZ_UI_PICTURES_H
/* The MODS menu's pictures, shared by both interfaces: the CLASSIC panel (ui/) and the MODERN shell's MENU tab
   (shell/shell_menu.c) draw them with the same code and only a different style. Pure drawing: a picture reads
   the plain data it is given and the canvas it draws on, nothing else. Geometry functions give the touch
   targets the drawing uses, so the two interfaces hit-test exactly what they draw. */
#include "ui_skin.h"
#include "picture_data.h"

struct xz_rect { int x, y, w, h; };
static inline int xz_rect_hit(struct xz_rect r, int x, int y) { return r.w>0&&x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h; }

/* Colours and shapes of one interface. MODERN: square tiles on #181818, blue selection. CLASSIC: rounded
   rows in the theme's own colours, its accent as the selection. */
struct xz_pic_style {
 int modern, radius;
 uint32_t bg, row, row_edge, ink, dim, faint, select, on_select, focus, good, warn, alarm, ruler, ruler_major;
};
struct xz_pic_style xz_pic_style_modern(void);
struct xz_pic_style xz_pic_style_classic(int theme);

/* Text in the menu's faces: size 12 or 20 (the atlas), 28, 34 or 40 (the numeral faces where the shell's font
   is linked; digits only, else 20). y is the top of the capitals. */
int xz_pic_text(struct xz_canvas c, int x, int y, const char *s, int size, uint32_t color);
int xz_pic_text_width(struct xz_canvas c, const char *s, int size);
void xz_pic_text_right(struct xz_canvas c, int right, int y, const char *s, int size, uint32_t color);
void xz_pic_text_center(struct xz_canvas c, int cx, int y, const char *s, int size, uint32_t color);
/* Wrapped 12 px text; returns the lines used. */
int xz_pic_wrap(struct xz_canvas c, int x, int y, int width, int pitch, int lines, const char *text, uint32_t color);
void xz_pic_triangle(struct xz_canvas c, int cx, int cy, int half, char direction, uint32_t color);
void xz_pic_check(struct xz_canvas c, int x, int y, uint32_t color);
/* A selectable box in the style: selected fills with the selection colour. */
void xz_pic_box(struct xz_canvas c, const struct xz_pic_style *s, struct xz_rect r, int selected, int outlined);
/* WORKS ON chips for logical decks in mixer order 3 1 2 4 (bit d = deck d+1). Returns the x after the last chip. */
int xz_pic_works_on(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int size, unsigned works_on, int label);

/* ---- PLAY > BEAT JUMP PADS ---- */
enum { XZ_PIC_JUMP_CHIPS=9 };
extern const int xz_pic_jump_ids[XZ_PIC_JUMP_CHIPS];  /* the offered sizes, as settings jump IDs (no ID 7) */
struct xz_pic_jump_geo {
 struct xz_rect tabs[2], stock, mine, frames[4], pads[8], chips[XZ_PIC_JUMP_CHIPS], live[4], ruler;
 int name_x, name_y, right;
};
/* The editor in the box from x, y, w wide (MODERN 100, 48, 690; CLASSIC 138, 50, 652). */
void xz_pic_jump_geometry(int x, int y, int w, int ruler_y, struct xz_pic_jump_geo *g);
void xz_pic_jump_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_jump_geo *g, const struct xz_pic_jump *j);
/* The ruler for one pair: NOW in the middle, the back and forward jumps of beats. */
void xz_pic_ruler(struct xz_canvas c, const struct xz_pic_style *s, struct xz_rect r, int beats, const char *back, const char *fwd);

/* ---- STEMS > STEM PADS ---- */
struct xz_pic_stem_geo { struct xz_rect modes[4], pads[8], bank[2], order[3], lights[2]; int x, w, caption_y[4], rule_y; };
void xz_pic_stem_geometry(int x, int w, struct xz_pic_stem_geo *g);
void xz_pic_stem_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_stem_geo *g, const struct xz_pic_stem_pads *p);

/* ---- STEMS > STEM COLOURS (MODERN, full width) ---- */
struct xz_pic_colour_geo { struct xz_rect swatches[3][9], preview, reset; int name_x[3], name_y, bass_x, bass_y; };
void xz_pic_colour_geometry(int x, int y, int w, struct xz_pic_colour_geo *g);
void xz_pic_colour_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_colour_geo *g, const struct xz_pic_colours *p);

/* ---- small pictures for choice tiles and HOME ---- */
/* A miniature play screen. view: 0 two decks, 1 four 1 2 3 4, 2 four 3 1 2 4; interface 1 MODERN, 0 CLASSIC.
   decks34: decks 3 and 4 are live (else greyed). */
void xz_pic_mini_screen(struct xz_canvas c, int x, int y, int w, int h, int view, int interface, int decks34);
/* Two decks with the stem buttons where layout puts them (ui/wave_viewport.h values, XZ_MODERN_STEMS_PADS for
   the small buttons by the cues). */
void xz_pic_stem_layout(struct xz_canvas c, int x, int y, int w, int h, int layout, const uint32_t rgb[3]);
/* A strip of waveform in one colour scheme: 0 stock blue, 1 3-band, 2 stems, 3 RGB. */
void xz_pic_wave_swatch(struct xz_canvas c, int x, int y, int w, int h, int scheme, const uint32_t stem_rgb[3]);
/* The media buttons above the browse encoder: USB 1, USB 2, LINK and the button next to it (lit). */
void xz_pic_media_buttons(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int w, int h);
/* The loopback route, top down: route state as enum xz_ui_route_state, phase 0..1023 along the cable. */
void xz_pic_route(struct xz_canvas c, const struct xz_pic_style *s, int x, int y, int w, int h, int state, int fail_part, int phase, int labels);
/* HOME tile pictures, 74 x 44. section is the page. */
void xz_pic_home_icon(struct xz_canvas c, const struct xz_pic_style *s, int section, int x, int y, const struct xz_pic_home *h);
/* The 4 DECKS grid: rows xz_ui_grid_features, columns decks 3 1 2 4. focus = the row ringed, -1 none. */
struct xz_pic_grid_geo { struct xz_rect rows[10]; int x[2], header_y; };
/* MODERN 4 DECKS: the grid under the route card, its legend line under the rows. */
enum { XZ_PIC_DECKS_GRID_Y=198, XZ_PIC_DECKS_ROW_H=24, XZ_PIC_DECKS_LEGEND_Y=XZ_PIC_DECKS_GRID_Y+16+5*XZ_PIC_DECKS_ROW_H+3 };
void xz_pic_grid_geometry(int x, int y, int w, int row_h, struct xz_pic_grid_geo *g);
void xz_pic_grid_draw(struct xz_canvas c, const struct xz_pic_style *s, const struct xz_pic_grid_geo *g, const unsigned caps[4], int focus);
#endif
