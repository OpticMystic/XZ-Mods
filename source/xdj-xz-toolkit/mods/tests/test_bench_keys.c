/* Host test for bench_keys.c's core: request decoding, guards, refusals, the key and event sequences, the
   recorder arm, and the UiMain hand-off with a stand-in thread. No player, no firmware. */
#undef NDEBUG
#include "../bench_keys.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct key { unsigned code, op, channel; };
struct soft { int deck, kind, pressed, value; };
static struct fake {
 uint32_t clock; unsigned avail;
 struct key keys[32]; unsigned nkeys; int fail_release, fail_press;
 struct xz_bk_loop_view stock[2]; int stock_loop_rc; int pad_mode[2]; int side[2];
 struct xz_bk_extra_view extra[2]; struct soft events[32]; unsigned nevents; int refuse_events;
 struct xz_bk_rec_view rec; int rec_rc;
 int jumps, jump_deck, jump_beats; uint32_t jump_job; int jump_rc;
 /* stand-in UiMain */
 int threaded; pthread_mutex_t lock; pthread_cond_t wake; uint32_t queued[8]; unsigned nqueued, served; int stop;
} f;
static struct xz_bk bk;

static uint32_t now_ms(void *c) { (void)c; return f.clock; }
static unsigned available(void *c) { (void)c; return f.avail; }
static int send_key(void *c, unsigned code, unsigned op, unsigned ch) {
 (void)c;
 if ((op == XZ_BK_OP_RELEASE && f.fail_release) || (op == XZ_BK_OP_PRESS && f.fail_press)) return -EAGAIN;
 assert(f.nkeys < 32); f.keys[f.nkeys++] = (struct key){code, op, ch}; return 0;
}
static int32_t key_values[32];
static int send_key_value(void *c, unsigned code, unsigned op, unsigned ch, int32_t value) {
 int rc = send_key(c, code, op, ch);
 if (!rc) key_values[f.nkeys - 1] = value;
 return rc;
}
static int post_jump(void *c, int deck, int beats, uint32_t job) {
 (void)c;
 if (f.jump_rc) return f.jump_rc;
 f.jumps++; f.jump_deck = deck; f.jump_beats = beats; f.jump_job = job;
 if (f.threaded) {
  pthread_mutex_lock(&f.lock);
  if (f.nqueued == 8) { pthread_mutex_unlock(&f.lock); return -EBUSY; }
  f.queued[f.nqueued++] = job; pthread_cond_signal(&f.wake); pthread_mutex_unlock(&f.lock);
 }
 return 0;
}
static int stock_loop(void *c, int deck, struct xz_bk_loop_view *o) { (void)c; *o = f.stock[deck]; return f.stock_loop_rc; }
static int stock_pad_mode(void *c, int deck) { (void)c; return f.pad_mode[deck]; }
static int side_deck(void *c, int side) { (void)c; return f.side[side]; }
static int extra_view(void *c, int deck, struct xz_bk_extra_view *o) { (void)c; *o = f.extra[deck - 2]; return 0; }
static int extra_input(void *c, int deck, enum xz_bk_soft_kind kind, int pressed, int value) {
 (void)c;
 if (f.refuse_events) return 0;
 assert(f.nevents < 32); f.events[f.nevents++] = (struct soft){deck, (int)kind, pressed, value}; return 1;
}
static int recorder(void *c, struct xz_bk_rec_view *o) { (void)c; *o = f.rec; return f.rec_rc; }
static uint32_t entropy(void *c) { (void)c; return 0x1234567u; }

static void reset(void) {
 memset(&f, 0, sizeof f);
 f.avail = XZ_BK_AV_KEYS | XZ_BK_AV_DECK_KEYS | XZ_BK_AV_JUMP | XZ_BK_AV_LOOP_READ | XZ_BK_AV_REC_KEYS;
 f.clock = 100000; f.side[0] = 1; f.side[1] = 2;
 for (int d = 0; d < 2; d++) {
  f.stock[d] = (struct xz_bk_loop_view){.known = 1, .pad = -1, .in_ms = -1, .out_ms = -1, .length_milli = -1};
  f.extra[d] = (struct xz_bk_extra_view){.ready = 1, .revision = 40u + (uint32_t)d,
   .loop = {.known = 1, .pad = -1, .in_ms = -1, .out_ms = -1, .length_milli = -1}};
 }
 f.rec = (struct xz_bk_rec_view){.known = 1, .recording = 0, .generation = 3};
 struct xz_bk_env env = {.now_ms = now_ms, .available = available, .send_key = send_key, .post_jump = post_jump,
  .stock_loop = stock_loop, .stock_pad_mode = stock_pad_mode, .side_deck = side_deck, .extra_view = extra_view,
  .extra_input = extra_input, .recorder = recorder, .entropy = entropy, .send_key_value = send_key_value};
 memset(key_values, 0, sizeof key_values);
 xz_bk_init(&bk, &env);
}
static const struct xz_bk_native_deck deck1 = {1, 777u, 0};
static struct xz_bench_result run(uint32_t op, uint32_t deck, int32_t a0, int32_t a1, int32_t a2, int32_t a3,
                                  const struct xz_bk_native_deck *n) {
 struct xz_bench_request q = {XZ_BENCH_VERSION, 1, 0, op, deck, {a0, a1, a2, a3}};
 struct xz_bench_result r; memset(&r, 0x5a, sizeof r);
 xz_bk_apply(&bk, &q, &r, n);
 return r;
}
static void expect_tap(unsigned at, unsigned code, unsigned channel) {
 assert(f.nkeys >= at + 2);
 assert(f.keys[at].code == code && f.keys[at].op == XZ_BK_OP_PRESS && f.keys[at].channel == channel);
 assert(f.keys[at + 1].code == code && f.keys[at + 1].op == XZ_BK_OP_RELEASE && f.keys[at + 1].channel == channel);
}

static void test_decode(void) {
 reset();
 assert(XZ_BK_OP_MASK == 0x3fc000u && (16382u | XZ_BK_OP_MASK) == 4194302u);
 assert(run(13, 0, 0, 0, 0, 0, &deck1).status == -EINVAL);
 assert(run(22, 4, 0, 0, 0, 0, NULL).status == -EINVAL);
 assert(run(XZ_BK_OP_BROWSE_KEY, 0, 0, 0, 0, 0, &deck1).status == -EINVAL);          /* deck 4 only */
 assert(run(XZ_BK_OP_LOOP, 5, XZ_BK_LOOP_IN, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_LOOP, 4, XZ_BK_LOOP_IN, 777, 0, 0, &deck1).status == -EINVAL);  /* deck ops need a deck */
 assert(run(XZ_BK_OP_REC_STATE, 0, 0, 0, 0, 0, &deck1).status == -EINVAL);          /* global ops need deck 4 */
 assert(run(XZ_BK_OP_STATUS, 2, 0, 0, 0, 0, NULL).status == -EINVAL);
 assert(run(XZ_BK_OP_LOOP, 0, 0, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_LOOP, 0, 8, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_JUMP, 0, 0, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_JUMP, 0, 257, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_JUMP, 0, -257, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_HOT_CUE, 0, 8, 777, 0, 0, &deck1).status == -EINVAL);
 assert(run(XZ_BK_OP_HOT_CUE, 2, -1, 40, 0, 0, NULL).status == -EINVAL);
 assert(xz_bk_beat_pad(250) == 0 && xz_bk_beat_pad(1000) == 2 && xz_bk_beat_pad(32000) == 7 && xz_bk_beat_pad(125) < 0);
 assert(f.nkeys == 0 && f.nevents == 0 && f.jumps == 0);
 puts("PASS decode: op and deck domains, argument ranges, nothing sent on a malformed request");
}

static void test_guards(void) {
 reset(); f.avail = 0;
 struct xz_bench_result r = run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_IN, 777, 0, 0, &deck1);
 assert(r.status == -EOPNOTSUPP && f.nkeys == 0);
 assert(run(XZ_BK_OP_JUMP, 0, 4, 777, 0, 0, &deck1).status == -EOPNOTSUPP && f.jumps == 0);
 assert(run(XZ_BK_OP_LOOP_STATE, 0, -1, 0, 0, 0, &deck1).status == -EOPNOTSUPP);
 assert(run(XZ_BK_OP_HOT_CUE, 1, 0, 777, 0, 0, &deck1).status == -EOPNOTSUPP);
 /* Keys without the deck code checks: still refused. */
 f.avail = XZ_BK_AV_KEYS | XZ_BK_AV_LOOP_READ;
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_IN, 777, 0, 0, &deck1).status == -EOPNOTSUPP && f.nkeys == 0);
 /* Software decks do not depend on the stock guards. */
 r = run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_IN, 40, 0, 0, NULL);
 assert(r.status == 0 && f.nevents == 2);
 f.avail = 0;
 r = run(XZ_BK_OP_STATUS, 4, 0, 0, 0, 0, NULL);
 assert(r.status == 0 && r.args[0] == (int32_t)XZ_BK_AV_REC_STATE);
 /* An adapter that could not install the stock callbacks answers "not available", never crashes. */
 struct xz_bk_env none = {.now_ms = now_ms}; xz_bk_init(&bk, &none);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_IN, 777, 0, 0, &deck1).status == -EOPNOTSUPP);
 assert(run(XZ_BK_OP_LOOP_STATE, 1, -1, 0, 0, 0, &deck1).status == -EOPNOTSUPP);
 assert(run(XZ_BK_OP_LOOP, 3, XZ_BK_LOOP_IN, 41, 0, 0, NULL).status == -EOPNOTSUPP);
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, 1, 0, XZ_BK_REC_MAGIC, NULL).status == -EOPNOTSUPP);
 assert(run(XZ_BK_OP_REC_STATE, 4, 1, 0, 0, 0, NULL).status == -ENODATA);
 puts("PASS guards: failed code checks or missing callbacks answer not available; DECK 3/4 unaffected");
}

static void test_stock_loops(void) {
 reset();
 struct xz_bk_native_deck stale = {1, 778u, 0}, unsettled = {0, 777u, 0}, held = {1, 777u, 1};
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &stale).status == -ESTALE);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &unsettled).status == -ESTALE);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, NULL).status == -ESTALE);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &held).status == -EBUSY);
 f.side[0] = 3; /* the left controls drive DECK 3: the key would go there */
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &deck1).status == -EBUSY);
 f.side[0] = -EAGAIN;
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &deck1).status == -EAGAIN);
 f.side[0] = 1; f.stock_loop_rc = -EAGAIN;
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &deck1).status == -EAGAIN);
 f.stock_loop_rc = 0;
 assert(f.nkeys == 0);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 1000, 0, &deck1).status == -EOPNOTSUPP);
 struct xz_bench_result r = run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &deck1);
 assert(r.status == 0 && r.args[0] == 1 && r.args[1] == (int32_t)XZ_BK_LS_KNOWN && f.nkeys == 2);
 expect_tap(0, XZ_BK_KEY_4BEAT, 1);
 r = run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_EXIT, 777, 0, 0, &deck1);
 assert(r.status == 0 && r.args[0] == 0 && f.nkeys == 2); /* not looping: nothing to press */
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_HALVE, 777, 0, 0, &deck1).status == -ENOENT);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_DOUBLE, 777, 0, 0, &deck1).status == -ENOENT);
 f.stock[0].active = 1;
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_BEAT, 777, 4000, 0, &deck1).status == -EBUSY);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_RELOOP, 777, 0, 0, &deck1).status == -EBUSY);
 assert(f.nkeys == 2);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_HALVE, 777, 0, 0, &deck1).status == 0); expect_tap(2, XZ_BK_KEY_CALL_PREV, 1);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_DOUBLE, 777, 0, 0, &deck1).status == 0); expect_tap(4, XZ_BK_KEY_CALL_NEXT, 1);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_EXIT, 777, 0, 0, &deck1).status == 0); expect_tap(6, XZ_BK_KEY_RELOOP_EXIT, 1);
 f.stock[0].active = 0;
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_RELOOP, 777, 0, 0, &deck1).status == 0); expect_tap(8, XZ_BK_KEY_RELOOP_EXIT, 1);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_IN, 777, 0, 0, &deck1).status == 0); expect_tap(10, XZ_BK_KEY_LOOP_IN, 1);
 assert(run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_OUT, 777, 0, 0, &deck1).status == 0); expect_tap(12, XZ_BK_KEY_LOOP_OUT, 1);
 struct xz_bk_native_deck deck2 = {1, 900u, 0};
 assert(run(XZ_BK_OP_LOOP, 1, XZ_BK_LOOP_IN, 900, 0, 0, &deck2).status == 0); expect_tap(14, XZ_BK_KEY_LOOP_IN, 2);
 /* A release that cannot be posted is retried, then reported: stock would see a held key. */
 unsigned before = f.nkeys; f.fail_release = 1;
 r = run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_IN, 777, 0, 0, &deck1);
 assert(r.status == -EIO && r.args[3] == 1 && f.nkeys == before + 1);
 f.fail_release = 0; f.fail_press = 1; before = f.nkeys;
 r = run(XZ_BK_OP_LOOP, 0, XZ_BK_LOOP_IN, 777, 0, 0, &deck1);
 assert(r.status == -EAGAIN && r.args[3] == 0 && f.nkeys == before);
 puts("PASS stock loops: identity, contact, side and state refusals; one real key per request, tapped on the deck's channel");
}

static void test_soft_loops(void) {
 reset();
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_IN, 41, 0, 0, NULL).status == -ESTALE);
 f.extra[0].contact = 1;
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_IN, 40, 0, 0, NULL).status == -EBUSY);
 f.extra[0].contact = 0; f.extra[0].emergency = 1;
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_IN, 40, 0, 0, NULL).status == -EBUSY);
 f.extra[0].emergency = 0; f.extra[0].ready = 0;
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_IN, 40, 0, 0, NULL).status == -EAGAIN);
 f.extra[0].ready = 1;
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_BEAT, 40, 125, 0, NULL).status == -EINVAL);
 assert(f.nevents == 0);
 struct xz_bench_result r = run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_BEAT, 40, 500, 0, NULL);
 assert(r.status == 0 && f.nevents == 1 && f.events[0].kind == XZ_BK_SOFT_BEAT_LOOP && f.events[0].pressed == 1 &&
        f.events[0].value == 1 && f.events[0].deck == 2);
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_EXIT, 40, 0, 0, NULL).status == 0 && f.nevents == 1);
 f.extra[0].loop.active = 1;
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_BEAT, 40, 4000, 0, NULL).status == -EBUSY);
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_RELOOP, 40, 0, 0, NULL).status == -EBUSY);
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_HALVE, 40, 0, 0, NULL).status == 0);
 assert(f.nevents == 3 && f.events[1].kind == XZ_BK_SOFT_LOOP_HALF && f.events[1].pressed == 1 && f.events[2].pressed == 0);
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_DOUBLE, 40, 0, 0, NULL).status == 0 && f.events[3].kind == XZ_BK_SOFT_LOOP_DOUBLE);
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_EXIT, 40, 0, 0, NULL).status == 0 && f.events[5].kind == XZ_BK_SOFT_RELOOP);
 f.extra[0].loop.active = 0;
 assert(run(XZ_BK_OP_LOOP, 2, XZ_BK_LOOP_HALVE, 40, 0, 0, NULL).status == -ENOENT);
 assert(run(XZ_BK_OP_LOOP, 3, XZ_BK_LOOP_OUT, 41, 0, 0, NULL).status == 0 && f.events[7].kind == XZ_BK_SOFT_LOOP_OUT &&
        f.events[7].deck == 3 && f.events[8].pressed == 0);
 f.refuse_events = 1;
 assert(run(XZ_BK_OP_LOOP, 3, XZ_BK_LOOP_IN, 41, 0, 0, NULL).status == -EAGAIN);
 puts("PASS DECK 3/4 loops: player events with their releases, BEAT LOOP pad sizes, refusals before anything is sent");
}

static void test_jumps_and_cues(void) {
 reset();
 struct xz_bench_result r = run(XZ_BK_OP_JUMP, 0, -16, 777, 0, 0, &deck1);
 assert(r.status == 0 && r.args[0] == 1 && f.jumps == 1 && f.jump_deck == 0 && f.jump_beats == -16 && f.jump_job == 1);
 assert(f.nkeys == 0); /* a jump is a UiMain job, not a key */
 f.jump_rc = -EBUSY;
 assert(run(XZ_BK_OP_JUMP, 0, 32, 777, 0, 0, &deck1).status == -EBUSY);
 f.jump_rc = 0;
 r = run(XZ_BK_OP_JUMP, 0, 32, 777, 0, 0, &deck1);
 assert(r.status == 0 && r.args[0] == 2); /* the refused post did not use a job number */
 f.side[0] = 3; /* a jump names its deck directly; the side's target does not matter */
 assert(run(XZ_BK_OP_JUMP, 0, 1, 777, 0, 0, &deck1).status == 0);
 xz_bk_job_done(&bk, 1, 1); xz_bk_job_done(&bk, 2, 0);
 r = run(XZ_BK_OP_STATUS, 4, 0, 0, 0, 0, NULL);
 assert(r.args[1] == 3 && r.args[2] == 2 && r.args[3] == 0);
 r = run(XZ_BK_OP_JUMP, 3, 4, 41, 0, 0, NULL);
 assert(r.status == 0 && f.nevents == 1 && f.events[0].kind == XZ_BK_SOFT_BEAT_JUMP && f.events[0].value == 4 && f.events[0].deck == 3);
 /* Hot cues only in HOT CUE pad mode. */
 f.side[0] = 1; f.pad_mode[0] = 2;
 assert(run(XZ_BK_OP_HOT_CUE, 0, 3, 777, 0, 0, &deck1).status == -EBUSY && f.nkeys == 0);
 f.pad_mode[0] = -EAGAIN;
 assert(run(XZ_BK_OP_HOT_CUE, 0, 3, 777, 0, 0, &deck1).status == -EAGAIN);
 f.pad_mode[0] = 0;
 assert(run(XZ_BK_OP_HOT_CUE, 0, 3, 777, 0, 0, &deck1).status == 0); expect_tap(0, XZ_BK_KEY_PAD1 + 3, 1);
 f.extra[0].pad_mode = 1;
 assert(run(XZ_BK_OP_HOT_CUE, 2, 0, 40, 0, 0, NULL).status == -EBUSY);
 f.extra[0].pad_mode = 0;
 assert(run(XZ_BK_OP_HOT_CUE, 2, 7, 40, 0, 0, NULL).status == 0 && f.events[1].kind == XZ_BK_SOFT_HOT_CUE &&
        f.events[1].value == 7 && f.events[2].pressed == 0);
 puts("PASS jumps and hot cues: stock jumps as numbered UiMain jobs, DECK 3/4 as player events, pad mode respected");
}

static void test_loop_state(void) {
 reset();
 f.stock[1] = (struct xz_bk_loop_view){.known = 1, .active = 1, .pad = -1, .in_ms = 1000, .out_ms = 3000, .length_milli = 4000};
 struct xz_bk_native_deck deck2 = {1, 900u, 0};
 struct xz_bench_result r = run(XZ_BK_OP_LOOP_STATE, 1, -1, 0, 0, 0, &deck2);
 assert(r.status == 0 && r.args[0] == (int32_t)(XZ_BK_LS_KNOWN | XZ_BK_LS_ACTIVE) && r.args[1] == 1000 && r.args[2] == 3000 && r.args[3] == 4000);
 assert(run(XZ_BK_OP_LOOP_STATE, 1, 901, 0, 0, 0, &deck2).status == -ESTALE);
 assert(run(XZ_BK_OP_LOOP_STATE, 1, 900, 0, 0, 0, &deck2).status == 0);
 f.stock[0].known = 0;
 r = run(XZ_BK_OP_LOOP_STATE, 0, -1, 0, 0, 0, &deck1);
 assert(r.status == -EAGAIN && r.args[0] == 0 && r.args[1] == -1);
 f.extra[1].loop = (struct xz_bk_loop_view){.known = 1, .active = 1, .stored = 1, .edit = 2, .pad = 4, .in_ms = 5, .out_ms = 9, .length_milli = 4000};
 r = run(XZ_BK_OP_LOOP_STATE, 3, 41, 0, 0, 0, NULL);
 assert(r.status == 0 && r.args[0] == (int32_t)(XZ_BK_LS_KNOWN | XZ_BK_LS_ACTIVE | XZ_BK_LS_STORED | 2u << XZ_BK_LS_EDIT_SHIFT | 5u << XZ_BK_LS_PAD_SHIFT));
 assert(run(XZ_BK_OP_LOOP_STATE, 3, 40, 0, 0, 0, NULL).status == -ESTALE);
 puts("PASS loop read-back: flags, IN/OUT ms, length, identity check optional");
}

static int32_t arm(void) {
 struct xz_bench_result r = run(XZ_BK_OP_REC_STATE, 4, 1, 0, 0, 0, NULL);
 assert(r.status == 0 && (r.args[0] & 5) == 5 && r.args[3] > 0);
 return r.args[3];
}
static void test_recorder(void) {
 reset();
 /* Without an arm, with a stale or wrong token, or without the magic: never pressed. */
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, 0, 0, XZ_BK_REC_MAGIC, NULL).status == -EACCES);
 int32_t t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t ^ 1, 0, XZ_BK_REC_MAGIC, NULL).status == -EACCES);
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 0, XZ_BK_REC_MAGIC, NULL).status == -EACCES); /* the miss used up the arm */
 t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 0, 0, NULL).status == -EINVAL);
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 0, XZ_BK_REC_MAGIC, NULL).status == -EACCES);
 t = arm(); f.clock += XZ_BK_ARM_MS + 1;
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 0, XZ_BK_REC_MAGIC, NULL).status == -EACCES);
 assert(f.nkeys == 0);
 /* Unknown recorder: no arm, no press. */
 f.rec.known = 0;
 struct xz_bench_result r = run(XZ_BK_OP_REC_STATE, 4, 1, 0, 0, 0, NULL);
 assert(r.status == -ENODATA && r.args[3] == 0);
 f.rec.known = 1; t = arm(); f.rec.known = 0;
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 0, XZ_BK_REC_MAGIC, NULL).status == -ENODATA);
 f.rec.known = 1; f.rec_rc = -1;
 assert(run(XZ_BK_OP_REC_STATE, 4, 1, 0, 0, 0, NULL).status == -ENODATA);
 f.rec_rc = 0;
 /* Key path checks failed: refused. */
 t = arm(); f.avail &= ~XZ_BK_AV_REC_KEYS;
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 0, XZ_BK_REC_MAGIC, NULL).status == -EOPNOTSUPP);
 f.avail |= XZ_BK_AV_REC_KEYS;
 /* The caller's idea of the state must match. */
 t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, 1, t, 1, XZ_BK_REC_MAGIC, NULL).status == -ESTALE);
 t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, 2, t, 0, XZ_BK_REC_MAGIC, NULL).status == -ENOENT); /* no recording to mark */
 assert(f.nkeys == 0);
 /* Start. */
 t = arm();
 r = run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_MASTER, t, 0, XZ_BK_REC_MAGIC, NULL);
 assert(r.status == 0 && r.args[0] == 3 && r.args[1] == 0); expect_tap(0, XZ_BK_KEY_MASTER_REC, 0);
 /* The writer starts: generation 4. A stop now is refused until 2.5 s after this bench saw it. */
 f.rec.recording = 1; f.rec.generation = 4; f.clock += 300;
 t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_MASTER, t, 1, XZ_BK_REC_MAGIC, NULL).status == -EBUSY);
 f.clock += XZ_BK_REC_MIN_MS - 1; t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_TRACK_MARK, t, 1, XZ_BK_REC_MAGIC, NULL).status == -EBUSY);
 f.clock += 1; t = arm();
 r = run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_TRACK_MARK, t, 1, XZ_BK_REC_MAGIC, NULL);
 assert(r.status == 0 && r.args[0] == 4); expect_tap(2, XZ_BK_KEY_TRACK_MARK, 0);
 /* Right after a TRACK MARK the stock timer runs again: no stop and no second mark. */
 f.rec.generation = 5; f.clock += 1000; t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_MASTER, t, 1, XZ_BK_REC_MAGIC, NULL).status == -EBUSY);
 f.clock += XZ_BK_REC_MIN_MS; t = arm();
 r = run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_MASTER, t, 1, XZ_BK_REC_MAGIC, NULL);
 assert(r.status == 0 && r.args[1] == 1); expect_tap(4, XZ_BK_KEY_MASTER_REC, 0);
 f.rec.recording = 0; t = arm();
 assert(run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_TRACK_MARK, t, 0, XZ_BK_REC_MAGIC, NULL).status == -ENOENT);
 /* A recording that was already running when the bench first looked counts from that first look. */
 reset(); f.rec.recording = 1; f.rec.generation = 9; t = arm(); f.clock += 100;
 int32_t t2 = arm();
 assert(t2 != t);
 assert(run(XZ_BK_OP_REC_PRESS, 4, XZ_BK_REC_MASTER, t2, 1, XZ_BK_REC_MAGIC, NULL).status == -EBUSY);
 assert(f.nkeys == 0);
 puts("PASS recorder: single-use 5 s arm token, magic, expected state, known state, 2.5 s after start/mark; channel-0 taps");
}

/* Stand-in UiMain: drains posted jobs on its own thread. The bench side never waits for it. */
static void *ui_main(void *unused) {
 (void)unused;
 pthread_mutex_lock(&f.lock);
 for (;;) {
  while (!f.nqueued && !f.stop) pthread_cond_wait(&f.wake, &f.lock);
  if (!f.nqueued && f.stop) break;
  uint32_t job = f.queued[0];
  memmove(f.queued, f.queued + 1, (f.nqueued - 1) * sizeof f.queued[0]); f.nqueued--;
  pthread_mutex_unlock(&f.lock);
  xz_bk_job_done(&bk, job, 1);
  pthread_mutex_lock(&f.lock); f.served++; pthread_cond_broadcast(&f.wake);
 }
 pthread_mutex_unlock(&f.lock);
 return NULL;
}
static void test_handoff(void) {
 reset(); f.threaded = 1;
 pthread_mutex_init(&f.lock, NULL); pthread_cond_init(&f.wake, NULL);
 /* UiMain is busy (lock held): the bench still posts and returns at once. */
 pthread_mutex_lock(&f.lock);
 pthread_t t; assert(!pthread_create(&t, NULL, ui_main, NULL));
 pthread_mutex_unlock(&f.lock);
 for (int i = 0; i < 5; i++) assert(run(XZ_BK_OP_JUMP, 0, i % 2 ? 4 : -4, 777, 0, 0, &deck1).status == 0);
 pthread_mutex_lock(&f.lock);
 while (f.served < 5) pthread_cond_wait(&f.wake, &f.lock);
 f.stop = 1; pthread_cond_broadcast(&f.wake); pthread_mutex_unlock(&f.lock);
 pthread_join(t, NULL);
 struct xz_bench_result r = run(XZ_BK_OP_STATUS, 4, 0, 0, 0, 0, NULL);
 assert(r.args[1] == 5 && r.args[2] == 5 && r.args[3] == 1);
 /* A full queue is refused without blocking and without using a job number. */
 f.threaded = 1; f.nqueued = 8; f.stop = 0;
 assert(run(XZ_BK_OP_JUMP, 0, 4, 777, 0, 0, &deck1).status == -EBUSY);
 r = run(XZ_BK_OP_STATUS, 4, 0, 0, 0, 0, NULL);
 assert(r.args[1] == 5);
 pthread_cond_destroy(&f.wake); pthread_mutex_destroy(&f.lock);
 puts("PASS hand-off: jobs posted without waiting, finished on the stand-in UiMain thread, counted in op 20");
}

/* Op 21: BROWSE, BACK and the rotary selector, as a tap or a press held for given ms; the turn by steps. */
static void test_browse_keys(void) {
 reset(); f.avail |= XZ_BK_AV_BROWSE_KEYS;
 struct xz_bench_result r;
 r = run(XZ_BK_OP_BROWSE_KEY, 0, 1, 0, 0, 0, NULL); assert(r.status == -EINVAL && !f.nkeys);   /* deck 4 only */
 r = run(XZ_BK_OP_BROWSE_KEY, 4, 5, 0, 0, 0, NULL); assert(r.status == -EINVAL && !f.nkeys);   /* not on the list */
 r = run(XZ_BK_OP_BROWSE_KEY, 4, 1, -1, 0, 0, NULL); assert(r.status == -EINVAL && !f.nkeys);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, 1, XZ_BK_HOLD_MAX_MS + 1, 0, 0, NULL); assert(r.status == -EINVAL && !f.nkeys);
 /* Tap: press and release back to back. */
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BROWSE, 0, 0, 0, NULL);
 assert(r.status == 0 && r.args[0] == 1 && !r.args[2]); expect_tap(0, XZ_BK_KEY_BROWSE, 0);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BACK, 0, 0, 0, NULL); assert(r.status == 0); expect_tap(2, XZ_BK_KEY_BACK, 0);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_PUSH, 0, 0, 0, NULL); assert(r.status == 0); expect_tap(4, XZ_BK_KEY_SELECTOR, 0);
 /* Held: the press now, the release from the tick once due; one held key at a time. */
 unsigned before = f.nkeys;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BROWSE, 600, 0, 0, NULL);
 assert(r.status == 0 && r.args[0] == 1 && r.args[1] == 600 && r.args[2] == 1 && r.args[3] == (int32_t)XZ_BK_KEY_BROWSE);
 assert(f.nkeys == before + 1 && f.keys[before].code == XZ_BK_KEY_BROWSE && f.keys[before].op == XZ_BK_OP_PRESS);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BACK, 0, 0, 0, NULL); assert(r.status == -EBUSY && f.nkeys == before + 1);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, 1, 0, 0, NULL); assert(r.status == -EBUSY && f.nkeys == before + 1);
 f.clock += 599; xz_bk_tick(&bk); assert(f.nkeys == before + 1);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_READ, 0, 0, 0, NULL); assert(r.status == 0 && r.args[1] == 1 && r.args[2] == 1);
 f.clock += 1; xz_bk_tick(&bk);
 assert(f.nkeys == before + 2 && f.keys[before + 1].code == XZ_BK_KEY_BROWSE && f.keys[before + 1].op == XZ_BK_OP_RELEASE);
 xz_bk_tick(&bk); assert(f.nkeys == before + 2);                                       /* released once */
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_READ, 0, 0, 0, NULL); assert(r.status == 0 && !r.args[2] && !r.args[3]);
 /* A release that cannot be posted is tried again on the next tick. */
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_PUSH, 500, 0, 0, NULL); assert(r.status == 0);
 f.fail_release = 1; f.clock += 500; xz_bk_tick(&bk); assert(f.keys[f.nkeys - 1].op == XZ_BK_OP_PRESS);
 f.fail_release = 0; f.clock += 10; xz_bk_tick(&bk);
 assert(f.keys[f.nkeys - 1].code == XZ_BK_KEY_SELECTOR && f.keys[f.nkeys - 1].op == XZ_BK_OP_RELEASE);
 /* Stopping the bench releases a held key at once. */
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BACK, 3000, 0, 0, NULL); assert(r.status == 0);
 before = f.nkeys; xz_bk_flush(&bk);
 assert(f.nkeys == before + 1 && f.keys[before].code == XZ_BK_KEY_BACK && f.keys[before].op == XZ_BK_OP_RELEASE);
 f.clock += 5000; xz_bk_tick(&bk); assert(f.nkeys == before + 1);
 /* The turn: 0x4207, operation 4, channel 1, the steps as the key's value. */
 before = f.nkeys;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, -3, 0, 0, NULL);
 assert(r.status == 0 && r.args[0] == 1 && f.nkeys == before + 1 && f.keys[before].code == XZ_BK_KEY_SELECTOR &&
        f.keys[before].op == XZ_BK_OP_ROTATE && f.keys[before].channel == 1 && key_values[before] == -3);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, 0, 0, 0, NULL); assert(r.status == -EINVAL);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, XZ_BK_TURN_MAX + 1, 0, 0, NULL); assert(r.status == -EINVAL);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, -XZ_BK_TURN_MAX, 0, 0, NULL); assert(r.status == 0);
 /* The guards: without them nothing is sent; reading still answers. */
 before = f.nkeys; f.avail &= ~XZ_BK_AV_BROWSE_KEYS;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BROWSE, 0, 0, 0, NULL); assert(r.status == -EOPNOTSUPP && f.nkeys == before);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_READ, 0, 0, 0, NULL); assert(r.status == 0);
 f.avail |= XZ_BK_AV_BROWSE_KEYS; f.avail &= ~XZ_BK_AV_KEYS;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, 1, 0, 0, NULL); assert(r.status == -EOPNOTSUPP && f.nkeys == before);
 /* No value-carrying sendKey: the turn is refused, the keys still work. */
 f.avail |= XZ_BK_AV_KEYS; bk.env.send_key_value = NULL;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_TURN, 1, 0, 0, NULL); assert(r.status == -EOPNOTSUPP);
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BROWSE, 0, 0, 0, NULL); assert(r.status == 0);
 puts("PASS browse keys: allow-list, tap, held release on the tick, one at a time, release on stop, turn by steps, guards");
}

/* Op 26: USB STOP of slot 1 or 2, pressed and held; stock's own timer stops the stick at 1500 ms. */
static int32_t usb_counts[4] = {3, 5, 1, 2};
static int usb_release_counts(void *c, int32_t out[4]) { (void)c; for (int i = 0; i < 4; i++) out[i] = usb_counts[i]; return 0; }
static void test_usb_stop(void) {
 reset(); f.avail |= XZ_BK_AV_USB_STOP;
 const int32_t M = (int32_t)XZ_BK_USB_STOP_MAGIC;
 struct xz_bench_result r;
 assert(XZ_BK_OP_USB_STOP == 26u && XZ_BK_OP_USB_STOP_MASK == (1u << 26) && !(XZ_BK_OP_MASK & XZ_BK_OP_USB_STOP_MASK));
 /* Refusals send nothing. */
 r = run(XZ_BK_OP_USB_STOP, 0, 1, 2000, 0, M, &deck1); assert(r.status == -EINVAL);   /* deck 4 only */
 r = run(XZ_BK_OP_USB_STOP, 4, 3, 2000, 0, M, NULL); assert(r.status == -EINVAL);     /* slot 1 or 2 */
 r = run(XZ_BK_OP_USB_STOP, 4, -1, 2000, 0, M, NULL); assert(r.status == -EINVAL);
 r = run(XZ_BK_OP_USB_STOP, 4, 1, 0, 0, M, NULL); assert(r.status == -EINVAL);        /* a hold, never a tap */
 r = run(XZ_BK_OP_USB_STOP, 4, 1, XZ_BK_HOLD_MAX_MS + 1, 0, M, NULL); assert(r.status == -EINVAL);
 r = run(XZ_BK_OP_USB_STOP, 4, 1, 2000, 0, 0, NULL); assert(r.status == -EINVAL);     /* the magic word */
 r = run(XZ_BK_OP_USB_STOP, 4, 1, 2000, 0, M - 1, NULL); assert(r.status == -EINVAL);
 assert(run(27, 4, 1, 2000, 0, M, NULL).status == -EINVAL);
 assert(!f.nkeys);
 /* The guards. */
 f.avail &= ~XZ_BK_AV_USB_STOP;
 r = run(XZ_BK_OP_USB_STOP, 4, 2, 2000, 0, M, NULL); assert(r.status == -EOPNOTSUPP && !f.nkeys);
 f.avail |= XZ_BK_AV_USB_STOP; f.avail &= ~XZ_BK_AV_KEYS;
 r = run(XZ_BK_OP_USB_STOP, 4, 2, 2000, 0, M, NULL); assert(r.status == -EOPNOTSUPP && !f.nkeys);
 f.avail |= XZ_BK_AV_KEYS;
 /* Slot 2 held 2 s: key 0x8002, channel 2, the press now and the release from the tick when due. */
 r = run(XZ_BK_OP_USB_STOP, 4, 2, 2000, 0, M, NULL);
 assert(r.status == 0 && r.args[0] == 1 && r.args[1] == 2000 && r.args[2] == 1 && r.args[3] == (int32_t)XZ_BK_KEY_USB_STOP);
 assert(f.nkeys == 1 && f.keys[0].code == 0x8002u && f.keys[0].op == XZ_BK_OP_PRESS && f.keys[0].channel == 2);
 /* One held key at a time, shared with op 21. */
 r = run(XZ_BK_OP_USB_STOP, 4, 1, 2000, 0, M, NULL); assert(r.status == -EBUSY && f.nkeys == 1);
 f.avail |= XZ_BK_AV_BROWSE_KEYS;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_BACK, 0, 0, 0, NULL); assert(r.status == -EBUSY && f.nkeys == 1);
 f.clock += XZ_BK_USB_STOP_HOLD_MS; xz_bk_tick(&bk); assert(f.nkeys == 1);   /* still down past stock's 1500 ms */
 f.clock += 499; xz_bk_tick(&bk); assert(f.nkeys == 1);
 f.clock += 1; xz_bk_tick(&bk);
 assert(f.nkeys == 2 && f.keys[1].code == 0x8002u && f.keys[1].op == XZ_BK_OP_RELEASE && f.keys[1].channel == 2);
 xz_bk_tick(&bk); assert(f.nkeys == 2);
 /* Slot 1, and stopping the bench releases it at once on its own channel. */
 r = run(XZ_BK_OP_USB_STOP, 4, 1, 3000, 0, M, NULL); assert(r.status == 0 && f.keys[2].channel == 1);
 xz_bk_flush(&bk);
 assert(f.nkeys == 4 && f.keys[3].code == 0x8002u && f.keys[3].op == XZ_BK_OP_RELEASE && f.keys[3].channel == 1);
 /* A refused press holds nothing. */
 f.fail_press = 1;
 r = run(XZ_BK_OP_USB_STOP, 4, 1, 2000, 0, M, NULL); assert(r.status == -EAGAIN && f.nkeys == 4);
 f.fail_press = 0;
 r = run(XZ_BK_OP_BROWSE_KEY, 4, XZ_BK_BROWSE_READ, 0, 0, 0, NULL); assert(r.status == 0 && !r.args[2]);
 /* The read: usb_release.h counters, refused without them. */
 r = run(XZ_BK_OP_USB_STOP, 4, 0, 0, 0, 0, NULL); assert(r.status == -EOPNOTSUPP);
 bk.env.usb_release = usb_release_counts;
 r = run(XZ_BK_OP_USB_STOP, 4, 0, 0, 0, 0, NULL);
 assert(r.status == 0 && r.args[0] == 3 && r.args[1] == 5 && r.args[2] == 1 && r.args[3] == 2 && f.nkeys == 4);
 puts("PASS usb stop: slot, hold and magic checks, guards, key 0x8002 on the slot's channel, timed release, one held key, release on stop, counters");
}

int main(void) {
 test_decode();
 test_guards();
 test_stock_loops();
 test_soft_loops();
 test_jumps_and_cues();
 test_loop_state();
 test_recorder();
 test_handoff();
 test_browse_keys();
 test_usb_stop();
 puts("PASS bench keys");
 return 0;
}
