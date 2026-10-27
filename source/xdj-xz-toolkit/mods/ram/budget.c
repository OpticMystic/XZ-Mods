/* SPDX-License-Identifier: MIT */
#include "budget.h"
#include <pthread.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <string.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t copier = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t copy_changed = PTHREAD_COND_INITIALIZER;
static unsigned copy_active, manual_waiters;
static struct xz_ram_usage usage;
static uint64_t (*sample)(void);
static uint64_t available(void) {
    struct sysinfo s;
    if (sample) return sample();
    return sysinfo(&s) ? 0 : (uint64_t)s.freeram * s.mem_unit;
}
uint64_t xz_ram_round(uint64_t n) {
    long p = sysconf(_SC_PAGESIZE);
    uint64_t page = p > 0 ? (uint64_t)p : 4096;
    if (!n || n > UINT64_MAX - (page - 1)) return 0;
    return (n + page - 1) / page * page;
}
static enum xz_ram_admission reserve_locked(unsigned assets, uint64_t n) {
    if (assets && (n > UINT64_MAX - usage.assets_held ||
        usage.assets_pending > UINT64_MAX - usage.assets_held - n)) return XZ_RAM_BAD_SIZE;
    uint64_t originals = usage.original_pending + usage.original_held;
    if (!assets && n > XZ_RAM_TOTAL_LIMIT - originals) return XZ_RAM_FULL;
    uint64_t free = available(), headroom = XZ_RAM_RESERVE;
    if (assets) headroom += XZ_RAM_TOTAL_LIMIT - originals;
    /* Subtract in order: no addition of untrusted byte counts can wrap. */
    const uint64_t charges[] = {headroom, usage.original_pending, usage.assets_pending, n};
    for (unsigned i = 0; i < 4; ++i) {
        if (charges[i] > free) return XZ_RAM_LOW_MEMORY;
        free -= charges[i];
    }
    if (assets) usage.assets_pending += n; else usage.original_pending += n;
    return XZ_RAM_ADMITTED;
}
enum xz_ram_admission xz_ram_reserve(struct xz_ram_ticket *t, enum xz_ram_allocation kind, uint64_t n) {
    if (!t || t->bytes || (kind != XZ_RAM_ORIGINAL && kind != XZ_RAM_ASSETS) ||
        (kind == XZ_RAM_ORIGINAL && n > XZ_RAM_TRACK_LIMIT)) return XZ_RAM_BAD_SIZE;
    n = xz_ram_round(n);
    if (!n) return XZ_RAM_BAD_SIZE;
    pthread_mutex_lock(&lock);
    enum xz_ram_admission r = reserve_locked(kind == XZ_RAM_ASSETS, n);
    if (r == XZ_RAM_ADMITTED) *t = (struct xz_ram_ticket){n, kind == XZ_RAM_ASSETS, 0};
    pthread_mutex_unlock(&lock);
    return r;
}
void xz_ram_publish(struct xz_ram_ticket *t) {
    pthread_mutex_lock(&lock);
    if (t->bytes && !t->held) {
        uint64_t *pending = t->assets ? &usage.assets_pending : &usage.original_pending;
        uint64_t *held = t->assets ? &usage.assets_held : &usage.original_held;
        *pending -= t->bytes; *held += t->bytes; t->held = 1;
    }
    pthread_mutex_unlock(&lock);
}
void xz_ram_return(struct xz_ram_ticket *t) {
    pthread_mutex_lock(&lock);
    uint64_t *n = t->assets ? (t->held ? &usage.assets_held : &usage.assets_pending) :
                             (t->held ? &usage.original_held : &usage.original_pending);
    *n -= t->bytes; memset(t, 0, sizeof(*t));
    pthread_mutex_unlock(&lock);
}
void xz_ram_usage(struct xz_ram_usage *out) { pthread_mutex_lock(&lock); *out = usage; pthread_mutex_unlock(&lock); }
void xz_ram_free_sampler(uint64_t (*fn)(void)) { pthread_mutex_lock(&lock); sample = fn; pthread_mutex_unlock(&lock); }
void xz_ram_copy_lock(int manual) {
    pthread_mutex_lock(&copier);
    if (manual) ++manual_waiters;
    while (copy_active || (!manual && manual_waiters)) pthread_cond_wait(&copy_changed, &copier);
    if (manual) --manual_waiters;
    copy_active = 1;
    pthread_mutex_unlock(&copier);
}
void xz_ram_copy_unlock(void) {
    pthread_mutex_lock(&copier); copy_active = 0;
    pthread_cond_broadcast(&copy_changed); pthread_mutex_unlock(&copier);
}
int xz_ram_assets_reserve(uint64_t n) {
    if (!n || n != xz_ram_round(n)) return 0;
    pthread_mutex_lock(&lock); int ok = reserve_locked(1, n) == XZ_RAM_ADMITTED;
    pthread_mutex_unlock(&lock); return ok;
}
void xz_ram_assets_publish(uint64_t n) { struct xz_ram_ticket t = {n, 1, 0}; xz_ram_publish(&t); }
void xz_ram_assets_cancel(uint64_t n) { struct xz_ram_ticket t = {n, 1, 0}; xz_ram_return(&t); }
void xz_ram_assets_release(uint64_t n) { struct xz_ram_ticket t = {n, 1, 1}; xz_ram_return(&t); }
