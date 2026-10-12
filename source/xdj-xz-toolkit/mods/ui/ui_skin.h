#ifndef XZ_MOD_UI_SKIN_H
#define XZ_MOD_UI_SKIN_H
/* Internal to the MODS panel renderer. The runtime includes ui.h only. */
#include "ui.h"

struct xz_canvas { uint16_t *p; size_t stride; int width, height, theme; };
struct xz_ui_look { int available, selected; uint32_t color; };
struct xz_ui_scene {
 struct xz_canvas c; const struct xz_ui *u; const struct xz_ui_model *m;
 const struct xz_ui_deck *d; const struct xz_theme_palette *p;
 const struct xz_ui_widget *w; const struct xz_ui_look *look; size_t n;
};
/* page_controls() in ui.c decides which controls exist. place() only assigns
   x,y,w,h to them; it never adds, drops or relabels a control. */
struct xz_ui_skin {
 void (*place)(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n);
 void (*render)(const struct xz_ui_scene *s);
 /* Geometry is fixed by xz_ui_inline_layout. look.selected is muted (MUTE) or
    bypass (BYPASS); s->d is deck 0, so use m->deck[w.deck]. */
 void (*render_inline)(const struct xz_ui_scene *s);
 void (*render_buttons)(struct xz_canvas c, int stems, int vj_visible, int vj_active);
};
void xz_ui_place(const struct xz_ui *,const struct xz_ui_model *,struct xz_ui_widget *,size_t);
void xz_ui_stem_setup(const struct xz_ui_scene *);
void xz_ui_stem_rows(const struct xz_ui_scene *);
void xz_ui_stem_colors(const struct xz_ui_scene *);
void xz_ui_page_details(const struct xz_ui_scene *);
extern const struct xz_ui_skin xz_skin_classic, xz_skin_lcars, xz_skin_matrix, xz_skin_analog;

/* Shared screen grid. Header 0..HEAD, section rail 0..RAIL_W, content
   CX..CX+CW by CY..CB, info dock DOCK_Y..480. Controls never enter the dock. */
enum { XZ_UI_HEAD=40, XZ_UI_RAIL_W=124, XZ_UI_CX=136, XZ_UI_CW=656, XZ_UI_CY=48, XZ_UI_CB=404, XZ_UI_DOCK_Y=412 };
/* DJ Profile list panel under the header's right corner. */
enum { XZ_UI_PROFILE_LIST_X=432, XZ_UI_PROFILE_LIST_Y=44, XZ_UI_PROFILE_LIST_W=360 };
int xz_ui_profile_list_bottom(const struct xz_ui *, const struct xz_ui_model *);
/* The header chip and the open list, drawn over every skin. */
void xz_ui_chrome_profile(const struct xz_ui_scene *s, uint32_t bg, uint32_t ink, uint32_t dim, uint32_t accent, uint32_t alarm);
/* The colours shared chrome uses in this theme. */
void xz_ui_chrome_colors(const struct xz_ui_model *m, const struct xz_theme_palette *p, uint32_t *bg, uint32_t *ink, uint32_t *accent);
/* A DJ logo from src (src_side square RGB565) in a circle or rounded square; NULL src draws e's
   monogram, or a silhouette when e is NULL too. */
void xz_ui_chrome_logo(struct xz_canvas c, int x, int y, int side, int circle, const uint16_t *src, int src_side,
 const struct xz_ui_dj_entry *e, uint32_t bg, uint32_t ink, uint32_t accent);
/* Longest whole-character prefix of text that fits width. */
void xz_ui_chrome_fit(struct xz_canvas c, int x, int y, int width, const char *text, int scale, uint32_t color);
/* DJ PROFILE page and editor (ui_profile.c). controls adds the page's widgets; place sets geometry
   for its kinds and returns 0 for others; touch returns -1 for kinds it does not own; back returns
   1 when it moved the editor a step instead of leaving the page. */
size_t xz_ui_profile_controls(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n);
int xz_ui_profile_place(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *a);
int xz_ui_profile_owns(enum xz_ui_action_kind kind);
int xz_ui_profile_ready(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *a);
int xz_ui_profile_touch(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *a, struct xz_ui_action out[XZ_UI_ACTIONS]);
int xz_ui_profile_back(struct xz_ui *u);
void xz_ui_profile_render(const struct xz_ui_scene *s);
const char *xz_ui_profile_describe(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *a, const char **title);
const char *xz_ui_profile_footer(const struct xz_ui *u, const struct xz_ui_model *m);
/* Why a profile control is not ready, for the footer notice. */
const char *xz_ui_profile_blocked(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *a);
/* BACK and CLOSE have physical keys, so the encoder skips them. */
int xz_ui_focusable(const struct xz_ui_widget *);
int xz_ui_widget_ready(const struct xz_ui *, const struct xz_ui_model *, const struct xz_ui_widget *);
struct xz_ui_look xz_ui_widget_look(const struct xz_ui *, const struct xz_ui_model *, const struct xz_ui_widget *);
/* Shared chrome every skin draws over its own background: rail icons, the
   focus ring on the encoder's control and the info dock. */
void xz_ui_chrome_icon(struct xz_canvas c, enum xz_ui_page page, int cx, int cy, uint32_t color);
void xz_ui_chrome_focus(const struct xz_ui_scene *s, uint32_t color);
void xz_ui_chrome_dock(const struct xz_ui_scene *s, uint32_t bg, uint32_t ink, uint32_t dim, uint32_t accent, uint32_t alarm);
void xz_ui_chrome_switch(struct xz_canvas c, int x, int y, int on, int available, uint32_t track, uint32_t knob, uint32_t accent);

extern const char *const xz_ui_lengths[6];
const char *xz_ui_footer_text(const struct xz_ui *u, const struct xz_ui_model *m);

/* Colours are 0xRRGGBB; every primitive clips to the canvas. */
uint32_t xz_blend(uint32_t a, uint32_t b, unsigned amount256);
void xz_rect(struct xz_canvas c, int x, int y, int w, int h, uint32_t color);
void xz_border(struct xz_canvas c, int x, int y, int w, int h, uint32_t color);
void xz_plot(struct xz_canvas c, int x, int y, uint32_t color, int alpha255);
/* Atlas text, or the 5x7 pixel font when xz_theme_pixel_text(c.theme). */
void xz_text(struct xz_canvas c, int x, int y, const char *s, int scale, int limit, uint32_t color);
void xz_pixel_text(struct xz_canvas c, int x, int y, const char *s, int dot, int limit, uint32_t color);
int xz_text_width(struct xz_canvas c, const char *s, int scale, int limit);
struct xz_theme_surface xz_surface(struct xz_canvas c);
void xz_frame(struct xz_canvas c, int theme, struct xz_ui_widget a, uint32_t fill, int selected);
/* Coordinates inside the result are relative to (x,y). An origin outside c gives an empty canvas. */
struct xz_canvas xz_sub(struct xz_canvas c, int x, int y, int w, int h);
/* corners: 1=TL 2=TR 4=BL 8=BR. radius is clamped to min(w,h)/2, so a large radius gives a pill. */
void xz_round_rect(struct xz_canvas c, int x, int y, int w, int h, int radius, unsigned corners, uint32_t color);
/* Centred on pixel (cx,cy). */
void xz_disc(struct xz_canvas c, int cx, int cy, int r, uint32_t color);
void xz_ring(struct xz_canvas c, int cx, int cy, int r, int thickness, uint32_t color);
/* All five arguments are 1/16 px fixed point; ends are round. */
void xz_line16(struct xz_canvas c, int x0, int y0, int x1, int y1, int width, uint32_t color);
/* Any integer degrees; result scaled by 1024. */
int xz_sin1024(int degrees);
int xz_cos1024(int degrees);
#endif
