/* SPDX-License-Identifier: MIT */
/* The shared recorder poller (recorder_poll.h). */
#define _GNU_SOURCE
#include "recorder_poll.h"
#include "recorder_state.h"

struct xz_rec_reading xz_recorder_poll_at(struct xz_recorder_poller *p, xz_recorder_read_fn read,
                                          uint32_t mono_now_ms, uint32_t caller_now_ms) {
 struct xz_rec_reading r={0};
 if(!p||!read)return r;
 if(!p->polled||mono_now_ms-p->polled_at_ms>=XZ_REC_POLL_MS){
  struct xz_recorder_state s={0};
  if(read(&s))s.known=0;              /* the seam returns -1 when it cannot tell */
  p->reading=xz_rec_observe(&p->tracker,&s,mono_now_ms);
  p->polled_at_ms=mono_now_ms;p->polled=1;
 }
 r=p->reading;
 if(!r.known)return r;
 /* Restamp into the caller's clock: the same instant, the caller's number for it. xz_rec_view_at runs the
    clock on from there (bounded by XZ_REC_EXTRAPOLATE_MS), so a held reading still stops in time. */
 r.read_at_ms=caller_now_ms-(mono_now_ms-r.read_at_ms);
 return r;
}

#ifndef XZ_RECORDER_POLL_HOST
#include "clock_compat.h"
#include "xz_mutex.h"
#include <pthread.h>
static pthread_mutex_t poll_mutex=XZ_MUTEX_INITIALIZER;
static struct xz_recorder_poller poller;
struct xz_rec_reading xz_recorder_reading(uint32_t caller_now_ms) {
 struct timespec t;
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return (struct xz_rec_reading){0};
 uint32_t mono=(uint32_t)((uint64_t)t.tv_sec*1000u+(uint32_t)t.tv_nsec/1000000u);
 pthread_mutex_lock(&poll_mutex);
 struct xz_rec_reading r=xz_recorder_poll_at(&poller,xz_recorder_state,mono,caller_now_ms);
 pthread_mutex_unlock(&poll_mutex);
 return r;
}
#endif
