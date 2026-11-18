/* SPDX-License-Identifier: MPL-2.0 */
#ifndef XZ_DISPLAY_REPORTS_H
#define XZ_DISPLAY_REPORTS_H
#include <pthread.h>
#include "../../../mods/xz_mutex.h"
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>

/* Process-lifetime diagnostics. A slow filesystem may lose reports, never a
 * display deadline: producers only try the short queue lock and never write. */
#define XZ_DISPLAY_REPORT_CAPACITY 8u
#define XZ_DISPLAY_REPORT_STACK_BYTES (2u * 1024u * 1024u)
/* The preloaded mods currently reserve about 817 KiB of static TLS per thread.
 * Two MiB leaves over one MiB for these display call chains while avoiding
 * the default eight-MiB reservation. Native application threads are untouched. */
static inline int xz_display_thread_create(pthread_t *thread,
                                           void *(*entry)(void *), void *argument) {
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    if (error) return error;
    error = pthread_attr_setstacksize(&attributes, XZ_DISPLAY_REPORT_STACK_BYTES);
    if (!error) error = pthread_create(thread, &attributes, entry, argument);
    pthread_attr_destroy(&attributes);
    return error;
}
struct xz_display_report { unsigned kind; size_t length; char text[800]; };
static struct xz_display_report xz_display_report_queue[XZ_DISPLAY_REPORT_CAPACITY];
static pthread_mutex_t xz_display_report_mutex = XZ_MUTEX_INITIALIZER;
static unsigned xz_display_report_read, xz_display_report_count;
static void (*xz_display_report_write)(unsigned, const char *, size_t);
static void (*xz_display_report_tick)(void);
__attribute__((visibility("default"))) uint32_t xz_display_reports_v1[5] = {1};

static void *xz_display_report_worker(void *unused) {
    (void)unused;
    syscall(SYS_prctl, PR_SET_NAME, "xz-display-log", 0, 0, 0);
    for (;;) {
        if (xz_display_report_tick) xz_display_report_tick();
        struct xz_display_report item;
        int found = 0;
        pthread_mutex_lock(&xz_display_report_mutex);
        if (xz_display_report_count) {
            item = xz_display_report_queue[xz_display_report_read];
            xz_display_report_read = (xz_display_report_read + 1u) % XZ_DISPLAY_REPORT_CAPACITY;
            xz_display_report_count--;
            found = 1;
        }
        pthread_mutex_unlock(&xz_display_report_mutex);
        if (found) {
            xz_display_report_write(item.kind, item.text, item.length);
            __atomic_add_fetch(&xz_display_reports_v1[3], 1u, __ATOMIC_RELAXED);
        } else usleep(100000);
    }
    return NULL;
}

static int xz_display_reports_start(void (*writer)(unsigned, const char *, size_t), void (*tick)(void)) {
    pthread_attr_t attributes;
    pthread_t thread;
    struct sched_param priority = {.sched_priority = 0};
    if (pthread_attr_init(&attributes)) {
        __atomic_store_n(&xz_display_reports_v1[4], 2, __ATOMIC_RELEASE);
        return 0;
    }
    int error = pthread_attr_setstacksize(&attributes, XZ_DISPLAY_REPORT_STACK_BYTES);
    if (!error) error = pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
    if (!error) error = pthread_attr_setschedpolicy(&attributes, SCHED_OTHER);
    if (!error) error = pthread_attr_setschedparam(&attributes, &priority);
    if (!error) error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (!error) {
        /* The creator runs on the display core, which is real-time for about 97% of the time. A normal-priority
         * writer there starves and can sit preempted while it holds a process-wide lock. Cores 0 and 2. */
        cpu_set_t cores;
        CPU_ZERO(&cores);
        CPU_SET(0, &cores); CPU_SET(2, &cores);
        (void)pthread_attr_setaffinity_np(&attributes, sizeof(cores), &cores);
    }
    xz_display_report_write = writer;
    xz_display_report_tick = tick;
    if (!error) error = pthread_create(&thread, &attributes, xz_display_report_worker, NULL);
    pthread_attr_destroy(&attributes);
    if (error) {
        __atomic_store_n(&xz_display_reports_v1[4], 2, __ATOMIC_RELEASE);
        return 0;
    }
    __atomic_store_n(&xz_display_reports_v1[4], 1, __ATOMIC_RELEASE);
    return 1;
}

/* 0 only before startup, allowing the old path when no presenter exists.
 * Startup failure and a full/contended queue consume and count a drop. */
static int xz_display_report_submit(unsigned kind, const char *text, size_t length) {
    int active = __atomic_load_n(&xz_display_reports_v1[4], __ATOMIC_ACQUIRE);
    if (!active) return 0;
    if (active != 1 || length >= sizeof(xz_display_report_queue[0].text) ||
        pthread_mutex_trylock(&xz_display_report_mutex)) {
        __atomic_add_fetch(&xz_display_reports_v1[2], 1u, __ATOMIC_RELAXED);
        return 1;
    }
    if (xz_display_report_count == XZ_DISPLAY_REPORT_CAPACITY) {
        __atomic_add_fetch(&xz_display_reports_v1[2], 1u, __ATOMIC_RELAXED);
    } else {
        unsigned slot = (xz_display_report_read + xz_display_report_count) % XZ_DISPLAY_REPORT_CAPACITY;
        struct xz_display_report *item = &xz_display_report_queue[slot];
        item->kind = kind; item->length = length;
        memcpy(item->text, text, length); item->text[length] = 0;
        xz_display_report_count++;
        __atomic_add_fetch(&xz_display_reports_v1[1], 1u, __ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&xz_display_report_mutex);
    return 1;
}
#endif
