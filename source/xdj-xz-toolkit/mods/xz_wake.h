/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_WAKE_H
#define XZ_WAKE_H
/* Waking a worker from a thread that must not wait.
 *
 * pthread_cond_signal and pthread_cond_wait both take the condition variable's internal lock, a plain lock
 * without priority inheritance. A low-priority waiter that loses the processor inside pthread_cond_wait keeps
 * that lock, and a real-time thread that signals then sleeps until the waiter runs again. Measured on build
 * r33: each DECK 3/4 thread signalled an idle-priority worker every 2 ms; while the stem workers kept cores 0
 * and 2 busy the worker did not run, and the deck threads waited 15 to 170 ms at a time (about one second of
 * lost audio per pair of loads).
 *
 * xz_wake has no lock. set() adds one to a counter and wakes with one private futex call (no memory-map lock,
 * nothing a preempted thread can hold). The waiter takes a ticket while it still holds its own mutex, releases
 * the mutex, and waits for the counter to move on from that ticket, so a set() in between is never lost:
 *
 *     lock(m); ...nothing to do...; uint32_t ticket = xz_wake_ticket(&w); unlock(m); xz_wake_wait(&w, ticket, 250);
 *
 * Rule: a thread at real-time priority never signals a condition variable that a lower-priority thread waits
 * on. It calls xz_wake_set, and only when there is something new to do. */
#include <stdint.h>
#include <time.h>
struct xz_wake { uint32_t count; };
static inline uint32_t xz_wake_ticket(const struct xz_wake *w) { return __atomic_load_n(&w->count, __ATOMIC_ACQUIRE); }
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
long syscall(long number, ...);
#define XZ_WAKE_FUTEX_WAIT_PRIVATE 128
#define XZ_WAKE_FUTEX_WAKE_PRIVATE 129
static inline void xz_wake_set(struct xz_wake *w) {
    __atomic_add_fetch(&w->count, 1, __ATOMIC_RELEASE);
    syscall(SYS_futex, &w->count, XZ_WAKE_FUTEX_WAKE_PRIVATE, 0x7fffffff, NULL, NULL, 0);
}
/* Returns when the counter no longer equals the ticket, after about <ms> milliseconds, or on a signal.
 * The limit is the kernel's own layout for SYS_futex, two longs, not struct timespec: a C library built with
 * 64-bit time_t on 32-bit ARM (any current toolchain) would pass a zero timeout and the wait would spin. */
static inline void xz_wake_wait(struct xz_wake *w, uint32_t ticket, unsigned ms) {
    struct { long seconds, nanoseconds; } limit = {(long)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    syscall(SYS_futex, &w->count, XZ_WAKE_FUTEX_WAIT_PRIVATE, ticket, &limit, NULL, 0);
}
#else
static inline void xz_wake_set(struct xz_wake *w) { __atomic_add_fetch(&w->count, 1, __ATOMIC_RELEASE); }
static inline void xz_wake_wait(struct xz_wake *w, uint32_t ticket, unsigned ms) {
    for (unsigned waited = 0; waited < ms && xz_wake_ticket(w) == ticket; waited += 2) {
        struct timespec nap = {0, 2000000}; nanosleep(&nap, NULL);
    }
}
#endif
/* The usual waiter, in place of pthread_cond_timedwait(&cond, mutex, about <ms> from now): called with
 * <mutex> held after the caller found nothing to do; returns with <mutex> held again. The ticket is taken
 * before the mutex is released, so every xz_wake_set made after a change under <mutex> wakes it. Spurious
 * returns and timeouts are normal: the caller looks at its state again, as it would after a condition wait. */
#include <pthread.h>
static inline void xz_wake_wait_locked(struct xz_wake *w, pthread_mutex_t *mutex, unsigned ms) {
    uint32_t ticket = xz_wake_ticket(w);
    pthread_mutex_unlock(mutex);
    xz_wake_wait(w, ticket, ms);
    pthread_mutex_lock(mutex);
}
#endif
