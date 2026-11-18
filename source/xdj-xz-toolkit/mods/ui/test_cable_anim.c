/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Cable explainer acceptance requires active assertions
#endif
/* The renderer's internals are under test too (the bake's streams, the frame clock). */
#include "cable_anim.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { GUARD = 64, SENTINEL = 0x5aa5 };
/* The placements' picture boxes: MODERN card, CLASSIC card, MODERN 4 DECKS, CLASSIC 4 DECKS, and the master. */
static const int sizes[][2] = {{262, 68}, {196, 58}, {300, 116}, {196, 74}, {1280, 376}};
struct buf { uint16_t *px; uint8_t *cov; struct xz_cable_picture pic; };
static struct buf make(int w, int h) {
 struct buf b;
 b.px = malloc(((size_t)w * h + 2 * GUARD) * 2); b.cov = calloc((size_t)w * h + 2 * GUARD, 1);
 for (int i = 0; i < w * h + 2 * GUARD; i++) b.px[i] = SENTINEL;
 for (int i = 0; i < GUARD; i++) { b.cov[i] = 0xa5; b.cov[GUARD + w * h + i] = 0xa5; }
 b.pic = (struct xz_cable_picture){b.px + GUARD, b.cov + GUARD, w, h};
 return b;
}
static void guards(const struct buf *b) {
 int n = b->pic.w * b->pic.h;
 for (int i = 0; i < GUARD; i++) {
  assert(b->px[i] == SENTINEL && b->px[GUARD + n + i] == SENTINEL);
  assert(b->cov[i] == 0xa5 && b->cov[GUARD + n + i] == 0xa5);
 }
 for (int i = 0; i < n; i++) assert(!b->pic.cov[i]);   /* the coverage plane is left clear */
}
static struct xz_cable_look look_for(int state, int part) {
 struct xz_cable_palette p = {0x0c0c0c, 0x161616, 0x050505, 0x9a9a9a, 0xffa82e, 0x007de1, 0xffffff, 0x28e028, 0xff4619, 0x4a4a4a, 0xb8b8b8, 0xffffff, 0};
 struct xz_cable_look l; xz_cable_look_of(&l, &p, state, part); return l;
}
static int differ(const struct xz_cable_picture *a, const uint16_t *b) {
 int n = 0; for (int i = 0; i < a->w * a->h; i++) n += a->px[i] != b[i]; return n;
}

static void test_streams(void) {
 /* Every line's visibility runs cover the loop exactly, inside the stream, intervals ordered. */
 uint32_t ends = 0;
 for (int i = 0; i < XZ_CA_POLYLINES + 2; i++) {
  uint32_t off = i < XZ_CA_POLYLINES ? xz_ca_polyline[i].vis : xz_ca_cable_vis[i - XZ_CA_POLYLINES];
  const uint8_t *p = xz_ca_vis + off; int frames = 0;
  while (frames < XZ_CA_FRAMES) {
   uint32_t n = 0; int shift = 0; uint8_t b;
   do { b = *p++; n |= (uint32_t)(b & 127) << shift; shift += 7; } while (b & 128);
   uint8_t h = *p++; int kind = h >> 6, k = h & 63;
   assert(n > 0 && (kind < 2 || k > 0));
   p += kind == 2 ? (size_t)k * 3 : kind == 3 ? (size_t)n * k * 3 : 0;
   frames += (int)n;
   assert((size_t)(p - xz_ca_vis) <= XZ_CA_VIS_BYTES);
  }
  assert(frames == XZ_CA_FRAMES);
  if ((uint32_t)(p - xz_ca_vis) > ends) ends = (uint32_t)(p - xz_ca_vis);
  for (int f = 0; f < XZ_CA_FRAMES; f++) {
   uint16_t iv[MAX_IV][2]; int k = vis_get(off, f, iv);
   assert(k >= 0 && k < MAX_IV);
   for (int j = 0; j < k; j++) { assert(iv[j][0] < iv[j][1] && iv[j][1] <= 4095); if (j) assert(iv[j][0] >= iv[j - 1][1]); }
  }
 }
 assert(ends == XZ_CA_VIS_BYTES);
 for (int i = 0; i < XZ_CA_POLYLINES; i++) {
  assert(xz_ca_polyline[i].first + xz_ca_polyline[i].count <= XZ_CA_VERTS && xz_ca_polyline[i].count >= 2);
  assert(xz_ca_polyline[i].layer < XZ_CABLE_LAYERS && xz_ca_polyline[i].space <= 2);
 }
 for (int i = 0; i < XZ_CA_FILLS; i++) assert(xz_ca_fill[i].first + xz_ca_fill[i].count <= XZ_CA_FILL_VERTS);
 for (int i = 0; i < XZ_CA_POCKETS; i++) assert(xz_ca_pocket[i].first + xz_ca_pocket[i].count <= XZ_CA_FILLS && xz_ca_pocket[i].rim + xz_ca_pocket[i].rim_count <= XZ_CA_FILL_VERTS);
 printf("PASS cable streams: %d lines and the cable's 2 silhouettes cover %d frames exactly; %d bytes of runs, intervals ordered\n",
  XZ_CA_POLYLINES, XZ_CA_FRAMES, XZ_CA_VIS_BYTES);
}
static void test_clock(void) {
 /* The frame comes from the clock alone: 60 per second, wrapping at the loop and at 2^32. */
 assert(xz_cable_loop_ms() == 13000u && XZ_CA_FRAMES == 780);
 struct frame a, b;
 for (uint32_t t = 0; t < 13000u; t += 7) {
  frame_at(t, &a); assert(a.f0 == (int)(t * 60u / 1000u) && a.f1 == (a.f0 + 1) % XZ_CA_FRAMES);
  frame_at(t + 13000u * 5u, &b); assert(b.f0 == a.f0 && b.t == a.t);
 }
 frame_at(0xffffffffu, &a); assert(a.f0 == (int)((0xffffffffu % 13000u) * 60u / 1000u));
 /* The seam: the last frame flows into the first (camera within one frame's travel). */
 struct frame last, first, mid0, mid1; frame_at(12999u, &last); frame_at(0, &first); frame_at(6000u, &mid0); frame_at(6017u, &mid1);
 assert(len(sub(last.cam, first.cam)) < 2.0f && len(sub(last.target, first.target)) < 2.0f);
 assert(len(sub(last.a.o, first.a.o)) < 0.5f && len(sub(last.b.o, first.b.o)) < 0.5f);
 printf("PASS cable clock: frame = clock x 60/s mod 780, wraps at the loop and at 2^32; seam within %.2f mm of camera travel\n",
  (double)len(sub(last.cam, first.cam)));
}
static void test_bounds_and_seam(void) {
 /* Every frame at every placement writes only its picture and leaves the coverage plane clear; the loop
    seam changes the picture no more than an ordinary frame does. */
 for (size_t s = 0; s < sizeof sizes / sizeof *sizes; s++) {
  int w = sizes[s][0], h = sizes[s][1], step = w > 400 ? 13 : 1;
  struct buf b = make(w, h); struct xz_cable_look l = look_for(1, 0);
  uint16_t *prev = malloc((size_t)w * h * 2);
  int worst = 0, seam = 0;
  for (int f = 0; f < XZ_CA_FRAMES; f += step) {
   uint32_t t = (uint32_t)f * 1000u / 60u;
   assert(xz_cable_draw(&b.pic, t, &l, XZ_CABLE_SIGNAL_LOOP, 0));
   guards(&b);
   if (f) { int d = differ(&b.pic, prev); if (d > worst) worst = d; }
   memcpy(prev, b.pic.px, (size_t)w * h * 2);
  }
  if (step == 1) {
   xz_cable_draw(&b.pic, 13000u - 17u, &l, XZ_CABLE_SIGNAL_LOOP, 0); memcpy(prev, b.pic.px, (size_t)w * h * 2);
   xz_cable_draw(&b.pic, 0, &l, XZ_CABLE_SIGNAL_LOOP, 0); seam = differ(&b.pic, prev);
   assert(seam <= worst);
  }
  /* the same moment draws the same picture */
  xz_cable_draw(&b.pic, 4321u, &l, XZ_CABLE_SIGNAL_LOOP, 0); memcpy(prev, b.pic.px, (size_t)w * h * 2);
  xz_cable_draw(&b.pic, 4321u + 13000u, &l, XZ_CABLE_SIGNAL_LOOP, 0); assert(!differ(&b.pic, prev));
  printf("PASS cable %dx%d: %d frames in bounds, plane clear; seam changes %d px (largest frame step %d px); repeatable\n",
   w, h, (XZ_CA_FRAMES + step - 1) / step, seam, worst);
  free(prev); free(b.px); free(b.cov);
 }
}
static void test_tints_and_marks(void) {
 struct buf b = make(262, 68);
 uint16_t *base = malloc(262 * 68 * 2);
 /* Accent colours follow the state; the unit's own lines and fills do not. */
 static const int states[] = {1, 2, 3, 4, 5, 7};
 struct xz_cable_look amber = look_for(1, 0);
 xz_cable_draw(&b.pic, 0, &amber, XZ_CABLE_SIGNAL_NONE, 0); memcpy(base, b.pic.px, 262 * 68 * 2);
 for (size_t i = 1; i < sizeof states / sizeof *states; i++) {
  struct xz_cable_look l = look_for(states[i], 0);
  assert(l.ink[XZ_CABLE_DEVICE] == amber.ink[XZ_CABLE_DEVICE] && l.body == amber.body);
  xz_cable_draw(&b.pic, 0, &l, XZ_CABLE_SIGNAL_NONE, 0);
  assert(differ(&b.pic, base) > 0);
 }
 struct xz_cable_look lost = look_for(7, 0); assert(lost.ink[XZ_CABLE_CABLE] == 0xff4619 && lost.ink[XZ_CABLE_USB1] == 0xffa82e);
 assert(look_for(2, 0).ink[XZ_CABLE_CABLE] == 0x007de1 && look_for(5, 0).ink[XZ_CABLE_SOCKET] == 0x28e028);
 /* USB 2's slot is a softer accent than USB 1's. */
 assert(amber.ink[XZ_CABLE_USB2] != amber.ink[XZ_CABLE_USB1] && amber.ink[XZ_CABLE_USB2] != amber.ink[XZ_CABLE_DEVICE]);
 /* Failed: the failing part red with its mark, the other accents dim; the mark sits on the part at the
    still the card rests on (the overview for the input selectors). */
 static const int parts[] = {1, 2, 3, 4};
 static const int marks[] = {XZ_CABLE_MARK_USB1, XZ_CABLE_MARK_CABLE, XZ_CABLE_MARK_SOCKET, XZ_CABLE_MARK_SELECTOR};
 for (int i = 0; i < 4; i++) {
  struct xz_cable_look l = look_for(6, parts[i]);
  assert(l.mark == marks[i] && l.mark_fill == 0xff4619);
  int red = 0; for (int k = 1; k < XZ_CABLE_LAYERS; k++) red += l.ink[k] == 0xff4619;
  assert(red == 1);
  uint32_t t = xz_cable_rest_ms(l.mark); int x, y;
  assert(xz_cable_anchor(262, 68, t, l.mark, &x, &y));
  xz_cable_draw(&b.pic, t, &l, XZ_CABLE_SIGNAL_NONE, 0);
  /* the mark's disc: red pixels near the part */
  int found = 0; uint16_t r565 = rgb565(0xff4619);
  for (int yy = y - 16; yy <= y + 16; yy++) for (int xx = x - 4; xx <= x + 24; xx++)
   if (xx >= 0 && yy >= 0 && xx < 262 && yy < 68 && b.pic.px[yy * 262 + xx] == r565) found++;
  assert(found >= 12);
  guards(&b);
 }
 assert(xz_cable_rest_ms(XZ_CABLE_MARK_SELECTOR) == 10200u && xz_cable_rest_ms(XZ_CABLE_MARK_USB1) == 0u);
 free(base); free(b.px); free(b.cov);
 puts("PASS cable tints: amber/blue/white/green per state, red cable when lost, failing part red and marked at its rest, USB 2 softer");
}
static void test_budget_and_policy(void) {
 struct buf b = make(196, 58); struct xz_cable_look l = look_for(1, 0);
 /* A frame over the budget is not drawn: the picture keeps what it had. */
 for (int i = 0; i < 196 * 58; i++) b.pic.px[i] = 0x1234;
 assert(!xz_cable_draw(&b.pic, 2000u, &l, XZ_CABLE_SIGNAL_LOOP, 1u));
 for (int i = 0; i < 196 * 58; i++) assert(b.pic.px[i] == 0x1234);
 uint32_t ops = xz_cable_draw(&b.pic, 2000u, &l, XZ_CABLE_SIGNAL_LOOP, 0);
 assert(ops > 1 && xz_cable_draw(&b.pic, 2000u, &l, XZ_CABLE_SIGNAL_LOOP, ops) == ops);
 guards(&b);
 /* The frame policy: rested shows the still; the loop follows the clock in steps; signals run 0..1160. */
 uint32_t t; int sig;
 assert(xz_cable_motion_of(1) == XZ_CABLE_LOOP && xz_cable_motion_of(6) == XZ_CABLE_LOOP && xz_cable_motion_of(7) == XZ_CABLE_LOOP);
 assert(xz_cable_motion_of(2) == XZ_CABLE_SIGNAL && xz_cable_motion_of(3) == XZ_CABLE_SIGNAL && xz_cable_motion_of(4) == XZ_CABLE_SIGNAL);
 assert(xz_cable_motion_of(5) == XZ_CABLE_STILL && xz_cable_motion_of(0) == XZ_CABLE_HIDDEN && xz_cable_motion_of(99) == XZ_CABLE_HIDDEN);
 uint32_t k0 = xz_cable_frame_of(1, 0, 100000u, 0, 0, NULL, &t, &sig); assert(t == 0 && sig == XZ_CABLE_SIGNAL_NONE);
 assert(xz_cable_frame_of(1, 0, 900000u, 0, 0, NULL, &t, &sig) == k0);                         /* rested: one still */
 xz_cable_frame_of(6, 4, 100000u, 0, 0, NULL, &t, &sig); assert(t == 10200u);                   /* selectors: overview */
 uint32_t a = xz_cable_frame_of(1, 0, 100000u, 1, 0, NULL, &t, &sig); assert(t == 100000u / 16u * 16u % 13000u && sig == XZ_CABLE_SIGNAL_LOOP);
 assert(xz_cable_frame_of(1, 0, 100000u + 15u - 100000u % 16u, 1, 0, NULL, &t, &sig) == a);     /* same 16 ms step */
 assert(xz_cable_frame_of(1, 0, 100016u, 1, 0, NULL, &t, &sig) != a);
 uint32_t s33 = xz_cable_frame_of(1, 0, 99990u, 1, 33u, NULL, &t, &sig); assert(t == 99990u / 33u * 33u % 13000u);
 assert(xz_cable_frame_of(1, 0, 99990u / 33u * 33u + 32u, 1, 33u, NULL, &t, &sig) == s33);     /* 30 fps steps */
 for (uint32_t now = 0; now < 2400u; now += 5) {
  xz_cable_frame_of(2, 0, now, 1, 0, NULL, &t, &sig); assert(t == 0 && sig >= 0 && sig <= 1160);
 }
 /* The loop's clock: it starts on the main shot, follows the clock in steps, plays on to the still once it
    may no longer move (never more than one loop, never backwards), holds it, and starts over on the main
    shot when it moves again or after a gap in the frames. Frames come every 16 ms unless a gap is meant. */
 struct xz_cable_clock c = {0};
 uint32_t t0 = 123457u, at = t0, last = 0;
 xz_cable_frame_of(1, 0, at, 1, 0, &c, &t, &sig); assert(t == 0 && sig == XZ_CABLE_SIGNAL_LOOP && xz_cable_clock_running(&c));
 while (at - t0 < 5000u) { at += 16u; xz_cable_frame_of(1, 0, at, 1, 0, &c, &t, &sig); }
 assert(t == (at - t0) / 16u * 16u);
 while (at - t0 < 13040u) { at += 16u; xz_cable_frame_of(1, 0, at, 1, 0, &c, &t, &sig); }
 assert(t == (at - t0) / 16u * 16u % 13000u && t < 100u);                                  /* round the seam */
 uint32_t stop_from = at;
 for (int i = 0; i < 2000 && xz_cable_clock_running(&c); i++) {
  last = t; at += 16u; xz_cable_frame_of(1, 0, at, 0, 0, &c, &t, &sig);
  assert(xz_cable_clock_running(&c) ? sig == XZ_CABLE_SIGNAL_LOOP && t > last : t == 0 && sig == XZ_CABLE_SIGNAL_NONE);
 }
 assert(!xz_cable_clock_running(&c) && at - stop_from <= 13000u && t == 0);
 at += 16u; xz_cable_frame_of(1, 0, at, 0, 0, &c, &t, &sig); assert(t == 0 && !xz_cable_clock_running(&c));
 at += 16u; xz_cable_frame_of(1, 0, at, 1, 0, &c, &t, &sig); assert(t == 0 && xz_cable_clock_running(&c));
 uint32_t go = at;
 while (at - go < 1000u) { at += 33u; xz_cable_frame_of(1, 0, at, 1, 33u, &c, &t, &sig); }
 assert(t == (at - go) / 33u * 33u);                                                       /* 30 fps steps */
 at += 2000u; xz_cable_frame_of(1, 0, at, 1, 0, &c, &t, &sig); assert(t == 0);            /* a 2 s gap: from the top */
 /* the selectors' rest is the overview: the loop plays on to it */
 struct xz_cable_clock d = {0};
 at = 5000u; xz_cable_frame_of(6, 4, at, 1, 0, &d, &t, &sig); assert(t == 0);
 while (xz_cable_clock_running(&d) && at < 40000u) { at += 16u; xz_cable_frame_of(6, 4, at, at < 7000u, 0, &d, &t, &sig); }
 assert(t == 10200u && at - 5000u >= 10200u && at - 5000u < 10200u + 16u);
 /* signal states leave the clock stopped: a running loop that turns into a signal stops on the spot */
 xz_cable_frame_of(1, 0, 90000u, 1, 0, &c, &t, &sig); assert(xz_cable_clock_running(&c));
 xz_cable_frame_of(2, 0, 90016u, 1, 0, &c, &t, &sig); assert(!xz_cable_clock_running(&c) && t == 0 && sig >= 0);
 free(b.px); free(b.cov);
 puts("PASS cable budget and policy: over budget keeps the last picture; rest still; 16/33 ms steps; signal 0..1160; the loop starts on the main shot and plays on to its rest");
}
int main(void) { test_streams(); test_clock(); test_bounds_and_seam(); test_tints_and_marks(); test_budget_and_policy(); return 0; }
