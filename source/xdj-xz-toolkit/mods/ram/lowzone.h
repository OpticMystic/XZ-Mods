/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_LOWZONE_H
#define XZ_RAM_LOWZONE_H
/* The XZ kernel splits user and kernel space at 2 GiB and places every unhinted
 * mapping upward from 0x2aaab000. About a hundred native 8 MiB thread stacks
 * live there, which leaves 40-180 MiB free. MODS bulk mappings in that range
 * exhausted it: the player exited with std::bad_alloc and stem workers failed
 * with EAGAIN. The kernel never chooses the range between the brk heap and
 * 0x2aaab000 on its own, so bulk MODS mappings ask for it by address and keep
 * the kernel's own choice as the fallback. The floor leaves the heap room to
 * double (it ends near 73 MiB with four decks and two stem tracks loaded); the
 * ceiling is the base address the runtime library is linked at.
 * Mappings start on a 1 MiB step. On a 4 MiB step sixteen 1.5 MiB buffers took
 * 64 MiB of the zone, and the second deck's stem store had no room (r42). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define XZ_LOWZONE_FLOOR 0x0a000000u
#define XZ_LOWZONE_CEILING 0x26000000u
#define XZ_LOWZONE_STEP 0x00100000u
/* Stem stores stay below this line. The 112 MiB above it are for what a deck needs first: the RAM copies of the
 * loaded tracks (about 12 MiB each for compressed files, 128 MiB at most together) and the stream buffers. */
#define XZ_LOWZONE_STORE_CEILING (XZ_LOWZONE_CEILING - 0x07000000u)
static inline int xz_lowzone_contains(const void *address) {
    return (uintptr_t)address >= XZ_LOWZONE_FLOOR && (uintptr_t)address < XZ_LOWZONE_CEILING;
}
#if defined(__linux__)
#include <sys/mman.h>
#include <sys/types.h>
static inline void *xz_lowzone_mmap(size_t bytes, int protection, int flags, int fd, off_t offset) {
#if defined(__arm__) && defined(__linux__)
    size_t span = (bytes + 0xfffu) & ~(size_t)0xfffu;
    if (span && span <= XZ_LOWZONE_CEILING - XZ_LOWZONE_FLOOR)
        for (uintptr_t at = (XZ_LOWZONE_CEILING - span) & ~(uintptr_t)(XZ_LOWZONE_STEP - 1);
             at >= XZ_LOWZONE_FLOOR; at -= XZ_LOWZONE_STEP) {
            void *map = mmap((void *)at, bytes, protection, flags, fd, offset);
            if (map == (void *)at) return map;
            if (map != MAP_FAILED) munmap(map, bytes);
        }
#endif
    return mmap(NULL, bytes, protection, flags, fd, offset);
}
/* The same zone from the floor upward, for the few very large mappings (a whole-track stem store is three of
 * about 50 MiB). Everything else is placed from the ceiling downward, so the two kinds do not cut each other's
 * free room into pieces: on r40 the zone had 117 MiB free and no gap above 20 MiB, and the second deck's store
 * had nowhere to go. MAP_FAILED when the zone has no room below XZ_LOWZONE_STORE_CEILING: there is no fall-back
 * to the kernel's own choice, which on the unit is the player's crowded region. */
static inline void *xz_lowzone_mmap_low(size_t bytes, int protection, int flags, int fd, off_t offset) {
#if defined(__arm__) && defined(__linux__)
    size_t span = (bytes + 0xfffu) & ~(size_t)0xfffu;
    if (span && span <= XZ_LOWZONE_STORE_CEILING - XZ_LOWZONE_FLOOR)
        for (uintptr_t at = XZ_LOWZONE_FLOOR; at + span <= XZ_LOWZONE_STORE_CEILING; at += XZ_LOWZONE_STEP) {
            void *map = mmap((void *)at, bytes, protection, flags, fd, offset);
            if (map == (void *)at) return map;
            if (map != MAP_FAILED) munmap(map, bytes);
        }
    return MAP_FAILED;
#else
    return mmap(NULL, bytes, protection, flags, fd, offset);
#endif
}
#endif
/* Pinning and releasing a large mapping from a background worker.
 *
 * mlock, munlock and munmap hold the process's memory-map lock for their whole range: 2560 pages for a 10 MiB
 * track, 22000 for a stem set. MODS workers run at idle or normal priority, so the scheduler can take the
 * processor away while one holds that lock. Every thread that then faults a page or maps memory waits behind
 * it: the display thread and both DECK 3/4 players stalled together for about 300 ms (a display hitch and a
 * quarter-second dropout). These helpers work in 2 MiB pieces and run each piece at the lowest real-time
 * priority, so a piece takes a few milliseconds and the worker is not parked by normal-priority work while it
 * holds the lock. It sleeps between pieces and returns to its own priority at the end. */
#if defined(__arm__) && defined(__linux__)
#include <sched.h>
#include <time.h>
#define XZ_LOWZONE_PIECE 0x00200000u
struct xz_lowzone_prompt { int policy, raised; struct sched_param parameter; };
static inline void xz_lowzone_prompt_begin(struct xz_lowzone_prompt *p) {
    p->raised = 0;
    p->policy = sched_getscheduler(0);
    if (p->policy < 0 || p->policy == SCHED_FIFO || p->policy == SCHED_RR || sched_getparam(0, &p->parameter)) return;
    struct sched_param lowest = {.sched_priority = 1};
    p->raised = !sched_setscheduler(0, SCHED_FIFO, &lowest);
}
static inline void xz_lowzone_prompt_end(const struct xz_lowzone_prompt *p) {
    if (p->raised) sched_setscheduler(0, p->policy, &p->parameter);
}
static inline void xz_lowzone_rest(void) { struct timespec nap = {0, 500000}; nanosleep(&nap, NULL); }
static inline int xz_lowzone_mlock(void *address, size_t bytes) {
    struct xz_lowzone_prompt prompt;
    int result = 0;
    xz_lowzone_prompt_begin(&prompt);
    for (size_t at = 0; at < bytes && !result; at += XZ_LOWZONE_PIECE) {
        size_t piece = bytes - at < XZ_LOWZONE_PIECE ? bytes - at : XZ_LOWZONE_PIECE;
        result = mlock((char *)address + at, piece);
        xz_lowzone_rest();
    }
    xz_lowzone_prompt_end(&prompt);
    return result;
}
static inline void xz_lowzone_release(void *address, size_t bytes) {
    struct xz_lowzone_prompt prompt;
    if (!address) return;
    xz_lowzone_prompt_begin(&prompt);
    for (size_t at = 0; at < bytes; at += XZ_LOWZONE_PIECE) {
        size_t piece = bytes - at < XZ_LOWZONE_PIECE ? bytes - at : XZ_LOWZONE_PIECE;
        munlock((char *)address + at, piece);
        munmap((char *)address + at, piece);
        xz_lowzone_rest();
    }
    xz_lowzone_prompt_end(&prompt);
}
#elif defined(__linux__)
static inline int xz_lowzone_mlock(void *address, size_t bytes) { return mlock(address, bytes); }
static inline void xz_lowzone_release(void *address, size_t bytes) { if (address) { munlock(address, bytes); munmap(address, bytes); } }
#endif
#if defined(__arm__) && defined(__linux__)
static inline void *xz_lowzone_alloc(size_t bytes) {
    void *map = xz_lowzone_mmap(bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return map == MAP_FAILED ? NULL : map;
}
static inline void xz_lowzone_free(void *map, size_t bytes) {
    if (map) munmap(map, bytes);
}
#else
/* Host builds keep the heap so sanitizers still see every buffer bound. */
static inline void *xz_lowzone_alloc(size_t bytes) { return malloc(bytes); }
static inline void xz_lowzone_free(void *map, size_t bytes) { (void)bytes; free(map); }
#endif
#endif
