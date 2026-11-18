/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "stem_pool.h"
#include "../ram/lowzone.h"
#include "../xz_mutex.h"
#include "../clock_compat.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__arm__) && defined(__linux__)
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#if defined(__linux__)
extern int xz_ram_assets_reserve(uint64_t) __attribute__((weak));
extern int xz_ram_assets_reserve_yielding(uint64_t) __attribute__((weak));
extern void xz_ram_assets_publish(uint64_t) __attribute__((weak));
extern void xz_ram_assets_cancel(uint64_t) __attribute__((weak));
extern void xz_ram_assets_release(uint64_t) __attribute__((weak));
extern uint32_t xz_ram_track_refusals(void) __attribute__((weak));
#else
static int (*xz_ram_assets_reserve)(uint64_t);
static int (*xz_ram_assets_reserve_yielding)(uint64_t);
static void (*xz_ram_assets_publish)(uint64_t);
static void (*xz_ram_assets_cancel)(uint64_t);
static void (*xz_ram_assets_release)(uint64_t);
static uint32_t (*xz_ram_track_refusals)(void);
#endif
#define POOL_LEDGER (xz_ram_assets_reserve && xz_ram_assets_publish && xz_ram_assets_cancel && xz_ram_assets_release)

/* Host tests may stand in for the clock and for the system's refusals. The unit has neither hook. */
#if !(defined(__arm__) && defined(__linux__)) && defined(__linux__)
extern uint32_t xz_stem_pool_host_ms(void) __attribute__((weak));
extern int xz_stem_pool_host_refuse(void) __attribute__((weak));
#else
static uint32_t (*xz_stem_pool_host_ms)(void);
static int (*xz_stem_pool_host_refuse)(void);
#endif

/* owner: holder index; -1 spare; -2 lost: its holder left without giving it back, so it is never handed on (a
 * reader may still use it) and never released. */
struct pool_extent { void *map; int fd, owner; };

static pthread_mutex_t pool_lock = XZ_MUTEX_INITIALIZER;
static struct {
    int joined[XZ_STEM_HOLDERS];
    struct xz_stem_want want[XZ_STEM_HOLDERS];
    uint16_t grant[XZ_STEM_HOLDERS]; /* written under the lock, read with atomics */
    unsigned held[XZ_STEM_HOLDERS];
    unsigned limit, cap, mapped; /* mapped counts allocations under way */
    struct pool_extent extent[XZ_STEM_POOL_EXTENTS];
    uint32_t refusals_seen;
    int track_lowered, refusal_lowered;
    unsigned track_cap, refusal_cap;
    uint32_t track_since, refusal_since; /* 0: not stamped yet (the lowering came through want, without a clock) */
} stem_pool = {.limit = XZ_STEM_POOL_EXTENTS, .cap = XZ_STEM_POOL_EXTENTS};
static _Thread_local int pool_refusal;

static uint32_t pool_now_ms(void) {
    if (xz_stem_pool_host_ms) return xz_stem_pool_host_ms();
    struct timespec t;
    if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 1;
    uint32_t ms = (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
    return ms ? ms : 1u;
}

unsigned xz_stem_pool_disk_free_mib(void) {
#if defined(__arm__) && defined(__linux__)
    struct statvfs disk;
    return statvfs("/dev/shm", &disk) ? 0u : (unsigned)(((uint64_t)disk.f_bavail * disk.f_frsize) >> 20);
#else
    return 0u;
#endif
}

/* One extent from the system: refusal codes as the store used them. On the unit a RAM-disk file whose pages are
 * written first and mapped afterwards. A plain anonymous mapping would find its pages in page faults, and with free
 * memory at its floor each fault reclaims page cache while it holds the memory-map lock; every thread that then
 * maps or grows memory waits behind it (the DECK 3/4 threads lost 40 ms of audio at a time). Written through the
 * file, the same work happens inside this thread's own write calls. */
static void *pool_extent_alloc(int *fd, int *why) {
    *fd = -1; *why = 0;
    if (xz_stem_pool_host_refuse && (*why = xz_stem_pool_host_refuse())) return NULL;
    if (POOL_LEDGER && !(xz_ram_assets_reserve_yielding ? xz_ram_assets_reserve_yielding(XZ_STEM_EXTENT_BYTES)
                                                   : xz_ram_assets_reserve(XZ_STEM_EXTENT_BYTES))) { *why = 1; return NULL; }
#if defined(__arm__) && defined(__linux__)
    /* Never run the RAM disk out of blocks: on Linux 3.0 a failing posix_fallocate keeps every page it wrote until
       the file is emptied, and meanwhile any shared mapping of a RAM-disk file whose pages are not yet allocated
       dies with SIGBUS on first touch, and other writers (the log, RAM tracks) fail. Keep 32 MiB spare. */
    struct statvfs disk;
    void *map = MAP_FAILED;
    int file = -1;
    if (statvfs("/dev/shm", &disk) || (uint64_t)disk.f_bavail * disk.f_frsize < (uint64_t)XZ_STEM_EXTENT_BYTES + (32u << 20)) *why = 2;
    else {
        char name[] = "/dev/shm/xz-ram-flat-XXXXXX";
        file = mkstemp(name);
        *why = 3;
        if (file >= 0) {
            unlink(name);
            if (!fcntl(file, F_SETFD, FD_CLOEXEC) && !posix_fallocate(file, 0, (off_t)XZ_STEM_EXTENT_BYTES)) {
                *why = 4;
                map = xz_lowzone_mmap_low(XZ_STEM_EXTENT_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
            }
            /* Only in the low zone: outside it the store would take the address space the player's own threads and
               heap live in (ram/lowzone.h; the player exits with std::bad_alloc when that runs out). */
            if (map != MAP_FAILED && !xz_lowzone_contains(map)) { munmap(map, XZ_STEM_EXTENT_BYTES); map = MAP_FAILED; }
            if (map == MAP_FAILED) { if (ftruncate(file, 0)) {} close(file); }
        }
    }
    if (map == MAP_FAILED) { if (POOL_LEDGER) xz_ram_assets_cancel(XZ_STEM_EXTENT_BYTES); return NULL; }
    *fd = file;
#else
    void *map = xz_lowzone_alloc(XZ_STEM_EXTENT_BYTES);
    if (!map) { *why = 4; if (POOL_LEDGER) xz_ram_assets_cancel(XZ_STEM_EXTENT_BYTES); return NULL; }
#endif
    /* posix_fallocate has written every page: the memory is in use from here on and the kernel no longer reports
     * it as available. A charge left pending would be subtracted from that figure a second time (ram/budget.c
     * reserve_locked), so track loads would be refused that fit. */
    if (POOL_LEDGER) xz_ram_assets_publish(XZ_STEM_EXTENT_BYTES);
    *why = 0;
    return map;
}

/* Unmapping holds the memory-map lock: in short pieces at the lowest real-time priority (ram/lowzone.h). */
static void pool_extent_free(struct pool_extent e) {
#if defined(__arm__) && defined(__linux__)
    xz_lowzone_release(e.map, XZ_STEM_EXTENT_BYTES);
    /* Emptied before the last close, so the pages return whatever still names the file. */
    if (e.fd >= 0) { if (ftruncate(e.fd, 0)) {} close(e.fd); }
#else
    xz_lowzone_free(e.map, XZ_STEM_EXTENT_BYTES);
#endif
    if (POOL_LEDGER) xz_ram_assets_release(XZ_STEM_EXTENT_BYTES);
}

static void pool_arbitrate_locked(void) {
    struct xz_stem_want want[XZ_STEM_HOLDERS];
    uint16_t grant[XZ_STEM_HOLDERS];
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) want[i] = stem_pool.joined[i] ? stem_pool.want[i] : (struct xz_stem_want){0};
    xz_stem_arbitrate(want, stem_pool.cap, grant);
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) __atomic_store_n(&stem_pool.grant[i], grant[i], __ATOMIC_RELEASE);
}

/* `now` 0: no clock read (want). Lowers the cap when a track load was refused, ends timed limits, re-arbitrates. */
static void pool_refresh_locked(uint32_t now) {
    uint32_t refusals = xz_ram_track_refusals ? xz_ram_track_refusals() : 0;
    if (refusals != stem_pool.refusals_seen) {
        stem_pool.refusals_seen = refusals;
        stem_pool.track_cap = stem_pool.mapped > XZ_STEM_POOL_TRACK_STEP ? stem_pool.mapped - XZ_STEM_POOL_TRACK_STEP : 0;
        stem_pool.track_lowered = 1; stem_pool.track_since = now;
    }
    if (now) {
        if (stem_pool.track_lowered && !stem_pool.track_since) stem_pool.track_since = now;
        if (stem_pool.refusal_lowered && !stem_pool.refusal_since) stem_pool.refusal_since = now;
        if (stem_pool.track_lowered && now - stem_pool.track_since >= XZ_STEM_POOL_TRACK_HOLD_MS) stem_pool.track_lowered = 0;
        if (stem_pool.refusal_lowered && now - stem_pool.refusal_since >= XZ_STEM_POOL_REFUSAL_HOLD_MS) stem_pool.refusal_lowered = 0;
    }
    unsigned cap = stem_pool.limit;
    if (stem_pool.track_lowered && stem_pool.track_cap < cap) cap = stem_pool.track_cap;
    if (stem_pool.refusal_lowered && stem_pool.refusal_cap < cap) cap = stem_pool.refusal_cap;
    stem_pool.cap = cap;
    pool_arbitrate_locked();
}

static unsigned pool_deficit_locked(void) {
    unsigned short_of = 0;
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++)
        if (stem_pool.joined[i] && stem_pool.held[i] < stem_pool.grant[i]) short_of += stem_pool.grant[i] - stem_pool.held[i];
    return short_of;
}

static unsigned pool_spare_locked(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < XZ_STEM_POOL_EXTENTS; i++) n += stem_pool.extent[i].map && stem_pool.extent[i].owner == -1;
    return n;
}

/* Spare extents beyond what holders below their grant can use, or beyond the cap, leave the table here and go
 * back to the system after the lock is released. */
static unsigned pool_surplus_locked(struct pool_extent *out) {
    unsigned keep = pool_deficit_locked(), spare = pool_spare_locked(), n = 0;
    for (unsigned i = XZ_STEM_POOL_EXTENTS; i-- > 0 && spare;) {
        struct pool_extent *e = &stem_pool.extent[i];
        if (!e->map || e->owner != -1) continue;
        if (spare <= keep && stem_pool.mapped <= stem_pool.cap) break;
        out[n++] = *e; *e = (struct pool_extent){NULL, -1, -1};
        spare--; stem_pool.mapped--;
    }
    return n;
}

static void pool_release_all(const struct pool_extent *list, unsigned n) {
    for (unsigned i = 0; i < n; i++) pool_extent_free(list[i]);
}

int xz_stem_pool_join(void) {
    pthread_mutex_lock(&pool_lock);
    int any = 0, holder = -1;
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) any |= stem_pool.joined[i];
    if (!any) {
        /* Read while nobody holds anything, so a test or a field trial can lower it between tracks: from the
           environment, or from a one-line file on the RAM disk, which can be written while the player runs. */
        const char *text = getenv("XZ_MODS_STEM_POOL_EXTENTS");
        char *end = NULL;
#if defined(__arm__) && defined(__linux__)
        char line[16];
        int file = open("/dev/shm/xz-stem-pool-extents", O_RDONLY | O_CLOEXEC);
        if (file >= 0) {
            ssize_t n = read(file, line, sizeof line - 1);
            close(file);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
            if (n > 0) { line[n] = 0; text = line; }
        }
#endif
        long value = text && *text ? strtol(text, &end, 10) : -1;
        stem_pool.limit = value >= 0 && end && !*end && value < (long)XZ_STEM_POOL_EXTENTS ? (unsigned)value : XZ_STEM_POOL_EXTENTS;
        if (!stem_pool.mapped) stem_pool.refusals_seen = xz_ram_track_refusals ? xz_ram_track_refusals() : 0;
    }
    for (unsigned i = 0; i < XZ_STEM_HOLDERS && holder < 0; i++)
        if (!stem_pool.joined[i]) {
            holder = (int)i;
            stem_pool.joined[i] = 1; stem_pool.held[i] = 0; stem_pool.want[i] = (struct xz_stem_want){0};
        }
    if (holder >= 0) pool_refresh_locked(0);
    pthread_mutex_unlock(&pool_lock);
    return holder;
}

void xz_stem_pool_leave(int holder) {
    if (holder < 0 || holder >= (int)XZ_STEM_HOLDERS) return;
    struct pool_extent out[XZ_STEM_POOL_EXTENTS];
    pthread_mutex_lock(&pool_lock);
    for (unsigned i = 0; i < XZ_STEM_POOL_EXTENTS; i++) if (stem_pool.extent[i].map && stem_pool.extent[i].owner == holder) stem_pool.extent[i].owner = -2;
    stem_pool.joined[holder] = 0; stem_pool.held[holder] = 0; stem_pool.want[holder] = (struct xz_stem_want){0};
    pool_refresh_locked(0);
    unsigned n = pool_surplus_locked(out);
    pthread_mutex_unlock(&pool_lock);
    pool_release_all(out, n);
}

void xz_stem_pool_want(int holder, const struct xz_stem_want *want) {
    if (holder < 0 || holder >= (int)XZ_STEM_HOLDERS || !want) return;
    pthread_mutex_lock(&pool_lock);
    if (stem_pool.joined[holder]) { stem_pool.want[holder] = *want; pool_refresh_locked(0); }
    pthread_mutex_unlock(&pool_lock);
}

unsigned xz_stem_pool_grant(int holder) {
    return holder < 0 || holder >= (int)XZ_STEM_HOLDERS ? 0u : __atomic_load_n(&stem_pool.grant[holder], __ATOMIC_ACQUIRE);
}

int xz_stem_pool_refusal(void) { return pool_refusal; }

void *xz_stem_pool_take(int holder) {
    pool_refusal = 0;
    if (holder < 0 || holder >= (int)XZ_STEM_HOLDERS) return NULL;
    uint32_t now = pool_now_ms();
    pthread_mutex_lock(&pool_lock);
    pool_refresh_locked(now);
    if (!stem_pool.joined[holder] || stem_pool.held[holder] >= stem_pool.grant[holder]) { pthread_mutex_unlock(&pool_lock); return NULL; }
    for (unsigned i = 0; i < XZ_STEM_POOL_EXTENTS; i++) {
        struct pool_extent *e = &stem_pool.extent[i];
        if (e->map && e->owner == -1) { e->owner = holder; stem_pool.held[holder]++; pthread_mutex_unlock(&pool_lock); return e->map; }
    }
    if (stem_pool.mapped >= stem_pool.cap) { pthread_mutex_unlock(&pool_lock); return NULL; }
    stem_pool.mapped++;
    pthread_mutex_unlock(&pool_lock);
    int fd, why;
    void *map = pool_extent_alloc(&fd, &why);
    pthread_mutex_lock(&pool_lock);
    if (!map) {
        stem_pool.mapped--;
        pool_refusal = why ? why : 4;
        stem_pool.refusal_cap = stem_pool.mapped; stem_pool.refusal_lowered = 1; stem_pool.refusal_since = pool_now_ms();
        pool_refresh_locked(0);
        pthread_mutex_unlock(&pool_lock);
        return NULL;
    }
    for (unsigned i = 0; i < XZ_STEM_POOL_EXTENTS; i++)
        if (!stem_pool.extent[i].map) { stem_pool.extent[i] = (struct pool_extent){map, fd, holder}; break; }
    stem_pool.held[holder]++;
    pthread_mutex_unlock(&pool_lock);
    return map;
}

void xz_stem_pool_give(int holder, void *map) {
    if (holder < 0 || holder >= (int)XZ_STEM_HOLDERS || !map) return;
    struct pool_extent out[XZ_STEM_POOL_EXTENTS];
    uint32_t now = pool_now_ms();
    pthread_mutex_lock(&pool_lock);
    for (unsigned i = 0; i < XZ_STEM_POOL_EXTENTS; i++)
        if (stem_pool.extent[i].map == map && stem_pool.extent[i].owner == holder) { stem_pool.extent[i].owner = -1; stem_pool.held[holder]--; break; }
    pool_refresh_locked(now);
    unsigned n = pool_surplus_locked(out);
    pthread_mutex_unlock(&pool_lock);
    pool_release_all(out, n);
}

void xz_stem_pool_service(void) {
    struct pool_extent out[XZ_STEM_POOL_EXTENTS];
    uint32_t now = pool_now_ms();
    pthread_mutex_lock(&pool_lock);
    pool_refresh_locked(now);
    unsigned n = pool_surplus_locked(out);
    pthread_mutex_unlock(&pool_lock);
    pool_release_all(out, n);
}

void xz_stem_pool_status(struct xz_stem_pool_status *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&pool_lock);
    out->limit = stem_pool.limit; out->cap = stem_pool.cap; out->mapped = stem_pool.mapped; out->spare = pool_spare_locked();
    for (unsigned i = 0; i < XZ_STEM_POOL_EXTENTS; i++) out->lost += stem_pool.extent[i].map && stem_pool.extent[i].owner == -2;
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) {
        out->holder[i].joined = (unsigned)stem_pool.joined[i];
        out->holder[i].held = stem_pool.held[i]; out->holder[i].grant = stem_pool.grant[i];
        out->holder[i].need = stem_pool.want[i].need; out->holder[i].klass = stem_pool.want[i].klass; out->holder[i].order = stem_pool.want[i].order;
    }
    pthread_mutex_unlock(&pool_lock);
}
