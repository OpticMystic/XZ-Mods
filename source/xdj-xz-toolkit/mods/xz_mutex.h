/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_MUTEX_H
#define XZ_MUTEX_H
#include <pthread.h>

/* Priority-inheriting mutexes for every lock MODS owns.
 *
 * MODS workers run at normal or idle priority. The stock audio, reader and interface threads and the MODS
 * display threads run at real-time priority, and this kernel never throttles real-time work. With a plain
 * mutex, a worker that is descheduled while it holds a lock keeps every real-time waiter blocked for as long
 * as other real-time threads occupy the worker's core. A priority-inheriting mutex lends the highest waiting
 * priority to the holder until it unlocks, so that wait is bounded by the critical section alone.
 *
 * glibc stores the protocol in the mutex kind (32 is PTHREAD_MUTEX_PRIO_INHERIT_NP, the value
 * pthread_mutex_init writes for PTHREAD_PRIO_INHERIT), so a static initialiser needs no start-up call.
 * xz_mutex_self_check() proves the constant against pthread_mutex_init on the running C library and that
 * the kernel implements FUTEX_LOCK_PI. Uncontended lock and unlock stay in user space as before. */
#if defined(__linux__) && defined(__GLIBC__)
#define XZ_MUTEX_INITIALIZER { .__data = { .__kind = 32 } }
#define XZ_MUTEX_PRIORITY_INHERITING 1
#else
#define XZ_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define XZ_MUTEX_PRIORITY_INHERITING 0
#endif

static inline int xz_mutex_init(pthread_mutex_t *mutex)
{
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes)) return pthread_mutex_init(mutex, NULL);
    int error = pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT);
    if (!error) error = pthread_mutex_init(mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
    return error ? pthread_mutex_init(mutex, NULL) : 0;
}

/* 0 when a statically initialised MODS mutex is exactly what pthread_mutex_init builds for priority
 * inheritance here, and locking one works. Nonzero means the static initialiser must not be trusted. */
static inline int xz_mutex_self_check(void)
{
#if XZ_MUTEX_PRIORITY_INHERITING
    pthread_mutex_t fixed = XZ_MUTEX_INITIALIZER, built;
    pthread_mutexattr_t attributes;
    if (pthread_mutexattr_init(&attributes)) return 1;
    int error = pthread_mutexattr_setprotocol(&attributes, PTHREAD_PRIO_INHERIT);
    if (!error) error = pthread_mutex_init(&built, &attributes);
    pthread_mutexattr_destroy(&attributes);
    if (error) return 2;
    int same = fixed.__data.__kind == built.__data.__kind;
    pthread_mutex_destroy(&built);
    if (!same) return 3;
    if (pthread_mutex_lock(&fixed)) return 4;
    if (pthread_mutex_trylock(&fixed) == 0) return 5;
    if (pthread_mutex_unlock(&fixed)) return 6;
#endif
    return 0;
}
#endif
