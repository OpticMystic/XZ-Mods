#include "../diary/diary.h"
#pragma weak xz_diary_busy_begin
#pragma weak xz_diary_busy_end
/* SPDX-License-Identifier: MIT */
#include "budget.h"
#include <pthread.h>
#include "../xz_mutex.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <string.h>
static pthread_mutex_t lock = XZ_MUTEX_INITIALIZER;
static pthread_mutex_t copier = XZ_MUTEX_INITIALIZER;
static pthread_cond_t copy_changed = PTHREAD_COND_INITIALIZER;
static unsigned copy_active, manual_waiters;
static uint32_t copy_demand; /* original-file copies running or waiting for their turn */
static struct xz_ram_usage usage;
static uint64_t (*sample)(void);
/* Cold file cache the kernel drops on demand, in bytes. 0 when it cannot be read. */
static uint64_t droppable_cache(void) {
    char text[2048];
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0) return 0;
    text[n] = 0;
    const char *row = strstr(text, "Inactive(file):");
    return row ? strtoull(row + 15, NULL, 10) * 1024u : 0;
}
/* Memory a new RAM copy may take without pushing the player towards the out-of-memory killer.
 * Free pages alone are the wrong measure on this unit: after some hours of play the page cache (the music
 * USB is read through it) holds every spare page and MemFree rests near 10 MB for good. Counting only free
 * pages then refused every later RAM track and stem set, and their audio was read from the USB during
 * play. The inactive file list is the clean, cold part of that cache; the reserve below still applies. */
static uint32_t track_load_at, track_loading_at;
/* Whole seconds since boot are fine for a six-second window, and need no clock call this file lacks. */
static uint32_t quiet_clock_ms(void) {
    struct sysinfo s;
    return sysinfo(&s) ? 0 : (uint32_t)s.uptime * 1000u;
}
void xz_ram_track_load_begin(void) {
    uint32_t now = quiet_clock_ms();
    __atomic_store_n(&track_load_at, now ? now : 1u, __ATOMIC_RELEASE);
}
void xz_ram_track_loading_seen(void) {
    uint32_t now = quiet_clock_ms();
    __atomic_store_n(&track_loading_at, now ? now : 1u, __ATOMIC_RELEASE);
}
int xz_ram_track_load_window(void) {
    uint32_t at = __atomic_load_n(&track_load_at, __ATOMIC_ACQUIRE);
    if (!at) return 0;
    uint32_t now = quiet_clock_ms(), age = now - at;
    if (age < XZ_RAM_TRACK_LOAD_QUIET_MS) return 1;
    uint32_t seen = __atomic_load_n(&track_loading_at, __ATOMIC_ACQUIRE);
    return age < XZ_RAM_TRACK_LOADING_LIMIT_MS && seen && (uint32_t)(now - seen) < XZ_RAM_TRACK_LOADING_HOLD_MS;
}
int xz_ram_track_load_busy(void) {
    return xz_ram_track_load_window() || __atomic_load_n(&copy_demand, __ATOMIC_ACQUIRE) != 0;
}
static uint64_t available(void) {
    struct sysinfo s;
    if (sample) return sample();
    if (sysinfo(&s)) return 0;
    return (uint64_t)s.freeram * s.mem_unit + droppable_cache();
}
uint64_t xz_ram_available_now(void) { return available(); }
uint64_t xz_ram_round(uint64_t n) {
    long p = sysconf(_SC_PAGESIZE);
    uint64_t page = p > 0 ? (uint64_t)p : 4096;
    if (!n || n > UINT64_MAX - (page - 1)) return 0;
    return (n + page - 1) / page * page;
}
static uint32_t track_refusals;
uint32_t xz_ram_track_refusals(void) { return __atomic_load_n(&track_refusals, __ATOMIC_ACQUIRE); }
static enum xz_ram_admission reserve_locked(unsigned assets, uint64_t n, int yielding) {
    if (assets && (n > UINT64_MAX - usage.assets_held ||
        usage.assets_pending > UINT64_MAX - usage.assets_held - n)) return XZ_RAM_BAD_SIZE;
    uint64_t originals = usage.original_pending + usage.original_held;
    if (!assets && n > XZ_RAM_TOTAL_LIMIT - originals) return XZ_RAM_FULL;
    uint64_t free = available(), headroom = XZ_RAM_RESERVE;
    if (assets && !yielding) headroom += XZ_RAM_TOTAL_LIMIT - originals;
    /* Subtract in order: no addition of untrusted byte counts can wrap. */
    const uint64_t charges[] = {headroom, usage.original_pending, usage.assets_pending, n};
    for (unsigned i = 0; i < 4; ++i) {
        if (charges[i] > free) {
            if (!assets) __atomic_add_fetch(&track_refusals, 1, __ATOMIC_RELEASE);
            return XZ_RAM_LOW_MEMORY;
        }
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
    enum xz_ram_admission r = reserve_locked(kind == XZ_RAM_ASSETS, n, 0);
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
    __atomic_add_fetch(&copy_demand, 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&copier);
    if (manual) ++manual_waiters;
    while (copy_active || (!manual && manual_waiters)) pthread_cond_wait(&copy_changed, &copier);
    if (manual) --manual_waiters;
    copy_active = 1;
    if(xz_diary_busy_begin)xz_diary_busy_begin(3);
    pthread_mutex_unlock(&copier);
}
void xz_ram_copy_unlock(void) {
    pthread_mutex_lock(&copier); copy_active = 0;
    if(xz_diary_busy_end)xz_diary_busy_end(3);
    pthread_cond_broadcast(&copy_changed); pthread_mutex_unlock(&copier);
    __atomic_sub_fetch(&copy_demand, 1, __ATOMIC_RELEASE);
}
int xz_ram_assets_reserve(uint64_t n) {
    if (!n || n != xz_ram_round(n)) return 0;
    pthread_mutex_lock(&lock); int ok = reserve_locked(1, n, 0) == XZ_RAM_ADMITTED;
    pthread_mutex_unlock(&lock); return ok;
}
int xz_ram_assets_reserve_yielding(uint64_t n) {
    if (!n || n != xz_ram_round(n)) return 0;
    pthread_mutex_lock(&lock); int ok = reserve_locked(1, n, 1) == XZ_RAM_ADMITTED;
    pthread_mutex_unlock(&lock); return ok;
}
void xz_ram_assets_publish(uint64_t n) { struct xz_ram_ticket t = {n, 1, 0}; xz_ram_publish(&t); }
void xz_ram_assets_cancel(uint64_t n) { struct xz_ram_ticket t = {n, 1, 0}; xz_ram_return(&t); }
void xz_ram_assets_release(uint64_t n) { struct xz_ram_ticket t = {n, 1, 1}; xz_ram_return(&t); }
