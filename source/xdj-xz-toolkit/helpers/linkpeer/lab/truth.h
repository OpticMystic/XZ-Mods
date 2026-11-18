/* SPDX-License-Identifier: MIT */
/* The lab's simulated master, as the measuring program sees it: bar phase = ref_phase + (t - ref_us) * true_bpm
   / 60e6 at CLOCK_MONOTONIC t, until the next event. Written by standin.c, read by measure.c, under the same
   sequence lock as the Link record. */
#ifndef XZ_LAB_TRUTH_H
#define XZ_LAB_TRUTH_H
#include "xz_link_shm.h"

#define XZ_LAB_TRUTH_MAGIC 0x48545258u /* "XRTH" */

struct xz_lab_truth_rec {
 uint32_t seq, events;
 uint64_t ref_us;
 double ref_phase, true_bpm, nominal_bpm;
 uint64_t disturb_us;      /* the latest scripted event (start, seek, tempo change) */
};
struct xz_lab_truth {
 uint32_t magic, pad;
 struct xz_lab_truth_rec rec;
};
#endif
