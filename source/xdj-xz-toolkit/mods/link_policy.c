/* SPDX-License-Identifier: MIT */
#include "link_policy.h"
#include <math.h>
#include <string.h>

/* Proportional pull on the filtered phase error e (seconds, session minus master): tempo = bpm * (1 + r - KP*e)
   while the clock ratio r is being learnt (rule 1 below), a 10 s time constant. */
#define KP 0.1
/* Rule 2, once the ratio is learnt: phase errors are absorbed at KA per second up to ABSORB_US (1 ms -> 10 ppm) and
   at that 10 ppm up to FAST_US, so observation noise (+-0.5 ms stamps), a gap, or Link re-measuring its clock offset
   to other peers after a stall (a few hundred microseconds at once) moves the tempo by at most 10 ppm instead of
   tens. Two errors are not noise and are pulled back at KP: the phase a DJ tempo move leaves behind while the new
   tempo reaches the session (about 1.4 ms for +2 %; `recovering`, inside the held shown value, for at least
   RECOVER_MIN_US and until the error is under RECOVERED_US), and an error beyond FAST_US, the steady bound (a DJ move
   during a stall; `released` until it is under ABSORB_US, which also lets go of the shown value, rule 3). Beyond
   XZ_LINK_STEP_US the error is forced. */
#define KA 0.01
#define ABSORB_US 1000.0
#define FAST_US 2000.0
#define RECOVER_MIN_US 5000000u
#define RECOVER_MAX_US 60000000u
#define RECOVERED_US 300.0
#define FILTER_US 3000000.0     /* phase error low-pass time constant */
#define DT_MAX_S 0.2            /* longest observation gap one filter step may span */
/* Rule 1, the clock ratio. It is a property of two crystals, the master's audio clock and the system clock, so it
   is measured on the master alone: how far the master's bar phase runs ahead of where its own BPM puts it, per
   second of CLOCK_MONOTONIC (struct xz_link_ratio, a straight-line fit weighted over XZ_LINK_RATIO_TAU_S). The Link
   session, its phase jumps and the commands sent play no part, so a seek, a loop, a pitch change, a pause, a
   stall or a master change between this unit's decks cannot teach it anything false. An interval in which the BPM
   changed or the phase moved more than RATIO_JUMP_US against the prediction (a seek, a loop jump, a new master)
   starts a new segment of the fit (fit_add). */
#define RATIO_JUMP_US 2000.0
#define RATIO_MIN_S 10.0        /* the fit is used from this much observation time on */
#define MOVING_PER_S 1e-4       /* master BPM changing faster than this (relative, per second) is a ramp */
#define LEAD_US 40000.0         /* command latency plus half a period: where a ramp's tempo is aimed */
#define DJ_MOVE 20e-6           /* master BPM change since the last tempo command that goes out at once */
#define FAR 100e-6              /* session tempo this far off goes out at once (new master, foreign change) */
#define NEAR 6e-6               /* below this the session tempo is left alone (Link's own step is 2-3 ppm) */
#define NEAR_HELD 0.5e-6        /* while a shown value is held: any other Link step goes out (rule 2 keeps it rare) */
#define TRIM_GAP_US 1000000u    /* trim updates at most once a second */
/* Rule 3, the shown tempo. Link turns a tempo into whole microseconds per beat (link/Tempo.hpp microsPerBeat), so
   peers see it in steps of 2-3 ppm, and apps show it to 0.01 BPM. When the tempo that holds phase lies between two
   steps that show differently (128 BPM with +40 ppm needs 128.00512: 128.0049 shows 128.00, 128.0052 shows 128.01),
   trimming freely flips the shown value every few seconds. So once the ratio is learnt the commanded tempo stays
   inside one shown value: the one the master's tempo shows (SHOWN_NEAR of slack, so a ratio that moves a little
   does not change it). It moves to the neighbouring value only when the master's tempo is near that one too and
   staying would cost more than CROSS_US of phase, and a value reached that way is kept at least DWELL_US. A tempo
   change by the DJ or a new master is shown at once; at a boundary, on the side that pays back the phase the move
   left behind. At such a boundary that is one change every 10-60 minutes, against flicker. The hold is for errors
   under FAST_US: a larger one (a DJ move during a stall) is recovered first, the shown value following it, and the
   hold comes back on the master's own shown value once the error is under ABSORB_US. */
#define CROSS_US 1000.0
#define DWELL_US 61000000u      /* 60 s as peers see it: a command takes up to tens of ms to reach them */
#define SHOWN_NEAR 10e-6
/* Rule 4, a master that does not move. The stock deck at the end of its track keeps its play flag and its BPM while
   its position stands still (seen on the unit, r29, 2026-11-15: a force every second, each one 4 beats on). Each
   interval between two observations must show the master's bar phase moving by what its own tempo says, within
   RUN_TOL_US (stamp jitter, read uncertainty and a pitch step inside the interval stay under 2 ms). A step moves it
   once; a master that stands still fails every interval. A failed interval sends nothing and restarts the count of
   observations a step needs (XZ_LINK_STEP_COUNT), so a step is forced only after the master has moved by its tempo
   for that many observations. A force also needs the latest RUN_WINDOW intervals' residuals to add up to less than
   RUN_TOL_US, so a master that creeps (a few percent off its BPM, under RUN_TOL_US per interval) is not forced
   either. The session runs on at its tempo, as for a paused master, until the master moves again. */
#define RUN_TOL_US 5000.0
#define RUN_WINDOW 3u

static double wrap_bar(double beats)
{
 double w = fmod(beats + 2.0, 4.0);
 if (w < 0) w += 4.0;
 return w - 2.0;
}

static double clamp(double v, double limit)
{
 return v > limit ? limit : v < -limit ? -limit : v;
}

void xz_link_policy_init(struct xz_link_policy *p)
{
 memset(p, 0, sizeof *p);
}

/* The observation history goes; the clock ratio and the held shown value stay (rule 1, rule 3). */
static void forget(struct xz_link_policy *p)
{
 p->have = 0;
 p->steps = 0;
 memset(p->run_us, 0, sizeof p->run_us);
 p->filtered = 0;
 p->slope = 0;
}

/* Rule 1, continued: one clock per fit. Every deck of this unit runs from the one audio crystal, so this unit's
   fit holds for all of them. A master on another device is another clock: its fit starts empty (an EXTERNAL
   master with a new epoch is taken as a new device), and this unit's comes back when the master does. */
static void use_clock(struct xz_link_policy *p, const struct xz_link_master *m)
{
 int external = m->source == XZ_LINK_SOURCE_EXTERNAL;
 if (external && (p->clock != XZ_LINK_CLOCK_EXTERNAL || p->clock_epoch != m->epoch)) {
  if (p->clock == XZ_LINK_CLOCK_UNIT) p->unit = p->fit;
  memset(&p->fit, 0, sizeof p->fit);
  p->shown = 0;
  p->clock = XZ_LINK_CLOCK_EXTERNAL;
 } else if (!external && p->clock == XZ_LINK_CLOCK_EXTERNAL) {
  p->fit = p->unit;
  p->shown = 0;
  p->clock = XZ_LINK_CLOCK_UNIT;
 }
 p->clock_epoch = m->epoch;
}

/* Adds the point at at_us whose residual lies inc_us after the previous point's. NAN breaks the segment: the
   interval says nothing about the clock (a seek, a loop jump, a new master, a BPM change, a gap the phase cannot
   be followed across), and the next segment may sit at any offset, so the samples either side of a break cannot
   bias the slope. */
static void fit_add(struct xz_link_ratio *q, uint64_t at_us, double inc_us)
{
 if (q->any && isnan(inc_us)) {
  if (q->s0 > 0) {
   q->puu += q->suu - q->su * q->su / q->s0;
   q->pur += q->sur - q->su * q->sr / q->s0;
  }
  q->s0 = q->su = q->suu = q->sr = q->sur = 0;
 } else if (q->any) {
  double d = (double)(int64_t)(at_us - q->last_us) / 1e6, w = exp(-d / XZ_LINK_RATIO_TAU_S);
  /* Moves the origin to the new point (u -> u - d, residual -> residual - inc_us) and ages everything by d. */
  double s0 = q->s0, su = q->su, sr = q->sr;
  q->suu = w * (q->suu - 2 * d * su + d * d * s0);
  q->sur = w * (q->sur - d * sr - inc_us * su + d * inc_us * s0);
  q->su = w * (su - d * s0);
  q->sr = w * (sr - inc_us * s0);
  q->s0 = w * s0;
  q->puu *= w;
  q->pur *= w;
  q->seconds += d;
 }
 q->any = 1;
 q->last_us = at_us;
 q->s0 += 1;
 double uu = q->puu + q->suu - q->su * q->su / q->s0, ur = q->pur + q->sur - q->su * q->sr / q->s0;
 if (q->seconds >= RATIO_MIN_S && uu > 0) q->ratio = clamp(ur / uu / 1e6, XZ_LINK_RATIO_MAX);
}

/* The tempo peers run at for a commanded one: whole microseconds per beat. */
static double quantized(double bpm)
{
 return 60e6 / (double)llround(60e6 / bpm);
}

static long shown_of(double bpm)
{
 return lround(quantized(bpm) * 100.0);
}

/* Whether bpm lies within SHOWN_NEAR of the tempos that show `shown`. */
static int near_shown(double bpm, long shown)
{
 double slack = SHOWN_NEAR * bpm;
 return bpm >= (shown - 0.5) / 100.0 - slack && bpm <= (shown + 0.5) / 100.0 + slack;
}

/* The quantized tempo nearest to bpm that shows between lo and hi (in 0.01 BPM). */
static double within_shown(double bpm, long lo, long hi)
{
 double low = (lo - 0.5) / 100.0, high = (hi + 0.5) / 100.0;
 double t = bpm < low ? low : bpm > high ? high : bpm;
 double u = (double)llround(60e6 / t);
 for (int i = 0; i < 4 && lround(60e6 / u * 100.0) > hi; i++) u += 1;
 for (int i = 0; i < 4 && lround(60e6 / u * 100.0) < lo; i++) u -= 1;
 return 60e6 / u;
}

/* Rule 2: the pull for a filtered phase error, relative. */
static double absorb(double ef_us)
{
 double a = fabs(ef_us);
 double pull = KA * (a < ABSORB_US ? a : ABSORB_US);
 return clamp(copysign(pull, ef_us) / 1e6, XZ_LINK_PULL_MAX);
}

/* Rule 3: the tempo to command for `desired` while a shown value is held. `target` is the master's tempo on the
   system clock. ef_us is the filtered phase error, 0 when no change for phase may be made. */
static double hold_shown(struct xz_link_policy *p, double bpm, double target, double desired, double ef_us, uint64_t now)
{
 int dwelt = !p->crossed || now - p->crossed_us >= DWELL_US;
 if (!p->shown || fabs(bpm - p->shown_bpm) > DJ_MOVE * bpm || (dwelt && !near_shown(target, p->shown))) {
  /* At a boundary after a DJ tempo move, the side that pays back the phase the move left behind. */
  long s = shown_of(target);
  if (p->move_dir && near_shown(target, s + p->move_dir)) s += p->move_dir;
  p->move_dir = 0;
  p->shown = s;
  p->shown_bpm = bpm;
  p->crossed = 0;
  dwelt = 1;
 }
 /* Compared as shown values: Link's own rounding to whole microseconds per beat is not the hold. */
 long want = shown_of(desired);
 int dir = want > p->shown ? 1 : want < p->shown ? -1 : 0;
 /* A session behind the master (ef < 0) needs a faster tempo: only that direction pays the phase back. Only to a
    neighbour the master's tempo is near (a boundary): one further away would hold the tempo tens of ppm off it
    for the whole dwell. A target inside its shown value never needs to leave it; the error is pulled back inside. */
 if (dir && fabs(ef_us) > CROSS_US && (ef_us < 0) == (dir > 0) && dwelt && near_shown(target, p->shown + dir)) {
  p->shown += dir;
  p->shown_bpm = bpm;
  p->crossed = 1;
  p->crossed_us = now;
  p->crossings++;
 }
 return within_shown(desired, p->shown, p->shown);
}

static int hold(struct xz_link_policy *p, int why)
{
 p->hold = why;
 return 0;
}

static int emit(struct xz_link_policy *p, uint64_t now, int kind, double tempo, double beat, uint64_t at,
                struct xz_link_command *out)
{
 if (!++p->serial) p->serial = 1;
 out->kind = kind;
 out->serial = p->serial;
 out->tempo_bpm = tempo;
 out->beat = beat;
 out->at_us = at;
 p->pending = 1;
 p->sent_us = now;
 return 1;
}

int xz_link_policy_step(struct xz_link_policy *p, const struct xz_link_master *m, const struct xz_link_view *l,
                        uint64_t now, struct xz_link_command *out)
{
 memset(out, 0, sizeof *out);
 if (!l || !l->valid || !l->enabled || !(l->tempo_bpm > 0) || l->at_us > now + 100000u ||
     now - l->at_us > XZ_LINK_STATE_STALE_US) {
  p->pending = 0;
  forget(p);
  return hold(p, XZ_LINK_HOLD_NO_LINK);
 }
 if (p->pending) {
  if (l->applied_serial == p->serial) p->pending = 0;
  else if (now - p->sent_us < XZ_LINK_ACK_US) return hold(p, XZ_LINK_HOLD_PENDING);
  else p->pending = 0;
 }
 if (!m || !m->valid || !(m->bpm > 0)) {
  forget(p);
  return hold(p, XZ_LINK_HOLD_NO_MASTER);
 }
 if (!m->playing) {
  forget(p);
  return hold(p, XZ_LINK_HOLD_PAUSED);
 }
 if (m->manual) {
  forget(p);
  return hold(p, XZ_LINK_HOLD_MANUAL);
 }
 /* A stock sample is stamped when it is heard, output latency after it was read: it may lie ahead of now. */
 int64_t age = (int64_t)(now - m->at_us);
 if (age < -(int64_t)XZ_LINK_AHEAD_US || age >= (int64_t)XZ_LINK_FRESH_US) {
  p->stale++;
  return hold(p, XZ_LINK_HOLD_STALE);
 }
 if (m->uncertainty_us >= XZ_LINK_UNCERTAIN_US) {
  p->uncertain++;
  return hold(p, XZ_LINK_HOLD_UNCERTAIN);
 }
 int same = p->have && m->source == p->source && m->epoch == p->epoch;
 if (same && m->at_us <= p->last_at_us) return hold(p, XZ_LINK_HOLD_REPEAT);
 if (!same) forget(p);
 use_clock(p, m);
 double dt = same ? (double)(m->at_us - p->last_at_us) / 1e6 : 0;
 double slope = same && dt > 0.005 ? (m->bpm - p->last_bpm) / dt : 0, moving = MOVING_PER_S * m->bpm;
 /* Aim ahead only on a ramp seen twice: a single jump of the pitch fader is a step, not a slope. */
 int ramp = fabs(slope) > moving && fabs(p->slope) > moving && slope * p->slope > 0 && fabs(slope) < 2 * fabs(p->slope) &&
            fabs(p->slope) < 2 * fabs(slope);
 int bending = fabs(slope) > moving, dj_move = same && fabs(m->bpm - p->last_bpm) > 1e-6 * m->bpm;
 /* A faster master leaves the session behind until the new tempo reaches it: it then needs a faster tempo still. */
 if (dj_move) p->move_dir = m->bpm > p->last_bpm ? 1 : -1;
 /* Rule 1: the master's phase residual over this interval, when the interval can say anything about its clock. */
 double inc_us = NAN;
 /* Rule 4: did the master move by what its tempo says since the last observation? */
 int jumped = 0;
 if (same && dt > 0) {
  double run_us = wrap_bar(m->bar_phase - p->last_phase - (m->bpm + p->last_bpm) / 2 * dt / 60.0) * 60e6 / m->bpm;
  jumped = fabs(run_us) > RUN_TOL_US;
  if (jumped) p->not_running++;
  memmove(p->run_us + 1, p->run_us, sizeof p->run_us - sizeof p->run_us[0]);
  p->run_us[0] = jumped ? 0 : run_us;
 }
 if (same && dt > 0 && m->bpm == p->last_bpm) {
  double moved_us = wrap_bar(m->bar_phase - p->last_phase - m->bpm * dt / 60.0) * 60e6 / m->bpm;
  if (fabs(moved_us - p->fit.ratio * dt * 1e6) <= RATIO_JUMP_US) inc_us = moved_us;
 }
 fit_add(&p->fit, m->at_us, inc_us);
 p->ratio = p->fit.seconds >= RATIO_MIN_S ? p->fit.ratio : 0;
 p->learnt_s = p->fit.seconds;
 p->slope = slope;
 p->have = 1;
 p->source = m->source;
 p->epoch = m->epoch;
 p->last_at_us = m->at_us;
 p->last_bpm = m->bpm;
 p->last_phase = m->bar_phase;
 p->used++;
 p->hold = XZ_LINK_HOLD_NONE;
 /* Rule 4: a seek holds for one pass; a master that stands still holds until it moves again. */
 if (jumped) {
  p->steps = 0;
  return hold(p, XZ_LINK_HOLD_NOT_RUNNING);
 }

 double session = l->beat + (double)(int64_t)(m->at_us - l->at_us) * l->tempo_bpm / 60e6;
 double e_beats = wrap_bar(session - m->bar_phase);
 double e_us = e_beats * 60e6 / m->bpm;
 p->error_us = e_us;
 double ahead = ramp ? slope * ((double)age + LEAD_US) / 1e6 : 0;
 double target = m->bpm * (1 + p->ratio);
 int learnt = p->learnt_s >= XZ_LINK_LEARN_S, held = learnt && !bending && !p->released;
 double desired;
 if (!held) p->shown = 0;
 if (fabs(e_us) > XZ_LINK_STEP_US) {
  /* Not fed to the filter: one wild sample says nothing about the phase; a real step is forced. */
  desired = target + ahead;
  if (held) desired = hold_shown(p, m->bpm, target, desired, 0, now);
  double crept = 0;
  for (unsigned i = 0; i < RUN_WINDOW; i++) crept += p->run_us[i];
  if (++p->steps >= XZ_LINK_STEP_COUNT && fabs(crept) <= RUN_TOL_US &&
      (!p->force_commands || now - p->last_force_us >= XZ_LINK_FORCE_GAP_US)) {
   p->steps = 0;
   p->filtered = 0;
   p->last_force_us = now;
   p->force_commands++;
   p->sent_master_bpm = m->bpm;
   p->tempo_sent_us = now;
   return emit(p, now, XZ_LINK_CMD_FORCE, quantized(desired), session - e_beats, m->at_us, out);
  }
 } else {
  p->steps = 0;
  if (!p->filtered) {
   p->error_f_us = e_us;
   p->filtered = 1;
  } else {
   /* A sample after a gap weighs no more than one after DT_MAX_S: a stall must not swing the filter. */
   double a = (dt > DT_MAX_S ? DT_MAX_S : dt) * 1e6 / FILTER_US;
   p->error_f_us += (e_us - p->error_f_us) * a;
  }
  double ef = p->error_f_us;
  /* Rule 3 yields to a phase error beyond the steady bound: that is recovered at full pull and the shown value
     follows it; the hold comes back on the master's own shown value once the error is under ABSORB_US. */
  if (learnt && fabs(ef) > FAST_US) p->released = 1;
  else if (fabs(ef) < ABSORB_US) p->released = 0;
  held = learnt && !bending && !p->released;
  if (!held) p->shown = 0;
  if (dj_move) {
   p->recovering = 1;
   p->recover_us = now;
  } else if (p->recovering && now - p->recover_us >= RECOVER_MIN_US &&
             (fabs(ef) < RECOVERED_US || now - p->recover_us >= RECOVER_MAX_US)) {
   p->recovering = 0;
   p->move_dir = 0;
  }
  double pull = learnt && !p->released && !p->recovering ? absorb(ef) : clamp(KP * ef / 1e6, XZ_LINK_PULL_MAX);
  desired = m->bpm * (1 + p->ratio - pull) + ahead;
  if (held) desired = hold_shown(p, m->bpm, target, desired, ef, now);
 }
 double off = fabs(desired - l->tempo_bpm) / desired;
 int moved = fabs(m->bpm - p->sent_master_bpm) > DJ_MOVE * m->bpm;
 if (off > FAR || (off > (p->shown ? NEAR_HELD : NEAR) && (moved || ramp || now - p->tempo_sent_us >= TRIM_GAP_US))) {
  p->sent_master_bpm = m->bpm;
  p->tempo_sent_us = now;
  p->tempo_commands++;
  return emit(p, now, XZ_LINK_CMD_TEMPO, quantized(desired), 0, 0, out);
 }
 return 0;
}
