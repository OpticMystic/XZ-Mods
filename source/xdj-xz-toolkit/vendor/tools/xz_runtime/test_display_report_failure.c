/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
static int refuse_create(pthread_t *thread, const pthread_attr_t *attributes,
                         void *(*entry)(void *), void *argument) {
    (void)thread; (void)attributes; (void)entry; (void)argument; return EAGAIN;
}
#define pthread_create refuse_create
#include "xz_display_reports.h"
static void forbidden_write(unsigned kind, const char *text, size_t length) {
    (void)kind; (void)text; (void)length; assert(!"No worker exists");
}
int main(void) {
    assert(!xz_display_reports_start(forbidden_write, NULL));
    assert(xz_display_report_submit(0, "frame", 5));
    assert(xz_display_reports_v1[1] == 0 && xz_display_reports_v1[2] == 1);
    puts("PASS worker creation failure drops diagnostics without synchronous fallback.");
    return 0;
}
