/* SPDX-License-Identifier: MIT */
/* Deterministic simulation of the Ableton Link "send tempo" policy (link_policy.c).
 *
 * Simulated pieces, all on one microsecond time line (the system clock, CLOCK_MONOTONIC):
 *   Link    the helper's session timeline, run as Link runs it: in whole microseconds per beat. A command is
 *           applied 10-30 ms after it is written; the helper publishes its state every 20 ms. A foreign peer can
 *           shift the session. The tempo other apps show is that quantized tempo to 0.01 BPM.
 *   master  a deck whose audio clock runs drift ppm off the system clock. Its bar phase is sampled every
 *           33-40 ms (irregular, as the stock interface thread samples the stock clock), the sample time is
 *           reported with +-0.5 ms jitter and an uncertainty of 0.3-0.8 ms.
 *   service the policy runs every 38-42 ms on the latest sample and the latest published Link state.
 * Truth is measured every 10 ms: session bar phase minus the master's true bar phase.
 *
 * Bounds asserted (link_policy.h):
 *   a. steady state: |session tempo - master tempo on the system clock| <= 0.005 BPM, once the trim has
 *      learnt the clock difference (TRIM_SETTLED_S after the first lock);
 *      The shown tempo (0.01 BPM) changes at most twice in ten steady minutes.
 *   b. steady state: |phase error| < 2 ms; |mean| < 0.1 ms, or < 1 ms where the true tempo lies between two Link
 *      steps that show differently (the shown-tempo hold in link_policy.c trades that much phase for a steady
 *      display); no force in steady state; a step over 10 ms
 *      gets exactly one force; never two forces within 1 s;
 *   c. no command from an observation 150 ms old or older, or with uncertainty of 2 ms or more; nothing
 *      while the master is paused or absent; a DJ tempo change reaches the session within 300 ms;
 *   d. nothing while the master is scratched; never a second command while one is unacknowledged.
 *   e. observation gaps (0.3-1.0 s every 5-40 s; half of them stall the whole host: the service, the helper's
 *      publishes and its command queue, and afterwards the session as this peer sees it moves by up to +-0.5 ms,
 *      as Link re-measures its clock offset to the other peers; the two samples after a gap carry up to +-1 ms
 *      more stamp error), drift
 *      +-40 and +-3 ppm, 128.00, 130.56 and 174.00 BPM, 10 minutes: steady |phase error| < 2 ms, at most 2 changes
 *      of the shown tempo from 100 s, and the commanded tempo stays within GAP_SWING_PPM of the master's tempo
 *      while it is held (rule 2 in link_policy.c: phase is absorbed slowly, gaps do not swing the tempo);
 *   f. after the clock ratio is learnt, a seek, a loop jump, a pitch change of +2 % and a master change between
 *      two decks of this unit (one audio crystal), with gaps (none within the event's settling): the ratio is kept
 *      (no relearning), the shown tempo is one of the two values around the new tempo 3 s after the event and
 *      changes at most once in the next 5 minutes. A pitch change inside a 300 ms host stall leaves ~6 ms of
 *      phase: steady (< 2 ms) from 20 s, shown value judged from 30 s, then at most one change in 5 minutes;
 *   g. a master on an external device is another clock: the ratio is dropped and relearnt (about LEARN_S), and
 *      this unit's ratio is back at once when the master returns to it;
 *   h. a shown value that had to change for phase stays at least 60 s (a peer nudging the session back and forth);
 *   i. a master that says it plays but does not move (the stock deck at the end of its track keeps its play flag and
 *      its BPM while the position stands still; on the unit this forced the session every second, 2026-11-15, r29):
 *      no command while it stands still, the session runs on at its tempo, and when the master moves again the
 *      step gets exactly one force.
 * Exit status 0 only when every scenario passes. tests/mutate_link_policy.py breaks each rule in a copy of
 * link_policy.c and expects this program to fail. */
#include "../link_policy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MS 1000ull
#define S 1000000ull
#define TRIM_SETTLED_S 150.0
#define STEADY_AFTER_US (5 * S)
#define FORCE_SETTLE_US (2 * S)
#define GAP_SWING_PPM 12.0     /* rule 2: held tempo vs the master's (Link's 2-3 ppm step plus the slow pull) */
#define SETTLE_EVENT_US (3 * S)
#define AFTER_EVENT_US (300 * S)
#define DWELL_MIN_US (60 * S)

static int failures, checks;
#define CHECK(c, ...)                                                                                              \
 do {                                                                                                              \
  checks++;                                                                                                        \
  if (!(c)) {                                                                                                      \
   failures++;                                                                                                     \
   printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                     \
   printf(__VA_ARGS__);                                                                                            \
   printf("\n");                                                                                                   \
  }                                                                                                                \
 } while (0)

/* ---- random numbers (xorshift64*) ------------------------------------------------------------------------ */
static uint64_t rng_state;
static double uni(void)
{
 rng_state ^= rng_state >> 12;
 rng_state ^= rng_state << 25;
 rng_state ^= rng_state >> 27;
 return (double)((rng_state * 2685821657736338717ull) >> 11) / 9007199254740992.0;
}
static uint64_t between(uint64_t lo, uint64_t hi) { return lo + (uint64_t)(uni() * (double)(hi - lo + 1)); }

static double wrap_bar(double b)
{
 double w = fmod(b + 2.0, 4.0);
 if (w < 0) w += 4.0;
 return w - 2.0;
}

/* ---- simulated Link ---------------------------------------------------------------------------------------- */
struct sim_link {
 double tempo, beat0;
 uint64_t t0;
 struct { int active; uint64_t apply_at; struct xz_link_command c; } q[4];
 uint32_t applied;
 struct xz_link_view pub;
 uint64_t next_pub;
};
static double micros_per_beat(double bpm) { return (double)llround(60e6 / bpm); }
static double link_beat(const struct sim_link *L, uint64_t t) { return L->beat0 + ((double)t - (double)L->t0) / micros_per_beat(L->tempo); }
static long link_shown(const struct sim_link *L) { return lround(60e6 / micros_per_beat(L->tempo) * 100.0); }
static void link_pivot(struct sim_link *L, uint64_t t, double tempo)
{
 L->beat0 = link_beat(L, t);
 L->t0 = t;
 L->tempo = tempo;
}
static void link_apply(struct sim_link *L, const struct xz_link_command *c, uint64_t t)
{
 if (c->kind == XZ_LINK_CMD_TEMPO) link_pivot(L, t, c->tempo_bpm);
 if (c->kind == XZ_LINK_CMD_FORCE) {
  L->tempo = c->tempo_bpm;
  L->beat0 = c->beat;
  L->t0 = c->at_us;
 }
 L->applied = c->serial;
}

/* ---- simulated master -------------------------------------------------------------------------------------- */
struct sim_master {
 int source;
 uint32_t epoch;
 double bpm;          /* the deck's own tempo (what it reports) */
 double drift;        /* audio clock vs system clock */
 double phase;        /* unwrapped bar phase in beats at time t */
 uint64_t t;
 int playing, manual, present;
 int frozen;          /* reports playing, does not move (end of track) */
 double crawl;        /* when nonzero, moves at this fraction of what it reports */
 /* ramp */
 double ramp_to, ramp_per_us;
 /* observation */
 uint64_t next_capture;
 struct xz_link_master cache;
 int capture_off;            /* stale: no new samples */
 uint64_t late_us;           /* samples delivered this late */
 int uncertain;              /* samples with 3 ms uncertainty and +-15 ms phase noise */
 double glitch_once_us;      /* the next sample's phase is off by this much */
 unsigned stamp_noise_n;     /* this many next samples carry up to +-1 ms more stamp error (after a stall) */
 struct { uint64_t due; struct xz_link_master m; } late_q[64];
 unsigned late_n;
};
static double master_true_bpm(const struct sim_master *M) { return M->bpm * (1 + M->drift); }
static void master_advance(struct sim_master *M, uint64_t t)
{
 double dt = (double)(t - M->t);
 if (M->ramp_per_us != 0) {
  double next = M->bpm + M->ramp_per_us * dt;
  if ((M->ramp_per_us > 0 && next >= M->ramp_to) || (M->ramp_per_us < 0 && next <= M->ramp_to)) {
   next = M->ramp_to;
   M->ramp_per_us = 0;
  }
  if (M->playing && !M->manual) M->phase += dt * (M->bpm + next) / 2 * (1 + M->drift) / 60e6;
  M->bpm = next;
 } else if (M->playing && !M->manual && !M->frozen) {
  M->phase += dt * master_true_bpm(M) / 60e6 * (M->crawl != 0 ? M->crawl : 1.0);
 }
 if (M->manual) M->phase += (uni() - 0.5) * 0.02; /* platter motion */
 M->t = t;
}
static void master_capture(struct sim_master *M, uint64_t t)
{
 struct xz_link_master m;
 xz_link_master_none(&m);
 if (M->present) {
  m.valid = 1;
  m.source = M->source;
  m.epoch = M->epoch;
  m.bpm = M->bpm;
  double phase = M->phase;
  m.uncertainty_us = (uint32_t)between(300, 800);
  if (M->uncertain) {
   m.uncertainty_us = 3000;
   phase += (uni() - 0.5) * 2 * 15000.0 / 60e6 * M->bpm;
  }
  if (M->glitch_once_us != 0) {
   phase += M->glitch_once_us / 60e6 * M->bpm;
   M->glitch_once_us = 0;
  }
  m.bar_phase = fmod(phase, 4.0);
  if (m.bar_phase < 0) m.bar_phase += 4.0;
  m.at_us = (uint64_t)((double)t + (uni() - 0.5) * 1000.0);
  if (M->stamp_noise_n) {
   M->stamp_noise_n--;
   m.at_us = (uint64_t)((double)m.at_us + (uni() - 0.5) * 2000.0);
  }
  m.playing = M->playing;
  m.manual = M->manual;
 }
 if (M->late_us) {
  if (M->late_n < 64) {
   M->late_q[M->late_n].due = t + M->late_us;
   M->late_q[M->late_n++].m = m;
  }
 } else {
  M->cache = m;
 }
}
static void master_deliver_late(struct sim_master *M, uint64_t t)
{
 unsigned keep = 0;
 for (unsigned i = 0; i < M->late_n; i++) {
  if (M->late_q[i].due <= t) M->cache = M->late_q[i].m;
  else M->late_q[keep++] = M->late_q[i];
 }
 M->late_n = keep;
}

/* ---- one run ----------------------------------------------------------------------------------------------- */
#define MAX_SAMPLES 70000
#define MAX_EVENTS 64
struct run;
typedef void (*script_fn)(struct run *, uint64_t t);
struct run {
 const char *name;
 double drift;
 uint64_t length;
 struct sim_link L;
 struct sim_master M;
 struct xz_link_policy P;
 uint64_t next_service, first_lock, last_disturb, extra_settle;
 int disturb_kind; /* 1 when the disturbance is a step that needs one force */
 /* bookkeeping */
 uint64_t steps_at[MAX_EVENTS];
 unsigned steps_n;
 uint64_t force_at[256];
 unsigned forces;
 uint32_t tempo_cmds;
 double max_cmd_age_us, max_cmd_uncertainty;
 int cmd_while_paused, cmd_while_manual, cmd_while_pending, cmd_while_absent;
 uint64_t quiet_from, quiet_to; /* scenario window in which no command may go out */
 uint64_t free_from, free_to;   /* scenario window in which forces need no scripted step (spacing still holds) */
 int cmd_in_quiet;
 /* measurements */
 double err[MAX_SAMPLES];
 unsigned n;
 double max_tempo_dev, max_tempo_dev_nominal, max_abs_err, sum_err;
 unsigned steady_n;
 long last_shown;
 unsigned shown_changes;
 /* tempo reach check */
 uint64_t reach_from;
 double reach_bpm_true;
 uint64_t reach_at;
 double reach_tol;
 script_fn script;
 double ramp_max_err;
 uint64_t ramp_from, ramp_to_t;
 /* observation gaps (bound e) */
 int gaps;
 uint64_t next_gap, stall_until;
 int host_stall;               /* the current gap stalls the service and the helper too */
 unsigned gap_count;
 /* shown tempo in a window, every 10 ms while the master plays (bounds e, f, h) */
 uint64_t win_from, win_to;
 long win_last;
 unsigned win_changes;
 uint64_t win_at[64];
 /* commanded tempo while the shown value is held, steady windows from win_from (bound e) */
 double held_swing_ppm;
 /* scripted event after the ratio is learnt (bounds f, g) */
 uint64_t event_at, settle_us;  /* settle_us: when the shown value is judged, after the event */
 double event_max_err;          /* largest |phase error| from the event until then */
 long shown_at_settle;
 double true_at_settle;
 int hold_at_settle;
 unsigned learnt_lost;         /* policy passes in [event, event + 303 s] with the ratio not learnt */
 double ratio_before, ratio_after;
 /* external master (bound g) */
 double ext_learnt_after, back_learnt_after, back_ratio_after, ext_ratio_late;
 uint64_t ext_relearnt_at;
};

static void disturb(struct run *R, uint64_t t, int step)
{
 R->last_disturb = t + R->extra_settle;
 if (step && R->steps_n < MAX_EVENTS) R->steps_at[R->steps_n++] = t;
}

static void service(struct run *R, uint64_t t)
{
 struct xz_link_command c;
 int pending_before = R->P.pending && R->L.pub.applied_serial != R->P.serial && t - R->P.sent_us < XZ_LINK_ACK_US;
 const struct xz_link_master *m = &R->M.cache;
 if (!xz_link_policy_step(&R->P, m, &R->L.pub, t, &c)) return;
 if (pending_before) R->cmd_while_pending++;
 /* Judged on the sample the policy had: one taken just before a pause or a scratch is still a playing master.
    The scenario's quiet window covers the time after the change. */
 if (!m->valid) R->cmd_while_absent++;
 if (!m->playing) R->cmd_while_paused++;
 if (m->manual) R->cmd_while_manual++;
 if (t >= R->quiet_from && t < R->quiet_to) R->cmd_in_quiet++;
 double age = (double)t - (double)m->at_us;
 if (age > R->max_cmd_age_us) R->max_cmd_age_us = age;
 if (m->uncertainty_us > R->max_cmd_uncertainty) R->max_cmd_uncertainty = m->uncertainty_us;
 if (c.kind == XZ_LINK_CMD_FORCE) {
  if (R->forces < 256) R->force_at[R->forces] = t;
  R->forces++;
  if (!R->first_lock) R->first_lock = t;
 } else {
  R->tempo_cmds++;
 }
 for (unsigned i = 0; i < 4; i++)
  if (!R->L.q[i].active) {
   R->L.q[i].active = 1;
   R->L.q[i].apply_at = t + between(10 * MS, 30 * MS);
   R->L.q[i].c = c;
   break;
  }
}

static void measure(struct run *R, uint64_t t)
{
 if (!R->M.present || !R->M.playing || R->M.manual || R->M.frozen || R->M.crawl != 0) return;
 double e_us = wrap_bar(link_beat(&R->L, t) - R->M.phase) * 60e6 / master_true_bpm(&R->M);
 if (!R->first_lock && fabs(e_us) < 2000) R->first_lock = t;
 if (t >= R->ramp_from && t < R->ramp_to_t && fabs(e_us) > R->ramp_max_err) R->ramp_max_err = fabs(e_us);
 if (R->reach_from && !R->reach_at && fabs(R->L.tempo - R->reach_bpm_true) <= R->reach_tol) R->reach_at = t;
 if (R->win_to && t >= R->win_from && t < R->win_to) {
  long shown = link_shown(&R->L);
  if (R->win_last && shown != R->win_last) {
   if (R->win_changes < 64) R->win_at[R->win_changes] = t;
   R->win_changes++;
  }
  R->win_last = shown;
 }
 if (R->event_at && t >= R->event_at && t < R->event_at + R->settle_us) {
  double e = fabs(wrap_bar(link_beat(&R->L, t) - R->M.phase) * 60e6 / master_true_bpm(&R->M));
  if (e > R->event_max_err) R->event_max_err = e;
 }
 if (R->event_at && t == R->event_at + R->settle_us) {
  R->shown_at_settle = link_shown(&R->L);
  R->true_at_settle = master_true_bpm(&R->M);
  R->hold_at_settle = R->P.shown != 0;
 }
 if (t < R->last_disturb + STEADY_AFTER_US || !R->first_lock || t < R->first_lock + STEADY_AFTER_US) return;
 if (R->n < MAX_SAMPLES) R->err[R->n++] = e_us;
 R->sum_err += e_us;
 R->steady_n++;
 if (fabs(e_us) > R->max_abs_err) R->max_abs_err = fabs(e_us);
 if (R->win_to && t >= R->win_from && t < R->win_to && R->P.shown) {
  double swing = fabs(60e6 / micros_per_beat(R->L.tempo) / master_true_bpm(&R->M) - 1) * 1e6;
  if (swing > R->held_swing_ppm) R->held_swing_ppm = swing;
 }
 if ((double)(t - R->first_lock) >= TRIM_SETTLED_S * 1e6) {
  long shown = link_shown(&R->L);
  if (R->last_shown && shown != R->last_shown) R->shown_changes++;
  R->last_shown = shown;
  double dev = fabs(60e6 / micros_per_beat(R->L.tempo) - master_true_bpm(&R->M));
  if (dev > R->max_tempo_dev) R->max_tempo_dev = dev;
  double nominal = fabs(60e6 / micros_per_beat(R->L.tempo) - R->M.bpm);
  if (nominal > R->max_tempo_dev_nominal) R->max_tempo_dev_nominal = nominal;
 }
}

static int cmp_abs(const void *a, const void *b)
{
 double x = fabs(*(const double *)a), y = fabs(*(const double *)b);
 return x < y ? -1 : x > y;
}

struct result {
 double mean, p99, max;
 unsigned forces, tempo_cmds;
};

static struct result run(struct run *R, uint64_t seed)
{
 rng_state = seed;
 struct sim_link *L = &R->L;
 struct sim_master *M = &R->M;
 L->tempo = 120.0;
 L->beat0 = 0.3;
 L->t0 = 0;
 L->next_pub = 0;
 M->drift = R->drift;
 M->t = 0;
 M->next_capture = between(0, 40 * MS);
 xz_link_policy_init(&R->P);
 R->next_service = 5 * MS;
 if (R->gaps) R->next_gap = between(5 * S, 40 * S);
 for (uint64_t t = 0; t <= R->length; t += MS) {
  if (R->script) R->script(R, t);
  master_advance(M, t);
  /* An observation gap: no samples; a host stall also stops the service and the helper. The first two samples
     after it carry extra stamp error, as a sampler that was itself held up would stamp them. */
  /* Random gaps keep clear of a scripted event and its settling; EV_STALL_PITCH puts one there on purpose. */
  if (R->gaps && R->event_at && t >= R->next_gap && t + 1500 * MS >= R->event_at && t < R->event_at + R->settle_us + 2 * S)
   R->next_gap = R->event_at + R->settle_us + 2 * S;
  if (R->gaps && t >= R->next_gap) {
   R->stall_until = t + between(300, 1000) * MS;
   R->host_stall = uni() < 0.5;
   R->next_gap = R->stall_until + between(5 * S, 40 * S);
   R->gap_count++;
  }
  int gap = t < R->stall_until, host = gap && R->host_stall;
  if (R->gaps && t == R->stall_until) {
   M->stamp_noise_n = 2;
   /* After a host stall Link re-measures its clock offset to the other peers: the session as this peer sees
      it moves by up to +-0.5 ms. */
   if (R->host_stall) {
    link_pivot(L, t, L->tempo);
    L->beat0 += (uni() - 0.5) * 1000.0 / micros_per_beat(L->tempo);
   }
  }
  if (t >= M->next_capture) {
   if (!M->capture_off && !gap) master_capture(M, t);
   M->next_capture = t + between(33 * MS, 40 * MS);
  }
  master_deliver_late(M, t);
  for (unsigned i = 0; i < 4 && !host; i++)
   if (L->q[i].active && L->q[i].apply_at <= t) {
    link_apply(L, &L->q[i].c, t);
    L->q[i].active = 0;
   }
  if (t >= L->next_pub && !host) {
   L->pub = (struct xz_link_view){.valid = 1, .enabled = 1, .peers = 1, .applied_serial = L->applied,
                                  .tempo_bpm = L->tempo, .beat = link_beat(L, t), .at_us = t};
   L->next_pub = t + 20 * MS;
  }
  if (t >= R->next_service && !host) {
   service(R, t);
   R->next_service = t + between(38 * MS, 42 * MS);
   if (R->event_at && t >= R->event_at && t < R->event_at + R->settle_us + AFTER_EVENT_US &&
       R->P.learnt_s < XZ_LINK_LEARN_S)
    R->learnt_lost++;
  }
  if (t % (10 * MS) == 0) measure(R, t);
 }
 struct result r = {0};
 qsort(R->err, R->n, sizeof R->err[0], cmp_abs);
 r.mean = R->steady_n ? R->sum_err / R->steady_n : 0;
 r.p99 = R->n ? fabs(R->err[(unsigned)(R->n * 0.99) < R->n ? (unsigned)(R->n * 0.99) : R->n - 1]) : 0;
 r.max = R->max_abs_err;
 r.forces = R->forces;
 r.tempo_cmds = R->tempo_cmds;
 return r;
}

/* Shared checks for every run. */
static void common_checks(struct run *R, const struct result *r)
{
 const char *n = R->name;
 CHECK(R->steady_n > 100, "%s: too few steady samples (%u)", n, R->steady_n);
 CHECK(r->max < 2000.0, "%s: steady phase error %.0f us >= 2 ms", n, r->max);
 CHECK(R->max_cmd_age_us < XZ_LINK_FRESH_US, "%s: a command used a %.0f us old observation", n, R->max_cmd_age_us);
 CHECK(R->max_cmd_uncertainty < XZ_LINK_UNCERTAIN_US, "%s: a command used an observation of %.0f us uncertainty", n,
       R->max_cmd_uncertainty);
 CHECK(!R->cmd_while_paused, "%s: %d commands while the master was paused", n, R->cmd_while_paused);
 CHECK(!R->cmd_while_absent, "%s: %d commands with no master", n, R->cmd_while_absent);
 CHECK(!R->cmd_while_manual, "%s: %d commands while the master was scratched", n, R->cmd_while_manual);
 CHECK(!R->cmd_while_pending, "%s: %d commands while one was unacknowledged", n, R->cmd_while_pending);
 CHECK(!R->cmd_in_quiet, "%s: %d commands in the window where none may go out", n, R->cmd_in_quiet);
 for (unsigned i = 1; i < R->forces && i < 256; i++)
  CHECK(R->force_at[i] - R->force_at[i - 1] >= XZ_LINK_FORCE_GAP_US, "%s: forces %u and %u only %.0f ms apart", n,
        i - 1, i, (R->force_at[i] - R->force_at[i - 1]) / 1e3);
 /* Forces happen only for the first lock and the scripted steps: one per step, within 1 s of it. */
 unsigned explained = 0;
 for (unsigned i = 0; i < R->forces && i < 256; i++) {
  uint64_t at = R->force_at[i];
  int ok = (i == 0 && at < 2 * S) || (at >= R->free_from && at < R->free_to);
  for (unsigned k = 0; k < R->steps_n && !ok; k++) ok = at >= R->steps_at[k] && at - R->steps_at[k] < 1 * S;
  explained += (unsigned)ok;
  CHECK(ok, "%s: force at %.3f s is not the first lock or a scripted step (steady-state force)", n, at / 1e6);
 }
 for (unsigned k = 0; k < R->steps_n; k++) {
  unsigned hits = 0;
  for (unsigned i = 0; i < R->forces && i < 256; i++) hits += R->force_at[i] >= R->steps_at[k] && R->force_at[i] - R->steps_at[k] < 1 * S;
  CHECK(hits == 1, "%s: step at %.3f s got %u forces (want exactly 1)", n, R->steps_at[k] / 1e6, hits);
 }
 (void)explained;
}

static void print(const struct run *R, const struct result *r)
{
 printf("%-34s drift %+3.0f ppm: steady |err| mean %+7.1f us p99 %7.1f us max %7.1f us; forces %3u tempo %4u; "
        "tempo dev vs master %.4f BPM (vs nominal %.4f), shown %.2f, %u changes\n",
        R->name, R->drift * 1e6, r->mean, r->p99, r->max, r->forces, r->tempo_cmds, R->max_tempo_dev,
        R->max_tempo_dev_nominal, R->last_shown / 100.0, R->shown_changes);
}

/* ---- scenarios ---------------------------------------------------------------------------------------------- */
static void start_master(struct run *R, double bpm, int source)
{
 memset(&R->M, 0, sizeof R->M);
 R->M.bpm = bpm;
 R->M.source = source;
 R->M.epoch = 7;
 R->M.playing = 1;
 R->M.present = 1;
 R->M.phase = 1.37;
}

static void script_none(struct run *R, uint64_t t) { (void)R; (void)t; }

static void script_ramp(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.ramp_to = R->M.bpm * 1.08;
  R->M.ramp_per_us = (R->M.ramp_to - R->M.bpm) / (5.0 * S);
  R->ramp_from = t;
  R->ramp_to_t = t + 5 * S;
 }
 if (t == 65 * S) {
  disturb(R, t, 0);
  R->reach_from = t;
  R->reach_bpm_true = master_true_bpm(&R->M);
 }
}

static void script_pitch_step(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.bpm *= 1.01;
  disturb(R, t, 0);
  R->reach_from = t;
  R->reach_bpm_true = master_true_bpm(&R->M);
 }
}

static void script_seek(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.phase += 1.37; /* a jump of 1.37 beats in the track */
  disturb(R, t, 1);
 }
}

/* A one-bar loop set 30 ms long without QUANTIZE: each pass lands 30 ms earlier in the bar. */
static void script_loop(struct run *R, uint64_t t)
{
 static double into;
 if (t == 60 * S) into = 0;
 if (t >= 60 * S && t < 80 * S) {
  double beat_us = 60e6 / master_true_bpm(&R->M);
  into += MS;
  if (into >= 4 * beat_us + 30000) {
   into -= 4 * beat_us + 30000;
   R->M.phase -= 4 + 30000 / beat_us;
   disturb(R, t, 1);
  }
 }
}

static void script_master_change(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.source = XZ_LINK_SOURCE_DECK3;
  R->M.epoch = 99;
  R->M.bpm = 126.5;
  R->M.phase += 2.61;
  disturb(R, t, 1);
  R->reach_from = t;
  R->reach_bpm_true = master_true_bpm(&R->M);
 }
}

static void script_pause(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.playing = 0;
  R->quiet_from = t + 100 * MS; /* the last playing sample may still be in hand */
  R->quiet_to = t + 5 * S;
 }
 if (t == 65 * S) {
  R->M.playing = 1;
  disturb(R, t, 1);
 }
}

static void script_absent(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.present = 0;
  R->quiet_from = t + 100 * MS;
  R->quiet_to = t + 3 * S;
 }
 if (t == 63 * S) {
  R->M.present = 1;
  disturb(R, t, 0); /* the deck played on: no step */
 }
}

static void script_stale(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.capture_off = 1;
  R->quiet_from = t + 150 * MS;
  R->quiet_to = t + 2 * S;
 }
 if (t == 62 * S) {
  R->M.capture_off = 0;
  disturb(R, t, 0);
 }
}

/* Samples arrive 400 ms late for 2 s while the DJ moves the pitch: none of them may drive a command. The
   session runs a second at the old tempo meanwhile, so the phase is about 2 ms behind when fresh samples
   return: under the force threshold, closed by the trim (time constant 10 s), hence the longer settle. The new
   tempo must still go out at once, with the trim's pull on top: at least 90 % of the change within 300 ms. */
static void script_late(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->extra_settle = 10 * S;
  disturb(R, t, 0);
  R->M.late_us = 400 * MS;
  R->quiet_from = t + 150 * MS;
  R->quiet_to = t + 2 * S;
 }
 if (t == 61 * S) R->M.bpm *= 1.002;
 if (t == 62 * S) {
  R->M.late_us = 0;
  R->M.late_n = 0;
  disturb(R, t, 0);
  R->reach_from = t;
  R->reach_bpm_true = master_true_bpm(&R->M);
  R->reach_tol = R->M.bpm * 0.002 * 0.1;
 }
}

static void script_uncertain(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.uncertain = 1;
  R->quiet_from = t + 150 * MS;
  R->quiet_to = t + 1 * S;
 }
 if (t == 61 * S) {
  R->M.uncertain = 0;
  disturb(R, t, 0);
 }
}

static void script_glitch(struct run *R, uint64_t t)
{
 if (t == 60 * S) R->M.glitch_once_us = 40000;
}

static void script_foreign(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  link_pivot(&R->L, t, R->L.tempo);
  R->L.beat0 += 0.5;
  disturb(R, t, 1);
 }
}

/* A foreign peer forces the session every 300 ms for 5 s. */
static void script_fight(struct run *R, uint64_t t)
{
 if (t >= 60 * S && t < 65 * S && (t - 60 * S) % (300 * MS) == 0) {
  link_pivot(&R->L, t, R->L.tempo);
  R->L.beat0 += 0.25;
  disturb(R, t, 0);
 }
 if (t == 60 * S) {
  R->free_from = t;
  R->free_to = t + 6 * S;
 }
}

/* Bound i, as seen on the unit: the master track ends at 60 s. The stock deck keeps its play flag and BPM and
   reports the same position for 30 s; then the DJ starts the next track (a new epoch, another bar phase). */
static void script_track_end(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.frozen = 1;
  R->quiet_from = t + 100 * MS; /* the last moving sample may still be in hand */
  R->quiet_to = t + 30 * S;
 }
 if (t == 90 * S) {
  R->M.frozen = 0;
  R->M.epoch++;
  R->M.phase += 1.7;
  disturb(R, t, 1);
 }
}

/* The same without a new epoch: the position stands still for 2 s (an engine that stopped advancing it) and then
   moves on from where the music really is. */
static void script_stuck(struct run *R, uint64_t t)
{
 static double held;
 if (t == 60 * S) {
  held = R->M.phase;
  R->M.frozen = 1;
  R->quiet_from = t + 100 * MS;
  R->quiet_to = t + 2 * S;
 }
 if (t == 62 * S) {
  R->M.frozen = 0;
  R->M.phase = held + 2.0 * master_true_bpm(&R->M) / 60.0 + 0.4; /* a stall in the reading, then 0.4 beats off */
  disturb(R, t, 1);
 }
}

/* A master that creeps: it moves at 92 % of the tempo it reports for 20 s (3 ms short per 40 ms interval, under the
   per-interval bound). The session is not forced after it; when it moves at its tempo again, one force. */
static void script_crawl(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.crawl = 0.92;
  R->free_from = R->free_to = 0;
 }
 if (t == 80 * S) {
  R->M.crawl = 0;
  disturb(R, t, 1);
 }
}

static void script_scratch(struct run *R, uint64_t t)
{
 if (t == 60 * S) {
  R->M.manual = 1;
  R->quiet_from = t + 100 * MS;
  R->quiet_to = t + 3 * S;
 }
 if (t == 63 * S) {
  R->M.manual = 0;
  R->M.phase += 0.17; /* where the record ended up */
  disturb(R, t, 1);
 }
}

static struct run *fresh(const char *name, double drift, uint64_t length, double bpm, script_fn s)
{
 static struct run R;
 memset(&R, 0, sizeof R);
 R.name = name;
 R.drift = drift;
 R.length = length;
 R.script = s;
 R.ramp_from = R.ramp_to_t = UINT64_MAX;
 R.reach_tol = 0.005;
 R.settle_us = SETTLE_EVENT_US;
 start_master(&R, bpm, XZ_LINK_SOURCE_DECK1);
 return &R;
}

static uint64_t seed_for(const char *name, double drift)
{
 uint64_t h = 1469598103934665603ull;
 for (const char *p = name; *p; p++) h = (h ^ (uint8_t)*p) * 1099511628211ull;
 return h ^ (uint64_t)(drift > 0 ? 1 : 2);
}

/* Whether the two Link steps around a tempo show different 0.01 BPM values. */
static int at_shown_boundary(double bpm)
{
 double u = floor(60e6 / bpm);
 return lround(60e6 / u * 100.0) != lround(60e6 / (u + 1) * 100.0);
}

static void steady(const char *name, double bpm, double drift)
{
 struct run *R = fresh(name, drift, 600 * S, bpm, script_none);
 struct result r = run(R, seed_for(name, drift));
 int boundary = at_shown_boundary(bpm * (1 + drift));
 print(R, &r);
 if (boundary) printf("    %.5f BPM lies between two Link steps that show differently\n", bpm * (1 + drift));
 common_checks(R, &r);
 CHECK(R->max_tempo_dev <= 0.005, "%s: steady tempo %.4f BPM from the master's", name, R->max_tempo_dev);
 CHECK(fabs(r.mean) < (boundary ? 1000.0 : 100.0), "%s: steady mean phase error %.1f us", name, r.mean);
 CHECK(R->shown_changes <= 2, "%s: the shown tempo changed %u times in steady state", name, R->shown_changes);
 CHECK(r.forces <= 1, "%s: %u forces in 10 steady minutes", name, r.forces);
 CHECK(r.tempo_cmds <= 600, "%s: %u tempo commands in 10 minutes", name, r.tempo_cmds);
}

static void scenario(const char *name, double bpm, double drift, script_fn s, uint64_t length, int need_reach)
{
 struct run *R = fresh(name, drift, length, bpm, s);
 struct result r = run(R, seed_for(name, drift));
 print(R, &r);
 common_checks(R, &r);
 if (need_reach) {
  CHECK(R->reach_at && R->reach_at - R->reach_from <= 300 * MS, "%s: tempo reached the session after %.0f ms", name,
        R->reach_at ? (R->reach_at - R->reach_from) / 1e3 : -1.0);
  if (R->reach_at) printf("    tempo within 0.005 BPM of the new master tempo after %.0f ms\n", (R->reach_at - R->reach_from) / 1e3);
 }
 if (R->ramp_from != UINT64_MAX) printf("    largest phase error during the ramp %.0f us\n", R->ramp_max_err);
}

/* ---- gaps, events after learning, an external master, the shown value's dwell (bounds e-h) -------------------- */
static uint64_t seed2(const char *name, double bpm, double drift)
{
 uint64_t h = seed_for(name, drift);
 h ^= (uint64_t)llround(bpm * 100.0) * 0x9e3779b97f4a7c15ull;
 h ^= (uint64_t)llround(fabs(drift) * 1e7) * 0xc2b2ae3d27d4eb4full;
 return h ? h : 1;
}

static void print_window(const struct run *R)
{
 printf("    shown tempo: %u changes in [%.0f s, %.0f s)", R->win_changes, R->win_from / 1e6, R->win_to / 1e6);
 for (unsigned i = 0; i < R->win_changes && i < 64; i++) printf("%s%.2f s", i ? ", " : " at ", R->win_at[i] / 1e6);
 if (R->gaps) printf("; %u gaps", R->gap_count);
 printf("; held tempo at most %.1f ppm from the master's; %u passes unlearnt after the event\n", R->held_swing_ppm,
        R->learnt_lost);
}

static void gap_run(double bpm, double drift)
{
 char name[80];
 snprintf(name, sizeof name, "gaps %.2f BPM %+.0f ppm, 10 min", bpm, drift * 1e6);
 struct run *R = fresh(name, drift, 600 * S, bpm, script_none);
 R->gaps = 1;
 R->win_from = 100 * S;
 R->win_to = R->length + 1;
 struct result r = run(R, seed2(name, bpm, drift));
 print(R, &r);
 print_window(R);
 common_checks(R, &r);
 CHECK(R->gap_count >= 10, "%s: only %u gaps", name, R->gap_count);
 CHECK(r.forces <= 1, "%s: %u forces", name, r.forces);
 CHECK(R->win_changes <= 2, "%s: the shown tempo changed %u times from 100 s", name, R->win_changes);
 CHECK(R->held_swing_ppm <= GAP_SWING_PPM, "%s: the held tempo swung %.1f ppm from the master's (rule 2)", name,
       R->held_swing_ppm);
}

enum { EV_SEEK, EV_LOOP, EV_PITCH, EV_DECK, EV_STALL_PITCH };
static int event_kind;

static void script_event(struct run *R, uint64_t t)
{
 if (t == R->event_at - 100 * MS) R->ratio_before = R->P.ratio;
 if (t == R->event_at) {
  switch (event_kind) {
  case EV_SEEK: /* the stock deck's epoch does not change on a seek */
   R->M.phase += 1.37;
   disturb(R, t, 1);
   break;
  case EV_LOOP: /* one pass of a loop set 30 ms long; the software decks start a new epoch */
   R->M.phase -= 4 + 30000 / (60e6 / master_true_bpm(&R->M));
   R->M.epoch++;
   disturb(R, t, 1);
   break;
  case EV_PITCH:
   R->M.bpm *= 1.02;
   disturb(R, t, 0);
   break;
  case EV_STALL_PITCH: /* the host stalls 300 ms just as the DJ moves the pitch: the session lags ~6 ms */
   R->M.bpm *= 1.02;
   R->stall_until = t + 300 * MS;
   R->host_stall = 1;
   R->extra_settle = 15 * S;
   disturb(R, t, 0);
   break;
  case EV_DECK: /* DECK 3 runs from the same audio crystal: same drift */
   R->M.source = XZ_LINK_SOURCE_DECK3;
   R->M.epoch = 99;
   R->M.bpm = 130.56;
   R->M.phase += 2.61;
   disturb(R, t, 1);
   break;
  }
 }
 if (t == R->event_at + 10 * S) R->ratio_after = R->P.ratio;
}

static void event_run(const char *what, int kind, double drift, uint64_t settle_us)
{
 char name[80];
 snprintf(name, sizeof name, "learnt, gaps, then %s", what);
 event_kind = kind;
 struct run *R = fresh(name, drift, 150 * S + settle_us + AFTER_EVENT_US + 2 * S, 128.0, script_event);
 R->gaps = 1;
 R->event_at = 150 * S;
 R->settle_us = settle_us;
 R->win_from = R->event_at + settle_us;
 R->win_to = R->win_from + AFTER_EVENT_US;
 struct result r = run(R, seed2(name, 128.0, drift));
 print(R, &r);
 print_window(R);
 printf("    %.0f s after the event: shows %.2f, master %.5f, held %d; largest phase error until then %.0f us; ratio %+.2f ppm "
        "before, %+.2f ppm 10 s after\n", settle_us / 1e6, R->shown_at_settle / 100.0, R->true_at_settle, R->hold_at_settle,
        R->event_max_err, R->ratio_before * 1e6, R->ratio_after * 1e6);
 common_checks(R, &r);
 CHECK(!R->learnt_lost, "%s: the clock ratio was being relearnt after the event (%u passes)", name, R->learnt_lost);
 CHECK(fabs(R->ratio_after - R->ratio_before) < 2e-6, "%s: the ratio moved %.1f ppm across the event", name,
       (R->ratio_after - R->ratio_before) * 1e6);
 CHECK(R->hold_at_settle, "%s: the shown tempo is not held %.0f s after the event", name, settle_us / 1e6);
 CHECK(fabs(R->shown_at_settle - R->true_at_settle * 100.0) < 1.0, "%s: shows %.2f %.0f s after the event, master %.5f", name,
       R->shown_at_settle / 100.0, settle_us / 1e6, R->true_at_settle);
 CHECK(R->win_changes <= 1, "%s: the shown tempo changed %u times in the 5 minutes after settling", name, R->win_changes);
}

/* The master moves to a networked player: another clock. The ratio learnt for this unit's crystal must not be
   applied to it; it is learnt anew, which takes about LEARN_S. When the master comes back to this unit, this
   unit's ratio is used again at once. */
#define EXT_AT (150 * S)
#define BACK_AT (360 * S)
static void script_external(struct run *R, uint64_t t)
{
 if (t == EXT_AT) {
  R->ratio_before = R->P.ratio;
  R->M.source = XZ_LINK_SOURCE_EXTERNAL;
  R->M.epoch = 500;
  R->M.drift = R->drift > 0 ? -25e-6 : 25e-6;
  R->M.phase += 0.9;
  disturb(R, t, 1);
 }
 if (t == EXT_AT + 500 * MS) R->ext_learnt_after = R->P.learnt_s;
 if (t > EXT_AT + 500 * MS && t < BACK_AT && !R->ext_relearnt_at && R->P.learnt_s >= XZ_LINK_LEARN_S) R->ext_relearnt_at = t;
 if (t == BACK_AT - 1 * S) R->ext_ratio_late = R->P.ratio;
 if (t == BACK_AT) {
  R->M.source = XZ_LINK_SOURCE_DECK1;
  R->M.epoch = 8;
  R->M.drift = R->drift;
  R->M.phase += 1.1;
  disturb(R, t, 1);
 }
 if (t == BACK_AT + 500 * MS) {
  R->back_learnt_after = R->P.learnt_s;
  R->back_ratio_after = R->P.ratio;
 }
}

static void external_run(double drift)
{
 const char *name = "external master, then back to this unit";
 struct run *R = fresh(name, drift, 480 * S, 128.0, script_external);
 R->event_at = BACK_AT;
 R->win_from = BACK_AT + SETTLE_EVENT_US;
 R->win_to = R->length + 1;
 struct result r = run(R, seed2(name, 128.0, drift));
 double ext = drift > 0 ? -25e-6 : 25e-6;
 print(R, &r);
 print_window(R);
 printf("    external clock %+.0f ppm: ratio %+.2f ppm before, learnt %.1f s 0.5 s after the switch; relearnt %s%.1f s "
        "after it, ratio %+.2f ppm; back on this unit: learnt %.1f s, ratio %+.2f ppm\n",
        ext * 1e6, R->ratio_before * 1e6, R->ext_learnt_after, R->ext_relearnt_at ? "" : "NEVER ",
        R->ext_relearnt_at ? (R->ext_relearnt_at - EXT_AT) / 1e6 : 0.0, R->ext_ratio_late * 1e6, R->back_learnt_after,
        R->back_ratio_after * 1e6);
 common_checks(R, &r);
 CHECK(R->ext_learnt_after < XZ_LINK_LEARN_S, "%s: the unit's clock ratio was kept for the external master", name);
 CHECK(R->ext_relearnt_at, "%s: the external clock ratio was never learnt", name);
 CHECK(fabs(R->ext_ratio_late - ext) < 3e-6, "%s: ratio %+.2f ppm for an external clock of %+.0f ppm", name,
       R->ext_ratio_late * 1e6, ext * 1e6);
 CHECK(R->back_learnt_after >= XZ_LINK_LEARN_S && fabs(R->back_ratio_after - R->ratio_before) < 0.5e-6,
       "%s: back on this unit the ratio is %+.2f ppm (learnt %.1f s), want this unit's %+.2f ppm at once", name,
       R->back_ratio_after * 1e6, R->back_learnt_after, R->ratio_before * 1e6);
 CHECK(!R->learnt_lost, "%s: this unit's ratio was being relearnt after the return (%u passes)", name, R->learnt_lost);
 CHECK(R->hold_at_settle && fabs(R->shown_at_settle - R->true_at_settle * 100.0) < 1.0,
       "%s: 3 s after the return shows %.2f (held %d), master %.5f", name, R->shown_at_settle / 100.0, R->hold_at_settle,
       R->true_at_settle);
 CHECK(R->win_changes <= 1, "%s: the shown tempo changed %u times after the return", name, R->win_changes);
}

/* A peer nudges the session 1.5 ms against the side the shown value holds, so the shown value must change for
   phase; 10 s later it nudges 3 ms the other way. The new shown value stays at least 60 s, then changes back
   because staying would cost more than 1 ms. Nudges are under the step threshold: no force. */
static void nudge(struct run *R, uint64_t t, double us)
{
 link_pivot(&R->L, t, R->L.tempo);
 R->L.beat0 += us / micros_per_beat(R->L.tempo);
 disturb(R, t, 0);
}

static void script_nudge(struct run *R, uint64_t t)
{
 static double first;
 if (t == 150 * S) {
  first = 60e6 / micros_per_beat(R->L.tempo) < master_true_bpm(&R->M) ? -1500.0 : 1500.0;
  nudge(R, t, first);
 }
 if (t == 160 * S) nudge(R, t, -2 * first);
}

static void dwell_run(double drift)
{
 const char *name = "peer nudges 1.5 ms, then 3 ms back";
 struct run *R = fresh(name, drift, 300 * S, 128.0, script_nudge);
 R->win_from = 100 * S;
 R->win_to = R->length + 1;
 struct result r = run(R, seed2(name, 128.0, drift));
 print(R, &r);
 print_window(R);
 common_checks(R, &r);
 CHECK(r.forces <= 1, "%s: %u forces", name, r.forces);
 CHECK(R->win_changes >= 2, "%s: the shown value changed %u times; it must change for phase and back", name,
       R->win_changes);
 for (unsigned i = 1; i < R->win_changes && i < 64; i++)
  CHECK(R->win_at[i] - R->win_at[i - 1] >= DWELL_MIN_US, "%s: the shown value changed at %.2f s and again at %.2f s",
        name, R->win_at[i - 1] / 1e6, R->win_at[i] / 1e6);
}

/* ---- builders, picker and single-call rules ------------------------------------------------------------------ */
static void builders(void)
{
 struct xz_link_master m;
 struct xz_sync_native_snapshot s = {.available = 1, .phase_available = 1, .native_deck = 1, .master_id = 1, .bpm = 128.f,
                                     .bar_phase = 2.5f, .captured_us = 1000000, .output_latency_us = 12000,
                                     .uncertainty_us = 400, .playing = 1, .source_revision = 5, .track_identity = 77};
 xz_link_master_from_stock(&s, &m);
 CHECK(m.valid && m.source == XZ_LINK_SOURCE_DECK2 && m.bpm == 128.0 && m.bar_phase == 2.5 && m.at_us == 1012000 &&
       m.uncertainty_us == 400 && m.playing && !m.manual && m.epoch, "stock builder");
 uint32_t e = m.epoch;
 s.source_revision = 6;
 xz_link_master_from_stock(&s, &m);
 CHECK(m.epoch != e, "stock epoch follows the load");
 s.manual = 1;
 xz_link_master_from_stock(&s, &m);
 CHECK(m.valid && m.manual, "stock manual flag");
 s.bar_phase = 4.f;
 xz_link_master_from_stock(&s, &m);
 CHECK(!m.valid && m.source == XZ_LINK_SOURCE_DECK2, "stock bar phase 4 refused");
 s.bar_phase = 1.f;
 s.phase_available = 0;
 xz_link_master_from_stock(&s, &m);
 CHECK(!m.valid, "stock without phase refused");

 struct djl_deck_facts f;
 memset(&f, 0, sizeof f);
 f.content_epoch = 3;
 f.play = DJL_PLAY_PLAYING;
 f.track_bpm_x100 = 12800;
 f.pitch_q20 = DJL_PITCH_UNITY;
 f.timeline.captured_us = 5000000;
 f.timeline.playhead_us = 10000000;
 f.timeline.speed_q20 = (int32_t)(1.02 * DJL_PITCH_UNITY);
 f.timeline.next_beat = 21;
 f.timeline.ahead_ms[0] = 10100;
 f.timeline.ahead_bar[0] = 3;
 xz_link_master_from_facts(0, &f, 0, 1000, 9, &m);
 CHECK(m.valid && m.playing && m.source == XZ_LINK_SOURCE_DECK3 && fabs(m.bpm - 128.0 * f.timeline.speed_q20 / DJL_PITCH_UNITY) < 1e-9 &&
       fabs(m.bar_phase - (2.0 - 100000.0 / 468750.0)) < 1e-9 && m.at_us == 5000000 && m.uncertainty_us == 1000, "facts builder");
 e = m.epoch;
 xz_link_master_from_facts(0, &f, 0, 1000, 10, &m);
 CHECK(m.epoch != e, "facts epoch follows the transport generation");
 f.timeline.ahead_bar[0] = 1;
 xz_link_master_from_facts(1, &f, 1, 1000, 9, &m);
 CHECK(m.valid && m.manual && m.source == XZ_LINK_SOURCE_DECK4 && fabs(m.bar_phase - (4.0 - 100000.0 / 468750.0)) < 1e-9,
       "facts bar phase wraps below the downbeat");
 f.timeline.ahead_ms[0] = 10600;
 xz_link_master_from_facts(0, &f, 0, 1000, 9, &m);
 CHECK(!m.valid, "facts with a grid gap refused");
 f.timeline.ahead_ms[0] = 10100;
 f.play = DJL_PLAY_PAUSED;
 xz_link_master_from_facts(0, &f, 0, 1000, 9, &m);
 CHECK(m.valid && !m.playing, "facts paused");
 f.play = DJL_PLAY_PLAYING;
 f.timeline.ahead_ms[0] = DJL_NO_TIME;
 xz_link_master_from_facts(0, &f, 0, 1000, 9, &m);
 CHECK(!m.valid, "facts without a beat ahead refused");

 struct xz_master_observation o = {.deck = XZ_MASTER_DECK3, .revision = 4, .observed_ms = 10000, .operator_epoch = 2};
 s.available = 1;
 s.native_deck = 0;
 s.master_id = 0;
 CHECK(xz_link_pick_master(&o, 1, 10050000, &s, 1) == XZ_MASTER_DECK3, "current observation names deck 3");
 CHECK(xz_link_pick_master(&o, 1, 10200000, &s, 1) == XZ_MASTER_DECK1, "stale observation: the stock clock's master");
 CHECK(xz_link_pick_master(&o, 0, 10050000, &s, 0) == XZ_MASTER_UNKNOWN, "nothing known");
 o.deck = XZ_MASTER_NONE;
 CHECK(xz_link_pick_master(&o, 1, 10050000, &s, 1) == XZ_MASTER_NONE, "current observation: no master");
 struct xz_link_master soft[2];
 xz_link_master_from_facts(0, &f, 0, 1000, 9, &soft[0]);
 xz_link_master_none(&soft[1]);
 xz_link_master_select(XZ_MASTER_DECK2, &s, 1, soft, &m);
 CHECK(!m.valid && m.source == XZ_LINK_SOURCE_DECK2, "stock snapshot of the other deck is not used");
 xz_link_master_select(XZ_MASTER_EXTERNAL, &s, 1, soft, &m);
 CHECK(!m.valid && m.source == XZ_LINK_SOURCE_EXTERNAL, "external master is not sent");
 xz_link_master_select(XZ_MASTER_DECK4, &s, 1, soft, &m);
 CHECK(!m.valid && m.source == XZ_LINK_SOURCE_DECK4, "deck 4 without its shape");

 /* A sample stamped a few ms ahead of now (output latency) is used; one 60 ms ahead is not. */
 struct xz_link_policy p;
 struct xz_link_command c;
 struct xz_link_view l = {.valid = 1, .enabled = 1, .tempo_bpm = 128, .beat = 0, .at_us = 10000000};
 struct xz_link_master ahead = {.valid = 1, .source = 1, .bpm = 128, .bar_phase = 0, .at_us = 10003000, .uncertainty_us = 500,
                                .playing = 1, .epoch = 1};
 xz_link_policy_init(&p);
 (void)xz_link_policy_step(&p, &ahead, &l, 10000000, &c);
 CHECK(p.hold == XZ_LINK_HOLD_NONE && p.used == 1, "a sample 3 ms ahead is used (hold %d)", p.hold);
 ahead.at_us = 10060000;
 xz_link_policy_init(&p);
 (void)xz_link_policy_step(&p, &ahead, &l, 10000000, &c);
 CHECK(p.hold == XZ_LINK_HOLD_STALE, "a sample 60 ms ahead is refused (hold %d)", p.hold);
 puts("builders, picker and single-call rules: done");
}

int main(void)
{
 builders();
 for (int sign = -1; sign <= 1; sign += 2) {
  double d = sign * 40e-6;
  steady("steady 128.00 BPM 10 min", 128.0, d);
  steady("steady 174.00 BPM 10 min", 174.0, d);
  scenario("pitch ramp +8 % over 5 s", 128.0, d, script_ramp, 120 * S, 1);
  scenario("pitch step +1 %", 128.0, d, script_pitch_step, 90 * S, 1);
  scenario("seek 1.37 beats", 128.0, d, script_seek, 90 * S, 0);
  scenario("loop 1 bar + 30 ms for 20 s", 128.0, d, script_loop, 100 * S, 0);
  scenario("master change deck 1 -> deck 3", 128.0, d, script_master_change, 90 * S, 1);
  scenario("pause 5 s and resume", 128.0, d, script_pause, 90 * S, 0);
  scenario("no master for 3 s", 128.0, d, script_absent, 90 * S, 0);
  scenario("stale observations 2 s", 128.0, d, script_stale, 90 * S, 0);
  scenario("late observations 2 s, pitch moved", 128.0, d, script_late, 90 * S, 1);
  scenario("uncertain reads 1 s", 128.0, d, script_uncertain, 90 * S, 0);
  scenario("one wild observation +40 ms", 128.0, d, script_glitch, 90 * S, 0);
  scenario("foreign force half a beat", 128.0, d, script_foreign, 90 * S, 0);
  scenario("foreign peer forcing every 300 ms", 128.0, d, script_fight, 90 * S, 0);
  scenario("scratch 3 s", 128.0, d, script_scratch, 90 * S, 0);
  scenario("track end: plays, stands still 30 s", 174.0 * 1.06714, d, script_track_end, 120 * S, 0);
  scenario("position stuck 2 s, then 0.4 beats off", 128.0, d, script_stuck, 90 * S, 0);
  scenario("master creeps at 92 % for 20 s", 128.0, d, script_crawl, 110 * S, 0);
 }
 static const double gap_bpm[] = {128.0, 130.56, 174.0}, gap_drift[] = {40e-6, -40e-6, 3e-6, -3e-6};
 for (unsigned b = 0; b < 3; b++)
  for (unsigned k = 0; k < 4; k++) gap_run(gap_bpm[b], gap_drift[k]);
 for (int sign = -1; sign <= 1; sign += 2) {
  double d = sign * 40e-6;
  event_run("seek 1.37 beats", EV_SEEK, d, SETTLE_EVENT_US);
  event_run("loop jump 1 bar + 30 ms", EV_LOOP, d, SETTLE_EVENT_US);
  event_run("pitch +2 %", EV_PITCH, d, SETTLE_EVENT_US);
  event_run("master deck 1 -> deck 3 (130.56)", EV_DECK, d, SETTLE_EVENT_US);
  /* Not the 3 s of bound f: the stall leaves ~6 ms of phase, which is recovered first (steady from 20 s, shown
     value judged from 30 s). */
  event_run("pitch +2 % in a 300 ms host stall", EV_STALL_PITCH, d, 30 * S);
  external_run(d);
  dwell_run(d);
 }
 printf("%s link policy simulation: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
 return failures ? 1 : 0;
}
