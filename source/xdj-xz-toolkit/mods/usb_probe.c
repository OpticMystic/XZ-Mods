/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include "usb_probe.h"
#include "xz_thread.h"
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define XZ_PROBES 8u
static xz_probe_check checks[XZ_PROBES];
static int results[XZ_PROBES];
static uint32_t started;

static void *worker(void *unused)
{
    (void)unused;
    for (;;) {
        for (unsigned i = 0; i < XZ_PROBES; i++) {
            xz_probe_check check = __atomic_load_n(&checks[i], __ATOMIC_ACQUIRE);
            if (check) __atomic_store_n(&results[i], check() != 0, __ATOMIC_RELEASE);
        }
        struct timespec nap = {2, 0};
        nanosleep(&nap, NULL);
    }
    return NULL;
}
int xz_probe_cached(xz_probe_check check)
{
    if (!check) return 0;
    for (unsigned i = 0; i < XZ_PROBES; i++) {
        xz_probe_check seen = __atomic_load_n(&checks[i], __ATOMIC_ACQUIRE);
        if (seen == check) return __atomic_load_n(&results[i], __ATOMIC_ACQUIRE);
        if (seen) continue;
        xz_probe_check empty = NULL;
        if (__atomic_compare_exchange_n(&checks[i], &empty, check, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            if (!__atomic_exchange_n(&started, 1u, __ATOMIC_ACQ_REL)) {
                pthread_t thread;
                if (XZ_WORKER_CREATE(&thread, worker, NULL)) __atomic_store_n(&started, 0u, __ATOMIC_RELEASE);
                else pthread_detach(thread);
            }
            return 0;
        }
        if (empty == check) return __atomic_load_n(&results[i], __ATOMIC_ACQUIRE);
    }
    return 0;
}
