/* SPDX-License-Identifier: MIT */
#ifndef XZ_CORE_USE_H
#define XZ_CORE_USE_H
/* Core use trail (private diagnostic).
 *
 * A "DECK 3/4 CORE STALL" line says that a real-time loop on the DECK 3/4 core lost tens of milliseconds, not
 * who had the core meanwhile. Nothing throttles real-time threads on this unit, and the stock player keeps
 * threads of its own on that core, some above the DECK 3/4 priorities. This trail keeps, for the last ten
 * seconds, the processor time of every thread of the player that may run on the core, and the kernel's own
 * count of how the core's time divided (user, system, interrupt, soft interrupt, idle, waiting for a disk).
 *
 * sample() is called every 10 ms by the kernel guard's normal-priority canaries (any of them; a call that
 * meets another in progress returns at once) and keeps one sample every 20 ms; it reads a clock per thread
 * and /proc/stat. report() is for the thread that logs the stall: it takes no lock, reads no file and
 * allocates nothing. */
#include <stddef.h>
#include <stdint.h>
#define XZ_CORE_USE_CPU 2u
void xz_core_use_sample(uint64_t now_us);
/* What used the core between two moments (CLOCK_MONOTONIC microseconds), as one line of text: the span the
 * two nearest samples cover, up to eight threads that used a millisecond or more (name, thread id, F/R/n for
 * FIFO, round-robin or normal with the real-time priority, processor time; a trailing * marks a thread that
 * may also run on other cores), the sum over all threads, and the kernel's tick counts for the core. Returns
 * the text's length; 0 while no sample newer than to_us exists (ask again shortly); -1 when the trail no
 * longer holds the span. */
int xz_core_use_report(uint64_t from_us, uint64_t to_us, char *text, size_t size);
#endif
