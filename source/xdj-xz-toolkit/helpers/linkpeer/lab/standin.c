/* SPDX-License-Identifier: MIT */
/* Player stand-in for the Ableton Link lab: the REAL player half (tree mods/link_runtime.c, link_policy.c,
   link_clock.c) runs against a simulated stock master clock, every 40 ms as on the unit's network worker.
   The runtime creates the record and starts the real xz-link-peer by vfork+execve from XZ_LINK_DIR.

   The simulated master: deck 1, 128.00 BPM by its own clock, which runs `drift` off CLOCK_MONOTONIC. Its bar
   phase is sampled every 33-40 ms (irregular), the sample time reported with +-0.5 ms jitter and 0.3-0.8 ms
   uncertainty, the way the stock interface thread samples the stock clock. Script: a seek of +1.37 beats at
   `seek_s`, a tempo change of `tempo_pct` at `tempo_s`.

   Truth goes to a small record (truth.h) that the measuring program in the other namespace reads.

   With `gaps` 1 the master's observations stop for 0.3-1.0 s every 5-40 s (the cached sample stays as it was, as
   when the stock interface thread is held up), never within 1.5 s before or 2 s after the seek or the tempo change,
   whose own checks need the observations. The gaps go to the result file (obs_gaps_s, start:length in seconds from
   the start), as do the stand-in's own service passes that came more than 100 ms late (service_gaps_s, the time the
   late pass ran and how long the gap before it was). `trace` gets one line per service pass.

   Usage: standin <seconds> <drift_ppm> <seek_s> <tempo_s> <tempo_pct> <truth-path> <log-path> <result-path>
                  [gaps 0|1] [trace.csv]

   Track end (environment XZ_LAB_FREEZE_S and XZ_LAB_FREEZE_LEN_S, run_track_end.sh): from XZ_LAB_FREEZE_S the master
   keeps reporting playing and its BPM while its bar phase stands still, as the stock deck does at the end of its
   track (seen on the unit, r29); after XZ_LAB_FREEZE_LEN_S the next track starts (a new source revision, the bar
   phase 1.7 beats on). The result file gets freeze_s and resume_s. */
#define _GNU_SOURCE
#include "link_runtime.h"
#include "link_policy.h"
#include "truth.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static FILE *lab_log;
void xz_log(const char *line)
{
 struct timespec t;
 clock_gettime(CLOCK_MONOTONIC, &t);
 fprintf(lab_log, "%ld.%06ld %s\n", (long)t.tv_sec, t.tv_nsec / 1000, line);
 fflush(lab_log);
}

static uint64_t mono_us(void)
{
 struct timespec t;
 clock_gettime(CLOCK_MONOTONIC, &t);
 return (uint64_t)t.tv_sec * 1000000u + (uint64_t)(t.tv_nsec / 1000);
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static double uni(void)
{
 rng ^= rng >> 12;
 rng ^= rng << 25;
 rng ^= rng >> 27;
 return (double)((rng * 2685821657736338717ull) >> 11) / 9007199254740992.0;
}

/* ---- the simulated master: piecewise linear phase in CLOCK_MONOTONIC -------------------------------------- */
static struct xz_lab_truth *truth;
static struct { uint64_t ref_us; double ref_phase, bpm, drift; uint32_t revision; } master;
static uint64_t next_sample_us, sample_us;
static uint64_t freeze_from = UINT64_MAX, freeze_to = UINT64_MAX; /* track end, CLOCK_MONOTONIC */
static double frozen_phase;
static struct xz_sync_native_snapshot cache;
/* Injected observation gaps (`gaps` 1): no new samples in [gap_from, gap_to). */
static int gaps_on;
static uint64_t gap_from, gap_to, gap_start_us;
static double gap_seek_s, gap_tempo_s;
static char gap_list[2048];
static uint64_t rng2 = 0x2545f4914f6cdd1dull;
static double uni2(void)
{
 rng2 ^= rng2 >> 12;
 rng2 ^= rng2 << 25;
 rng2 ^= rng2 >> 27;
 return (double)((rng2 * 2685821657736338717ull) >> 11) / 9007199254740992.0;
}
static int near_event(double t)
{
 return (t + 1.5 >= gap_seek_s && t < gap_seek_s + 2.0) || (t + 1.5 >= gap_tempo_s && t < gap_tempo_s + 2.0);
}
static void plan_gap(uint64_t after)
{
 for (;;) {
  gap_from = after + (uint64_t)((5.0 + uni2() * 35.0) * 1e6);
  gap_to = gap_from + (uint64_t)((0.3 + uni2() * 0.7) * 1e6);
  double a = (gap_from - gap_start_us) / 1e6, b = (gap_to - gap_start_us) / 1e6;
  if (!near_event(a) && !near_event(b) && !(a < gap_seek_s && b > gap_seek_s) && !(a < gap_tempo_s && b > gap_tempo_s)) break;
  after = gap_to;
 }
 size_t n = strlen(gap_list);
 snprintf(gap_list + n, sizeof gap_list - n, " %.3f:%.3f", (gap_from - gap_start_us) / 1e6, (gap_to - gap_from) / 1e6);
}

static double true_bpm(void) { return master.bpm * (1 + master.drift); }
static double phase_at(uint64_t t) { return master.ref_phase + ((double)t - (double)master.ref_us) * true_bpm() / 60e6; }

static void publish_truth(uint64_t disturb_us)
{
 struct xz_lab_truth_rec r;
 memset(&r, 0, sizeof r);
 r.ref_us = master.ref_us;
 r.ref_phase = master.ref_phase;
 r.true_bpm = true_bpm();
 r.nominal_bpm = master.bpm;
 r.disturb_us = disturb_us;
 r.events = truth->rec.events + 1;
 xz_link_seq_write(&truth->rec, &r, sizeof r);
}

/* Re-anchors the phase line at now, then applies a change. */
static void rebase(uint64_t now)
{
 master.ref_phase = phase_at(now);
 master.ref_us = now;
}

static int stock_clock(struct xz_sync_native_snapshot *s)
{
 uint64_t now = mono_us();
 if (gaps_on && now >= gap_to) plan_gap(gap_to);
 if (gaps_on && now >= gap_from) {
  /* Inside a gap: no new sample; the samples the gap swallowed are never taken. */
  while (next_sample_us <= now) next_sample_us += 33000u + (uint64_t)(uni() * 7000.0);
  *s = cache;
  return 1;
 }
 while (now >= next_sample_us) {
  sample_us = next_sample_us;
  double jitter = (uni() - 0.5) * 1000.0;
  memset(&cache, 0, sizeof cache);
  cache.available = cache.phase_available = 1;
  cache.native_deck = 0;
  cache.master_id = 0;
  cache.bpm = (float)master.bpm;
  double p = fmod(sample_us >= freeze_from && sample_us < freeze_to ? frozen_phase : phase_at(sample_us), 4.0);
  cache.bar_phase = (float)(p < 0 ? p + 4.0 : p);
  cache.captured_us = (uint64_t)((double)sample_us + jitter);
  cache.captured_ms = (uint32_t)(cache.captured_us / 1000u);
  cache.uncertainty_us = 300u + (uint32_t)(uni() * 500.0);
  cache.playing = 1;
  cache.source_revision = master.revision;
  cache.track_identity = 4242;
  next_sample_us += 33000u + (uint64_t)(uni() * 7000.0);
 }
 *s = cache;
 return 1;
}

static int observe_none(struct xz_master_observation *o)
{
 memset(o, 0, sizeof *o);
 o->deck = XZ_MASTER_UNKNOWN;
 return 0;
}

int main(int argc, char **argv)
{
 if (argc < 9 || argc > 11) {
  fprintf(stderr, "usage: standin <seconds> <drift_ppm> <seek_s> <tempo_s> <tempo_pct> <truth> <log> <result> [gaps] [trace]\n");
  return 2;
 }
 gaps_on = argc > 9 && atoi(argv[9]) == 1;
 FILE *trace = argc > 10 ? fopen(argv[10], "w") : NULL;
 if (trace) fprintf(trace, "t_s,hold,phase_error_us,session_bpm,tempo_commands,force_commands,obs_gap\n");
 double seconds = atof(argv[1]), drift_ppm = atof(argv[2]), seek_s = atof(argv[3]), tempo_s = atof(argv[4]),
        tempo_pct = atof(argv[5]);
 lab_log = fopen(argv[7], "w");
 FILE *result = fopen(argv[8], "w");
 if (!lab_log || !result) return 2;
 int fd = open(argv[6], O_RDWR | O_CREAT | O_TRUNC, 0644);
 if (fd < 0 || ftruncate(fd, sizeof(struct xz_lab_truth))) return 2;
 truth = mmap(NULL, sizeof *truth, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 close(fd);
 if (truth == MAP_FAILED) return 2;
 memset(truth, 0, sizeof *truth);

 uint64_t start = mono_us();
 master.ref_us = start;
 master.ref_phase = 0.37;
 master.bpm = 128.0;
 master.drift = drift_ppm * 1e-6;
 master.revision = 1;
 next_sample_us = start + 5000;
 gap_start_us = start;
 gap_seek_s = seek_s;
 gap_tempo_s = tempo_s;
 gap_from = gap_to = UINT64_MAX;
 if (gaps_on) plan_gap(start);
 publish_truth(start);
 truth->magic = XZ_LAB_TRUTH_MAGIC;

 static const struct xz_link_test_ops ops = {.observe = observe_none, .stock_clock = stock_clock};
 xz_link_runtime_test_install(&ops);
 xz_link_runtime_start();
 xz_link_runtime_set_enabled(1);

 const char *fz = getenv("XZ_LAB_FREEZE_S"), *fl = getenv("XZ_LAB_FREEZE_LEN_S");
 double freeze_s = fz ? atof(fz) : -1, freeze_len_s = fl ? atof(fl) : 30.0;
 int frozen = 0, resumed = 0;
 int seeked = 0, tempo_done = 0;
 uint64_t max_gap = 0, gaps_over_100 = 0, passes = 0, last = 0;
 uint32_t forces_seen = 0;
 char forces[512] = "", late[4096] = "";
 struct timespec next;
 clock_gettime(CLOCK_MONOTONIC, &next);
 for (;;) {
  uint64_t now = mono_us();
  if (last) {
   uint64_t gap = now - last;
   if (gap > max_gap) max_gap = gap;
   if (gap > 100000) {
    gaps_over_100++;
    size_t n = strlen(late);
    snprintf(late + n, sizeof late - n, " %.3f:%.3f", (now - start) / 1e6, gap / 1e6);
   }
  }
  last = now;
  double t = (now - start) / 1e6;
  if (t >= seconds) break;
  if (!seeked && t >= seek_s) {
   seeked = 1;
   rebase(now);
   master.ref_phase += 1.37;
   publish_truth(now);
   xz_log("LAB: seek +1.37 beats");
  }
  if (!tempo_done && t >= tempo_s) {
   tempo_done = 1;
   rebase(now);
   master.bpm *= 1 + tempo_pct / 100.0;
   publish_truth(now);
   xz_log("LAB: tempo change");
  }
  if (freeze_s >= 0 && !frozen && t >= freeze_s) {
   frozen = 1;
   frozen_phase = phase_at(now);
   freeze_from = now;
   freeze_to = now + (uint64_t)(freeze_len_s * 1e6);
   xz_log("LAB: track end, the master stands still and says it plays");
  }
  if (frozen && !resumed && now >= freeze_to) {
   resumed = 1;
   master.ref_us = now;
   master.ref_phase = frozen_phase + 1.7;
   master.revision++;
   publish_truth(now);
   xz_log("LAB: next track starts");
  }
  xz_link_runtime_service();
  passes++;
  struct xz_link_status now_status;
  int have_status = xz_link_runtime_status(&now_status);
  if (have_status && now_status.force_commands != forces_seen) {
   forces_seen = now_status.force_commands;
   size_t n = strlen(forces);
   snprintf(forces + n, sizeof forces - n, " %.3f", (mono_us() - start) / 1e6);
  }
  if (trace && have_status) {
   uint64_t tn = mono_us();
   fprintf(trace, "%.3f,%d,%d,%.6f,%u,%u,%d\n", (tn - start) / 1e6, now_status.hold, now_status.phase_error_us,
           now_status.session_bpm, now_status.tempo_commands, now_status.force_commands, gaps_on && tn >= gap_from && tn < gap_to);
  }
  next.tv_nsec += XZ_LINK_SERVICE_MS * 1000000L;
  if (next.tv_nsec >= 1000000000L) {
   next.tv_nsec -= 1000000000L;
   next.tv_sec++;
  }
  /* After a stall do not run a burst of passes. */
  struct timespec now_ts;
  clock_gettime(CLOCK_MONOTONIC, &now_ts);
  if ((now_ts.tv_sec - next.tv_sec) * 1000000000L + (now_ts.tv_nsec - next.tv_nsec) > 0) next = now_ts;
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR) {
  }
 }
 struct xz_link_status s;
 while (!xz_link_runtime_status(&s)) usleep(1000);
 fprintf(result, "passes %llu\nmax_service_gap_us %llu\nservice_gaps_over_100ms %llu\n", (unsigned long long)passes,
         (unsigned long long)max_gap, (unsigned long long)gaps_over_100);
 fprintf(result, "state %d\nhelper_starts %u\npeers %u\nsession_bpm %.6f\ntempo_commands %u\nforce_commands %u\nsource %d\nhold %d\n"
                 "phase_error_us %d\nsoft_publishes %u\n",
         (int)s.state, s.helper_starts, s.peers, s.session_bpm, s.tempo_commands, s.force_commands, s.source, s.hold,
         s.phase_error_us, s.soft_publishes);
 fprintf(result, "start_us %llu\nforce_times_s%s\nservice_gaps_s%s\nobs_gaps %d\nobs_gaps_s%s\n", (unsigned long long)start,
         forces, late, gaps_on, gaps_on ? gap_list : "");
 if (freeze_s >= 0) fprintf(result, "freeze_s %.3f\nresume_s %.3f\n", freeze_s, freeze_s + freeze_len_s);
 xz_link_runtime_set_enabled(0);
 for (int i = 0; i < 50; i++) {
  unsigned wake = xz_link_runtime_service();
  if (!wake) break;
  usleep(40000);
 }
 xz_link_runtime_status(&s);
 fprintf(result, "after_off_state %d\nafter_off_shm_ready %d\n", (int)s.state, s.shm_ready);
 xz_link_runtime_stop();
 if (trace) fclose(trace);
 fclose(result);
 fclose(lab_log);
 return 0;
}
