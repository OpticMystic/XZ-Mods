#define _GNU_SOURCE
/* Private bench operations 14..21 (bench_keys.h). The decisions are a pure core over struct xz_bk_env; the
   firmware 1.26 adapter below the core is the only part that touches the player. */
#include "bench_keys.h"
#include <errno.h>
#include <string.h>

_Static_assert(XZ_BK_OP_MASK == 0x3fc000u, "runtime.c passes 16382u | XZ_BK_OP_MASK as 4194302u");
_Static_assert((16382u | XZ_BK_OP_MASK) == 4194302u, "runtime.c bench op mask");

int xz_bk_beat_pad(int32_t milli) {
 static const int32_t sizes[8] = {250, 500, 1000, 2000, 4000, 8000, 16000, 32000}; /* extra_player.c pads */
 for (int i = 0; i < 8; i++) if (sizes[i] == milli) return i;
 return -1;
}

void xz_bk_init(struct xz_bk *b, const struct xz_bk_env *env) {
 memset(b, 0, sizeof *b);
 if (env) b->env = *env;
}

void xz_bk_job_done(struct xz_bk *b, uint32_t job, int32_t result) {
 (void)job;
 __atomic_store_n(&b->last_job, result, __ATOMIC_RELEASE);
 __atomic_add_fetch(&b->jobs_done, 1u, __ATOMIC_RELEASE);
}

static uint32_t now(struct xz_bk *b) { return b->env.now_ms ? b->env.now_ms(b->env.ctx) : 0; }
static unsigned avail(struct xz_bk *b) { return b->env.available ? b->env.available(b->env.ctx) : 0; }

static int32_t loop_flags(const struct xz_bk_loop_view *v) {
 if (!v->known) return 0;
 unsigned pad = v->pad >= 0 && v->pad < 255 ? (unsigned)v->pad + 1u : 0u;
 return (int32_t)(XZ_BK_LS_KNOWN | (v->active ? XZ_BK_LS_ACTIVE : 0u) | (v->stored ? XZ_BK_LS_STORED : 0u) |
                  ((unsigned)v->edit & 3u) << XZ_BK_LS_EDIT_SHIFT | pad << XZ_BK_LS_PAD_SHIFT);
}

/* A key is a press and its release, back to back, as a quick tap. A press whose release could not be posted is
   reported: the stock key manager would see a held key and fire its long-press action. */
static int tap(struct xz_bk *b, unsigned code, unsigned channel, struct xz_bench_result *r) {
 int rc = b->env.send_key(b->env.ctx, code, XZ_BK_OP_PRESS, channel);
 if (rc < 0) return rc;
 rc = b->env.send_key(b->env.ctx, code, XZ_BK_OP_RELEASE, channel);
 if (rc < 0) rc = b->env.send_key(b->env.ctx, code, XZ_BK_OP_RELEASE, channel);
 if (rc < 0) { r->args[3] = 1; return -EIO; }
 r->args[0]++;
 return 0;
}

static int soft_tap(struct xz_bk *b, int deck, enum xz_bk_soft_kind kind, int value, struct xz_bench_result *r) {
 if (!b->env.extra_input(b->env.ctx, deck, kind, 1, value)) return -EAGAIN;
 /* Every down gets its up (extra_decks.h); a refused up would leave the player owing it, so try twice. */
 if (!b->env.extra_input(b->env.ctx, deck, kind, 0, value) && !b->env.extra_input(b->env.ctx, deck, kind, 0, value)) {
  r->args[3] = 1; return -EIO;
 }
 r->args[0]++;
 return 0;
}

/* Decks 1/2: the shell's identity of the loaded track, no platter contact, and the side's controls on this deck
   (a side driving DECK 3/4 would carry the key there). */
static int stock_gate(struct xz_bk *b, int deck, int32_t identity, const struct xz_bk_native_deck *n, int keys) {
 unsigned need = keys ? XZ_BK_AV_KEYS | XZ_BK_AV_DECK_KEYS : XZ_BK_AV_JUMP;
 if ((avail(b) & need) != need) return -EOPNOTSUPP;
 if (!n || !n->settled || identity <= 0 || n->id != (uint32_t)identity) return -ESTALE;
 if (n->contact) return -EBUSY;
 if (keys) {
  int side = b->env.side_deck(b->env.ctx, deck);
  if (side < 0) return -EAGAIN;
  if (side != deck + 1) return -EBUSY;
 }
 return 0;
}

static int soft_gate(struct xz_bk *b, int deck, int32_t identity, struct xz_bk_extra_view *v) {
 if (!b->env.extra_view || !b->env.extra_input) return -EOPNOTSUPP;
 memset(v, 0, sizeof *v);
 if (b->env.extra_view(b->env.ctx, deck, v) < 0 || !v->ready) return -EAGAIN;
 if (v->revision != (uint32_t)identity) return -ESTALE;
 if (v->contact || v->emergency) return -EBUSY;
 return 0;
}

static int loop_stock(struct xz_bk *b, int deck, const struct xz_bench_request *q, struct xz_bench_result *r,
                      const struct xz_bk_native_deck *n) {
 int rc = stock_gate(b, deck, q->args[1], n, 1);
 if (rc) return rc;
 if (!(avail(b) & XZ_BK_AV_LOOP_READ)) return -EOPNOTSUPP;
 struct xz_bk_loop_view v; memset(&v, 0, sizeof v);
 if (b->env.stock_loop(b->env.ctx, deck, &v) < 0 || !v.known) return -EAGAIN;
 r->args[1] = loop_flags(&v);
 unsigned ch = (unsigned)deck + 1u, code;
 switch (q->args[0]) {
 /* 4 BEAT/EXIT sets a four-beat loop when none runs; other lengths are 4 BEAT then CALL halve/double, which the
    PC side composes with a read-back between steps. */
 case XZ_BK_LOOP_BEAT: if (q->args[2] != 4000) return -EOPNOTSUPP; if (v.active) return -EBUSY; code = XZ_BK_KEY_4BEAT; break;
 case XZ_BK_LOOP_IN: code = XZ_BK_KEY_LOOP_IN; break;
 case XZ_BK_LOOP_OUT: code = XZ_BK_KEY_LOOP_OUT; break;
 case XZ_BK_LOOP_EXIT: if (!v.active) return 0; code = XZ_BK_KEY_RELOOP_EXIT; break;
 case XZ_BK_LOOP_RELOOP: if (v.active) return -EBUSY; code = XZ_BK_KEY_RELOOP_EXIT; break;
 /* Outside a loop CALL moves between cue points instead, so these need a running loop. */
 case XZ_BK_LOOP_HALVE: if (!v.active) return -ENOENT; code = XZ_BK_KEY_CALL_PREV; break;
 case XZ_BK_LOOP_DOUBLE: if (!v.active) return -ENOENT; code = XZ_BK_KEY_CALL_NEXT; break;
 default: return -EINVAL;
 }
 return tap(b, code, ch, r);
}

static int loop_soft(struct xz_bk *b, int deck, const struct xz_bench_request *q, struct xz_bench_result *r) {
 struct xz_bk_extra_view v;
 int rc = soft_gate(b, deck, q->args[1], &v);
 if (rc) return rc;
 r->args[1] = loop_flags(&v.loop);
 switch (q->args[0]) {
 case XZ_BK_LOOP_BEAT: {
  int pad = xz_bk_beat_pad(q->args[2]);
  if (pad < 0) return -EINVAL;
  if (v.loop.active) return -EBUSY;
  /* A BEAT LOOP pad press carries no release obligation; the player ignores the up. */
  if (!b->env.extra_input(b->env.ctx, deck, XZ_BK_SOFT_BEAT_LOOP, 1, pad)) return -EAGAIN;
  r->args[0] = 1; return 0;
 }
 case XZ_BK_LOOP_IN: return soft_tap(b, deck, XZ_BK_SOFT_LOOP_IN, 0, r);
 case XZ_BK_LOOP_OUT: return soft_tap(b, deck, XZ_BK_SOFT_LOOP_OUT, 0, r);
 case XZ_BK_LOOP_EXIT: return v.loop.active ? soft_tap(b, deck, XZ_BK_SOFT_RELOOP, 0, r) : 0;
 case XZ_BK_LOOP_RELOOP: return v.loop.active ? -EBUSY : soft_tap(b, deck, XZ_BK_SOFT_RELOOP, 0, r);
 case XZ_BK_LOOP_HALVE: return v.loop.active ? soft_tap(b, deck, XZ_BK_SOFT_LOOP_HALF, 0, r) : -ENOENT;
 case XZ_BK_LOOP_DOUBLE: return v.loop.active ? soft_tap(b, deck, XZ_BK_SOFT_LOOP_DOUBLE, 0, r) : -ENOENT;
 default: return -EINVAL;
 }
}

static int loop_state(struct xz_bk *b, int deck, const struct xz_bench_request *q, struct xz_bench_result *r,
                      const struct xz_bk_native_deck *n) {
 struct xz_bk_loop_view v; memset(&v, 0, sizeof v);
 if (deck < 2) {
  if (!(avail(b) & XZ_BK_AV_LOOP_READ)) return -EOPNOTSUPP;
  if (q->args[0] != -1 && (!n || !n->settled || q->args[0] <= 0 || n->id != (uint32_t)q->args[0])) return -ESTALE;
  if (b->env.stock_loop(b->env.ctx, deck, &v) < 0) return -EAGAIN;
 } else {
  struct xz_bk_extra_view e; memset(&e, 0, sizeof e);
  if (!b->env.extra_view || b->env.extra_view(b->env.ctx, deck, &e) < 0) return -EAGAIN;
  if (q->args[0] != -1 && (!e.ready || e.revision != (uint32_t)q->args[0])) return -ESTALE;
  v = e.loop;
 }
 r->args[0] = loop_flags(&v);
 r->args[1] = v.known ? v.in_ms : -1;
 r->args[2] = v.known ? v.out_ms : -1;
 r->args[3] = v.known ? v.length_milli : -1;
 return v.known ? 0 : -EAGAIN;
}

static int beat_jump(struct xz_bk *b, int deck, const struct xz_bench_request *q, struct xz_bench_result *r,
                     const struct xz_bk_native_deck *n) {
 int beats = q->args[0];
 if (!beats || beats < -256 || beats > 256) return -EINVAL;
 if (deck < 2) {
  int rc = stock_gate(b, deck, q->args[1], n, 0);
  if (rc) return rc;
  uint32_t job = __atomic_add_fetch(&b->jobs_posted, 1u, __ATOMIC_ACQ_REL);
  rc = b->env.post_jump(b->env.ctx, deck, beats, job);
  if (rc < 0) { __atomic_sub_fetch(&b->jobs_posted, 1u, __ATOMIC_ACQ_REL); return rc; }
  r->args[0] = (int32_t)job; return 0;
 }
 struct xz_bk_extra_view v;
 int rc = soft_gate(b, deck, q->args[1], &v);
 if (rc) return rc;
 if (!b->env.extra_input(b->env.ctx, deck, XZ_BK_SOFT_BEAT_JUMP, 1, beats)) return -EAGAIN;
 r->args[0] = 1; return 0;
}

static int hot_cue(struct xz_bk *b, int deck, const struct xz_bench_request *q, struct xz_bench_result *r,
                   const struct xz_bk_native_deck *n) {
 int pad = q->args[0];
 if (pad < 0 || pad > 7) return -EINVAL;
 if (deck < 2) {
  int rc = stock_gate(b, deck, q->args[1], n, 1);
  if (rc) return rc;
  int mode = b->env.stock_pad_mode(b->env.ctx, deck);
  if (mode < 0) return -EAGAIN;
  if (mode != 0) return -EBUSY; /* the pads do something else in this pad mode */
  return tap(b, XZ_BK_KEY_PAD1 + (unsigned)pad, (unsigned)deck + 1u, r);
 }
 struct xz_bk_extra_view v;
 int rc = soft_gate(b, deck, q->args[1], &v);
 if (rc) return rc;
 if (v.pad_mode != 0) return -EBUSY;
 return soft_tap(b, deck, XZ_BK_SOFT_HOT_CUE, pad, r);
}

/* Remembers when this bench first saw the current recording and its latest file (start, TRACK MARK, split). */
static void rec_observe(struct xz_bk *b, const struct xz_bk_rec_view *v, uint32_t t) {
 if (!v->known) return;
 if (!v->recording) { b->rec.seen = 0; return; }
 if (!b->rec.seen) { b->rec.seen = 1; b->rec.since = b->rec.mark_at = t; b->rec.generation = v->generation; }
 else if (v->generation != b->rec.generation) { b->rec.mark_at = t; b->rec.generation = v->generation; }
}

static int rec_state(struct xz_bk *b, const struct xz_bench_request *q, struct xz_bench_result *r) {
 if (q->args[0] != 0 && q->args[0] != 1) return -EINVAL;
 struct xz_bk_rec_view v; memset(&v, 0, sizeof v);
 int known = b->env.recorder && b->env.recorder(b->env.ctx, &v) == 0 && v.known;
 uint32_t t = now(b);
 if (b->arm.valid && t - b->arm.at > XZ_BK_ARM_MS) b->arm.valid = 0;
 if (!known) { b->arm.valid = 0; return -ENODATA; }
 rec_observe(b, &v, t);
 if (q->args[0] == 1) {
  uint32_t e = b->env.entropy ? b->env.entropy(b->env.ctx) : 0;
  uint32_t token = (e ^ (++b->token_counter * 0x9e3779b9u) ^ (t * 0x85ebca6bu)) & 0x7fffffffu;
  if (!token) token = 1;
  b->arm.token = token; b->arm.at = t; b->arm.valid = 1;
  r->args[3] = (int32_t)token;
 }
 r->args[0] = 1 | (v.recording ? 2 : 0) | (b->arm.valid ? 4 : 0);
 r->args[1] = (int32_t)v.elapsed_ms;
 r->args[2] = (int32_t)v.generation;
 return 0;
}

static int rec_press(struct xz_bk *b, const struct xz_bench_request *q, struct xz_bench_result *r) {
 /* Any press request uses up the arm, matched or not: a stray or replayed request can never fire, and a wrong
    token cannot be retried against the same arm. */
 int armed = b->arm.valid && q->args[1] == (int32_t)b->arm.token && now(b) - b->arm.at <= XZ_BK_ARM_MS;
 b->arm.valid = 0;
 if ((q->args[0] != XZ_BK_REC_MASTER && q->args[0] != XZ_BK_REC_TRACK_MARK) || q->args[3] != XZ_BK_REC_MAGIC ||
     (q->args[2] != 0 && q->args[2] != 1)) return -EINVAL;
 if (!armed) return -EACCES;
 if ((avail(b) & (XZ_BK_AV_KEYS | XZ_BK_AV_REC_KEYS)) != (XZ_BK_AV_KEYS | XZ_BK_AV_REC_KEYS)) return -EOPNOTSUPP;
 struct xz_bk_rec_view v; memset(&v, 0, sizeof v);
 if (!b->env.recorder || b->env.recorder(b->env.ctx, &v) != 0 || !v.known) return -ENODATA;
 uint32_t t = now(b);
 rec_observe(b, &v, t);
 r->args[0] = (int32_t)v.generation; r->args[1] = v.recording;
 if (q->args[2] != v.recording) return -ESTALE;
 if (q->args[0] == XZ_BK_REC_TRACK_MARK && !v.recording) return -ENOENT;
 /* Stop and TRACK MARK wait out the stock 2 s timer with margin, counted from what this bench saw. */
 if (v.recording && (!b->rec.seen || t - b->rec.mark_at < XZ_BK_REC_MIN_MS)) return -EBUSY;
 int rc = tap(b, q->args[0] == XZ_BK_REC_MASTER ? XZ_BK_KEY_MASTER_REC : XZ_BK_KEY_TRACK_MARK, 0, r);
 r->args[0] = (int32_t)v.generation;
 if (!rc && q->args[0] == XZ_BK_REC_TRACK_MARK) b->rec.mark_at = t;
 return rc;
}

/* Op 21: one browse-section key. A held key is released by the bench's tick when due, and at once when the
   bench stops; never two held at a time, so a key is never left down. */
static void browse_reply(struct xz_bk *b, struct xz_bench_result *r) {
 int active = __atomic_load_n(&b->held.active, __ATOMIC_ACQUIRE);
 int32_t left = active ? (int32_t)(b->held.due - now(b)) : 0;
 r->args[1] = left > 0 ? left : 0; r->args[2] = active; r->args[3] = active ? b->held.code : 0;
}
static int browse_key(struct xz_bk *b, const struct xz_bench_request *q, struct xz_bench_result *r) {
 int32_t key = q->args[0], amount = q->args[1];
 if (key < XZ_BK_BROWSE_READ || key > XZ_BK_BROWSE_TURN) return -EINVAL;
 if (key == XZ_BK_BROWSE_READ) { browse_reply(b, r); return 0; }
 if ((avail(b) & (XZ_BK_AV_KEYS | XZ_BK_AV_BROWSE_KEYS)) != (XZ_BK_AV_KEYS | XZ_BK_AV_BROWSE_KEYS) || !b->env.send_key) return -EOPNOTSUPP;
 if (__atomic_load_n(&b->held.active, __ATOMIC_ACQUIRE)) { browse_reply(b, r); return -EBUSY; }
 if (key == XZ_BK_BROWSE_TURN) {
  if (!amount || amount > XZ_BK_TURN_MAX || amount < -XZ_BK_TURN_MAX) return -EINVAL;
  if (!b->env.send_key_value) return -EOPNOTSUPP;
  int rc = b->env.send_key_value(b->env.ctx, XZ_BK_KEY_SELECTOR, XZ_BK_OP_ROTATE, 1, amount);
  if (rc < 0) return rc;
  r->args[0] = 1; browse_reply(b, r); return 0;
 }
 if (amount < 0 || amount > XZ_BK_HOLD_MAX_MS) return -EINVAL;
 unsigned code = key == XZ_BK_BROWSE_BROWSE ? XZ_BK_KEY_BROWSE : key == XZ_BK_BROWSE_BACK ? XZ_BK_KEY_BACK : XZ_BK_KEY_SELECTOR;
 if (!amount) { int rc = tap(b, code, 0, r); if (!rc) browse_reply(b, r); return rc; }
 int rc = b->env.send_key(b->env.ctx, code, XZ_BK_OP_PRESS, 0);
 if (rc < 0) return rc;
 b->held.code = (uint16_t)code; b->held.channel = 0; b->held.due = now(b) + (uint32_t)amount;
 __atomic_store_n(&b->held.active, 1, __ATOMIC_RELEASE);
 r->args[0] = 1; browse_reply(b, r); return 0;
}
/* Op 26: USB STOP of a slot, held for the given time; stock's own long-press timer acts at 1500 ms. */
static int usb_stop_key(struct xz_bk *b, const struct xz_bench_request *q, struct xz_bench_result *r) {
 int32_t slot = q->args[0], hold = q->args[1];
 if (!slot) {
  if (!b->env.usb_release) return -EOPNOTSUPP;
  int32_t counts[4] = {0, 0, 0, 0};
  int rc = b->env.usb_release(b->env.ctx, counts);
  if (rc < 0) return rc;
  memcpy(r->args, counts, sizeof counts);
  return 0;
 }
 if ((slot != 1 && slot != 2) || hold < 1 || hold > XZ_BK_HOLD_MAX_MS || q->args[3] != (int32_t)XZ_BK_USB_STOP_MAGIC) return -EINVAL;
 if ((avail(b) & (XZ_BK_AV_KEYS | XZ_BK_AV_USB_STOP)) != (XZ_BK_AV_KEYS | XZ_BK_AV_USB_STOP) || !b->env.send_key) return -EOPNOTSUPP;
 if (__atomic_load_n(&b->held.active, __ATOMIC_ACQUIRE)) { browse_reply(b, r); return -EBUSY; }
 int rc = b->env.send_key(b->env.ctx, XZ_BK_KEY_USB_STOP, XZ_BK_OP_PRESS, (unsigned)slot);
 if (rc < 0) return rc;
 b->held.code = (uint16_t)XZ_BK_KEY_USB_STOP; b->held.channel = (uint8_t)slot; b->held.due = now(b) + (uint32_t)hold;
 __atomic_store_n(&b->held.active, 1, __ATOMIC_RELEASE);
 r->args[0] = 1; browse_reply(b, r); return 0;
}
static void browse_release(struct xz_bk *b, int force) {
 if (!__atomic_load_n(&b->held.active, __ATOMIC_ACQUIRE)) return;
 if (!force && (int32_t)(now(b) - b->held.due) < 0) return;
 if (!__atomic_exchange_n(&b->held.active, 0, __ATOMIC_ACQ_REL)) return;   /* the tick and a stop never both send */
 int rc = b->env.send_key(b->env.ctx, b->held.code, XZ_BK_OP_RELEASE, b->held.channel);
 if (rc < 0) rc = b->env.send_key(b->env.ctx, b->held.code, XZ_BK_OP_RELEASE, b->held.channel);
 if (rc < 0 && !force) __atomic_store_n(&b->held.active, 1, __ATOMIC_RELEASE); /* the next tick tries again */
}
void xz_bk_tick(struct xz_bk *b) { if (b) browse_release(b, 0); }
void xz_bk_flush(struct xz_bk *b) { if (b) browse_release(b, 1); }

void xz_bk_apply(struct xz_bk *b, const struct xz_bench_request *q, struct xz_bench_result *r,
                 const struct xz_bk_native_deck *n) {
 if (!r) return;
 memset(r, 0, sizeof *r); r->status = -EINVAL;
 if (!b || !q || q->deck > 4 || ((q->op < XZ_BK_OP_FIRST || q->op > XZ_BK_OP_LAST) && q->op != XZ_BK_OP_USB_STOP)) return;
 int d = (int)q->deck;
 if (q->op <= XZ_BK_OP_HOT_CUE && d > 3) return;
 if (q->op >= XZ_BK_OP_REC_PRESS && d != 4) return;
 if (d < 2 && q->op != XZ_BK_OP_LOOP_STATE &&
     (!b->env.send_key || !b->env.post_jump || !b->env.stock_loop || !b->env.stock_pad_mode || !b->env.side_deck)) {
  r->status = -EOPNOTSUPP; return;
 }
 if (d < 2 && q->op == XZ_BK_OP_LOOP_STATE && !b->env.stock_loop) { r->status = -EOPNOTSUPP; return; }
 switch (q->op) {
 case XZ_BK_OP_LOOP: r->status = d < 2 ? loop_stock(b, d, q, r, n) : loop_soft(b, d, q, r); break;
 case XZ_BK_OP_LOOP_STATE: r->status = loop_state(b, d, q, r, n); break;
 case XZ_BK_OP_JUMP: r->status = beat_jump(b, d, q, r, n); break;
 case XZ_BK_OP_HOT_CUE: r->status = hot_cue(b, d, q, r, n); break;
 case XZ_BK_OP_REC_PRESS:
  if (!b->env.send_key) { b->arm.valid = 0; r->status = -EOPNOTSUPP; break; }
  r->status = rec_press(b, q, r); break;
 case XZ_BK_OP_REC_STATE: r->status = rec_state(b, q, r); break;
 case XZ_BK_OP_BROWSE_KEY: r->status = browse_key(b, q, r); break;
 case XZ_BK_OP_USB_STOP: r->status = usb_stop_key(b, q, r); break;
 case XZ_BK_OP_STATUS: {
  struct xz_bk_rec_view v; memset(&v, 0, sizeof v);
  unsigned a = avail(b);
  if (b->env.recorder && b->env.recorder(b->env.ctx, &v) == 0 && v.known) a |= XZ_BK_AV_REC_STATE;
  r->args[0] = (int32_t)a;
  r->args[1] = (int32_t)__atomic_load_n(&b->jobs_posted, __ATOMIC_ACQUIRE);
  r->args[2] = (int32_t)__atomic_load_n(&b->jobs_done, __ATOMIC_ACQUIRE);
  r->args[3] = __atomic_load_n(&b->last_job, __ATOMIC_ACQUIRE);
  r->status = 0; break;
 }
 default: break;
 }
}

#ifndef XZ_BENCH_KEYS_NO_RUNTIME
/* ---- Firmware 1.26 adapter. Every address below is covered by a code-window hash in guard_windows. ---- */
#include "runtime.h"
#include "clock_compat.h"
#include "deck_select.h"
#include "extra_decks.h"
#include "recorder_state.h"
#include <limits.h>
#include <math.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#if !defined(__arm__) || defined(__aarch64__)
#error bench_keys native adapter requires the XZ 32-bit ARM ABI
#endif

/* FNV-1a 64 of stock code windows (bench-keys/work/gen_guards.py, stock 1.26 md5 6a7ccb45...). No stock bytes
   are stored here. None of the windows contains an address the mods patch. */
enum { G_KEYS = 1, G_DECK = 2, G_JUMP = 4, G_LOOP = 8, G_REC = 16, G_BROWSE = 32, G_USB_STOP = 64 };
static const struct { uint32_t address, length; uint64_t hash; unsigned use; } guard_windows[] = {
 {0x2ea3f8u, 0x09cu, 0x3ab5a78786fee982ull, G_KEYS},          /* IKeyManager::sendKey: post branch, field stores */
 {0x2ea974u, 0x008u, 0xd8ab76f8a562dd0eull, G_KEYS},          /* sendKey literals: DataSet vtable, mainMsgManager */
 {0x47ba58u, 0x024u, 0xe20e1a821e6a097aull, G_KEYS},          /* ui::KeyManager vtable: sendKey +8 */
 {0x2ecaf0u, 0x100u, 0xb5129cd04a221ab9ull, G_KEYS},          /* KeyMsgHandler::onMessage replays sendKey */
 {0x296430u, 0x014u, 0x5f1f7bbe3b016b2full, G_KEYS | G_DECK}, /* IUiObjManager::getKeyManager */
 {0x2b7ce4u, 0x024u, 0xe02c8e0808897ddbull, G_KEYS | G_JUMP}, /* PanelComPeerLinux::isOnMessageThread */
 {0x2b7e14u, 0x090u, 0x14a0d143aeede186ull, G_KEYS | G_JUMP}, /* PanelComPeerLinux::postMessage */
 {0x47c444u, 0x010u, 0x87e46169202cf541ull, G_KEYS | G_JUMP}, /* its MsgManager vtable */
 {0x2b7cdcu, 0x008u, 0x65492929a947d8a4ull, G_KEYS | G_JUMP}, /* MsgManager-subobject thunk: this - 0x40 */
 {0x2b7e0cu, 0x008u, 0x65492929a947d8a4ull, G_KEYS | G_JUMP}, /* MsgManager-subobject thunk: this - 0x40 */
 {0x2575d0u, 0x03cu, 0x176e25710eac78ecull, G_KEYS | G_JUMP}, /* IMsgHandler::postMessageInternal */
 {0x257564u, 0x04cu, 0x719a88b434639de4ull, G_JUMP},          /* MsgManager::handleMessage */
 {0x0444ccu, 0x02cu, 0x75c5c36418b22c18ull, G_JUMP},          /* DjEngineIF::playBeatJump facade */
 {0x044574u, 0x004u, 0x3724c3ea8a87e6b2ull, G_JUMP},          /* its PlayEngine singleton literal */
 {0x04b050u, 0x030u, 0x5d7feb63aca0f52full, G_JUMP | G_LOOP}, /* PlayEngine::playBeatJump: players vector */
 {0x066320u, 0x040u, 0xca3472c5d6d00c2bull, G_JUMP},          /* Player::playBeatJump: signed beats */
 {0x063318u, 0x00cu, 0x31c2856e260a4b07ull, G_LOOP},          /* Player::isLooping */
 {0x08b270u, 0x010u, 0x981507017786a98aull, G_LOOP},          /* LoopStatusManager::getCurrentLoopStatus */
 {0x0634acu, 0x0ecu, 0x3d3173e8778ba6baull, G_LOOP},          /* Player::getCurrentLoopTime */
 {0x0635a4u, 0x028u, 0xcfcfdc4accc03fb1ull, G_LOOP},          /* Player::setLoopCutterDouble: loaded +0xe66 */
 {0x28e88cu, 0x57cu, 0xe477ad62ba7ba0eaull, G_DECK},          /* PlayerInnards::onPhysicalKey */
 {0x28c274u, 0x288u, 0x60d8ff5441d07988ull, G_DECK},          /* PlayerInnards::onKey_4Beat: +0x44/+0x45 locks */
 {0x28cae0u, 0x078u, 0x6a3138b3b643245aull, G_DECK},          /* onKey_AutoBeatLoop: pad mode +0x80 */
 {0x288918u, 0x0f0u, 0x8dab9f9029046fb2ull, G_DECK},          /* onEv_CallPrev/CallNext: halve/double */
 {0x27bbe0u, 0x04cu, 0x279f832349f60ad7ull, G_DECK},          /* ui::Player::asEventCode: 0x4324/0x4325 */
 {0x255820u, 0x0b8u, 0x0c78a4b17bbc2c55ull, G_DECK},          /* PlayerSkeleton::onKeyEvent table */
 {0x255d10u, 0x058u, 0x8e438037c8631419ull, G_DECK},          /* events 31/32 -> CallNext/CallPrev */
 {0x258d6cu, 0x040u, 0x1f901e5b4ea05e8aull, G_DECK},          /* JogCpuCom: deck keys via sendKey */
 {0x258038u, 0x078u, 0x0566850599625bb8ull, G_REC},           /* ui::Mixer::asEventCode: 0x401/0x402 */
 {0x2b112cu, 0x0a8u, 0xc427e52195809b82ull, G_REC},           /* MixerSkeleton::onKeyEvent */
 {0x2b3f30u, 0x0acu, 0xa84652bfb36c9946ull, G_REC},           /* MasterInnards::onEv_Rec: 2 s timer */
 {0x2b41b8u, 0x074u, 0x640fd4a39bb3117full, G_REC},           /* MasterInnards::onEv_TrackMark */
 {0x2ba8e8u, 0x088u, 0x55f2144db59f7aabull, G_REC},           /* panel reader sends 0x401/0x402 on channel 0 */
 /* Op 21 (browse-encoder/work/browse_guards.py). */
 {0x25dda8u, 0x034u, 0xf6346921b8871c8cull, G_BROWSE},        /* MixerCpuCom: BACK 0x4208 press/release, channel 0 */
 {0x25ddf4u, 0x038u, 0x715c754f219aa2ebull, G_BROWSE},        /* MixerCpuCom: selector push 0x4207, channel 0 */
 {0x25df64u, 0x034u, 0x1db78321b87a365full, G_BROWSE},        /* MixerCpuCom: selector turn 0x4207 op 4, channel 1, value */
 {0x2a9d3cu, 0x050u, 0x21cae4b275b8e406ull, G_BROWSE},        /* BrowseUiIfDpl::onKey: 0x4207 -> browse kind 8 */
 {0x2aa230u, 0x05cu, 0xb0a81dac0e3a8254ull, G_BROWSE},        /* BrowseUiIfDpl::onKey: 0x209 -> browse kind 4 */
 {0x2ee4c0u, 0x104u, 0x60dc12bec7938a48ull, G_BROWSE},        /* KeyInput::getLongPressTime: hold times */
 {0x0e14c0u, 0x0f0u, 0xa7894f242f31f1a8ull, G_BROWSE},        /* UiKey_KeyPush: entry flags/state */
 {0x0e15e8u, 0x05cu, 0xc917261366bb5c81ull, G_BROWSE},        /* BrowseKeyProcessing: handler while state != 0 */
 /* Op 26 (stick-removal/work/usb_guards.py). */
 {0x2bb384u, 0x044u, 0xd6323b8f5e737241ull, G_USB_STOP},      /* checkDeckData: deck byte 9 bit 4 -> 0x8002 op 0/2, channel = deck */
 {0x2ee4f4u, 0x01cu, 0xedddbc3804d0390bull, G_USB_STOP},      /* KeyInput::getLongPressTime: 0x8002 -> 1500 ms */
 {0x2ee604u, 0x00cu, 0x3a368618a193a2ffull, G_USB_STOP},      /* isReleaseAvailableAfterLongPressed: 0x8002 yes */
 {0x2a0090u, 0x060u, 0x6274582a584fd952ull, G_USB_STOP},      /* UsbStorageManager::onKey: 0x8002, channel == +0x84 */
 {0x29fdc4u, 0x078u, 0xb4b84ac7de389278ull, G_USB_STOP},      /* onUsbStopKey: press/hold/release dispatch */
 {0x29ff24u, 0x03cu, 0x93c2f969f38be427ull, G_USB_STOP},      /* onUsbStopKey: hold -> request_usb_stop */
};
#define UI_OBJ_MANAGER_CELL 0x1c8a308u  /* [0x1c8a1f0+0x118], getKeyManager 0x296430 */
#define KEY_MANAGER_VPTR 0x47ba58u
#define MAIN_MSG_MANAGER_CELL 0x1c89c94u /* uif::MsgManager::mainMsgManager */
#define PEER_MSG_VPTR 0x47c444u          /* PanelComPeerLinux's MsgManager subobject (base + 0x40) */
#define PEER_BASE_MSG_VPTR 0x479bd4u     /* ui::PanelComPeer's: what mainMsgManager is on the unit (read 2026-11-16) */
#define PEER_TID_OFFSET 0x5cu            /* base + 0x9c, from the subobject */
#define PEER_RUNNING_OFFSET 0x48u        /* base + 0x88: postMessage sleeps in a loop until UiMain sets it */
#define PLAY_ENGINE_CELL 0x10e9688u
#define FN_SEND_KEY 0x2ea3f8u
#define FN_POST_INTERNAL 0x2575d0u
#define FN_PLAY_BEAT_JUMP 0x444ccu

static struct xz_bk bench;
static int bench_ready;
static unsigned guard_state; /* 0 not checked, else 0x100 | passed G_* */

static uint64_t fnv(uint64_t h, const unsigned char *p, size_t n) {
 for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
 return h;
}
static unsigned guards(void) {
 unsigned state = __atomic_load_n(&guard_state, __ATOMIC_ACQUIRE);
 if (state) return state & 0xffu;
 unsigned failed = 0;
 unsigned char buffer[256];
 for (unsigned i = 0; i < sizeof guard_windows / sizeof guard_windows[0]; i++) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (uint32_t at = 0; at < guard_windows[i].length; at += sizeof buffer) {
   uint32_t n = guard_windows[i].length - at < sizeof buffer ? guard_windows[i].length - at : (uint32_t)sizeof buffer;
   if (xz_read_memory(guard_windows[i].address + at, buffer, n)) return 0; /* not readable yet: ask again later */
   h = fnv(h, buffer, n);
  }
  if (h != guard_windows[i].hash) failed |= guard_windows[i].use;
 }
 unsigned passed = (G_KEYS | G_DECK | G_JUMP | G_LOOP | G_REC | G_BROWSE | G_USB_STOP) & ~failed;
 __atomic_store_n(&guard_state, 0x100u | passed, __ATOMIC_RELEASE);
 if (failed) xz_log("Bench keys: stock code differs from firmware 1.26; the affected bench operations are refused");
 return passed;
}
static unsigned available(void *ctx) {
 (void)ctx;
 unsigned g = guards(), a = 0;
 if (g & G_KEYS) a |= XZ_BK_AV_KEYS;
 if ((g & (G_KEYS | G_DECK)) == (G_KEYS | G_DECK)) a |= XZ_BK_AV_DECK_KEYS;
 if (g & G_JUMP) a |= XZ_BK_AV_JUMP;
 if (g & G_LOOP) a |= XZ_BK_AV_LOOP_READ;
 if ((g & (G_KEYS | G_REC)) == (G_KEYS | G_REC)) a |= XZ_BK_AV_REC_KEYS;
 if ((g & (G_KEYS | G_BROWSE)) == (G_KEYS | G_BROWSE)) a |= XZ_BK_AV_BROWSE_KEYS;
 if ((g & (G_KEYS | G_USB_STOP)) == (G_KEYS | G_USB_STOP)) a |= XZ_BK_AV_USB_STOP;
 return a;
}

static int sane(uint32_t p, uint32_t span) { return p >= 0x10000u && !(p & 3u) && p <= UINT32_MAX - span; }
static int word(uint32_t address, uint32_t *out) { return xz_read_memory(address, out, 4); }

static uint32_t now_ms(void *ctx) {
 (void)ctx; struct timespec t;
 if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 0;
 return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}
static uint32_t entropy(void *ctx) {
 (void)ctx; struct timespec t; uint32_t stack = 0;
 if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) t.tv_nsec = 0;
 return (uint32_t)t.tv_nsec ^ (uint32_t)(uintptr_t)&stack ^ (uint32_t)getpid() << 16;
}

/* The message manager UiMain drains, and a refusal when the caller is UiMain itself: sendKey would then run the
   whole key synchronously inside the bench's caller instead of as a fresh message. */
static int msg_manager(uint32_t preferred, uint32_t *out) {
 uint32_t mm = preferred, vptr, tid; unsigned char running = 0;
 if (!mm && word(MAIN_MSG_MANAGER_CELL, &mm)) return -EAGAIN;
 if (!sane(mm, 0x100) || word(mm, &vptr) || (vptr != PEER_MSG_VPTR && vptr != PEER_BASE_MSG_VPTR) || word(mm + PEER_TID_OFFSET, &tid) ||
     xz_read_memory(mm + PEER_RUNNING_OFFSET, &running, 1) || !running) return -EAGAIN;
 if (tid && tid == (uint32_t)syscall(SYS_gettid)) return -EDEADLK;
 *out = mm; return 0;
}

static int send_key_value(void *ctx, unsigned code, unsigned operation, unsigned channel, int32_t value) {
 (void)ctx;
 if (!(available(NULL) & XZ_BK_AV_KEYS)) return -EOPNOTSUPP;
 uint32_t manager, km, vptr, input, handler, own_mm, mm;
 if (word(UI_OBJ_MANAGER_CELL, &manager) || !sane(manager, 0x100) || word(manager + 0x64, &km) || !sane(km, 0x100) ||
     word(km, &vptr) || vptr != KEY_MANAGER_VPTR) return -EAGAIN;
 /* sendKey throws when its own key input (+0x84) is missing and posts through +0x58 and +0x04 / mainMsgManager. */
 if (word(km + 0x84, &input) || !input || word(km + 0x58, &handler) || !sane(handler, 8) || word(km + 4, &own_mm)) return -EAGAIN;
 int rc = msg_manager(own_mm, &mm);
 if (rc) return rc;
 /* uif::IKeyManager::sendKey(int, Operation, Channel, long, float, long), the call MixerCpuCom, JogCpuCom and the
    panel reader make for every button. The float travels as its bits (soft-float ABI); 0.0f is 0. */
 ((void (*)(void *, int, int, int, int32_t, uint32_t, int32_t))(uintptr_t)FN_SEND_KEY)(
  (void *)(uintptr_t)km, (int)code, (int)operation, (int)channel, value, 0u, 0);
 return 0;
}
static int send_key(void *ctx, unsigned code, unsigned operation, unsigned channel) { return send_key_value(ctx, code, operation, channel, 0); }

/* PlayerInnards of a stock deck (as ui_runtime.c key_player), checked by its channel byte. */
static uint32_t innards_of(int deck) {
 uint32_t manager, array, count, player, innards; unsigned char channel;
 if (deck < 0 || deck > 1 || word(UI_OBJ_MANAGER_CELL, &manager) || !sane(manager, 0x4c) || word(manager + 0x40, &array) ||
     word(manager + 0x48, &count) || count < 2 || !sane(array, 8) || word(array + (uint32_t)deck * 4u, &player) ||
     !sane(player, 0x13c) || word(player + 0x138, &innards) || !sane(innards, 0x84) ||
     xz_read_memory(innards + 0x26, &channel, 1) || channel != deck + 1) return 0;
 return innards;
}
static int stock_pad_mode(void *ctx, int deck) {
 (void)ctx; uint32_t innards = innards_of(deck), mode;
 if (!innards || word(innards + 0x80, &mode) || mode > 16) return -EAGAIN;
 return (int)mode;
}
static int side_deck(void *ctx, int side) {
 (void)ctx; struct xz_ds_selection s;
 if (side < 0 || side > 1 || !xz_runtime_deck_selection_snapshot(&s)) return -EAGAIN;
 return s.deck[side];
}

/* Stock loop state from the Player, guarded reads only, no call into the player. */
struct loop_raw { unsigned char loaded, looping; int32_t in, out; uint16_t num, den; };
static int loop_raw(int deck, struct loop_raw *o) {
 uint32_t engine, range[2], player, lsm, base, index;
 memset(o, 0, sizeof *o);
 if (word(PLAY_ENGINE_CELL, &engine) || !sane(engine, 0x20) || xz_read_memory(engine + 12, range, 8) ||
     range[1] < range[0] || range[1] - range[0] < 8 || !sane(range[0], 8) ||
     word(range[0] + (uint32_t)deck * 4u, &player) || !sane(player, 0xf00) ||
     xz_read_memory(player + 0xe66, &o->loaded, 1)) return -1;
 if (!o->loaded) return 0;
 if (word(player + 0xed4, &lsm) || !sane(lsm, 0x10) || xz_read_memory(lsm + 8, &o->looping, 1) ||
     word(lsm, &base) || word(lsm + 12, &index) || index > 1024u || !sane(base, 52u * 1025u)) return -1;
 uint32_t status = base + 52u * index;
 if (xz_read_memory(status, &o->in, 4) || xz_read_memory(status + 4, &o->out, 4) ||
     xz_read_memory(status + 8, &o->num, 2) || xz_read_memory(status + 10, &o->den, 2)) return -1;
 return 0;
}
static int32_t frames_ms(int32_t frames) {
 /* Player units are 44.1 kHz frames; getCurrentLoopTime rounds up the same way. INT_MIN+1 means none. */
 return frames == INT_MIN + 1 || frames < 0 ? -1 : (int32_t)ceil((double)frames / 44.1);
}
static int stock_loop(void *ctx, int deck, struct xz_bk_loop_view *out) {
 (void)ctx;
 memset(out, 0, sizeof *out); out->pad = -1;
 if (!(available(NULL) & XZ_BK_AV_LOOP_READ) || deck < 0 || deck > 1) return -EOPNOTSUPP;
 /* UiMain may be changing the loop: two equal reads in a row, three tries. */
 for (int attempt = 0; attempt < 3; attempt++) {
  struct loop_raw a, b;
  if (loop_raw(deck, &a) || loop_raw(deck, &b)) continue;
  if (memcmp(&a, &b, sizeof a)) continue;
  out->known = a.loaded; out->active = a.loaded && a.looping;
  out->in_ms = frames_ms(a.in); out->out_ms = frames_ms(a.out);
  out->length_milli = a.den ? (int32_t)((uint32_t)a.num * 1000u / a.den) : -1;
  return 0;
 }
 return -EAGAIN;
}

/* Beat jump jobs: a stock IMsgHandler whose onMessage (vtable slot 0) is ours, posted with
   postMessageInternal; MsgManager::handleMessage calls it on UiMain and, because it returns 0, leaves our
   data set alone. The slot stays ours until the job clears it. */
struct job_slot { const void *const *vptr; uint32_t msg_manager; int deck, beats; uint32_t job, state; };
#define JOB_SLOTS 8u
static struct job_slot jobs[JOB_SLOTS];
static uint32_t jobs_in_flight;
static int job_on_message(void *self, void *data);
static const void *const job_vtable[1] = {(const void *)job_on_message};
static int run_jump(int deck, int beats) {
 uint32_t engine, innards = innards_of(deck); unsigned char locks[2];
 if (!(available(NULL) & XZ_BK_AV_JUMP)) return -EOPNOTSUPP;
 if (word(PLAY_ENGINE_CELL, &engine) || !engine) return -EAGAIN; /* never enter the singleton factory */
 /* The same refusal onKey_4Beat makes while the deck's input is locked (+0x44/+0x45). */
 if (!innards || xz_read_memory(innards + 0x44, locks, 2)) return -EAGAIN;
 if (locks[0] || locks[1]) return -EBUSY;
 return ((int (*)(void *, int, int))(uintptr_t)FN_PLAY_BEAT_JUMP)(NULL, deck, beats) ? 1 : 0;
}
static int job_on_message(void *self, void *data) {
 struct job_slot *slot = data;
 (void)self;
 if (slot >= jobs && slot < jobs + JOB_SLOTS && __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == 1u) {
  int32_t result = run_jump(slot->deck, slot->beats);
  xz_bk_job_done(&bench, slot->job, result);
  __atomic_store_n(&slot->state, 0u, __ATOMIC_RELEASE);
  __atomic_sub_fetch(&jobs_in_flight, 1u, __ATOMIC_ACQ_REL);
 }
 return 0;
}
static int post_jump(void *ctx, int deck, int beats, uint32_t job) {
 (void)ctx;
 if (!(available(NULL) & XZ_BK_AV_JUMP)) return -EOPNOTSUPP;
 uint32_t mm;
 int rc = msg_manager(0, &mm);
 if (rc) return rc;
 struct job_slot *slot = NULL;
 for (unsigned i = 0; i < JOB_SLOTS && !slot; i++) {
  uint32_t idle = 0;
  if (__atomic_compare_exchange_n(&jobs[i].state, &idle, 2u, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) slot = &jobs[i];
 }
 if (!slot) return -EBUSY;
 slot->vptr = job_vtable; slot->msg_manager = mm; slot->deck = deck; slot->beats = beats; slot->job = job;
 __atomic_add_fetch(&jobs_in_flight, 1u, __ATOMIC_ACQ_REL);
 __atomic_store_n(&slot->state, 1u, __ATOMIC_RELEASE);
 /* IMsgHandler::postMessageInternal(handler, data): handler +4 is the message manager it posts to. */
 ((int (*)(void *, void *))(uintptr_t)FN_POST_INTERNAL)(slot, slot);
 return 0;
}

static int extra_view(void *ctx, int deck, struct xz_bk_extra_view *out) {
 (void)ctx;
 static struct xz_extra_decks s; /* tick thread only; too large for the caller's stack budget */
 memset(out, 0, sizeof *out); out->loop.pad = -1;
 if (deck < 2 || deck > 3 || !xz_extra_decks_snapshot(&s)) return -EAGAIN;
 const struct xz_extra_deck *k = &s.deck[deck - 2];
 out->ready = s.route_up && k->loaded;
 out->contact = k->platter_contact || k->held_playing;
 out->pad_mode = k->pad_mode;
 out->emergency = k->sync.source == XZ_EXTRA_SYNC_SOURCE_EMERGENCY;
 out->revision = k->timeline.load_revision;
 out->loop.known = k->loaded;
 out->loop.active = k->loop.active; out->loop.stored = k->loop.stored; out->loop.edit = k->loop.edit;
 out->loop.pad = k->loop.active ? k->active_pad : -1;
 out->loop.in_ms = k->loop.active || k->loop.stored ? k->loop.in_ms : -1;
 out->loop.out_ms = k->loop.active || k->loop.stored ? k->loop.out_ms : -1;
 static const int32_t pad_milli[8] = {250, 500, 1000, 2000, 4000, 8000, 16000, 32000};
 out->loop.length_milli = k->loop.active && k->pad_mode == XZ_EXTRA_PAD_BEAT_LOOP && k->active_pad >= 0 &&
                          k->active_pad < 8 ? pad_milli[k->active_pad] : -1;
 return 0;
}
static int extra_input(void *ctx, int deck, enum xz_bk_soft_kind kind, int pressed, int value) {
 (void)ctx;
 static const enum xz_extra_input map[] = {
  [XZ_BK_SOFT_LOOP_IN] = XZ_EXTRA_IN_LOOP_IN, [XZ_BK_SOFT_LOOP_OUT] = XZ_EXTRA_IN_LOOP_OUT,
  [XZ_BK_SOFT_RELOOP] = XZ_EXTRA_IN_RELOOP, [XZ_BK_SOFT_LOOP_HALF] = XZ_EXTRA_IN_LOOP_HALF,
  [XZ_BK_SOFT_LOOP_DOUBLE] = XZ_EXTRA_IN_LOOP_DOUBLE, [XZ_BK_SOFT_BEAT_LOOP] = XZ_EXTRA_IN_BEAT_LOOP,
  [XZ_BK_SOFT_BEAT_JUMP] = XZ_EXTRA_IN_BEAT_JUMP, [XZ_BK_SOFT_HOT_CUE] = XZ_EXTRA_IN_HOT_CUE};
 if (deck < 2 || deck > 3 || (unsigned)kind >= sizeof map / sizeof map[0]) return 0;
 struct xz_extra_deck_event e;
 memset(&e, 0, sizeof e);
 e.deck = deck - 2; e.kind = map[kind]; e.pressed = pressed;
 if (kind == XZ_BK_SOFT_BEAT_JUMP) e.beats = value; else e.hot_cue = value;
 return xz_extra_deck_input(&e);
}
static int recorder(void *ctx, struct xz_bk_rec_view *out) {
 (void)ctx; struct xz_recorder_state s;
 memset(out, 0, sizeof *out);
 if (xz_recorder_state(&s) || !s.known) return -1;
 out->known = 1; out->recording = s.recording; out->elapsed_ms = s.elapsed_ms; out->generation = s.generation;
 return 0;
}

#include "usb_release.h"
#pragma weak xz_usb_release_counts
static int usb_release(void *ctx, int32_t out[4]) {
 (void)ctx;
 if (!xz_usb_release_counts) return -EOPNOTSUPP;
 struct xz_usb_release_counts c;
 xz_usb_release_counts(&c);
 out[0] = (int32_t)c.begun; out[1] = (int32_t)c.unmounted; out[2] = (int32_t)c.busy; out[3] = (int32_t)c.loops;
 return 0;
}

void xz_bench_keys_apply(const struct xz_bench_request *q, struct xz_bench_result *r, const struct xz_bk_native_deck *n) {
 if (!bench_ready) {
  const struct xz_bk_env env = {.ctx = NULL, .now_ms = now_ms, .available = available, .send_key = send_key,
   .post_jump = post_jump, .stock_loop = stock_loop, .stock_pad_mode = stock_pad_mode, .side_deck = side_deck,
   .extra_view = extra_view, .extra_input = extra_input, .recorder = recorder, .entropy = entropy,
   .send_key_value = send_key_value, .usb_release = usb_release};
  xz_bk_init(&bench, &env); bench_ready = 1;
 }
 xz_bk_apply(&bench, q, r, n);
}
void xz_bench_keys_tick(void) { if (bench_ready) xz_bk_tick(&bench); }
int xz_bench_keys_quiesce(unsigned timeout_ms) {
 if (bench_ready) xz_bk_flush(&bench);
 for (unsigned waited = 0; __atomic_load_n(&jobs_in_flight, __ATOMIC_ACQUIRE); waited++) {
  if (waited >= timeout_ms) return 0;
  usleep(1000);
 }
 return 1;
}
#endif
