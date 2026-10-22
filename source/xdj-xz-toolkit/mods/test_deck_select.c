/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Deck select acceptance requires active assertions
#endif
#include "deck_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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
 assert(key(LEFT,0x4313,0).verdict==XZ_DS_SWALLOW);
 o=key(LEFT,0x4313,2);
 assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_SYNC&&o.event.pressed);
 o = ev(LEFT, 0x4108, 0, 1, 0, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_MASTER_TEMPO && o.event.shift);
 key(LEFT, 0x4108, 2);
 /* Pads always reach the software deck, which applies its own pad mode. */
 o = key(LEFT, 0x411b, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_HOT_CUE && o.event.hot_cue == 2);
 o = key(LEFT, 0x411b, 2);
 assert(o.verdict == XZ_DS_DELIVER && o.event.hot_cue == 2 && !o.event.pressed);
 /* Pad-mode buttons HOT CUE, BEAT LOOP, SLIP LOOP, BEAT JUMP go to the software deck with their mode;
    the native deck's pad mode never changes. 0x4118 stays swallowed. */
 for(int mode = 0; mode < 4; mode++) {
  o = ev(LEFT, (uint16_t)(0x4114 + mode), 0, mode == 0, 0, 0);
  assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_PAD_MODE && o.event.hot_cue == mode &&
         o.event.pressed && o.event.shift == (mode == 0));
  o = key(LEFT, (uint16_t)(0x4114 + mode), 2);
  assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_PAD_MODE && o.event.hot_cue == mode && !o.event.pressed);
  assert(key(RIGHT, (uint16_t)(0x4114 + mode), 0).verdict == XZ_DS_PASS);
  assert(key(RIGHT, (uint16_t)(0x4114 + mode), 2).verdict == XZ_DS_PASS);
 }
 o=key(LEFT,0x4118,0);assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_LOOP_4_8&&o.event.pressed);
 o=key(LEFT,0x4118,1);assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_LOOP_4_8&&o.event.held);
 o=key(LEFT,0x4118,3);assert(o.verdict==XZ_DS_DELIVER&&!o.event.pressed);
 o = key(LEFT, 0x4119, 0);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_HOT_CUE && o.event.hot_cue == 0);
 o = key(LEFT, 0x4119, 2);
 assert(o.verdict == XZ_DS_DELIVER && o.event.hot_cue == 0 && !o.event.pressed);
 assert(key(LEFT, 0x4120, 0).event.hot_cue == 7);
 key(LEFT, 0x4120, 2);
 static const struct { uint16_t key; enum xz_extra_input kind; } controls[] = {
  {0x410d, XZ_EXTRA_IN_LOOP_IN}, {0x410e, XZ_EXTRA_IN_LOOP_OUT}, {0x410f, XZ_EXTRA_IN_RELOOP},
  {0x4104, XZ_EXTRA_IN_VINYL}, {0x4107, XZ_EXTRA_IN_TEMPO_RANGE}, {0x4109, XZ_EXTRA_IN_TEMPO_RESET},
  {0x4324, XZ_EXTRA_IN_LOOP_DOUBLE}, {0x4325, XZ_EXTRA_IN_LOOP_HALF},
  {0x4110, XZ_EXTRA_IN_REVERSE}, {0x4111, XZ_EXTRA_IN_SLIP},
  {0x4121, XZ_EXTRA_IN_SEARCH}, {0x4122, XZ_EXTRA_IN_SEARCH}
 };
 for (unsigned i = 0; i < sizeof controls / sizeof *controls; ++i) {
  o = key(LEFT, controls[i].key, 0);
  assert(o.verdict == XZ_DS_DELIVER && o.event.kind == controls[i].kind && o.event.pressed);
  o = key(LEFT, controls[i].key, 1);
  if (controls[i].kind == XZ_EXTRA_IN_LOOP_IN) assert(o.verdict == XZ_DS_DELIVER && o.event.held);
  else assert(o.verdict == XZ_DS_SWALLOW);
  assert(key(LEFT, controls[i].key, 2).verdict == XZ_DS_DELIVER);
  assert(key(RIGHT, controls[i].key, 0).verdict == XZ_DS_PASS);
  assert(key(RIGHT, controls[i].key, 2).verdict == XZ_DS_PASS);
 }
 /* Unsupported per-deck keys never reach the native deck. */
 static const uint16_t swallowed[] = {
  0x4126, 0x4127, 0x4128, 0x4312, 0x4212, 0x4213, 0x430f, 0x430b, 0x430c};
 for(size_t i = 0; i < sizeof swallowed / sizeof *swallowed; i++) {
  assert(key(LEFT, swallowed[i], 0).verdict == XZ_DS_SWALLOW);
  assert(key(LEFT, swallowed[i], 2).verdict == XZ_DS_SWALLOW);
  assert(key(RIGHT, swallowed[i], 0).verdict == XZ_DS_PASS && key(RIGHT, swallowed[i], 2).verdict == XZ_DS_PASS);
 }
 /* LOAD, TIME MODE and QUANTIZE at the global hook: taken on the switched side only. LOAD's press asks the
    caller where it goes. */
 o = xz_deck_select_global_key(&d, 1, 0x430f, 0, now);
 assert(o.verdict == XZ_DS_LOAD && o.event.deck == LEFT && o.event.kind == XZ_EXTRA_IN_LOAD && o.event.pressed);
 assert(xz_deck_select_global_key(&d, 1, 0x430f, 2, now).verdict == XZ_DS_SWALLOW);
 assert(xz_deck_select_global_key(&d, 2, 0x430f, 0, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 2, 0x430f, 2, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 2, now).verdict == XZ_DS_PASS);   /* release without its press */
 /* TIME MODE: a short press switches the time display on release, a 1 s hold AUTO CUE; every deck, and
    stock still gets every contact. SHIFT: the side's deck only, stock never sees it. */
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 0, now).verdict == XZ_DS_PASS);
 o = xz_deck_select_global_key(&d, 1, 0x430b, 2, now);
 assert(o.verdict == XZ_DS_DELIVER && o.also_stock && o.event.kind == XZ_EXTRA_IN_TIME_MODE && !o.event.shift && o.event.deck == LEFT);
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 0, now).verdict == XZ_DS_PASS);
 o = xz_deck_select_global_key(&d, 1, 0x430b, 1, now);
 assert(o.verdict == XZ_DS_DELIVER && o.also_stock && o.event.kind == XZ_EXTRA_IN_AUTO_CUE);
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 1, now).verdict == XZ_DS_PASS);       /* repeated hold */
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 2, now).verdict == XZ_DS_PASS);       /* held: no TIME MODE */
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_SHIFT, 0, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, 0x430b, 0, now).verdict == XZ_DS_SWALLOW);
 o = xz_deck_select_global_key(&d, 1, 0x430b, 2, now);
 assert(o.verdict == XZ_DS_DELIVER && !o.also_stock && o.event.kind == XZ_EXTRA_IN_TIME_MODE && o.event.shift);
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_SHIFT, 2, now).verdict == XZ_DS_PASS);
 assert(d.side[LEFT].target == XZ_DS_EXTRA);
 /* DECK QUANTIZE (manual): a press is for every deck, so stock still gets it and the software deck of that
    side hears it too; SHIFT + press is for the side's deck only. Left drives DECK 3, right drives DECK 2. */
 o = xz_deck_select_global_key(&d, 1, 0x430c, 0, now);
 assert(o.verdict == XZ_DS_DELIVER && o.also_stock && o.event.kind == XZ_EXTRA_IN_QUANTIZE && o.event.deck == LEFT && !o.event.shift);
 assert(xz_deck_select_global_key(&d, 1, 0x430c, 2, now).verdict == XZ_DS_PASS);
 o = xz_deck_select_global_key(&d, 2, 0x430c, 0, now);
 assert(o.verdict == XZ_DS_DELIVER && o.also_stock && o.event.deck == RIGHT && !o.event.shift);
 assert(xz_deck_select_global_key(&d, 2, 0x430c, 2, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_SHIFT, 0, now).verdict == XZ_DS_PASS);
 o = xz_deck_select_global_key(&d, 1, 0x430c, 0, now);
 assert(o.verdict == XZ_DS_DELIVER && !o.also_stock && o.event.shift && o.event.deck == LEFT);
 assert(xz_deck_select_global_key(&d, 1, 0x430c, 2, now).verdict == XZ_DS_SWALLOW);  /* stock never saw it */
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_SHIFT, 2, now).verdict == XZ_DS_PASS);
 assert(d.side[LEFT].target == XZ_DS_EXTRA);                                           /* SHIFT was a modifier */
 assert(xz_deck_select_global_key(&d, 2, XZ_DS_KEY_SHIFT, 0, now).verdict == XZ_DS_PASS);
 o = xz_deck_select_global_key(&d, 2, 0x430c, 0, now);
 assert(o.verdict == XZ_DS_PASS && !o.also_stock);                                     /* native deck only */
 assert(xz_deck_select_global_key(&d, 2, 0x430c, 2, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 2, XZ_DS_KEY_SHIFT, 2, now).verdict == XZ_DS_PASS);
 assert(d.side[RIGHT].target == XZ_DS_NATIVE);
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
 /* A software SYNC moves the target even though no fader event occurred. */
 reset(1);fader(LEFT,0);double_press(LEFT);
 struct xz_extra_decks extra={0};extra.route_up=1;
 extra.deck[0].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO_RANGE);
 extra.deck[0].tempo_range=10;extra.deck[0].tempo_x100=500;extra.deck[0].tempo_revision=1;
 xz_deck_select_feedback(&d,&extra);
 assert(fader(LEFT,.2f).verdict==XZ_DS_SWALLOW);
 assert(fader(LEFT,.51f).verdict==XZ_DS_DELIVER);
 /* A late snapshot with the same revision must not undo a newer physical move. */
 xz_deck_select_feedback(&d,&extra);
 assert(fader(LEFT,.6f).verdict==XZ_DS_DELIVER);
 double_press(LEFT);
 extra.deck[0].tempo_x100=-300;extra.deck[0].tempo_revision++;
 xz_deck_select_feedback(&d,&extra);
 double_press(LEFT);
 assert(fader(LEFT,0).verdict==XZ_DS_SWALLOW);
 assert(fader(LEFT,-.31f).verdict==XZ_DS_DELIVER);
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
    /* Stock still receives every key; DECK QUANTIZE and TIME MODE also reach the software decks (they are
       for every deck). */
    struct xz_ds_outcome g = xz_deck_select_global_key(&d, 1, (uint16_t)code, op, now);
    assert(g.verdict == XZ_DS_PASS || (route && (code == 0x430c || code == 0x430b) && g.verdict == XZ_DS_DELIVER && g.also_stock));
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

static void test_fallback_channel_boundary(void) {
 static const uint16_t codes[]={XZ_DS_KEY_SHIFT,0x4306,0x4305,0x4101,0x4102};
 for(unsigned side=0;side<2;side++)for(unsigned n=0;n<sizeof codes/sizeof *codes;n++)
  for(unsigned op=0;op<=4;op++){
   unsigned char self[0x90]={0},input[24]={0};struct xz_ds_key k;
   self[0x26]=(unsigned char)(side+1);memcpy(input+8,&codes[n],2);input[11]=(unsigned char)op;
   /* An unspecified channel keeps the established physical-callback contract. */
   assert(xz_ds_key_decode(self,input,&k)&&k.side==(int)side);
   input[10]=(unsigned char)(side+1);
   assert(xz_ds_key_decode(self,input,&k)&&k.side==(int)side);
   input[10]=(unsigned char)(2-side);
   memset(&k,0x55,sizeof k);struct xz_ds_key untouched=k;
   assert(!xz_ds_key_decode(self,input,&k));
   assert(!memcmp(&k,&untouched,sizeof k));
   assert(xz_ds_key_decode_global(input,&k)&&k.side==(int)(1-side));
  }
}

/* LOAD on a switched side: the press asks, hold and release follow the answer, a native press keeps going
   to stock, and SHORTCUT DECK SELECT switches as SHIFT twice does. */
static void test_load_routes(void) {
 static const enum xz_ds_load_route routes[] = {XZ_DS_LOAD_SWALLOW, XZ_DS_LOAD_SHELL, XZ_DS_LOAD_STOCK};
 for(int r = 0; r < 3; r++) {
  reset(1);
  assert(double_press(RIGHT) && away(RIGHT));
  struct xz_ds_outcome o = xz_deck_select_global_key(&d, 2, XZ_DS_KEY_LOAD, 0, now);
  assert(o.verdict == XZ_DS_LOAD && o.event.deck == RIGHT);
  xz_deck_select_load_route(&d, RIGHT, routes[r]);
  assert(xz_deck_select_global_key(&d,2,XZ_DS_KEY_LOAD,0,now).verdict==XZ_DS_SWALLOW);
  enum xz_ds_verdict follow = routes[r] == XZ_DS_LOAD_STOCK ? XZ_DS_PASS : XZ_DS_SWALLOW;
  assert(xz_deck_select_global_key(&d, 2, XZ_DS_KEY_LOAD, 1, now).verdict == follow);
  /* The other side's LOAD is its own contact. */
  assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 0, now).verdict == XZ_DS_PASS);
  assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 2, now).verdict == XZ_DS_PASS);
  assert(xz_deck_select_global_key(&d, 2, XZ_DS_KEY_LOAD, 2, now).verdict == follow);
  /* After the release the contact is over: a stray release passes. */
  assert(xz_deck_select_global_key(&d, 2, XZ_DS_KEY_LOAD, 2, now).verdict == XZ_DS_PASS);
  o=xz_deck_select_global_key(&d,2,XZ_DS_KEY_LOAD,0,now);
  assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_INSTANT_DOUBLES);
  xz_deck_select_global_key(&d,2,XZ_DS_KEY_LOAD,2,now);
 }
 /* A route given with no LOAD down changes nothing. */
 reset(1);
 assert(double_press(LEFT));
 xz_deck_select_load_route(&d, LEFT, XZ_DS_LOAD_STOCK);
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 0, now).verdict == XZ_DS_LOAD);
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 2, now).verdict == XZ_DS_SWALLOW);
 /* A press that reached stock on the native deck keeps reaching stock, even if the side switches before
    its release. */
 reset(1);
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 0, now).verdict == XZ_DS_PASS);
 xz_deck_select_toggle(&d, LEFT, NULL);
 assert(away(LEFT));
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 1, now).verdict == XZ_DS_PASS);
 assert(xz_deck_select_global_key(&d, 1, XZ_DS_KEY_LOAD, 2, now).verdict == XZ_DS_PASS);
 /* LOAD reaching the PLAYER path on a switched side is still never stock's. */
 assert(key(LEFT, XZ_DS_KEY_LOAD, 0).verdict == XZ_DS_SWALLOW);
 /* SHORTCUT DECK SELECT. */
 reset(1);
 struct xz_ds_outcome o;
 memset(&o, 0, sizeof o); xz_deck_select_toggle(&d, RIGHT, &o);
 assert(o.toggled && !o.notice && away(RIGHT) && !away(LEFT));
 memset(&o, 0, sizeof o); xz_deck_select_toggle(&d, RIGHT, &o);
 assert(o.toggled && !away(RIGHT));
 /* Tempo pickup follows the tile's switch as it follows SHIFT twice. */
 fader(RIGHT, 0.5f);
 xz_deck_select_toggle(&d, RIGHT, NULL);
 assert(fader(RIGHT, 0.4f).verdict == XZ_DS_SWALLOW);
 o = fader(RIGHT, -0.1f);
 assert(o.verdict == XZ_DS_DELIVER && o.event.kind == XZ_EXTRA_IN_TEMPO);
 /* Route down: the notice, no switch. */
 reset(0);
 memset(&o, 0, sizeof o); xz_deck_select_toggle(&d, LEFT, &o);
 assert(!o.toggled && o.notice == XZ_DS_NOTICE_NO_ROUTE && !away(LEFT));
 xz_deck_select_toggle(&d, 2, &o); xz_deck_select_toggle(NULL, 0, &o);
 puts("PASS LOAD routes on a switched side and SHORTCUT DECK SELECT");
}

static void test_physical_jog_matrix(void) {
 static const int32_t deltas[]={-31,0,31,INT32_MIN,INT32_MAX};
 for(unsigned mask=0;mask<4;mask++)for(int side=0;side<2;side++){
  reset(1);
  for(int selected=0;selected<2;selected++)if(mask&(1u<<selected))xz_deck_select_toggle(&d,selected,NULL);
  unsigned char self[0x88]={0},input[24]={0};self[0x26]=(unsigned char)(side+1);
  uint16_t code=0x4305;memcpy(input+8,&code,2);input[11]=0x34;
  for(unsigned n=0;n<sizeof(deltas)/sizeof(*deltas);n++){
   float speed=-.75f;memcpy(input+12,&deltas[n],4);memcpy(input+16,&speed,4);
   struct xz_ds_key decoded;assert(xz_ds_key_decode(self,input,&decoded));
   struct xz_ds_outcome o=xz_deck_select_key(&d,&decoded,now++);
   if(mask&(1u<<side)){
    assert(o.verdict==XZ_DS_DELIVER&&o.event.deck==side&&o.event.kind==XZ_EXTRA_IN_JOG_TURN);
    assert(o.event.jog.delta==deltas[n]&&o.event.jog.speed_known&&o.event.jog.speed==speed);
   }else assert(o.verdict==XZ_DS_PASS);
  }
 }
 for(int side=0;side<2;side++){
  reset(1);xz_deck_select_toggle(&d,side,NULL);
  assert(key(side,0x4306,0).verdict==XZ_DS_DELIVER);
  xz_deck_select_toggle(&d,side,NULL);
  struct xz_ds_outcome o=key(side,0x4306,2);
  assert(o.verdict==XZ_DS_DELIVER&&o.event.deck==side&&!o.event.pressed);
  assert(key(side,0x4306,2).verdict==XZ_DS_PASS);
  reset(1);assert(key(side,0x4306,0).verdict==XZ_DS_PASS);
  xz_deck_select_toggle(&d,side,NULL);
  assert(key(side,0x4306,2).verdict==XZ_DS_PASS);
  reset(1);xz_deck_select_toggle(&d,side,NULL);
  assert(key(side,0x4306,0).verdict==XZ_DS_DELIVER);
  xz_deck_select_route(&d,0);
  o=key(side,0x4306,3);
  assert(o.verdict==XZ_DS_DELIVER&&o.event.deck==side&&!o.event.pressed);
  assert(key(side,0x4306,3).verdict==XZ_DS_PASS);
  xz_deck_select_route(&d,1);xz_deck_select_toggle(&d,side,NULL);
  assert(key(side,0x4306,0).event.pressed);
  assert(!key(side,0x4306,2).event.pressed);
 }
}

static void test_global_track_restart(void) {
 for(int side=0;side<2;side++) {
  reset(1);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,0,now).verdict==XZ_DS_PASS);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,2,now).verdict==XZ_DS_PASS);
  assert(double_press(side));
  struct xz_ds_outcome o=xz_deck_select_global_key(&d,side+1,0x4213,0,now);
  assert(o.verdict==XZ_DS_DELIVER&&o.event.deck==side&&o.event.kind==XZ_EXTRA_IN_TRACK_PREV);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,0,now).verdict==XZ_DS_SWALLOW);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,1,now).verdict==XZ_DS_SWALLOW);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,2,now).verdict==XZ_DS_SWALLOW);
  o=xz_deck_select_global_key(&d,side+1,0x4212,0,now);
  assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_TRACK_NEXT&&o.event.deck==side&&o.event.pressed);
  assert(xz_deck_select_global_key(&d,side+1,0x4212,0,now).verdict==XZ_DS_SWALLOW);
  xz_deck_select_global_key(&d,side+1,0x4212,2,now);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,0,now).verdict==XZ_DS_DELIVER);
  xz_deck_select_toggle(&d,side,NULL);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,2,now).verdict==XZ_DS_SWALLOW);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,0,now).verdict==XZ_DS_PASS);
  xz_deck_select_toggle(&d,side,NULL);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,2,now).verdict==XZ_DS_PASS);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,0,now).verdict==XZ_DS_DELIVER);
  xz_deck_select_route(&d,0);
  assert(xz_deck_select_global_key(&d,side+1,0x4213,2,now).verdict==XZ_DS_SWALLOW);
 }
}

int main(void) {
 for(int side=0;side<2;side++){
  reset(1);double_press(side);
  for(int end=0;end<3;end++){
   struct xz_ds_outcome o=ev(side,0x4123,5,0,0,(float)end/2.f);
   assert(o.verdict==XZ_DS_DELIVER&&o.event.kind==XZ_EXTRA_IN_VINYL_SPEED&&o.event.deck==side);
   assert(o.event.tempo_position==(end-1)*XZ_EXTRA_TEMPO_SPAN);
  }
  assert(ev(side,0x4123,5,0,0,NAN).verdict==XZ_DS_SWALLOW);
  assert(ev(side,0x4123,5,0,0,1.1f).verdict==XZ_DS_SWALLOW);
 }
 test_global_track_restart();
 test_double_press();
 test_slow_presses();
 test_modifier();
 test_three_presses();
 test_sides_independent();
 test_route();
 test_routing();
 test_physical_jog_matrix();
 test_ownership();
 test_tempo_pickup();
 test_unknown();
 test_stock_unchanged();
 test_duplicate_edges();
 test_decode_and_table();
 test_fallback_channel_boundary();
 test_load_routes();
 puts("deck select: all checks passed");
 return 0;
}
