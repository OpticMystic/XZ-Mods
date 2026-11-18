/* SPDX-License-Identifier: MIT */
/* Measures, from the reference Link peer's side, the bar-phase difference between the Link session and the lab's
   simulated master (truth.h). Every 10 ms it reads the reference helper's record and the truth record, takes
   both to the same CLOCK_MONOTONIC instant and wraps the difference to +-2 beats.

   A sample is VOID when this program woke late (> 30 ms since the previous one) or the reference record is older
   than 40 ms: a stalled reader or helper says nothing about the session. Samples within `settle_s` of a scripted
   event, and before `warm_s`, are not steady.

   Usage: measure <reference-record> <truth> <seconds> <warm_s> <settle_s> <csv> <summary> */
#define _GNU_SOURCE
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

static int64_t mono_us(void)
{
 struct timespec t;
 clock_gettime(CLOCK_MONOTONIC, &t);
 return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static void *map(const char *path, size_t size)
{
 for (int i = 0; i < 200; i++) {
  int fd = open(path, O_RDONLY);
  if (fd >= 0) {
   void *m = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
   close(fd);
   if (m != MAP_FAILED) return m;
  }
  usleep(50000);
 }
 return NULL;
}

static int cmp(const void *a, const void *b)
{
 double x = *(const double *)a, y = *(const double *)b;
 return x < y ? -1 : x > y;
}

#define MAX 100000
static double steady_abs[MAX];

int main(int argc, char **argv)
{
 if (argc != 8) {
  fprintf(stderr, "usage: measure <record> <truth> <seconds> <warm_s> <settle_s> <csv> <summary>\n");
  return 2;
 }
 double seconds = atof(argv[3]), warm_s = atof(argv[4]), settle_s = atof(argv[5]);
 const struct xz_link_shm *ref = map(argv[1], sizeof *ref);
 const struct xz_lab_truth *truth = map(argv[2], sizeof *truth);
 FILE *csv = fopen(argv[6], "w"), *sum = fopen(argv[7], "w");
 if (!ref || !truth || !csv || !sum) return 2;
 while (__atomic_load_n(&truth->magic, __ATOMIC_ACQUIRE) != XZ_LAB_TRUTH_MAGIC) usleep(10000);
 fprintf(csv, "t_s,error_us,ref_tempo,ref_peers,true_bpm,steady,void\n");
 int64_t start = mono_us(), last = 0;
 unsigned n = 0, total = 0, voids = 0, not_linked = 0, shown_changes = 0, steady_n = 0;
 double sum_err = 0, max_abs = 0, min_tempo = 1e9, max_tempo = 0, max_tempo_dev = 0;
 long last_shown = 0;
 struct timespec next;
 clock_gettime(CLOCK_MONOTONIC, &next);
 while (mono_us() - start < (int64_t)(seconds * 1e6)) {
  next.tv_nsec += 10000000L;
  if (next.tv_nsec >= 1000000000L) {
   next.tv_nsec -= 1000000000L;
   next.tv_sec++;
  }
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR) {
  }
  int64_t now = mono_us();
  struct xz_link_state st;
  struct xz_lab_truth_rec tr;
  int late = last && now - last > 30000;
  last = now;
  total++;
  if (!xz_link_seq_read(&ref->state, &st, sizeof st, 64) || !xz_link_seq_read(&truth->rec, &tr, sizeof tr, 64)) {
   voids++;
   continue;
  }
  int stale = now - st.monotonic_us > 40000;
  if (late || stale) {
   voids++;
   fprintf(csv, "%.3f,,,,,,1\n", (now - start) / 1e6);
   continue;
  }
  if (st.peers == 0) {
   not_linked++;
   continue;
  }
  double session = xz_link_beat_at(&st, now);
  double master = tr.ref_phase + ((double)now - (double)tr.ref_us) * tr.true_bpm / 60e6;
  double e = fmod(session - master + 2.0, 4.0);
  if (e < 0) e += 4.0;
  e -= 2.0;
  double e_us = e * 60e6 / tr.true_bpm;
  double t = (now - start) / 1e6;
  int steady = t >= warm_s && (double)(now - (int64_t)tr.disturb_us) >= settle_s * 1e6;
  fprintf(csv, "%.3f,%.1f,%.6f,%u,%.6f,%d,0\n", t, e_us, st.tempo_bpm, st.peers, tr.true_bpm, steady);
  if (!steady) continue;
  steady_n++;
  if (n < MAX) steady_abs[n++] = fabs(e_us);
  sum_err += e_us;
  if (fabs(e_us) > max_abs) max_abs = fabs(e_us);
  if (st.tempo_bpm < min_tempo) min_tempo = st.tempo_bpm;
  if (st.tempo_bpm > max_tempo) max_tempo = st.tempo_bpm;
  double dev = fabs(st.tempo_bpm - tr.true_bpm);
  if (dev > max_tempo_dev) max_tempo_dev = dev;
  long shown = lround(st.tempo_bpm * 100.0);
  if (last_shown && shown != last_shown) shown_changes++;
  last_shown = shown;
 }
 qsort(steady_abs, n, sizeof steady_abs[0], cmp);
 double abs_sum = 0;
 for (unsigned i = 0; i < n; i++) abs_sum += steady_abs[i];
 fprintf(sum, "start_us %lld\nsamples %u\nvoid %u\nnot_linked %u\nsteady %u\n", (long long)start, total, voids, not_linked,
         steady_n);
 fprintf(sum, "mean_us %.1f\nmean_abs_us %.1f\np99_us %.1f\nmax_us %.1f\n", steady_n ? sum_err / steady_n : 0.0,
         n ? abs_sum / n : 0.0, n ? steady_abs[(unsigned)(n * 0.99) < n ? (unsigned)(n * 0.99) : n - 1] : 0.0, max_abs);
 fprintf(sum, "ref_tempo_min %.6f\nref_tempo_max %.6f\nref_tempo_dev_max %.6f\nref_shown_last %.2f\nref_shown_changes %u\n",
         min_tempo, max_tempo, max_tempo_dev, last_shown / 100.0, shown_changes);
 fclose(sum);
 fclose(csv);
 return 0;
}
