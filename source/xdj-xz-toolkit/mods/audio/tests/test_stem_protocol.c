/* SPDX-License-Identifier: MIT */
/* The store's reader protocol (overcue_flat.h), reader against worker.
 *
 * One thread reads like the audio thread: it plays, jumps, plays backwards and loops, publishes its playhead,
 * and reads blocks through the bases the store hands it. It also reads chunks where a worker refills an extent
 * first (the first and last chunk of a held segment, and the chunk at the playhead's offset), and it reads every
 * block twice with a pause in between, still inside. The worker slides the bubble after the playhead while the
 * grant moves between 2 segments and the whole track.
 *
 * Every sample encodes its own deck frame, single and channel, so a sample read through an extent that was handed
 * to another segment, or rewritten under the reader, is a wrong value. With the reader wait removed from the store
 * (verify_stem_pool.py does that to a copy) this test fails. Usage: test_stem_protocol [seconds] */
#define _POSIX_C_SOURCE 200809L
#include "../overcue_flat.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error The protocol test needs active assertions
#endif

#define SEGMENTS 8u
#define FRAMES ((uint64_t)SEGMENTS * XZ_OC_FLAT_SEGMENT_FRAMES - 777u)
/* Takes per extent (8 extents) before the run may end. */
#define RECYCLES 300u

static inline int16_t pattern(uint64_t frame, unsigned single, unsigned channel) {
    uint32_t x = (uint32_t)frame * 6u + single * 2u + channel + 0x9e3779b9u;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return (int16_t)(x & 0xffffu);
}
static int pattern_read(void *context, unsigned single, int64_t first, size_t frames, int16_t *stereo) {
    (void)context;
    for (size_t i = 0; i < frames; i++)
        for (unsigned c = 0; c < 2; c++) {
            int64_t n = first + (int64_t)i;
            stereo[i * 2 + c] = n < 0 || (uint64_t)n >= FRAMES ? 0 : pattern((uint64_t)n, single, c);
        }
    return 0;
}

static struct xz_oc_flat *store;
static int64_t playhead;
static int reverse, stop, failed;
static uint64_t verified_blocks, verified_samples, uncovered;

static uint32_t next(uint32_t *seed) { *seed ^= *seed << 13; *seed ^= *seed >> 17; *seed ^= *seed << 5; return *seed; }
static void nap_us(long us) { struct timespec t = {0, us * 1000L}; nanosleep(&t, NULL); }

/* Checks frames [first, first + frames) through the given bases. */
static int check(int64_t first, size_t frames, const int16_t **base) {
    uint64_t c0 = (uint64_t)first / XZ_OC_FLAT_CHUNK;
    for (size_t i = 0; i < frames && (uint64_t)first + i < FRAMES; i++) {
        uint64_t at = (uint64_t)first + i;
        const int16_t *b = base[at / XZ_OC_FLAT_CHUNK - c0];
        for (unsigned r = 0; r < 3; r++) for (unsigned ch = 0; ch < 2; ch++) {
            int16_t got = b[(size_t)r * XZ_OC_FLAT_SINGLE + (at % XZ_OC_FLAT_CHUNK) * 2 + ch];
            if (got != pattern(at, r, ch)) {
                fprintf(stderr, "WRONG VALUE at deck frame %llu single %u channel %u: %d, expected %d (segment %llu chunk %llu)\n",
                        (unsigned long long)at, r, ch, got, pattern(at, r, ch),
                        (unsigned long long)(at / XZ_OC_FLAT_SEGMENT_FRAMES), (unsigned long long)(at / XZ_OC_FLAT_CHUNK));
                __atomic_store_n(&failed, 1, __ATOMIC_SEQ_CST);
                return 0;
            }
        }
    }
    __atomic_add_fetch(&verified_samples, frames * 6, __ATOMIC_RELAXED);
    return 1;
}

/* One visit: enter, take the bases, read the block twice with a pause, leave. */
static void visit(int64_t first, size_t frames, long pause_us) {
    const int16_t *base[XZ_OC_FLAT_BLOCK_CHUNKS];
    xz_oc_flat_enter(store);
    if (xz_oc_flat_bases(store, first, frames, base) && (uint64_t)first < FRAMES) {
        if (check(first, frames, base)) {
            if (pause_us) nap_us(pause_us);
            if (check(first, frames, base)) __atomic_add_fetch(&verified_blocks, 1, __ATOMIC_RELAXED);
        }
    } else __atomic_add_fetch(&uncovered, 1, __ATOMIC_RELAXED);
    xz_oc_flat_leave(store);
}

static void *reader(void *unused) {
    (void)unused;
    uint32_t seed = 0x12345u;
    int64_t at = 0, loop = (int64_t)(FRAMES / 3);
    int back = 0;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE) && !__atomic_load_n(&failed, __ATOMIC_ACQUIRE)) {
        size_t frames = 64 + next(&seed) % 4033;
        uint32_t choice = next(&seed) % 100;
        if (choice < 4) at = (int64_t)(next(&seed) % FRAMES);              /* jump */
        else if (choice < 6) back = !back;                                 /* direction */
        else if (choice < 9) at = loop;                                    /* loop back */
        else at += back ? -(int64_t)frames : (int64_t)frames;             /* play */
        if (at < 0) { at = 0; back = 0; }
        if ((uint64_t)at >= FRAMES) { at = (int64_t)FRAMES - 1; back = 1; }
        __atomic_store_n(&reverse, back, __ATOMIC_RELAXED);
        __atomic_store_n(&playhead, at, __ATOMIC_RELEASE);
        visit(at, frames, 0);
        /* Where an extent is refilled first after it changes hands: a held segment's first and last chunk, and
           the chunk at the playhead's offset in it. */
        uint32_t s = next(&seed) % SEGMENTS, where = next(&seed) % 3;
        uint64_t chunk = (uint64_t)s * XZ_OC_FLAT_SEGMENT +
            (where == 0 ? 0u : where == 1 ? XZ_OC_FLAT_SEGMENT - 1u : (uint64_t)(at / XZ_OC_FLAT_CHUNK) % XZ_OC_FLAT_SEGMENT);
        visit((int64_t)(chunk * XZ_OC_FLAT_CHUNK + next(&seed) % (XZ_OC_FLAT_CHUNK - 2048)), 2048, 150);
        nap_us(60);
    }
    return NULL;
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 60;
    setenv("XZ_MODS_STEM_POOL_EXTENTS", "8", 1);
    store = xz_oc_flat_create(44100, 44100, FRAMES, 0, 3, pattern_read, NULL);
    assert(store && store->direct && store->segments == SEGMENTS);
    struct xz_stem_want whole = {XZ_STEM_ENGAGED, SEGMENTS, 8, 1}, two = {XZ_STEM_PRELOAD, 2, 2, 1}, four = {XZ_STEM_PRELOAD, 4, 4, 1};
    xz_stem_pool_want(store->holder, &whole);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, reader, NULL));
    struct timespec began, now;
    clock_gettime(CLOCK_MONOTONIC, &began);
    uint32_t seed = 777, takes = 0, passes = 0;
    int16_t *seen[SEGMENTS] = {0};
    int phase = 0;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (double)(now.tv_sec - began.tv_sec) + (double)(now.tv_nsec - began.tv_nsec) / 1e9;
        if (__atomic_load_n(&failed, __ATOMIC_ACQUIRE) || elapsed > seconds) break;
        if (takes >= SEGMENTS * RECYCLES && elapsed > 3) break;
        /* The grant: mostly a small bubble that slides (extents change hands through the pool's spare list), and
           every few hundred passes the whole track and back to two (extents go back to the system and return). */
        if (!(passes % 150)) {
            phase = (phase + 1) % 6;
            xz_stem_pool_want(store->holder, phase == 0 ? &whole : phase == 3 ? &four : &two);
        }
        int32_t anchors[2] = {0, (int32_t)(FRAMES / 3)};
        int64_t near = __atomic_load_n(&playhead, __ATOMIC_ACQUIRE);
        int step = xz_oc_flat_pass(store, near, __atomic_load_n(&reverse, __ATOMIC_RELAXED), anchors, 2);
        assert(step >= 0);
        /* This thread is the store's only writer: its own view of the pointers is exact. */
        for (unsigned s = 0; s < SEGMENTS; s++) {
            if (store->seg[s] && store->seg[s] != seen[s]) takes++;
            seen[s] = store->seg[s];
        }
        passes++;
        if (!(next(&seed) % 4)) nap_us(20);
    }
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    pthread_join(thread, NULL);
    printf("%u passes, %u extents taken (%u per extent), %u segments given up, %llu blocks read twice and correct, "
           "%llu samples, %llu visits uncovered\n", passes, takes, takes / SEGMENTS, store->drops,
           (unsigned long long)verified_blocks, (unsigned long long)verified_samples, (unsigned long long)uncovered);
    assert(!failed);
    assert(takes >= SEGMENTS * RECYCLES && store->drops >= SEGMENTS * RECYCLES / 2 && verified_blocks > 10000);
    xz_oc_flat_destroy(store);
    struct xz_stem_pool_status pool;
    xz_stem_pool_status(&pool);
    assert(!pool.mapped && !pool.lost);
    puts("PASS reader protocol: no wrong value through a recycled extent");
    return 0;
}
