/* SPDX-License-Identifier: MIT */
/* Shared bounded drawing helpers for STATIC SURFER and WAVE RIDER, games by
   VJ OpticMystic. RGB565 only, no heap, no IO, no per-pixel trig: callers pass
   one sin/cos per object. Every primitive clips to its target. */
#ifndef XZ_GAME_MOTION_DRAW_H
#define XZ_GAME_MOTION_DRAW_H
#include <stddef.h>
#include <stdint.h>
#include "game_feel.h"

/* Clipped target: x in [x0,x1), y in [y0,y1). */
struct gm_tgt { uint16_t *p; size_t stride; int x0, y0, x1, y1; };

uint16_t gm_c565(uint32_t rgb);
uint16_t gm_mix(uint16_t d, uint16_t s, unsigned a256);
uint16_t gm_add(uint16_t a, uint16_t b);
uint16_t gm_scale(uint16_t v, unsigned k16);           /* k16 0..16 */
uint32_t gm_hash(uint32_t x);
int gm_isqrt(int v);

void gm_span(const struct gm_tgt *t, int xa, int xb, int y, uint16_t c);
void gm_span_mix(const struct gm_tgt *t, int xa, int xb, int y, uint16_t c, unsigned a);
void gm_span_add(const struct gm_tgt *t, int xa, int xb, int y, uint16_t c);
void gm_rect(const struct gm_tgt *t, int x, int y, int w, int h, uint16_t c);
void gm_rect_mix(const struct gm_tgt *t, int x, int y, int w, int h, uint16_t c, unsigned a);
/* Square-pen Bresenham, at most 1600 steps. a>=256 is opaque. */
void gm_line(const struct gm_tgt *t, int x0, int y0, int x1, int y1, int w, uint16_t c, unsigned a);
/* Convex polygon, n<=8. a>=256 is opaque. */
void gm_poly(const struct gm_tgt *t, const int *xs, const int *ys, int n, uint16_t c, unsigned a);
/* Additive soft disc built from three nested span discs. */
void gm_glow(const struct gm_tgt *t, int cx, int cy, int r, uint16_t c, unsigned a);

/* Rotated, independently x/y scaled sprite with alpha plane. (au,av) is the
   source anchor that lands on (x,y); rotation ang (screen clockwise) and the
   squash happen around that anchor, so feet stay planted. Rows outside
   [vlo,vhi) are skipped (used to lift a hat). amul scales alpha (256=full). */
/* rgb2/alpha2 (optional, same size and transform): a true two-pose blend in
   one pass. Both alpha planes are weighted by (1-blend2)/blend2 and colours
   are premultiplied, so the outgoing pose fades instead of ghosting. */
struct gm_sprite {
    const uint16_t *rgb; const uint8_t *alpha; int size;
    float au, av, sx, sy, sn, cs; int flip, vlo, vhi;
    uint16_t tint; unsigned tinta, amul;
    const uint16_t *rgb2; const uint8_t *alpha2; unsigned blend2;
};
void gm_blit(const struct gm_tgt *t, const struct gm_sprite *s, int x, int y);

/* Panorama 768xH source, wrapped horizontally, nearest-row scaled into
   [y0,y1). B is dithered over A by mix16 (0..16). gain16 per dest row
   (0..16), NULL = full. */
void gm_panorama(const struct gm_tgt *t, const uint16_t *A, const uint16_t *B, unsigned mix16,
                 int srcw, int srch, int src_y0, int src_h, int y0, int y1, int scroll,
                 const uint8_t *gain16);

/* Coherent crystal break of the SAME solid that was drawn: the convex outline
   (ox,oy offsets from cx,cy; 3..8 points) is cut into n triangles meeting at
   a fracture point pulled toward the contact (kx,ky offsets). Pieces fly
   away from the contact and along (dx,dy) by strength, spin and fall; a
   brief core flash at age<.14. age 0..1. */
void gm_shatter(const struct gm_tgt *t, int cx, int cy, const int *ox, const int *oy, int n,
                int kx, int ky, float age, float dx, float dy, float strength,
                uint16_t lit, uint16_t dark, uint16_t edge, uint16_t core, unsigned seed);

/* Barlow Semi Condensed atlas (game_font.h). size: 0=12 1=18 2=28 3=40 px.
   y is the top of the line box. Clipped, bounded to 160 glyphs. */
enum { GM_F12, GM_F18, GM_F28, GM_F40 };
void gm_text(const struct gm_tgt *t, int x, int y, const char *s, int size, uint16_t c, unsigned a);
void gm_text_lift(const struct gm_tgt *t, int x, int y, const char *s, int size, uint16_t c);
void gm_text_mid(const struct gm_tgt *t, int cx, int y, const char *s, int size, uint16_t c);
int gm_text_width(const char *s, int size);
int gm_text_height(int size);
/* A travelling surface ripple: two crests leave (cx,y) along the surface
   (slope dy/dx = slope), height amp, reaching reach px by age 1. */
void gm_ripple(const struct gm_tgt *t, int cx, int y, float slope, float age, int amp,
               int reach, uint16_t c, unsigned a);

/* Cat pose atlas (8 poses). A pose blend draws pose a and fades b over it. */
struct gm_cat {
    int x, y;            /* feet anchor on screen */
    float scale, sq;     /* sq<1 squash, >1 stretch; volume roughly kept */
    float ang;           /* screen clockwise radians, around the feet */
    unsigned pose, pose_b; float blend;
    uint16_t tint; unsigned tinta;
};
void gm_draw_cat(const struct gm_tgt *t, const struct gm_cat *c);
/* Birthday cow (4 poses, centre anchor). hat_lift raises the cone in px. */
void gm_draw_cow(const struct gm_tgt *t, int x, int y, float scale, float sq, float ang,
                 unsigned pose, int hat_lift, int flip, unsigned amul);
/* Transparent scenery tile (4 tiles, base anchor), tinted toward deep ink so
   it reads as background, never as an interactive material. */
void gm_draw_scenery(const struct gm_tgt *t, unsigned tile, int x, int base, float scale,
                     int flip, uint16_t tint, unsigned tinta);

/* Event helpers. */
float gm_ease_out(float u);
/* Damped spring: 1 at u=0 settling to 0, with one overshoot. */
float gm_spring(float u, float freq, float damp);
/* Newest event of kind matching at (|at-at0|<=tol) or any kind when kind==0. */
const struct xz_game_impact *gm_find(const struct xz_game_effects *f, unsigned kind, double at0, double tol);
#endif
