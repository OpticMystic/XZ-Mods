/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_THREAD_H
#define XZ_THREAD_H
#include <pthread.h>
/* Creates a MODS background worker that may run on cores 0, 1 and 2.
 *
 * A new thread inherits its creator's core, and the stock player pins its threads, so most MODS workers were
 * born confined to core 0. During a track load the stock player fills core 0 with real-time work for up to
 * 300 ms. A worker holding a lock there cannot run, and the display thread that needs the lock waits with it
 * (427 ms measured). A worker allowed on three cores is moved to an idle one by the scheduler, and a lent
 * priority (xz_mutex.h) can take effect. Core 3 stays reserved for the stock audio thread.
 * Weak so a unit test that links one source file falls back to pthread_create. */
int xz_worker_create(pthread_t *thread, void *(*entry)(void *), void *argument) __attribute__((weak));
#define XZ_WORKER_CREATE(thread, entry, argument) \
    (xz_worker_create ? xz_worker_create((thread), (entry), (argument)) : pthread_create((thread), NULL, (entry), (argument)))
#endif
