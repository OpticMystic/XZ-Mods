/* SPDX-License-Identifier: MIT */
/* The loopback-cable explainer (cable_anim.h). Every frame: interpolate the baked camera and plug poses,
   fill the solids back to front, then draw the visible parts of the feature lines and the cable's two
   silhouettes into a coverage plane and tint it per layer. All memory is the caller's picture. */
#include "cable_anim.h"
#include "cable_anim_data.h"
#include <math.h>
#include <string.h>

typedef struct { float x, y, z; } v3;
static v3 V(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static v3 add(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static v3 mul(v3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 cross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float len(v3 a) { return sqrtf(dot(a, a)); }
static v3 norm(v3 a) { float l = len(a); return l > 0 ? mul(a, 1.0f / l) : a; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static v3 vlerp(v3 a, v3 b, float t) { return V(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t)); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
/* floor / ceil / min / max without the C library: VFPv3 has no rounding instruction and the calls showed in
   the profile. Exact for the picture's coordinates (well inside int range). */
static int ifloor(float v) { int i = (int)v; return i - (v < (float)i); }
static int iceil(float v) { int i = (int)v; return i + (v > (float)i); }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float absf(float a) { return a < 0 ? -a : a; }
static const float DEG = 3.14159265f / 180.0f;
enum { NEAR_MM = 5, MAX_IV = 64, CABLE_N = 96, HALO = 6 };  /* HALO: the cable's glow, a layer under every line */

uint32_t xz_cable_loop_ms(void) { return XZ_CA_LOOP_MS; }
uint32_t xz_cable_rest_ms(int mark) { return mark == XZ_CABLE_MARK_SELECTOR ? XZ_CA_REST_SELECTOR_MS : 0; }

/* ---- the frame: baked samples at 60 fps, interpolated ---- */
struct pose { v3 o, x, y, z; };
struct frame { v3 cam, target; struct pose a, b; float gain[XZ_CABLE_LAYERS], label[2], signal; int label_left, f0, f1; float t; };
static struct pose pose_a(float x, float y, float z, float tilt) {
 float t = tilt * DEG; struct pose p;
 p.o = V(x, y, z); p.z = V(0, sinf(t), cosf(t)); p.x = V(1, 0, 0); p.y = cross(p.z, p.x);
 return p;
}
static struct pose pose_b(float x, float y, float z, float yaw, float roll) {
 float w = yaw * DEG, r = roll * DEG; struct pose p;
 p.o = V(x, y, z); p.z = norm(V(sinf(w), cosf(w), 0));
 p.x = norm(V(-cosf(w) * cosf(r), sinf(w) * cosf(r), sinf(r))); p.y = cross(p.z, p.x);
 return p;
}
static void frame_at(uint32_t t_ms, struct frame *fr) {
 uint32_t t = t_ms % (uint32_t)XZ_CA_LOOP_MS;
 float ft = (float)t * (float)XZ_CA_FPS / 1000.0f;
 int f0 = (int)ft; if (f0 >= XZ_CA_FRAMES) f0 = XZ_CA_FRAMES - 1;
 int f1 = (f0 + 1) % XZ_CA_FRAMES;
 float a = ft - (float)f0;
 const struct xz_ca_frame_row *p = &xz_ca_frame[f0], *q = &xz_ca_frame[f1];
#define L(field, k, scale) lerpf((float)p->field[k], (float)q->field[k], a) * (scale)
 fr->cam = V(L(cam, 0, XZ_CA_CAM), L(cam, 1, XZ_CA_CAM), L(cam, 2, XZ_CA_CAM));
 fr->target = V(L(cam, 3, XZ_CA_CAM), L(cam, 4, XZ_CA_CAM), L(cam, 5, XZ_CA_CAM));
 fr->a = pose_a(L(a, 0, XZ_CA_POS), L(a, 1, XZ_CA_POS), L(a, 2, XZ_CA_POS), L(a, 3, 0.01f));
 fr->b = pose_b(L(b, 0, XZ_CA_POS), L(b, 1, XZ_CA_POS), L(b, 2, XZ_CA_POS), L(b, 3, 0.01f), L(b, 4, 0.01f));
 for (int k = 0; k < XZ_CABLE_USB2; k++) fr->gain[k] = L(gain, k, 1.0f / 255.0f);
 fr->gain[XZ_CABLE_USB2] = fr->gain[XZ_CABLE_USB1];
 for (int k = 0; k < 2; k++) fr->label[k] = L(label, k, 1.0f / 255.0f);
#undef L
 fr->signal = p->pulse == 65535 ? -1.0f : q->pulse == 65535 ? (float)p->pulse / 65534.0f : lerpf((float)p->pulse, (float)q->pulse, a) / 65534.0f;
 fr->label_left = (a < 0.5f ? p : q)->label_left;
 fr->f0 = f0; fr->f1 = f1; fr->t = a;
}

/* ---- visibility: the bake's run streams (build_cable_anim.py) ---- */
static int vis_get(uint32_t off, int frame, uint16_t iv[][2]) {
 const uint8_t *p = xz_ca_vis + off; int at = 0;
 for (;;) {
  uint32_t n = 0; int shift = 0; uint8_t b;
  do { b = *p++; n |= (uint32_t)(b & 127) << shift; shift += 7; } while (b & 128);
  uint8_t h = *p++; int kind = h >> 6, k = h & 63;
  if (frame < at + (int)n) {
   if (kind == 0) return 0;
   if (kind == 1) { iv[0][0] = 0; iv[0][1] = 4095; return 1; }
   const uint8_t *q = p + (kind == 3 ? (size_t)(frame - at) * (size_t)k * 3 : 0);
   for (int i = 0; i < k; i++, q += 3) { iv[i][0] = (uint16_t)(q[0] << 4 | q[1] >> 4); iv[i][1] = (uint16_t)((q[1] & 15) << 8 | q[2]); }
   return k;
  }
  at += (int)n;
  p += kind == 2 ? (size_t)k * 3 : kind == 3 ? (size_t)n * (size_t)k * 3 : 0;
 }
}
/* Visible fractions of a line at the frame: interpolated when both baked frames agree on the count. */
static int vis_at(uint32_t off, const struct frame *fr, float out[][2]) {
 uint16_t a[MAX_IV][2], b[MAX_IV][2];
 int ka = vis_get(off, fr->f0, a), kb = vis_get(off, fr->f1, b);
 if (ka == kb) {
  for (int i = 0; i < ka; i++) for (int j = 0; j < 2; j++) out[i][j] = lerpf((float)a[i][j], (float)b[i][j], fr->t) / 4095.0f;
  return ka;
 }
 int k = fr->t < 0.5f ? ka : kb; uint16_t (*s)[2] = fr->t < 0.5f ? a : b;
 for (int i = 0; i < k; i++) for (int j = 0; j < 2; j++) out[i][j] = (float)s[i][j] / 4095.0f;
 return k;
}

/* ---- the camera ---- */
struct view { v3 pos, d, r, u; float f, cx, cy; int w, h; };
static void view_make(struct view *v, const struct frame *fr, int w, int h) {
 v->pos = fr->cam; v->d = norm(sub(fr->target, fr->cam));
 v->r = norm(cross(v->d, V(0, 0, 1))); v->u = cross(v->r, v->d);
 float aspect = (float)w / (float)h, th = tanf(XZ_CA_HFOV_DEG * 0.5f * DEG);
 float half_w = th * (aspect > XZ_CA_SAFE_ASPECT ? aspect / XZ_CA_SAFE_ASPECT : 1.0f);
 v->f = (float)w / (2.0f * half_w); v->cx = (float)w * 0.5f; v->cy = (float)h * 0.5f; v->w = w; v->h = h;
}
static v3 to_view(const struct view *v, v3 p) { v3 q = sub(p, v->pos); return V(dot(q, v->r), dot(q, v->u), dot(q, v->d)); }
static void project(const struct view *v, v3 q, float *x, float *y) { *x = v->cx + q.x / q.z * v->f; *y = v->cy - q.y / q.z * v->f; }
static v3 apply(const struct pose *p, v3 l) { return add(p->o, add(mul(p->x, l.x), add(mul(p->y, l.y), mul(p->z, l.z)))); }
static v3 model_point(const int16_t q[3], int space, const struct frame *fr) {
 v3 l = V((float)q[0] * XZ_CA_POS, (float)q[1] * XZ_CA_POS, (float)q[2] * XZ_CA_POS);
 return space == 1 ? apply(&fr->a, l) : space == 2 ? apply(&fr->b, l) : l;
}

/* ---- the cable: the model's cable_point (cable-anim/model), from the plug poses ---- */
struct cable { v3 a0, za, b0, zb, e, te; float span, w, l1, l2; };
static float smoothf(float t) { t = clampf(t, 0, 1); return t * t * t * (t * (t * 6 - 15) + 10); }
static v3 bez(v3 p0, v3 p1, v3 p2, v3 p3, float u) {
 float a = (1 - u) * (1 - u) * (1 - u), b = 3 * (1 - u) * (1 - u) * u, c = 3 * (1 - u) * u * u, d = u * u * u;
 return add(add(mul(p0, a), mul(p1, b)), add(mul(p2, c), mul(p3, d)));
}
static void cable_make(struct cable *c, const struct frame *fr) {
 c->za = fr->a.z; c->zb = fr->b.z;
 c->a0 = add(fr->a.o, mul(c->za, XZ_CA_A_TAIL)); c->b0 = add(fr->b.o, mul(c->zb, XZ_CA_B_TAIL));
 c->span = len(sub(c->a0, c->b0));
 c->w = smoothf((XZ_CA_REAR_Y + XZ_CA_DRAPE_FROM - c->a0.y) / XZ_CA_DRAPE_OVER);
 c->e = V(lerpf(c->a0.x, c->b0.x, XZ_CA_EDGE_X), XZ_CA_REAR_Y + XZ_CA_EDGE_DY, XZ_CA_TOP + XZ_CA_CABLE_R + XZ_CA_EDGE_DZ);
 c->te = norm(V(XZ_CA_EDGE_DIR_X * (c->b0.x - c->a0.x) / (c->span > 1 ? c->span : 1), XZ_CA_EDGE_DIR_Y, XZ_CA_EDGE_DIR_Z));
 c->l1 = len(sub(c->a0, c->e)); c->l2 = len(sub(c->e, c->b0));
}
static v3 cable_point(const struct cable *c, float u) {
 v3 f = bez(c->a0, add(c->a0, mul(c->za, XZ_CA_FREE_A0 * c->span + XZ_CA_FREE_A1)),
  add(c->b0, mul(c->zb, XZ_CA_FREE_B0 * c->span + XZ_CA_FREE_B1)), c->b0, u);
 if (c->w <= 0) return f;
 v3 d = u < 0.5f ? bez(c->a0, add(c->a0, mul(c->za, XZ_CA_SEG1_0 * c->l1)), sub(c->e, mul(c->te, XZ_CA_SEG1_1 * c->l1)), c->e, u * 2)
  : bez(c->e, add(c->e, mul(c->te, XZ_CA_SEG2_0 * c->l2)), add(c->b0, mul(c->zb, XZ_CA_SEG2_1 * c->l2)), c->b0, u * 2 - 1);
 return vlerp(f, d, c->w);
}

/* ---- drawing into the picture ---- */
struct ctx {
 const struct xz_cable_picture *pic; const struct view *v;
 int x0, y0, x1, y1;                /* touched part of the coverage plane, inclusive; x0 > x1 when none */
 uint32_t ops; int count_only;
};
static void touch(struct ctx *c, int x0, int y0, int x1, int y1) {
 if (x0 < c->x0) c->x0 = x0; if (y0 < c->y0) c->y0 = y0; if (x1 > c->x1) c->x1 = x1; if (y1 > c->y1) c->y1 = y1;
}
static uint16_t rgb565(uint32_t c) { return (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f)); }
static uint16_t blend565(uint16_t bg, uint16_t fg, unsigned a32) {
 uint32_t b = ((uint32_t)bg | (uint32_t)bg << 16) & 0x07e0f81fu, f = ((uint32_t)fg | (uint32_t)fg << 16) & 0x07e0f81fu;
 b += (f - b) * a32 >> 5; b &= 0x07e0f81fu;
 return (uint16_t)(b | b >> 16);
}
/* One anti-aliased capsule (round caps), half width hw px, into the coverage plane: 5 bits of coverage
   and the layer above them; a pixel keeps the strongest. weight scales the coverage (the signal's tail). */
static void capsule(struct ctx *c, float x0, float y0, float x1, float y1, float hw, int layer, float weight) {
 float r = hw + 0.5f, dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
 float lo_x = (x0 < x1 ? x0 : x1) - r, hi_x = (x0 > x1 ? x0 : x1) + r, lo_y = (y0 < y1 ? y0 : y1) - r, hi_y = (y0 > y1 ? y0 : y1) + r;
 int W = c->pic->w, H = c->pic->h;
 if (hi_x < 0 || hi_y < 0 || lo_x >= (float)W || lo_y >= (float)H) return;
 int ix0 = lo_x < 0 ? 0 : (int)lo_x, ix1 = hi_x >= (float)W ? W - 1 : (int)hi_x;
 int iy0 = lo_y < 0 ? 0 : (int)lo_y, iy1 = hi_y >= (float)H ? H - 1 : (int)hi_y;
 int steep = absf(dy) > absf(dx);
 float inv = l2 > 1e-6f ? 1.0f / l2 : 0.0f;
 /* walk the major axis; across it only the band the capsule can reach */
 int a0 = steep ? iy0 : ix0, a1 = steep ? iy1 : ix1;
 float slope = steep ? (absf(dy) > 1e-6f ? dx / dy : 0) : (absf(dx) > 1e-6f ? dy / dx : 0);
 float band = r * sqrtf(1.0f + slope * slope) + 0.5f, r2 = r * r, in = r - 1.0f, in2 = in > 0 ? in * in : -1.0f;
 for (int m = a0; m <= a1; m++) {
  float mc = (float)m + 0.5f;
  float along = steep ? clampf(mc, minf(y0, y1), maxf(y0, y1)) : clampf(mc, minf(x0, x1), maxf(x0, x1));
  float centre = steep ? (absf(dy) > 1e-6f ? x0 + (along - y0) * slope : x0) : (absf(dx) > 1e-6f ? y0 + (along - x0) * slope : y0);
  int n0 = ifloor(centre - band), n1 = ifloor(centre + band);
  int lim0 = steep ? ix0 : iy0, lim1 = steep ? ix1 : iy1;
  if (n0 < lim0) n0 = lim0; if (n1 > lim1) n1 = lim1;
  if (n0 > n1) continue;
  c->ops += (uint32_t)(n1 - n0 + 1);
  if (c->count_only) continue;
  for (int n = n0; n <= n1; n++) {
   int px = steep ? n : m, py = steep ? m : n;
   float qx = (float)px + 0.5f - x0, qy = (float)py + 0.5f - y0;
   float t = clampf((qx * dx + qy * dy) * inv, 0, 1);
   float ex = qx - t * dx, ey = qy - t * dy, d2 = ex * ex + ey * ey;
   if (d2 >= r2) continue;                                   /* outside: no root needed */
   float cov = (d2 <= in2 ? 1.0f : r - sqrtf(d2)) * weight;  /* inside: full */
   if (cov <= 0) continue;
   int v = cov >= 1 ? 31 : (int)(cov * 31.0f + 0.5f);
   if (!v) continue;
   uint8_t *dst = &c->pic->cov[(size_t)py * (size_t)W + (size_t)px];
   if (layer == HALO ? !*dst || (*dst >> 5 == HALO && v > (*dst & 31)) : v > (*dst & 31) || *dst >> 5 == HALO) *dst = (uint8_t)(layer << 5 | v);
  }
  if (steep) touch(c, n0, m, n1, m); else touch(c, m, n0, m, n1);
 }
}
/* A one-pixel line (Wu): two pixels per step across its major axis, shared by how near each is to the line,
   brightened by 1/cos of its slope so a diagonal reads as heavy as a straight line. No roots; the thin
   outlines are most of the picture. Same coverage plane and rules as the capsule. */
static void thin(struct ctx *c, float x0, float y0, float x1, float y1, int layer) {
 int W = c->pic->w, H = c->pic->h, steep = absf(y1 - y0) > absf(x1 - x0);
 if (steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
 if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
 float dx = x1 - x0, dy = y1 - y0, g = dx > 1e-6f ? dy / dx : 0.0f, gain = sqrtf(1.0f + g * g);
 int lim_m = steep ? H : W, lim_n = steep ? W : H;
 int m0 = ifloor(x0 + 0.5f), m1 = ifloor(x1 - 0.5f); if (m1 < m0) m1 = m0;
 if (m1 < 0 || m0 >= lim_m) return;
 if (m0 < 0) m0 = 0; if (m1 > lim_m - 1) m1 = lim_m - 1;
 c->ops += (uint32_t)(2 * (m1 - m0 + 1));
 if (c->count_only) return;
 float y = y0 + ((float)m0 + 0.5f - x0) * g - 0.5f;
 for (int m = m0; m <= m1; m++, y += g) {
  int n = ifloor(y); float f = y - (float)n;
  float cov[2] = {(1.0f - f) * gain, f * gain};
  for (int k = 0; k < 2; k++) {
   int nn = n + k; if (nn < 0 || nn >= lim_n) continue;
   float cv = cov[k]; if (cv <= 0.02f) continue;
   int v = cv >= 1 ? 31 : (int)(cv * 31.0f + 0.5f);
   int px = steep ? nn : m, py = steep ? m : nn;
   uint8_t *dst = &c->pic->cov[(size_t)py * (size_t)W + (size_t)px];
   if (v > (*dst & 31) || *dst >> 5 == HALO) *dst = (uint8_t)(layer << 5 | v);
  }
  int a = n < 0 ? 0 : n, b = n + 1 > lim_n - 1 ? lim_n - 1 : n + 1;
  if (a <= b) { if (steep) touch(c, a, m, b, m); else touch(c, m, a, m, b); }
 }
}
/* A segment in view space, clipped to the near plane, as a capsule. */
static void seg3(struct ctx *c, v3 a, v3 b, float hw, int layer, float weight) {
 if (a.z < NEAR_MM && b.z < NEAR_MM) return;
 if (a.z < NEAR_MM) a = vlerp(a, b, (NEAR_MM - a.z) / (b.z - a.z));
 else if (b.z < NEAR_MM) b = vlerp(b, a, (NEAR_MM - b.z) / (a.z - b.z));
 float x0, y0, x1, y1; project(c->v, a, &x0, &y0); project(c->v, b, &x1, &y1);
 capsule(c, x0, y0, x1, y1, hw, layer, weight);
}
/* Tints the touched coverage into the pixels and clears it: per layer an ink and an opacity 0..256. */
static void composite(struct ctx *c, const uint16_t ink[8], const unsigned alpha[8]) {
 if (c->x0 > c->x1) return;
 c->ops += (uint32_t)((c->x1 - c->x0 + 1) * (c->y1 - c->y0 + 1));
 if (!c->count_only) {
  unsigned tab[8][32];
  for (int l = 0; l < 8; l++) for (int v = 0; v < 32; v++) tab[l][v] = (unsigned)v * alpha[l] / (31u * 8u);
  int W = c->pic->w;
  for (int y = c->y0; y <= c->y1; y++) {
   uint8_t *cv = &c->pic->cov[(size_t)y * (size_t)W]; uint16_t *px = &c->pic->px[(size_t)y * (size_t)W];
   for (int x = c->x0; x <= c->x1; x++) {
    uint8_t v = cv[x]; if (!v) continue;
    cv[x] = 0; int l = v >> 5;
    unsigned a = tab[l][v & 31]; if (a) px[x] = blend565(px[x], ink[l], a > 32 ? 32 : a);
   }
  }
 }
 c->x0 = c->pic->w; c->y0 = c->pic->h; c->x1 = c->y1 = -1;
}

/* ---- fills: the solids back to front, the holes in them dark ---- */
enum { MAX_POLY = 32 };
static int clip_near(const v3 *in, int n, v3 *out) {
 int m = 0;
 for (int i = 0; i < n; i++) {
  v3 a = in[i], b = in[(i + 1) % n];
  int ia = a.z >= NEAR_MM, ib = b.z >= NEAR_MM;
  if (ia && m < MAX_POLY) out[m++] = a;
  if (ia != ib && m < MAX_POLY) out[m++] = vlerp(a, b, (NEAR_MM - a.z) / (b.z - a.z));
 }
 return m;
}
static void fill_poly(struct ctx *c, const float *xs, const float *ys, int n, uint16_t col) {
 float lo = ys[0], hi = ys[0];
 for (int i = 1; i < n; i++) { if (ys[i] < lo) lo = ys[i]; if (ys[i] > hi) hi = ys[i]; }
 int H = c->pic->h, W = c->pic->w;
 int r0 = iceil(lo - 0.5f), r1 = ifloor(hi - 0.5f);
 if (r0 < 0) r0 = 0; if (r1 > H - 1) r1 = H - 1;
 for (int y = r0; y <= r1; y++) {
  float yc = (float)y + 0.5f, xs_at[MAX_POLY]; int k = 0;
  for (int i = 0; i < n; i++) {
   int j = (i + 1) % n;
   if ((ys[i] <= yc) != (ys[j] <= yc) && k < MAX_POLY) xs_at[k++] = xs[i] + (yc - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
  }
  for (int i = 1; i < k; i++) for (int j = i; j > 0 && xs_at[j - 1] > xs_at[j]; j--) { float t = xs_at[j]; xs_at[j] = xs_at[j - 1]; xs_at[j - 1] = t; }
  for (int i = 0; i + 1 < k; i += 2) {
   int a = iceil(xs_at[i] - 0.5f), b = ifloor(xs_at[i + 1] - 0.5f);
   if (a < 0) a = 0; if (b > W - 1) b = W - 1;
   if (a > b) continue;
   c->ops += (uint32_t)(b - a + 1);
   if (!c->count_only) for (uint16_t *p = &c->pic->px[(size_t)y * (size_t)W + (size_t)a], *e = p + (b - a + 1); p < e; p++) *p = col;
  }
 }
}
/* One face in view space if it faces the camera (the camera is the origin), with its mean depth. */
static int face_view(struct ctx *c, const struct frame *fr, int i, v3 *q, float *depth) {
 int cnt = xz_ca_fill[i].count, space = xz_ca_fill[i].flags & 3;
 if (cnt < 3 || cnt > MAX_POLY) return 0;
 float zs = 0; v3 nrm = V(0, 0, 0);
 for (int k = 0; k < cnt; k++) { q[k] = to_view(c->v, model_point(xz_ca_fill_vert[xz_ca_fill[i].first + k], space, fr)); zs += q[k].z; }
 for (int k = 0; k < cnt; k++) { v3 a = q[k], b = q[(k + 1) % cnt]; nrm = add(nrm, V((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y))); }
 if (dot(nrm, q[0]) >= 0) return 0;
 *depth = zs / (float)cnt;
 return cnt;
}
/* Clips a picture polygon to a convex one (Sutherland-Hodgman), either winding. */
static int clip_convex(const float *xs, const float *ys, int n, const float *cx, const float *cy, int m, float *ox, float *oy) {
 float area = 0, ax[MAX_POLY], ay[MAX_POLY], bx[MAX_POLY], by[MAX_POLY];
 for (int i = 0; i < m; i++) area += cx[i] * cy[(i + 1) % m] - cx[(i + 1) % m] * cy[i];
 float sgn = area < 0 ? -1.0f : 1.0f;
 int k = n; for (int i = 0; i < n; i++) { ax[i] = xs[i]; ay[i] = ys[i]; }
 for (int e = 0; e < m && k; e++) {
  float ex0 = cx[e], ey0 = cy[e], ex1 = cx[(e + 1) % m], ey1 = cy[(e + 1) % m];
  int out = 0;
  for (int i = 0; i < k; i++) {
   float px = ax[i], py = ay[i], qx = ax[(i + 1) % k], qy = ay[(i + 1) % k];
   float sp = sgn * ((ex1 - ex0) * (py - ey0) - (ey1 - ey0) * (px - ex0)), sq = sgn * ((ex1 - ex0) * (qy - ey0) - (ey1 - ey0) * (qx - ex0));
   if (sp >= 0 && out < MAX_POLY) { bx[out] = px; by[out] = py; out++; }
   if ((sp >= 0) != (sq >= 0) && out < MAX_POLY) { float t = sp / (sp - sq); bx[out] = px + (qx - px) * t; by[out] = py + (qy - py) * t; out++; }
  }
  k = out; for (int i = 0; i < k; i++) { ax[i] = bx[i]; ay[i] = by[i]; }
 }
 for (int i = 0; i < k; i++) { ox[i] = ax[i]; oy[i] = ay[i]; }
 return k;
}
/* A face's picture polygon, near-clipped; 0 when nothing is left. */
static int face_poly(struct ctx *c, const v3 *q, int cnt, float *xs, float *ys) {
 v3 cl[MAX_POLY];
 int m = clip_near(q, cnt, cl);
 for (int j = 0; j < m; j++) project(c->v, cl[j], &xs[j], &ys[j]);
 return m >= 3 ? m : 0;
}
/* Faces [from, to) that face the camera, far to near. */
static int sorted_faces(struct ctx *c, const struct frame *fr, int from, int to, uint16_t *order) {
 float depth[XZ_CA_FILLS]; int n = 0;
 for (int i = from; i < to; i++) {
  v3 q[MAX_POLY]; float z;
  if (!face_view(c, fr, i, q, &z)) continue;
  int j = n++;
  while (j > 0 && depth[j - 1] < z) { depth[j] = depth[j - 1]; order[j] = order[j - 1]; j--; }
  depth[j] = z; order[j] = (uint16_t)i;
 }
 return n;
}
static void fill_faces(struct ctx *c, const struct frame *fr, int from, int to, uint16_t col) {
 uint16_t order[XZ_CA_FILLS]; int n = sorted_faces(c, fr, from, to, order);
 for (int k = 0; k < n; k++) {
  v3 q[MAX_POLY]; float z, xs[MAX_POLY], ys[MAX_POLY];
  int cnt = face_view(c, fr, order[k], q, &z), m = cnt ? face_poly(c, q, cnt, xs, ys) : 0;
  if (m) fill_poly(c, xs, ys, m, col);
 }
}
/* A recess's opening in the picture, if the recess faces the camera. */
static int rim_poly(struct ctx *c, const struct frame *fr, int p, float *xs, float *ys) {
 int cnt = xz_ca_pocket[p].rim_count; v3 q[MAX_POLY], nrm = V(0, 0, 0);
 if (cnt < 3 || cnt > MAX_POLY) return 0;
 for (int k = 0; k < cnt; k++) q[k] = to_view(c->v, model_point(xz_ca_fill_vert[xz_ca_pocket[p].rim + k], 0, fr));
 for (int k = 0; k < cnt; k++) { v3 a = q[k], b = q[(k + 1) % cnt]; nrm = add(nrm, V((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y))); }
 if (dot(nrm, q[0]) >= 0) return 0;
 return face_poly(c, q, cnt, xs, ys);
}
/* The fills: solids in one colour (order does not matter), each recess dark but only through its opening
   (a receptacle cavity also through its well's), then the slot covers and the plugs in front. */
static void fills(struct ctx *c, const struct frame *fr, uint16_t body, uint16_t hole) {
 fill_faces(c, fr, 0, XZ_CA_SOLIDS, body);
 for (int p = 0; p < XZ_CA_POCKETS; p++) {
  float rx[MAX_POLY], ry[MAX_POLY], px[MAX_POLY], py[MAX_POLY];
  int rn = rim_poly(c, fr, p, rx, ry), pn = 0;
  if (!rn) continue;
  if (xz_ca_pocket[p].parent != 255 && !(pn = rim_poly(c, fr, xz_ca_pocket[p].parent, px, py))) continue;
  uint16_t order[XZ_CA_FILLS];
  int n = sorted_faces(c, fr, xz_ca_pocket[p].first, xz_ca_pocket[p].first + xz_ca_pocket[p].count, order);
  for (int k = 0; k < n; k++) {
   v3 q[MAX_POLY]; float z, xs[MAX_POLY], ys[MAX_POLY], ax[MAX_POLY], ay[MAX_POLY];
   int cnt = face_view(c, fr, order[k], q, &z), m = cnt ? face_poly(c, q, cnt, xs, ys) : 0;
   if (m) m = clip_convex(xs, ys, m, rx, ry, rn, ax, ay);
   if (m >= 3 && pn) m = clip_convex(ax, ay, m, px, py, pn, xs, ys); else if (m >= 3) for (int j = 0; j < m; j++) { xs[j] = ax[j]; ys[j] = ay[j]; }
   if (m >= 3) fill_poly(c, xs, ys, m, xz_ca_fill[order[k]].flags & 4 ? hole : body);
  }
 }
 fill_faces(c, fr, XZ_CA_COVERS, XZ_CA_PLUGS, body);
 fill_faces(c, fr, XZ_CA_PLUGS, XZ_CA_FILLS, body);
}

/* ---- the lines ---- */
/* Line half-widths at a picture width: the master's at 1280 px, never thinner than reads on the panel. */
static float half_width(int layer, int w) {
 static const float at1280[XZ_CABLE_LAYERS] = {1.5f, 2.4f, 2.4f, 2.2f, 2.0f, 1.8f}, least[XZ_CABLE_LAYERS] = {1.0f, 1.45f, 1.45f, 1.35f, 1.15f, 1.15f};
 float v = at1280[layer] * (float)w / 1280.0f;
 return 0.5f * (v > least[layer] ? v : least[layer]);
}
/* The visible parts of every feature line: each corner projected once, a line wholly off the picture
   skipped, corners closer than a pixel to the last one drawn dropped (a small circle is not 48 capsules). */
enum { MAX_CORNERS = 65 };
static void lines(struct ctx *c, const struct frame *fr) {
 float iv[MAX_IV][2];
 for (int layer = 0; layer < XZ_CABLE_LAYERS; layer++) {
  float hw = half_width(layer, c->pic->w), reach = hw + 1.5f;
  for (int i = 0; i < XZ_CA_POLYLINES; i++) {
   if (xz_ca_polyline[i].layer != layer) continue;
   int k = vis_at(xz_ca_polyline[i].vis, fr, iv);
   if (!k) continue;
   int first = xz_ca_polyline[i].first, cnt = xz_ca_polyline[i].count, space = xz_ca_polyline[i].space;
   int closed = xz_ca_polyline[i].closed, n = closed ? cnt + 1 : cnt;
   v3 q[MAX_CORNERS]; float s[MAX_CORNERS], lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f; int behind = 0;
   for (int j = 0; j < n; j++) {
    int vi = first + (j < cnt ? j : 0);
    q[j] = to_view(c->v, model_point(xz_ca_vert[vi], space, fr));
    s[j] = j < cnt ? (float)xz_ca_along[vi] / 65535.0f : 1.0f;
    if (q[j].z < NEAR_MM) { behind = 1; continue; }
    float x, y; project(c->v, q[j], &x, &y);
    if (x < lo_x) lo_x = x; if (x > hi_x) hi_x = x; if (y < lo_y) lo_y = y; if (y > hi_y) hi_y = y;
   }
   if (!behind && (hi_x < -reach || hi_y < -reach || lo_x > (float)c->pic->w + reach || lo_y > (float)c->pic->h + reach)) continue;
   for (int j = 0; j < k; j++) {
    /* the interval [iv0, iv1] along the line, corner by corner */
    float a0 = iv[j][0], a1 = iv[j][1];
    int seg = 0; while (seg + 2 < n && s[seg + 1] <= a0) seg++;
    v3 from = s[seg + 1] > s[seg] ? vlerp(q[seg], q[seg + 1], clampf((a0 - s[seg]) / (s[seg + 1] - s[seg]), 0, 1)) : q[seg];
    float fx = 0, fy = 0; int have = from.z >= NEAR_MM; if (have) project(c->v, from, &fx, &fy);
    for (; seg + 1 < n && s[seg] < a1; seg++) {
     int last = s[seg + 1] >= a1 || seg + 2 >= n;
     v3 to = last && s[seg + 1] > s[seg] ? vlerp(q[seg], q[seg + 1], clampf((a1 - s[seg]) / (s[seg + 1] - s[seg]), 0, 1)) : q[seg + 1];
     if (from.z < NEAR_MM || to.z < NEAR_MM) { seg3(c, from, to, hw, layer, 1.0f); from = to; have = to.z >= NEAR_MM; if (have) project(c->v, to, &fx, &fy); if (last) break; continue; }
     float tx, ty; project(c->v, to, &tx, &ty);
     if (!last && (tx - fx) * (tx - fx) + (ty - fy) * (ty - fy) < 1.0f) continue;   /* under a pixel: wait for the next corner */
     if (hw <= 0.5f) thin(c, fx, fy, tx, ty, layer); else capsule(c, fx, fy, tx, ty, hw, layer, 1.0f);
     from = to; fx = tx; fy = ty;
     if (last) break;
    }
   }
  }
 }
}

/* The cable: its two silhouettes where visible (one centre line once it is too thin to show two), the
   tube filled between them, a halo, and the signal running along it. */
struct cable_draw {
 v3 p[CABLE_N + 1], l[CABLE_N + 1], r[CABLE_N + 1];          /* view space: centre and both silhouettes */
 float sp[CABLE_N + 1][2], sl[CABLE_N + 1][2], sr[CABLE_N + 1][2]; /* the same in the picture */
 uint8_t front[CABLE_N + 1];                                  /* all three in front of the near plane */
};
static void cable_points(const struct ctx *c, const struct frame *fr, struct cable_draw *d) {
 struct cable cb; cable_make(&cb, fr);
 for (int i = 0; i <= CABLE_N; i++) d->p[i] = cable_point(&cb, (float)i / CABLE_N);
 for (int i = 0; i <= CABLE_N; i++) {
  v3 t = sub(d->p[i < CABLE_N ? i + 1 : i], d->p[i > 0 ? i - 1 : i]);
  v3 n = norm(cross(t, sub(c->v->pos, d->p[i])));
  d->l[i] = to_view(c->v, add(d->p[i], mul(n, XZ_CA_CABLE_R)));
  d->r[i] = to_view(c->v, sub(d->p[i], mul(n, XZ_CA_CABLE_R)));
  d->p[i] = to_view(c->v, d->p[i]);
  d->front[i] = d->p[i].z >= NEAR_MM && d->l[i].z >= NEAR_MM && d->r[i].z >= NEAR_MM;
  if (d->front[i]) {
   project(c->v, d->p[i], &d->sp[i][0], &d->sp[i][1]); project(c->v, d->l[i], &d->sl[i][0], &d->sl[i][1]); project(c->v, d->r[i], &d->sr[i][0], &d->sr[i][1]);
  }
 }
}
static v3 along(const v3 *pts, float u) {
 float f = clampf(u, 0, 1) * CABLE_N; int i = (int)f; if (i >= CABLE_N) i = CABLE_N - 1;
 return vlerp(pts[i], pts[i + 1], f - (float)i);
}
/* A piece [a, b] of one cable curve (0 centre, 1 and 2 the silhouettes) inside one sample step, as a capsule:
   from the projected samples when both ends are in front, else clipped in view space. */
static void cable_piece(struct ctx *c, const struct cable_draw *d, int which, float a, float b, float hw, int layer, float weight) {
 const v3 *v = which == 0 ? d->p : which == 1 ? d->l : d->r;
 const float (*s)[2] = which == 0 ? d->sp : which == 1 ? d->sl : d->sr;
 float fa = clampf(a, 0, 1) * CABLE_N, fb = clampf(b, 0, 1) * CABLE_N; int i = (int)fa; if (i >= CABLE_N) i = CABLE_N - 1;
 if (d->front[i] && d->front[i + 1]) {
  float ta = fa - (float)i, tb = fb - (float)i;
  capsule(c, lerpf(s[i][0], s[i + 1][0], ta), lerpf(s[i][1], s[i + 1][1], ta), lerpf(s[i][0], s[i + 1][0], tb), lerpf(s[i][1], s[i + 1][1], tb), hw, layer, weight);
 } else seg3(c, along(v, a), along(v, b), hw, layer, weight);
}
/* Walks [u0, u1] in sample steps, calling the piece drawer; w0..w1 weight along it (the signal's tail). */
static void cable_span(struct ctx *c, const struct cable_draw *d, int which, float u0, float u1, float hw, int layer, float w0, float w1) {
 float a = u0;
 while (a < u1) {
  float b = ((float)ifloor(a * CABLE_N) + 1.0f) / CABLE_N; if (b > u1) b = u1;
  float wa = u1 > u0 ? lerpf(w0, w1, (a - u0) / (u1 - u0)) : w1, wb = u1 > u0 ? lerpf(w0, w1, (b - u0) / (u1 - u0)) : w1;
  cable_piece(c, d, which, a, b, hw, layer, 0.5f * (wa + wb));
  if (b <= a) break;
  a = b;
 }
}
static float thickness_px(const struct ctx *c, const struct cable_draw *d, float u) {
 float f = clampf(u, 0, 1) * CABLE_N; int i = (int)f; if (i >= CABLE_N) i = CABLE_N - 1;
 if (!d->front[i] || !d->front[i + 1]) return 99;
 float t = f - (float)i, dx = lerpf(d->sr[i][0] - d->sl[i][0], d->sr[i + 1][0] - d->sl[i + 1][0], t), dy = lerpf(d->sr[i][1] - d->sl[i][1], d->sr[i + 1][1] - d->sl[i + 1][1], t);
 (void)c;
 return sqrtf(dx * dx + dy * dy);
}
static void cable_fill(struct ctx *c, const struct frame *fr, const struct cable_draw *d, uint16_t body) {
 float lv[MAX_IV][2], rv[MAX_IV][2];
 int kl = vis_at(xz_ca_cable_vis[0], fr, lv), kr = vis_at(xz_ca_cable_vis[1], fr, rv);
 for (int i = 0; i < kl; i++) for (int j = 0; j < kr; j++) {
  float u0 = maxf(lv[i][0], rv[j][0]), u1 = minf(lv[i][1], rv[j][1]);
  if (u1 <= u0) continue;
  int s0 = (int)(u0 * CABLE_N), s1 = iceil(u1 * CABLE_N);
  for (int s = s0; s < s1 && s < CABLE_N; s++) {
   float a = maxf(u0, (float)s / CABLE_N), b = minf(u1, (float)(s + 1) / CABLE_N);
   v3 q[4] = {along(d->l, a), along(d->l, b), along(d->r, b), along(d->r, a)}, cl[MAX_POLY];
   int m = clip_near(q, 4, cl); if (m < 3) continue;
   float xs[MAX_POLY], ys[MAX_POLY];
   for (int k = 0; k < m; k++) project(c->v, cl[k], &xs[k], &ys[k]);
   fill_poly(c, xs, ys, m, body);
  }
 }
}
/* The silhouettes where visible; once the cable is too thin to show two lines, its centre line, once. */
static void cable_lines(struct ctx *c, const struct frame *fr, const struct cable_draw *d) {
 float hw = half_width(XZ_CABLE_CABLE, c->pic->w), iv[MAX_IV][2];
 for (int side = 0; side < 2; side++) {
  int k = vis_at(xz_ca_cable_vis[side], fr, iv);
  for (int i = 0; i < k; i++) {
   float a = iv[i][0];
   while (a < iv[i][1]) {
    float b = minf(iv[i][1], ((float)ifloor(a * CABLE_N) + 1.0f) / CABLE_N);
    if (thickness_px(c, d, 0.5f * (a + b)) < 2.0f * hw + 1.6f) { if (!side) cable_piece(c, d, 0, a, b, hw + 0.15f, XZ_CABLE_CABLE, 1.0f); }
    else cable_piece(c, d, side ? 2 : 1, a, b, hw, XZ_CABLE_CABLE, 1.0f);
    if (b <= a) break;
    a = b;
   }
  }
 }
}
/* The halo: a faint band along each silhouette (a thin cable: one band along its centre), like the
   master's glow strokes. */
static void cable_halo(struct ctx *c, const struct frame *fr, const struct cable_draw *d) {
 float glow = 1.0f + 2.4f * (float)c->pic->w / 1280.0f, iv[MAX_IV][2];
 for (int side = 0; side < 2; side++) {
  int k = vis_at(xz_ca_cable_vis[side], fr, iv);
  for (int i = 0; i < k; i++) {
   float a = iv[i][0];
   while (a < iv[i][1]) {
    float b = minf(iv[i][1], ((float)ifloor(a * CABLE_N) + 1.0f) / CABLE_N);
    float th = thickness_px(c, d, 0.5f * (a + b));
    if (th < 2.0f * glow + 2.0f) { if (!side) cable_piece(c, d, 0, a, b, 0.5f * th + glow, HALO, 1.0f); }
    else cable_piece(c, d, side ? 2 : 1, a, b, glow, HALO, 1.0f);
    if (b <= a) break;
    a = b;
   }
  }
 }
}
static void cable_signal(struct ctx *c, const struct frame *fr, const struct cable_draw *d, float head) {
 float iv[MAX_IV][2], tail = 0.16f;
 int k = vis_at(xz_ca_cable_vis[0], fr, iv);
 float hw = half_width(XZ_CABLE_CABLE, c->pic->w) + 0.9f;
 for (int i = 0; i < k; i++) {
  float u0 = maxf(iv[i][0], head - tail), u1 = minf(iv[i][1], head);
  if (u1 > u0) cable_span(c, d, 0, u0, u1, hw, 0, (u0 - (head - tail)) / tail, (u1 - (head - tail)) / tail);
 }
}

/* ---- labels and the failure mark ---- */
static int anchor_px(const struct view *v, const struct frame *fr, int mark, float *x, float *y) {
 v3 p;
 switch (mark) {
 case XZ_CABLE_MARK_USB1: p = V(xz_ca_anchor_usb1[0], xz_ca_anchor_usb1[1], xz_ca_anchor_usb1[2]); break;
 case XZ_CABLE_MARK_SOCKET: p = V(xz_ca_anchor_socket[0], xz_ca_anchor_socket[1], xz_ca_anchor_socket[2]); break;
 case XZ_CABLE_MARK_SELECTOR: p = V(xz_ca_anchor_sel4[0], xz_ca_anchor_sel4[1], xz_ca_anchor_sel4[2]); break;
 case XZ_CABLE_MARK_CABLE: { struct cable cb; cable_make(&cb, fr); p = cable_point(&cb, 0.5f); break; }
 default: return 0;
 }
 v3 q = to_view(v, p);
 if (q.z < NEAR_MM) return 0;
 project(v, q, x, y);
 return *x >= 0 && *y >= 0 && *x < (float)v->w && *y < (float)v->h;
}
int xz_cable_anchor(int w, int h, uint32_t t_ms, int mark, int *x, int *y) {
 if (w <= 0 || h <= 0) return 0;
 struct frame fr; frame_at(t_ms, &fr); struct view v; view_make(&v, &fr, w, h);
 float fx, fy;
 if (!anchor_px(&v, &fr, mark, &fx, &fy)) return 0;
 *x = (int)fx; *y = (int)fy; return 1;
}
static void disc(const struct xz_cable_picture *pic, float cx, float cy, float rad, uint16_t col) {
 for (int y = (int)(cy - rad - 1); y <= (int)(cy + rad + 1); y++) for (int x = (int)(cx - rad - 1); x <= (int)(cx + rad + 1); x++) {
  if (x < 0 || y < 0 || x >= pic->w || y >= pic->h) continue;
  float d = sqrtf(((float)x + 0.5f - cx) * ((float)x + 0.5f - cx) + ((float)y + 0.5f - cy) * ((float)y + 0.5f - cy));
  float cov = clampf(rad + 0.5f - d, 0, 1);
  if (cov > 0) { uint16_t *p = &pic->px[(size_t)y * (size_t)pic->w + (size_t)x]; *p = blend565(*p, col, (unsigned)(cov * 32.0f + 0.5f)); }
 }
}
static void mark(struct ctx *c, const struct frame *fr, const struct xz_cable_look *look) {
 float x, y;
 if (!anchor_px(c->v, fr, look->mark, &x, &y)) return;
 float rad = c->pic->h >= 100 ? 6.5f : 5.5f;
 x += rad + 2; y -= rad + 1;
 x = clampf(x, rad + 1, (float)c->pic->w - rad - 1); y = clampf(y, rad + 1, (float)c->pic->h - rad - 1);
 disc(c->pic, x, y, rad, rgb565(look->mark_fill));
 float e = rad * 0.42f;
 capsule(c, x - e, y - e, x + e, y + e, 0.75f, 1, 1.0f); capsule(c, x + e, y - e, x - e, y + e, 0.75f, 1, 1.0f);
 uint16_t ink[8] = {0}; unsigned alpha[8] = {0}; ink[1] = rgb565(look->mark_ink); alpha[1] = 256;
 composite(c, ink, alpha);
}
static void labels(struct ctx *c, const struct frame *fr, const struct xz_cable_look *look) {
 static const char *const text[2] = {"REAR USB", "USB 1"};
 const float *at[2] = {xz_ca_anchor_label_rear, xz_ca_anchor_label_top};
 struct xz_canvas cv = {c->pic->px, (size_t)c->pic->w, c->pic->w, c->pic->h, look->theme};
 int scale = c->pic->w >= 640 ? 2 : 1, th = 9 * scale, gap = 4 * scale + c->pic->w / 100;
 for (int i = 0; i < 2; i++) {
  if (fr->label[i] < 0.04f) continue;
  int left = fr->label_left >> i & 1;
  /* USB 1 set to the right (the slot seen from its side) hangs off the slot's front edge */
  const float *a = i == 1 && !left ? xz_ca_anchor_label_top_front : at[i];
  v3 q = to_view(c->v, V(a[0], a[1], a[2]));
  if (q.z < NEAR_MM) continue;
  float x, y; project(c->v, q, &x, &y);
  if (x < 0 || y < 0 || x >= (float)c->pic->w || y >= (float)c->pic->h) continue;
  int tw = xz_text_width(cv, text[i], scale, 99);
  int tx = left ? (int)x - gap - tw : (int)x + gap, ty = (int)y - th - gap;
  if (tx + tw > c->pic->w - 2) tx = c->pic->w - 2 - tw;
  if (tx < 2) tx = 2; if (ty < 2) ty = 2; if (ty > c->pic->h - th - 2) ty = c->pic->h - th - 2;
  /* a plate in the picture's own background keeps the word readable over lines */
  unsigned g = (unsigned)(fr->label[i] * 256.0f);
  uint16_t plate = rgb565(look->bg);
  for (int yy = ty - 2; yy < ty + th + 2; yy++) for (int xx = tx - 3; xx < tx + tw + 3; xx++) {
   if (xx < 0 || yy < 0 || xx >= c->pic->w || yy >= c->pic->h) continue;
   uint16_t *p = &c->pic->px[(size_t)yy * (size_t)c->pic->w + (size_t)xx];
   *p = blend565(*p, plate, g * 26u / 256u);
  }
  xz_text(cv, tx, ty, text[i], scale, 99, xz_blend(look->bg, look->label, g));
 }
}

/* ---- the frame ---- */
static void draw_frame(struct ctx *c, const struct frame *fr, const struct xz_cable_look *look, float signal) {
 const struct xz_cable_picture *pic = c->pic;
 uint16_t bg = rgb565(look->bg), body = rgb565(look->body), hole = rgb565(look->hole);
 if (!c->count_only) for (size_t i = 0, n = (size_t)pic->w * (size_t)pic->h; i < n; i++) pic->px[i] = bg;
 c->ops += (uint32_t)(pic->w * pic->h);
 fills(c, fr, body, hole);
 struct cable_draw d; cable_points(c, fr, &d);
 if (fr->gain[XZ_CABLE_CABLE] > 0.01f) cable_fill(c, fr, &d, body);
 uint16_t ink[8]; unsigned alpha[8] = {0};
 for (int k = 0; k < XZ_CABLE_LAYERS; k++) ink[k] = rgb565(look->ink[k]);
 ink[7] = ink[0];
 /* the halo under the lines, then every line; one pass tints them all */
 if (fr->gain[XZ_CABLE_CABLE] > 0.01f) cable_halo(c, fr, &d);
 lines(c, fr);
 cable_lines(c, fr, &d);
 for (int k = 0; k < XZ_CABLE_LAYERS; k++) alpha[k] = (unsigned)(clampf(fr->gain[k], 0, 1) * 256.0f);
 ink[HALO] = ink[XZ_CABLE_CABLE]; alpha[HALO] = (unsigned)(0.20f * clampf(fr->gain[XZ_CABLE_CABLE], 0, 1) * 256.0f);
 composite(c, ink, alpha);
 if (signal >= 0 && fr->gain[XZ_CABLE_CABLE] > 0.01f) {
  cable_signal(c, fr, &d, signal);
  uint16_t s[8]; unsigned sa[8] = {0}; for (int k = 0; k < 8; k++) s[k] = rgb565(xz_blend(look->ink[XZ_CABLE_CABLE], 0xffffff, 150));
  sa[0] = 256; composite(c, s, sa);
 }
 if (!c->count_only) {
  if (look->label) labels(c, fr, look);
  if (look->mark) mark(c, fr, look);
 }
}
uint32_t xz_cable_draw(const struct xz_cable_picture *pic, uint32_t t_ms, const struct xz_cable_look *look, int signal, uint32_t budget) {
 if (!pic || !pic->px || !pic->cov || !look || pic->w <= 0 || pic->h <= 0) return 0;
 struct frame fr; frame_at(t_ms, &fr);
 struct view v; view_make(&v, &fr, pic->w, pic->h);
 float sig = signal == XZ_CABLE_SIGNAL_LOOP ? (fr.signal < 0 ? -1.0f : fr.signal * 1.16f) : signal < 0 ? -1.0f : (float)signal / 1000.0f;
 struct ctx c = {pic, &v, pic->w, pic->h, -1, -1, 0, 1};
 if (budget) {
  /* what the frame would cost, without touching the picture */
  draw_frame(&c, &fr, look, sig);
  if (c.ops > budget) return 0;
  c.ops = 0; c.x0 = pic->w; c.y0 = pic->h; c.x1 = c.y1 = -1;
 }
 c.count_only = 0;
 memset(pic->cov, 0, (size_t)pic->w * (size_t)pic->h);
 draw_frame(&c, &fr, look, sig);
 return c.ops ? c.ops : 1;
}
void xz_cable_blit(struct xz_canvas c, int x, int y, const struct xz_cable_picture *pic) {
 if (!c.p || !pic || !pic->px) return;
 for (int r = 0; r < pic->h; r++) {
  int dy = y + r; if (dy < 0 || dy >= c.height) continue;
  int from = x < 0 ? -x : 0, to = x + pic->w > c.width ? c.width - x : pic->w;
  if (to <= from) continue;
  memcpy(c.p + (size_t)dy * c.stride + (size_t)(x + from), pic->px + (size_t)r * (size_t)pic->w + (size_t)from, (size_t)(to - from) * sizeof(uint16_t));
 }
}

/* ---- the placements' policy ---- */
enum xz_cable_motion xz_cable_motion_of(int state) {
 switch (state) {
 case 1: case 6: case 7: return XZ_CABLE_LOOP;            /* not detected, failed, lost */
 case 2: case 3: case 4: return XZ_CABLE_SIGNAL;          /* scanning, starting, waiting */
 case 5: return XZ_CABLE_STILL;                           /* ready */
 default: return XZ_CABLE_HIDDEN;                         /* no engine */
 }
}
void xz_cable_look_of(struct xz_cable_look *l, const struct xz_cable_palette *p, int state, int part) {
 memset(l, 0, sizeof *l);
 l->bg = p->bg; l->body = p->body; l->hole = p->hole; l->label = p->label; l->theme = p->theme;
 uint32_t accent = state == 2 ? p->blue : state == 3 || state == 4 ? p->white : state == 5 ? p->green : p->amber;
 for (int k = 1; k < XZ_CABLE_LAYERS; k++) l->ink[k] = accent;
 l->ink[XZ_CABLE_DEVICE] = p->edge;
 if (state == 7) l->ink[XZ_CABLE_CABLE] = p->red;
 if (state == 6 && part >= 1 && part <= 4) {
  /* parts: 1 port (USB 1), 2 cable, 3 socket, 4 selector */
  static const int layer[5] = {0, XZ_CABLE_USB1, XZ_CABLE_CABLE, XZ_CABLE_SOCKET, XZ_CABLE_SELECTOR};
  static const int mark[5] = {0, XZ_CABLE_MARK_USB1, XZ_CABLE_MARK_CABLE, XZ_CABLE_MARK_SOCKET, XZ_CABLE_MARK_SELECTOR};
  for (int k = 1; k < XZ_CABLE_LAYERS; k++) l->ink[k] = p->dim;
  l->ink[layer[part]] = p->red; l->mark = mark[part]; l->mark_fill = p->red; l->mark_ink = p->mark_ink;
 }
 if (l->ink[XZ_CABLE_USB2] != p->dim) l->ink[XZ_CABLE_USB2] = xz_blend(p->edge, l->ink[XZ_CABLE_USB1] == p->red ? accent : l->ink[XZ_CABLE_USB1], 120);
}
static uint32_t mix(uint32_t h, uint32_t v) { h ^= v; return h * 16777619u; }
uint32_t xz_cable_frame_of(int state, int part, uint32_t now_ms, int moving, uint32_t step_ms, uint32_t *t_ms, int *signal) {
 enum xz_cable_motion m = xz_cable_motion_of(state);
 uint32_t step = step_ms ? step_ms : 16u, rest = xz_cable_rest_ms(state == 6 && part == 4 ? XZ_CABLE_MARK_SELECTOR : 0);
 uint32_t q = now_ms / step * step;
 *t_ms = rest; *signal = XZ_CABLE_SIGNAL_NONE;
 if (moving && m == XZ_CABLE_LOOP) { *t_ms = q % (uint32_t)XZ_CA_LOOP_MS; *signal = XZ_CABLE_SIGNAL_LOOP; }
 else if (moving && m == XZ_CABLE_SIGNAL) {
  uint32_t period = state == 2 ? 800u : 1200u;
  *signal = (int)(q % period * 1160u / period);
 }
 return mix(mix(mix(mix(2166136261u, (uint32_t)state), (uint32_t)part), *t_ms), (uint32_t)(*signal + 3)) | 1u;
}
