/* SPDX-License-Identifier: MIT */
/* The segmented stem store (overcue_flat.h) on one thread:
 *   rank       which segments a store wants, most wanted first (pure): forward, reverse, anchors, track ends;
 *   reference  against a whole copy built by the pre-segment store: along play-through, jumps, reverse play and
 *              loops, with small grants and the whole track, every sample a reader may read equals the reference. */
#define _POSIX_C_SOURCE 200809L
#include "../overcue_flat.h"
#include "stem_store_reference.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error The store test needs active assertions
#endif

#define SEG ((int64_t)XZ_OC_FLAT_SEGMENT_FRAMES)

static void expect_rank(uint32_t segments, uint64_t frames, int64_t near, int reverse, const int32_t *anchors, unsigned count,
                        const uint32_t *want, unsigned n) {
    struct xz_oc_flat f = {.deck_rate = 44100, .frames = frames, .segments = segments};
    uint32_t rank[64], place[64];
    xz_oc_flat_rank(&f, near, reverse, anchors, count, rank, place);
    for (unsigned i = 0; i < n; i++) {
        if (rank[i] != want[i]) {
            fprintf(stderr, "rank %u: segment %u, expected %u (near %lld reverse %d)\n", i, rank[i], want[i], (long long)near, reverse);
            assert(0);
        }
        assert(place[rank[i]] == i);
    }
}

static void ranking(void) {
    const uint64_t ten = 10 * (uint64_t)SEG;
    expect_rank(10, ten, 4 * SEG + 100, 0, NULL, 0, (const uint32_t[]){4, 5, 6, 7, 3, 8, 9, 2, 1, 0}, 10);
    expect_rank(10, ten, 4 * SEG + 100, 1, NULL, 0, (const uint32_t[]){4, 3, 2, 1, 5, 0, 6, 7, 8, 9}, 10);
    /* Anchors: the track start, and a cue 2 s before the end of segment 7, which also wants segment 8. */
    const int32_t cue[] = {0, (int32_t)(8 * SEG - 2 * 44100)};
    expect_rank(10, ten, 4 * SEG + 100, 0, cue, 2, (const uint32_t[]){4, 5, 0, 7, 8, 6, 3, 9, 2, 1}, 10);
    /* The last 3 s of a segment, exactly at the line and one frame before it. */
    const int32_t at_line[] = {(int32_t)(8 * SEG - 3 * 44100)}, before_line[] = {(int32_t)(8 * SEG - 3 * 44100 - 1)};
    expect_rank(10, ten, 4 * SEG, 0, at_line, 1, (const uint32_t[]){4, 5, 7, 8}, 4);
    expect_rank(10, ten, 4 * SEG, 0, before_line, 1, (const uint32_t[]){4, 5, 7, 6}, 4);
    /* Anchors outside the track and duplicates change nothing. */
    const int32_t odd[] = {-5, (int32_t)ten + 10, (int32_t)(4 * SEG + 7)};
    expect_rank(10, ten, 4 * SEG + 100, 0, odd, 3, (const uint32_t[]){4, 5, 6, 7, 3, 8, 9, 2, 1, 0}, 10);
    /* Track ends. */
    expect_rank(10, ten, 0, 0, NULL, 0, (const uint32_t[]){0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, 10);
    expect_rank(10, ten, -400, 1, NULL, 0, (const uint32_t[]){0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, 10);
    expect_rank(10, ten, (int64_t)ten + 5000, 0, NULL, 0, (const uint32_t[]){9, 8, 7, 6, 5, 4, 3, 2, 1, 0}, 10);
    expect_rank(10, ten - SEG / 2, (int64_t)ten - SEG / 2 - 1, 0, NULL, 0, (const uint32_t[]){9, 8, 7}, 3);
    expect_rank(1, 1000, 500, 0, cue, 2, (const uint32_t[]){0}, 1);
    /* Any input: a permutation that starts at the playhead's segment and its neighbour in the playing direction;
       away from the ends, about three quarters of a bubble lie ahead. */
    uint32_t seed = 7;
    for (unsigned trial = 0; trial < 20000; trial++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        uint32_t segments = 1 + seed % 60;
        struct xz_oc_flat f = {.deck_rate = 44100, .frames = (uint64_t)segments * SEG - seed % 1000, .segments = segments};
        int64_t near = (int64_t)(seed % (segments * 4u + 1)) * (SEG / 4) - 3;
        int reverse = (int)((seed >> 7) & 1);
        int32_t anchors[12];
        unsigned count = (seed >> 9) % 13;
        for (unsigned i = 0; i < count; i++) anchors[i] = (int32_t)(((uint64_t)seed * (i + 3)) % (f.frames + 100)) - 50;
        uint32_t rank[64], place[64], seen[64] = {0};
        xz_oc_flat_rank(&f, near, reverse, anchors, count, rank, place);
        for (uint32_t i = 0; i < segments; i++) { assert(rank[i] < segments && !seen[rank[i]]); seen[rank[i]] = 1; assert(place[rank[i]] == i); }
        uint32_t p = xz_oc_flat_segment_of(&f, near);
        assert(rank[0] == p);
        if (segments > 1 && (reverse ? p > 0 : p + 1 < segments)) assert(rank[1] == (reverse ? p - 1 : p + 1));
    }
    struct xz_oc_flat wide = {.deck_rate = 44100, .frames = 61 * (uint64_t)SEG, .segments = 61};
    uint32_t rank[64], place[64];
    for (int reverse = 0; reverse < 2; reverse++) {
        xz_oc_flat_rank(&wide, 30 * SEG + 5, reverse, NULL, 0, rank, place);
        unsigned ahead = 0;
        for (unsigned i = 1; i < 41; i++) ahead += reverse ? rank[i] < 30 : rank[i] > 30;
        printf("bubble of 41 around segment 30 (%s): %u ahead, %u behind\n", reverse ? "reverse" : "forward", ahead, 40 - ahead);
        assert(ahead == 30);
    }
    puts("PASS ranking: forward, reverse, anchors and their 3 s tail, track ends, permutations, three quarters ahead");
}

static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
struct signal { uint64_t frames; uint32_t rate; };
/* A tone per single with noise on top, different on each channel: any misplaced sample shows. */
static int signal_read(void *context, unsigned single, int64_t first, size_t frames, int16_t *stereo) {
    const struct signal *s = context;
    for (size_t i = 0; i < frames; i++) {
        int64_t n = first + (int64_t)i;
        if (n < 0 || (uint64_t)n >= s->frames) { stereo[i * 2] = stereo[i * 2 + 1] = 0; continue; }
        double tone = 9000.0 * sin(2 * 3.14159265358979323846 * (220.0 + 110.0 * single) * (double)n / s->rate);
        stereo[i * 2] = (int16_t)lrint(tone + (double)(hash32((uint32_t)n * 3u + single) % 2001u) - 1000.0);
        stereo[i * 2 + 1] = (int16_t)lrint(-tone * 0.5 + (double)(hash32((uint32_t)n * 7u + single + 99u) % 801u) - 400.0);
    }
    return 0;
}

struct check { struct xz_oc_flat *f; int16_t *ref[3]; uint64_t frames; uint8_t *seen; uint64_t verified_chunks, verified_blocks; };
static void compare_chunk(struct check *k, uint64_t c, const int16_t *base) {
    uint64_t begin = c * XZ_OC_FLAT_CHUNK, end = begin + XZ_OC_FLAT_CHUNK;
    if (end > k->frames) end = k->frames;
    for (unsigned r = 0; r < 3; r++)
        if (memcmp(base + (size_t)r * XZ_OC_FLAT_SINGLE, k->ref[r] + begin * 2, (size_t)(end - begin) * 4)) {
            fprintf(stderr, "chunk %llu single %u differs from the reference\n", (unsigned long long)c, r);
            assert(0);
        }
}
/* Every chunk a reader may read now, checked once per time it was built. */
static void verify(struct check *k) {
    struct xz_oc_flat *f = k->f;
    uint32_t ready = 0, held = 0;
    for (uint32_t s = 0; s < f->segments; s++) held += f->seg[s] != NULL;
    assert(held == f->held);
    xz_oc_flat_enter(f);
    for (uint64_t c = 0; c < f->chunks; c++) {
        const int16_t *base = xz_oc_flat_chunk(f, c);
        if (base) { ready++; if (!k->seen[c]) { compare_chunk(k, c, base); k->verified_chunks++; } }
        k->seen[c] = base != NULL;
    }
    xz_oc_flat_leave(f);
    assert(ready == f->built && f->held <= f->segments);
}
/* A block as the audio thread reads it: through the bases it was given. */
static int read_block(struct check *k, int64_t first, size_t frames) {
    const int16_t *base[XZ_OC_FLAT_BLOCK_CHUNKS];
    xz_oc_flat_enter(k->f);
    int covered = xz_oc_flat_bases(k->f, first, frames, base);
    if (covered && (uint64_t)first < k->frames) {
        uint64_t c0 = (uint64_t)first / XZ_OC_FLAT_CHUNK;
        for (size_t i = 0; i < frames && (uint64_t)first + i < k->frames; i++) {
            uint64_t at = (uint64_t)first + i;
            const int16_t *b = base[at / XZ_OC_FLAT_CHUNK - c0];
            for (unsigned r = 0; r < 3; r++) for (unsigned ch = 0; ch < 2; ch++)
                assert(b[(size_t)r * XZ_OC_FLAT_SINGLE + (at % XZ_OC_FLAT_CHUNK) * 2 + ch] == k->ref[r][at * 2 + ch]);
        }
        k->verified_blocks++;
    }
    xz_oc_flat_leave(k->f);
    return covered;
}

static void set_grant(struct xz_oc_flat *f, unsigned grant) {
    struct xz_stem_want want = {XZ_STEM_PRELOAD, (uint16_t)grant, (uint16_t)(grant < 8 ? grant : 8), 1};
    xz_stem_pool_want(f->holder, &want);
    assert(xz_stem_pool_grant(f->holder) == grant);
}
/* Passes at one place until the playhead's block is covered; a grant of two or more always gets there. */
static unsigned settle_at(struct check *k, int64_t near, int reverse, const int32_t *anchors, unsigned count, unsigned limit) {
    for (unsigned pass = 0; pass < limit; pass++) {
        assert(xz_oc_flat_pass(k->f, near, reverse, anchors, count) >= 0);
        verify(k);
        if (read_block(k, near, 1024)) return pass + 1;
    }
    return 0;
}

static uint32_t seed = 99;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static void against_reference(uint32_t deck_rate, uint32_t stem_rate, uint64_t stem_frames, double offset) {
    struct signal sig = {stem_frames, stem_rate};
    struct check k = {0};
    assert(!reference_store(deck_rate, stem_rate, stem_frames, offset, signal_read, &sig, k.ref, &k.frames));
    struct xz_oc_flat *probe = xz_oc_flat_create(deck_rate, stem_rate, stem_frames, offset, 3, signal_read, &sig);
    assert(probe && probe->frames == k.frames);
    const unsigned segments = probe->segments, grants[] = {2, 3, 5, segments, segments + 4};
    printf("%u Hz stems on a %u Hz deck, offset %.2f: %llu frames, %u segments, %s\n", stem_rate, deck_rate, offset,
           (unsigned long long)k.frames, segments, probe->direct ? "copied" : "resampled");
    xz_oc_flat_destroy(probe);
    for (unsigned g = 0; g < sizeof(grants) / sizeof(grants[0]); g++) {
        unsigned grant = grants[g];
        k.f = xz_oc_flat_create(deck_rate, stem_rate, stem_frames, offset, 3, signal_read, &sig);
        assert(k.f);
        k.seen = calloc(k.f->chunks, 1);
        set_grant(k.f, grant);
        int64_t loop = (int64_t)(k.frames / 3) + 1234, end = (int64_t)k.frames;
        int32_t anchors[2] = {0, (int32_t)loop};
        unsigned slow = 0;
        /* Play through, one chunk per three passes (the worker builds faster than the deck plays). */
        for (int64_t at = 0; at < end; at += XZ_OC_FLAT_CHUNK) {
            for (unsigned pass = 0; pass < 3; pass++) { assert(xz_oc_flat_pass(k.f, at, 0, anchors, 1) >= 0); verify(&k); }
            read_block(&k, at, 4096);
        }
        /* Jumps anywhere, with new blocks at each place. */
        for (unsigned jump = 0; jump < 24; jump++) {
            int64_t at = (int64_t)(next() % k.frames);
            unsigned took = settle_at(&k, at, 0, anchors, 2, 200);
            assert(took);
            if (took > slow) slow = took;
            for (unsigned b = 0; b < 4; b++) read_block(&k, at + (int64_t)(next() % 8192) - 4096, 1 + next() % 4096);
        }
        /* Reverse play from the end. */
        for (int64_t at = end - 1; at > 0; at -= XZ_OC_FLAT_CHUNK) {
            for (unsigned pass = 0; pass < 3; pass++) { assert(xz_oc_flat_pass(k.f, at, 1, anchors, 2) >= 0); verify(&k); }
            read_block(&k, at - 4096 > 0 ? at - 4096 : 0, 4096);
        }
        /* A four-beat loop played round and round, then a jump back to its start from far away. */
        for (unsigned turn = 0; turn < 40; turn++) {
            for (int64_t at = loop; at < loop + 2 * 44100; at += 8192) {
                assert(xz_oc_flat_pass(k.f, at, 0, anchors, 2) >= 0); verify(&k);
                read_block(&k, at, 8192);
            }
        }
        assert(settle_at(&k, end - 50000, 0, anchors, 2, 200));
        assert(settle_at(&k, loop, 0, anchors, 2, 200));
        /* The bubble never holds more than its grant once it has settled; a whole grant ends complete. */
        for (unsigned pass = 0; pass < 400; pass++) { assert(xz_oc_flat_pass(k.f, loop, 0, anchors, 2) >= 0); verify(&k); }
        assert(k.f->held == (grant < segments ? grant : segments));
        if (grant >= segments) assert(xz_oc_flat_complete(k.f));
        else assert(!xz_oc_flat_complete(k.f));
        printf("  grant %2u: held %u, %u segments given up, %llu chunk builds checked, %llu blocks read, slowest jump %u passes\n",
               grant, k.f->held, k.f->drops, (unsigned long long)k.verified_chunks, (unsigned long long)k.verified_blocks, slow);
        xz_oc_flat_destroy(k.f);
        struct xz_stem_pool_status pool;
        xz_stem_pool_status(&pool);
        assert(!pool.mapped && !pool.lost);
        free(k.seen); k.verified_chunks = k.verified_blocks = 0;
    }
    for (unsigned r = 0; r < 3; r++) free(k.ref[r]);
}

/* A grant below two segments is no store, unless it is the whole track. */
static void small_grants(void) {
    struct signal sig = {44100u * 30u, 44100};
    struct xz_oc_flat *f = xz_oc_flat_create(44100, 44100, sig.frames, 0, 3, signal_read, &sig);
    assert(f && f->segments == 3);
    set_grant(f, 1);
    for (unsigned pass = 0; pass < 20; pass++) assert(xz_oc_flat_pass(f, 0, 0, NULL, 0) == 0);
    assert(!f->held);
    xz_oc_flat_destroy(f);
    struct signal one = {44100u * 8u, 44100};
    f = xz_oc_flat_create(44100, 44100, one.frames, 0, 3, signal_read, &one);
    assert(f && f->segments == 1);
    set_grant(f, 1);
    while (xz_oc_flat_pass(f, 0, 0, NULL, 0) == 1) {}
    assert(f->held == 1 && xz_oc_flat_complete(f));
    xz_oc_flat_destroy(f);
    puts("PASS a grant of one segment is no store, unless the track has one segment");
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "rank")) { ranking(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "reference")) {
        small_grants();
        against_reference(44100, 44100, 44100u * 100u, 30.0);   /* copied: 9 segments */
        against_reference(44100, 44100, 44100u * 40u, 29.4);    /* resampled at the same rate: 4 segments */
        against_reference(44100, 96000, 96000u * 25u, 2257.4);  /* resampled from 96 kHz: 3 segments */
        puts("PASS store against the whole-track reference: every readable sample equal, bit for bit");
        return 0;
    }
    return 2;
}
