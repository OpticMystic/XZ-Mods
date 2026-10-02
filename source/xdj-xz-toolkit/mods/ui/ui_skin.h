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
void xz_ui_page_details(const struct xz_ui_scene *);
extern const struct xz_ui_skin xz_skin_classic, xz_skin_lcars, xz_skin_matrix, xz_skin_analog;

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
