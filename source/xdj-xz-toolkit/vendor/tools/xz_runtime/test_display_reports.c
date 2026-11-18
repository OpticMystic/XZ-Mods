/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include "xz_display_reports.h"

/* Exact deployed mods TLS plus the timing bridge: NPTL reserves this inside
 * each new stack even when the worker never renders a waveform. */
__thread unsigned char test_native_tls[836196u + 56u];
static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static int entered, release_writer, received;
static pthread_t producer;
static void blocked_writer(unsigned kind, const char *text, size_t length) {
    struct sched_param parameter; int policy;
    test_native_tls[0] = 1; test_native_tls[sizeof(test_native_tls)-1] = 2;
    assert(!pthread_equal(producer, pthread_self()));
    pthread_attr_t attributes; size_t stack_bytes;
    assert(!pthread_getattr_np(pthread_self(), &attributes));
    assert(!pthread_attr_getstacksize(&attributes, &stack_bytes));
    assert(stack_bytes == XZ_DISPLAY_REPORT_STACK_BYTES);
    assert(!pthread_attr_destroy(&attributes));
    assert(!pthread_getschedparam(pthread_self(), &policy, &parameter));
    assert(policy == SCHED_OTHER && parameter.sched_priority == 0);
    assert(length == 5 && !strcmp(text, "frame"));
    pthread_mutex_lock(&test_mutex);
    if (!received) {
        entered = 1; pthread_cond_broadcast(&test_cond);
        while (!release_writer) pthread_cond_wait(&test_cond, &test_mutex);
    }
    assert(kind == (unsigned)received);
    received++; pthread_cond_broadcast(&test_cond);
    pthread_mutex_unlock(&test_mutex);
}
static void *too_small_worker(void *unused) { (void)unused; assert(!"TLS should refuse this stack"); return NULL; }
int main(void) {
    alarm(5); producer = pthread_self();
    pthread_attr_t small; pthread_t refused;
    assert(!pthread_attr_init(&small));
    assert(!pthread_attr_setstacksize(&small, 256u * 1024u));
    assert(pthread_create(&refused, &small, too_small_worker, NULL) == EINVAL);
    assert(!pthread_attr_destroy(&small));
    assert(!xz_display_report_submit(0, "frame", 5));
    assert(xz_display_reports_start(blocked_writer, NULL));
    assert(xz_display_report_submit(0, "frame", 5));
    pthread_mutex_lock(&test_mutex);
    while (!entered) pthread_cond_wait(&test_cond, &test_mutex);
    pthread_mutex_unlock(&test_mutex);

    /* The writer is deliberately stalled. Contention and a full queue cannot
     * block this producer: only this thread can release the writer below. */
    pthread_mutex_lock(&xz_display_report_mutex);
    assert(xz_display_report_submit(99, "frame", 5));
    pthread_mutex_unlock(&xz_display_report_mutex);
    for (unsigned n = 1; n <= XZ_DISPLAY_REPORT_CAPACITY; n++)
        assert(xz_display_report_submit(n, "frame", 5));
    assert(xz_display_report_submit(99, "frame", 5));
    assert(xz_display_report_submit(99, "frame", 800));
    assert(xz_display_reports_v1[1] == 9 && xz_display_reports_v1[2] == 3);
    pthread_mutex_lock(&test_mutex);
    assert(received == 0);
    release_writer = 1; pthread_cond_broadcast(&test_cond);
    while (received != 9) pthread_cond_wait(&test_cond, &test_mutex);
    pthread_mutex_unlock(&test_mutex);
    puts("PASS slow diagnostic writer cannot block producer; contention/full/oversize drops counted; FIFO content and SCHED_OTHER worker verified.");
    return 0;
}
