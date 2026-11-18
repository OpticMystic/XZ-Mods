/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include "xz_thread.h"
#include <sched.h>

int xz_worker_create(pthread_t *thread, void *(*entry)(void *), void *argument)
{
    pthread_attr_t attributes;
    if (!pthread_attr_init(&attributes)) {
        /* Cores 0 and 2. Core 1 carries the display threads and the stock interface thread at real-time
         * priority for about 97% of the time: a normal-priority worker there runs in rare gaps and can sit
         * preempted for 100-250 ms while it holds a lock the audio path needs. Core 3 is stock audio only. */
        cpu_set_t cores;
        CPU_ZERO(&cores);
        CPU_SET(0, &cores); CPU_SET(2, &cores);
        int error = pthread_attr_setaffinity_np(&attributes, sizeof(cores), &cores);
        if (!error) error = pthread_create(thread, &attributes, entry, argument);
        pthread_attr_destroy(&attributes);
        if (!error) return 0;
    }
    return pthread_create(thread, NULL, entry, argument);
}
