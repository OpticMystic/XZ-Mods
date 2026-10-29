/* SPDX-License-Identifier: MIT */
/* Replaces clock_compat.h inside one simulated unit: every CLOCK_MONOTONIC and
 * CLOCK_REALTIME read returns the shared simulated time. Include it before the
 * real module so the module's own include guard skips the syscall version. */
#ifndef SYNC_MASTER_SIM_CLOCK_H
#define SYNC_MASTER_SIM_CLOCK_H
#define XZ_CLOCK_COMPAT_H
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "sync_master_sim.h"
static inline int xz_clock_gettime_compat(clockid_t clock,struct timespec *out){
 if(!out){errno=EFAULT;return -1;}
 uint64_t us=sim_us+(clock==CLOCK_REALTIME?UINT64_C(1700000000000000):0);
 out->tv_sec=(time_t)(us/1000000u);out->tv_nsec=(long)(us%1000000u)*1000L;
 return 0;
}
#endif
