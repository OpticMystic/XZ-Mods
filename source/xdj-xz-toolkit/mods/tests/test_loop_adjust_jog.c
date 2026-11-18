/* DECK 3 / 4 loop adjust (XZ manual pp.47-48) heard through the fake transport: while the jog moves a loop
   edge, the loop keeps playing. Turning the jog must not restart what plays on every tick; only an edge that
   passes what is already queued for the wire may cut it. Usage: test-loop-adjust-jog FIXTURE_DIR.
   Pure host code with the player suite's fake transport; nothing touches a device. */
#define main retained_player_main
#include "test_extra_player.c"
#undef main

/* One wire turn of 5 ms on both decks, then the worker. */
static void tick_time(void) { clock_ms += 5; consume(0, 220.5); consume(1, 220.5); step(); }
static uint64_t heard_tag(int d, int *valid) {
 pthread_mutex_lock(&f.lock);
 uint64_t t = f.position.deck[d].source_after_completed; *valid = f.position.deck[d].source_valid != 0 && t != 0;
 pthread_mutex_unlock(&f.lock); return t;
}
/* heard_ms: source time the completed position moved forward (a wrap adds its part after IN); played_ms: output
   time the wire completed, so silence is wall_ms - played_ms. */
struct listen { unsigned flushes, restarts, wraps; double heard_ms, played_ms, wall_ms; };
static uint64_t played_frames(int d, uint32_t *generation) {
 pthread_mutex_lock(&f.lock);
 uint64_t n = f.position.deck[d].completed_frames; *generation = f.position.deck[d].applied_generation;
 pthread_mutex_unlock(&f.lock); return n;
}
/* Turn the jog by delta every period_ms for ticks ticks while the wire runs, and follow the completed source
   position (what the DJ hears). A backward move is a wrap when it lands near IN (IN as it was at any time in the
   turn: queued audio wrapped where IN was when it was rendered) after the position reached the loop's OUT (the
   lowest OUT of the turn); any other backward move is a restart. */
static struct listen turn(int d, int delta, unsigned ticks, unsigned period_ms) {
 struct listen l = {0};
 unsigned before = flushes(d), rate = debug(d).source_rate;
 int valid; uint64_t prev = heard_tag(d, &valid); if (!valid) prev = 0;
 uint64_t in_lo = debug(d).loop_in, in_hi = in_lo, out_lo = debug(d).loop_out, slack = (uint64_t)rate / 10;
 uint32_t generation, g; uint64_t played = played_frames(d, &generation);
 for (unsigned i = 0; i < ticks; i++) {
  jog(d, delta);
  for (unsigned t = 0; t < period_ms; t += 5) {
   tick_time(); l.wall_ms += 5;
   uint64_t in = debug(d).loop_in, out = debug(d).loop_out;
   if (in < in_lo) in_lo = in;
   if (in > in_hi) in_hi = in;
   if (out < out_lo) out_lo = out;
   uint64_t n = played_frames(d, &g);
   l.played_ms += (double)(g == generation ? n - played : n) * 1000.0 / XZ_USB_RATE; played = n; generation = g;
   uint64_t now = heard_tag(d, &valid);
   if (!valid) continue;
   if (prev && now >= prev) l.heard_ms += (double)(now - prev) * 1000.0 / rate;
   else if (prev) {
    if (now >= in_lo && now <= in_hi + slack && prev + slack >= out_lo) { l.wraps++; l.heard_ms += (double)(now >= in ? now - in : 0) * 1000.0 / rate; }
    else { l.restarts++; printf("  restart: from %llu to %llu (IN %llu..%llu, OUT >= %llu)\n", (unsigned long long)prev,
                               (unsigned long long)now, (unsigned long long)in_lo, (unsigned long long)in_hi, (unsigned long long)out_lo); }
   }
   prev = now;
  }
 }
 l.flushes = flushes(d) - before;
 return l;
}
static void report(const char *name, struct listen l) {
 printf("%s: %u jog flushes, %u wraps, %u restarts, the position moved %.0f ms and the wire played %.0f ms in %.0f ms\n",
        name, l.flushes, l.wraps, l.restarts, l.heard_ms, l.played_ms, l.wall_ms);
}
static void start_loop(int d) {
 /* A 500 ms loop, 500..1000 ms, playing. */
 seek(d, 500); hold(d, XZ_EXTRA_IN_LOOP_IN, 1); hold(d, XZ_EXTRA_IN_LOOP_IN, 0);
 seek(d, 1000); hold(d, XZ_EXTRA_IN_LOOP_OUT, 1); hold(d, XZ_EXTRA_IN_LOOP_OUT, 0);
 assert(debug(d).loop_active && debug(d).state == XZ_XP_PLAYING);
 for (int i = 0; i < 40; i++) tick_time();
}
static int failures_seen;
#define EXPECT(x) do { if (!(x)) { failures_seen++; printf("FAIL line=%d %s\n", __LINE__, #x); } } while (0)

int main(int argc, char **argv) {
 if (argc < 2) { fputs("usage: test-loop-adjust-jog FIXTURE_DIR\n", stderr); return 2; }
 snprintf(fixtures, sizeof fixtures, "%s", argv[1]); snprintf(anlz_dir, sizeof anlz_dir, "%s/anlz", argv[1]);
 assert(!pthread_mutex_init(&f.lock, NULL)); build_analysis();
 struct xz_xp_env env = {{NULL, fake_link, fake_submit, fake_flush, fake_position, fake_transition}, now_ms, resolve, master_bpm, NULL, NULL, 0, 0};
 for (int d = 0; d < 2; d++) {
  p = xz_xp_create(&env); assert(p);
  fake_route(7); step(); load(0, 1); load(1, 2);
  for (int i = 0; i < 20; i++) tick_time();
  start_loop(d);
  uint32_t rate = debug(d).source_rate;
  /* OUT ADJUST: the button lights, the jog pulls OUT in. A DJ turns about one tick per 10 ms. */
  hold(d, XZ_EXTRA_IN_LOOP_OUT, 1); hold(d, XZ_EXTRA_IN_LOOP_OUT, 0);
  EXPECT(debug(d).loop_edit == 2);
  struct listen slow = turn(d, -1, 30, 10);
  report(d ? "DECK 4 OUT in, slow" : "DECK 3 OUT in, slow", slow);
  EXPECT(debug(d).loop_out == ms_frames(d, 1000) - 30u * (rate / 150u));
  EXPECT(slow.restarts == 0);
  EXPECT(slow.flushes <= slow.wraps + 1);
  EXPECT(slow.heard_ms >= slow.wall_ms * 0.8 && slow.played_ms >= slow.wall_ms * 0.95);
  /* Fast: pull OUT in until the loop is very short (each tick is half a frame, 1/150 s). */
  struct listen fast = turn(d, -3, 40, 5);
  report(d ? "DECK 4 OUT in, fast" : "DECK 3 OUT in, fast", fast);
  uint64_t shortest = debug(d).loop_out - debug(d).loop_in;
  printf("  loop now %.1f ms\n", (double)shortest * 1000.0 / rate);
  EXPECT(debug(d).loop_active && debug(d).loop_edit == 2);
  EXPECT(shortest < (uint64_t)rate / 10 && shortest >= rate / 150u); /* stock MIN_LOOP_WIDTH: half a frame */
  EXPECT(fast.restarts == 0);
  EXPECT(fast.flushes <= fast.wraps + 2);
  /* A 7 ms loop wraps several times between samples, so judge the wire: an OUT pulled past what is heard wraps
     at once, and like every jump that ends the wire's partial block (at most one 5 ms turn each). */
  EXPECT(fast.played_ms >= fast.wall_ms - 5.0 * fast.flushes && fast.flushes * 4 <= fast.wraps + 4);
  /* Push it out again. */
  struct listen back = turn(d, 2, 40, 10);
  report(d ? "DECK 4 OUT out again" : "DECK 3 OUT out again", back);
  EXPECT(debug(d).loop_out - debug(d).loop_in > shortest + (uint64_t)rate / 4);
  EXPECT(back.restarts == 0);
  EXPECT(back.flushes <= back.wraps + 1);
  EXPECT(back.heard_ms >= back.wall_ms * 0.8 && back.played_ms >= back.wall_ms * 0.95);
  /* The same button ends it; the loop plays on at its new length. */
  unsigned before = flushes(d);
  hold(d, XZ_EXTRA_IN_LOOP_OUT, 1); hold(d, XZ_EXTRA_IN_LOOP_OUT, 0);
  EXPECT(!debug(d).loop_edit && debug(d).loop_active && debug(d).state == XZ_XP_PLAYING);
  EXPECT(flushes(d) == before);
  /* IN ADJUST: IN moves within 30 frames of where it was, the loop keeps playing. */
  hold(d, XZ_EXTRA_IN_LOOP_IN, 1); hold(d, XZ_EXTRA_IN_LOOP_IN, 0);
  EXPECT(debug(d).loop_edit == 1);
  struct listen in_turn = turn(d, 1, 30, 10);
  report(d ? "DECK 4 IN later" : "DECK 3 IN later", in_turn);
  EXPECT(debug(d).loop_in == ms_frames(d, 500) + 30u * (rate / 150u));
  EXPECT(in_turn.restarts == 0);
  EXPECT(in_turn.flushes <= in_turn.wraps + 1);
  EXPECT(in_turn.heard_ms >= in_turn.wall_ms * 0.8 && in_turn.played_ms >= in_turn.wall_ms * 0.95);
  hold(d, XZ_EXTRA_IN_LOOP_IN, 1); hold(d, XZ_EXTRA_IN_LOOP_IN, 0);
  EXPECT(!debug(d).loop_edit && debug(d).loop_active);
  xz_xp_destroy(p); p = NULL;
 }
 if (failures_seen) { printf("FAIL loop adjust: %d checks failed\n", failures_seen); return 1; }
 puts("PASS loop adjust: the jog moves OUT and IN while the loop plays; no restart per tick, flushes only where an edge passes the queued audio");
 return 0;
}
