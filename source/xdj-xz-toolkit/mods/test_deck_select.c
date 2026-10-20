/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Deck select acceptance requires active assertions
#endif
#include "deck_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { LEFT, RIGHT };
static struct xz_deck_select d;
static uint32_t now;

static struct xz_ds_outcome ev(int side, uint16_t code, unsigned op, int shift, int32_t delta, float value) {
 struct xz_ds_key k = {side, code, op, shift, delta, value};
 return xz_deck_select_key(&d, &k, now);
}
static struct xz_ds_outcome key(int side, uint16_t code, unsigned op) { return ev(side, code, op, 0, 0, 0); }
static struct xz_ds_outcome fader(int side, float value) { return ev(side, XZ_DS_KEY_TEMPO, 5, 0, 0, value); }
static int shift_tap(int side, uint32_t gap) {
 int toggled = key(side, XZ_DS_KEY_SHIFT, 0).toggled;
 now += 60; toggled |= key(side, XZ_DS_KEY_SHIFT, 2).toggled;
 now += gap;
 return toggled;
}
static int double_press(int side) {
 int a = shift_tap(side, 100), b = shift_tap(side, 1000);
 return a || b;
}
static void reset(int route_up) { xz_deck_select_init(&d); xz_deck_select_route(&d, route_up); now = 1000; }
static int away(int side) { return d.side[side].target == XZ_DS_EXTRA; }

static void test_double_press(void) {
 reset(1);
 assert(double_press(LEFT) && away(LEFT) && !away(RIGHT));
 assert(double_press(LEFT) && !away(LEFT));
 /* SHIFT itself always reaches stock. */
 assert(key(RIGHT, XZ_DS_KEY_SHIFT, 0).verdict == XZ_DS_PASS);
 assert(key(RIGHT, XZ_DS_KEY_SHIFT, 2).verdict == XZ_DS_PASS);
}

static void test_slow_presses(void) {
 reset(1);
 /* Second press-down 401 ms after the first: a new count, not a toggle. */
 key(LEFT, XZ_DS_KEY_SHIFT, 0); now += 60; key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 341;
 assert(!key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled && !away(LEFT));
 now += 60; key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 100;
 /* That slow press counts as the first of a new pair. */
 assert(key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled && away(LEFT));
 key(LEFT, XZ_DS_KEY_SHIFT, 2);
 /* Exactly at the window still toggles. */
 reset(1);
 key(LEFT, XZ_DS_KEY_SHIFT, 0); now += 60; key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 340;
 assert(key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled);
}

static void test_modifier(void) {
 reset(1);
 /* SHIFT + PLAY keeps its stock meaning and the press does not count. */
 key(LEFT, XZ_DS_KEY_SHIFT, 0);
 assert(ev(LEFT, 0x4101, 0, 1, 0, 0).verdict == XZ_DS_PASS);
 assert(ev(LEFT, 0x4101, 2, 1, 0, 0).verdict == XZ_DS_PASS);
 now += 50; key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 50;
 assert(!key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled && !away(LEFT));
 key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 1000;
 /* A key between the two presses breaks the pair. */
 reset(1);
 shift_tap(LEFT, 50);
 key(LEFT, 0x4119, 0); key(LEFT, 0x4119, 2);
 assert(!key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled);
 /* A jog turn while SHIFT is held is SHIFT + jog. */
 reset(1);
 key(LEFT, XZ_DS_KEY_SHIFT, 0); ev(LEFT, 0x4305, 4, 1, 3, 0.5f); key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 50;
 assert(!key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled);
 /* A key on the other side is not this side's modifier. */
 reset(1);
 key(LEFT, XZ_DS_KEY_SHIFT, 0); key(RIGHT, 0x4101, 0); key(RIGHT, 0x4101, 2); key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 50;
 assert(key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled);
}

static void test_three_presses(void) {
 reset(1);
 int toggles = shift_tap(LEFT, 80) + shift_tap(LEFT, 80) + shift_tap(LEFT, 1000);
 assert(toggles == 1 && away(LEFT));
 /* The third press started a count: a fourth within the window toggles back. */
 reset(1);
 toggles = shift_tap(LEFT, 80) + shift_tap(LEFT, 80) + shift_tap(LEFT, 80) + shift_tap(LEFT, 1000);
 assert(toggles == 2 && !away(LEFT));
}

static void test_sides_independent(void) {
 reset(1);
 /* Interleaved: left first press, right first press, left second, right second. */
 key(LEFT, XZ_DS_KEY_SHIFT, 0); key(RIGHT, XZ_DS_KEY_SHIFT, 0); now += 40;
 key(LEFT, XZ_DS_KEY_SHIFT, 2); key(RIGHT, XZ_DS_KEY_SHIFT, 2); now += 40;
 assert(key(LEFT, XZ_DS_KEY_SHIFT, 0).toggled && away(LEFT) && !away(RIGHT));
 assert(key(RIGHT, XZ_DS_KEY_SHIFT, 0).toggled && away(RIGHT));
 key(LEFT, XZ_DS_KEY_SHIFT, 2); key(RIGHT, XZ_DS_KEY_SHIFT, 2); now += 1000;
 assert(double_press(RIGHT) && away(LEFT) && !away(RIGHT));
}

static void test_route(void) {
 reset(0);
 struct xz_ds_outcome o;
 shift_tap(LEFT, 80);
 o = key(LEFT, XZ_DS_KEY_SHIFT, 0);
 assert(!o.toggled && o.notice == XZ_DS_NOTICE_NO_ROUTE && !away(LEFT) && o.verdict == XZ_DS_PASS);
 key(LEFT, XZ_DS_KEY_SHIFT, 2); now += 1000;
 /* Route down drop: both sides return to DECK 1 / DECK 2. */
 reset(1);
 double_press(LEFT); double_press(RIGHT);
 assert(away(LEFT) && away(RIGHT));
 xz_deck_select_route(&d, 0);
 assert(!away(LEFT) && !away(RIGHT));
 assert(key(LEFT, 0x4101, 0).verdict == XZ_DS_PASS);
 key(LEFT, 0x4101, 2);
 xz_deck_select_route(&d, 1);
 assert(!away(LEFT));
}

static void test_routing(void) {
 reset(1);
 double_press(LEFT);
 struct xz_ds_outcome o = key(LEFT, 0x4101, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.deck == 0 && o.event.kind == XZ_EXTRA_IN_PLAY && o.event.pressed);
 o = key(LEFT, 0x4101, 2);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_PLAY && !o.event.pressed);
 assert(key(RIGHT, 0x4101, 0).verdict == XZ_DS_PASS && key(RIGHT, 0x4101, 2).verdict == XZ_DS_PASS);
 o = key(LEFT, 0x4102, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_CUE);
 key(LEFT, 0x4102, 2);
 o = ev(LEFT, 0x4305, 4, 0, -7, 1.25f);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_JOG_TURN && o.event.jog.delta == -7);
 assert(o.event.jog.speed_known && o.event.jog.speed == 1.25f);
 assert(ev(RIGHT, 0x4305, 4, 0, -7, 1.25f).verdict == XZ_DS_PASS);
 o = key(LEFT, 0x4306, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_JOG_TOUCH && o.event.pressed);
 assert(key(LEFT, 0x4306, 2).event.kind == XZ_EXTRA_IN_JOG_TOUCH);
 o = key(LEFT, 0x4313, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_SYNC);
 key(LEFT, 0x4313, 2);
 o = ev(LEFT, 0x4108, 0, 1, 0, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_MASTER_TEMPO && o.event.shift);
 key(LEFT, 0x4108, 2);
 /* Pads in the software deck's HOT CUE mode; any other pad mode swallows them. */
 o = key(LEFT, 0x411b, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_HOT_CUE && o.event.hot_cue == 2);
 o = key(LEFT, 0x411b, 2);
 assert(o.verdict == XZ_DS_DELIVER && o.event.hot_cue == 2 && !o.event.pressed);
 assert(key(LEFT, 0x4116, 0).verdict == XZ_DS_SWALLOW);
 key(LEFT, 0x4116, 2);
 assert(key(LEFT, 0x4119, 0).verdict == XZ_DS_SWALLOW && key(LEFT, 0x4119, 2).verdict == XZ_DS_SWALLOW);
 assert(ev(LEFT, 0x4114, 0, 1, 0, 0).verdict == XZ_DS_SWALLOW && !d.side[LEFT].extra_hot_cue);
 key(LEFT, 0x4114, 0); key(LEFT, 0x4114, 2);
 assert(key(LEFT, 0x4120, 0).event.hot_cue == 7);
 key(LEFT, 0x4120, 2);
 /* Loop, beat jump and the other per-deck keys never reach the native deck. */
 static const uint16_t swallowed[] = {0x410d, 0x410e, 0x410f, 0x4110, 0x4111, 0x4104, 0x4107, 0x4109,
  0x4121, 0x4122, 0x4126, 0x4127, 0x4128, 0x4312, 0x4324, 0x4325, 0x4212, 0x4213, 0x430f, 0x430b, 0x430c};
 for(size_t i = 0; i < sizeof swallowed / sizeof *swallowed; i++) {
  assert(key(LEFT, swallowed[i], 0).verdict == XZ_DS_SWALLOW);
  assert(key(LEFT, swallowed[i], 2).verdict == XZ_DS_SWALLOW);
  assert(key(RIGHT, swallowed[i], 0).verdict == XZ_DS_PASS && key(RIGHT, swallowed[i], 2).verdict == XZ_DS_PASS);
 }
 /* LOAD, TIME MODE and QUANTIZE at the global hook: taken on the switched side only. */
 assert(xz_deck_select_global_key(&d, 1, 0x430f, 0, now).verdict == XZ_DS_SWALLOW);
 assert(xz_deck_select_global_key(&d, 2, 0x430f, 0, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 2, now).verdict == XZ_DS_SWALLOW);
 /* Browse and the other global keys pass at the global hook, whatever the side. */
 assert(xz_deck_select_global_key(&d, 0, 0x4208, 0, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, 0x4101, 0, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, 0x209, 0, now).verdict == XZ_DS_PASS);
 /* The right side switched too: its PLAY goes to DECK 4. */
 now += 1000; double_press(RIGHT);
 o = key(RIGHT, 0x4101, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.deck == 1);
 key(RIGHT, 0x4101, 2);
}

static void test_ownership(void) {
 reset(1);
 /* CUE held on DECK 1 across the switch: its hold and release still reach DECK 1. */
 assert(key(LEFT, 0x4102, 0).verdict == XZ_DS_PASS);
 assert(double_press(LEFT) && away(LEFT));
 assert(key(LEFT, 0x4102, 1).verdict == XZ_DS_PASS);
 assert(key(LEFT, 0x4102, 2).verdict == XZ_DS_PASS);
 /* PLAY pressed on DECK 3, side switched back before the release: DECK 3 gets the release. */
 assert(key(LEFT, 0x4101, 0).verdict == XZ_DS_DELIVER);
 now += 1000;
 assert(double_press(LEFT) && !away(LEFT));
 struct xz_ds_outcome o = key(LEFT, 0x4101, 2);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_PLAY && !o.event.pressed);
 /* A release nobody owns on a native side reaches stock, as today. */
 assert(key(LEFT, 0x4101, 2).verdict == XZ_DS_PASS);
}

static void test_tempo_pickup(void) {
 reset(1);
 /* DECK 1 at +0.30 (stock receives it). */
 assert(fader(LEFT, 0.30f).verdict == XZ_DS_PASS);
 double_press(LEFT);
 /* DECK 3 sits at 0 %: the fader must come down through the centre first. */
 assert(fader(LEFT, 0.25f).verdict == XZ_DS_SWALLOW);
 assert(fader(LEFT, 0.10f).verdict == XZ_DS_SWALLOW);
 struct xz_ds_outcome o = fader(LEFT, -0.02f);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_TEMPO && o.event.tempo_position == -200);
 o = fader(LEFT, -0.50f);
 assert(o.verdict == XZ_DS_DELIVER && o.event.tempo_position == -5000);
 /* Back to DECK 1, still at +0.30 while the fader sits at -0.50: held until the fader crosses back. */
 now += 1000; double_press(LEFT);
 assert(fader(LEFT, -0.20f).verdict == XZ_DS_SWALLOW);
 assert(fader(LEFT, 0.29f).verdict == XZ_DS_SWALLOW);
 assert(fader(LEFT, 0.31f).verdict == XZ_DS_PASS);
 assert(fader(LEFT, 0.40f).verdict == XZ_DS_PASS);
 /* And DECK 3 kept -0.50: going there from +0.40 needs the fader down through -0.50. */
 now += 1000; double_press(LEFT);
 assert(fader(LEFT, 0.0f).verdict == XZ_DS_SWALLOW);
 assert(fader(LEFT, -0.45f).verdict == XZ_DS_SWALLOW);
 o = fader(LEFT, -0.6f);
 assert(o.verdict == XZ_DS_DELIVER && o.event.tempo_position == -6000);
 /* Crossing from below works too, and landing within the slack counts. */
 reset(1);
 fader(LEFT, -0.40f);
 double_press(LEFT);
 assert(fader(LEFT, -0.30f).verdict == XZ_DS_SWALLOW);
 assert(fader(LEFT, -0.004f).verdict == XZ_DS_DELIVER);
 /* The other side's fader is untouched throughout. */
 assert(fader(RIGHT, 0.7f).verdict == XZ_DS_PASS);
 /* A route drop returns to DECK 1 through the same pickup. */
 fader(LEFT, 0.5f);
 xz_deck_select_route(&d, 0);
 assert(fader(LEFT, 0.45f).verdict == XZ_DS_SWALLOW);
 assert(fader(LEFT, -0.41f).verdict == XZ_DS_PASS);
 /* No pickup without a known position on DECK 1: its first fader event passes. */
 reset(1);
 double_press(LEFT); double_press(LEFT);
 assert(fader(LEFT, 0.8f).verdict == XZ_DS_PASS);
 /* Pickup primitive: already there means not armed. */
 struct xz_ds_pickup p;
 xz_ds_pickup_arm(&p, 1000, 1, 1030);
 assert(!p.armed && xz_ds_pickup_feed(&p, 5000));
 xz_ds_pickup_arm(&p, 1000, 0, 0);
 assert(!xz_ds_pickup_feed(&p, 3000) && !xz_ds_pickup_feed(&p, 2000) && xz_ds_pickup_feed(&p, 999));
}

static void test_unknown(void) {
 reset(1);
 assert(key(LEFT, 0x4199, 0).verdict == XZ_DS_PASS);
 double_press(LEFT);
 struct xz_ds_outcome o = key(LEFT, 0x4199, 0);
 assert(o.verdict == XZ_DS_SWALLOW && o.unknown_first);
 o = key(LEFT, 0x4199, 1);
 assert(o.verdict == XZ_DS_SWALLOW && !o.unknown_first);
 /* An unknown release passes: stock ignores a release without its press. */
 assert(key(LEFT, 0x4199, 2).verdict == XZ_DS_PASS);
 assert(ev(LEFT, 0x4400, 4, 0, 1, 0).verdict == XZ_DS_SWALLOW);
 assert(key(RIGHT, 0x4199, 0).verdict == XZ_DS_PASS && !key(RIGHT, 0x4199, 0).unknown_first);
 /* Each code is logged once. */
 assert(!key(LEFT, 0x4199, 0).unknown_first);
}

/* With either side on its native deck, every code and operation behaves as without this module. */
static void test_stock_unchanged(void) {
 for(int route = 0; route < 2; route++) {
  reset(route);
  if(route) double_press(RIGHT);
  for(unsigned code = 0; code <= 0xffff; code++)
   for(unsigned op = 0; op <= 5; op++) {
    if(code == XZ_DS_KEY_SHIFT) continue;
    struct xz_ds_outcome o = ev(LEFT, (uint16_t)code, op, (int)(code & 1), 3, 0.25f);
    assert(o.verdict == XZ_DS_PASS && !o.toggled && !o.unknown_first);
    assert(xz_deck_select_global_key(&d, 1, (uint16_t)code, op, now).verdict == XZ_DS_PASS);
    if(!route) assert(ev(RIGHT, (uint16_t)code, op, 0, 3, 0.25f).verdict == XZ_DS_PASS);
   }
 }
}

static void test_duplicate_edges(void) {
 /* SHIFT seen at both hooks: every edge twice, still one toggle. */
 reset(1);
 int toggles = 0;
 for(int press = 0; press < 2; press++) {
  struct xz_ds_outcome a = xz_deck_select_global_key(&d, 1, XZ_DS_KEY_SHIFT, 0, now), b = key(LEFT, XZ_DS_KEY_SHIFT, 0);
  toggles += a.toggled + b.toggled;
  now += 40;
  xz_deck_select_global_key(&d, 1, XZ_DS_KEY_SHIFT, 2, now); key(LEFT, XZ_DS_KEY_SHIFT, 2);
  now += 60;
 }
 assert(toggles == 1 && away(LEFT));
 /* A flag poller feeds the same edges. */
 reset(1);
 struct xz_ds_outcome o;
 memset(&o, 0, sizeof o);
 xz_deck_select_shift(&d, RIGHT, 1, now, &o); now += 30; xz_deck_select_shift(&d, RIGHT, 0, now, &o);
 now += 30; xz_deck_select_shift(&d, RIGHT, 1, now, &o);
 assert(o.toggled && away(RIGHT));
}

static void test_decode_and_table(void) {
 unsigned char self[0x90] = {0}, input[24] = {0};
 struct xz_ds_key k;
 uint16_t code = 0x4305; int32_t delta = -12; float speed = 0.75f;
 memcpy(input + 8, &code, 2); input[11] = 0x34; memcpy(input + 12, &delta, 4); memcpy(input + 16, &speed, 4);
 assert(!xz_ds_key_decode(self, input, &k));
 self[0x26] = 2; self[0x34] = 1;
 assert(xz_ds_key_decode(self, input, &k) && k.side == 1 && k.code == 0x4305 && k.op == 4 && k.shift);
 assert(k.delta == -12 && k.value == 0.75f);
 self[0x26] = 3;
 assert(!xz_ds_key_decode(self, input, &k) && !xz_ds_key_decode(NULL, input, &k));
 /* Rows never overlap and every ownership slot fits the 64-bit masks. */
 unsigned seen = 0, slot = 0;
 for(unsigned c = 0; c <= 0xffff; c++) if(xz_ds_row_for((uint16_t)c, &slot)) { assert(slot < 64); seen++; }
 assert(seen <= 64);
 assert(xz_ds_row_for(0x4119, &slot) && xz_ds_row_for(0x4120, &slot) && !xz_ds_row_for(0x4121 + 0x100, NULL));
}

int main(void) {
 test_double_press();
 test_slow_presses();
 test_modifier();
 test_three_presses();
 test_sides_independent();
 test_route();
 test_routing();
 test_ownership();
 test_tempo_pickup();
 test_unknown();
 test_stock_unchanged();
 test_duplicate_edges();
 test_decode_and_table();
 puts("deck select: all checks passed");
 return 0;
}
