/* SPDX-License-Identifier: MIT */
/* The stem pool (stem_pool.c) with a stand-in clock, RAM ledger and system refusals: joins and leaves, grants
 * after every want, giving and taking between holders, tracks-first lowering, refusal backoff, and the count of
 * every extent at the end. */
#define _POSIX_C_SOURCE 200809L
#include "../stem_pool.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error The pool test needs active assertions
#endif

static uint32_t clock_ms = 1000;
static int refuse_with;
static uint32_t track_refusals;
static uint64_t ledger_pending, ledger_held, ledger_reserves, ledger_releases;
uint32_t xz_stem_pool_host_ms(void) { return clock_ms; }
int xz_stem_pool_host_refuse(void) { return refuse_with; }
uint32_t xz_ram_track_refusals(void) { return track_refusals; }
int xz_ram_assets_reserve(uint64_t n) { ledger_pending += n; ledger_reserves++; return 1; }
int xz_ram_assets_reserve_yielding(uint64_t n) { return xz_ram_assets_reserve(n); }
void xz_ram_assets_publish(uint64_t n) { assert(ledger_pending >= n); ledger_pending -= n; ledger_held += n; }
void xz_ram_assets_cancel(uint64_t n) { assert(ledger_pending >= n); ledger_pending -= n; }
void xz_ram_assets_release(uint64_t n) { assert(ledger_held >= n); ledger_held -= n; ledger_releases++; }

/* What each holder holds, as the test sees it. */
#define MAX_HELD 64
static void *held[XZ_STEM_HOLDERS][MAX_HELD];
static unsigned count[XZ_STEM_HOLDERS];

static struct xz_stem_pool_status status(void) { struct xz_stem_pool_status s; xz_stem_pool_status(&s); return s; }
static unsigned checks;
static void check(void) {
    struct xz_stem_pool_status s = status();
    unsigned total = 0;
    for (unsigned h = 0; h < XZ_STEM_HOLDERS; h++) {
        assert(s.holder[h].held == count[h]);
        total += count[h];
        /* No extent is handed to two holders at once. */
        if (!(checks++ & 63))
            for (unsigned i = 0; i < count[h]; i++) for (unsigned g = 0; g < XZ_STEM_HOLDERS; g++) for (unsigned j = 0; j < count[g]; j++)
                assert((g == h && j == i) || held[g][j] != held[h][i]);
    }
    /* No extent lost: every mapped extent is held, spare, or knowingly abandoned, and the ledger agrees. */
    assert(s.mapped == total + s.spare + s.lost);
    assert(s.mapped <= s.limit);
    assert(ledger_held == (uint64_t)s.mapped * XZ_STEM_EXTENT_BYTES && !ledger_pending);
    unsigned grants = 0;
    for (unsigned h = 0; h < XZ_STEM_HOLDERS; h++) grants += s.holder[h].grant;
    assert(grants <= s.cap && s.cap <= s.limit);
}
static int take(int h) {
    void *e = xz_stem_pool_take(h);
    if (!e) return 0;
    assert(count[h] < MAX_HELD);
    memset(e, h + 1, 64); /* the memory is usable */
    held[h][count[h]++] = e;
    return 1;
}
static void give(int h) { assert(count[h]); xz_stem_pool_give(h, held[h][--count[h]]); }
static void fill(int h) { while (take(h)) {} }
static void settle(void) {
    /* Holders above their grant give back, then holders below take, as store workers do pass by pass. */
    for (unsigned round = 0; round < 4; round++) {
        for (int h = 0; h < (int)XZ_STEM_HOLDERS; h++) while (count[h] > xz_stem_pool_grant(h)) give(h);
        for (int h = 0; h < (int)XZ_STEM_HOLDERS; h++) if (status().holder[h].joined) fill(h);
    }
    check();
}
static void want(int h, uint8_t klass, uint16_t need, uint32_t order) {
    struct xz_stem_want w = {klass, need, 8, order};
    xz_stem_pool_want(h, &w);
    /* Grants after every want are the arbiter's, over the joined holders and the current cap. */
    struct xz_stem_pool_status s = status();
    struct xz_stem_want all[XZ_STEM_HOLDERS];
    uint16_t expect[XZ_STEM_HOLDERS];
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++)
        all[i] = s.holder[i].joined ? (struct xz_stem_want){(uint8_t)s.holder[i].klass, (uint16_t)s.holder[i].need, 8, s.holder[i].order}
                                    : (struct xz_stem_want){0};
    xz_stem_arbitrate(all, s.cap, expect);
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) assert(xz_stem_pool_grant((int)i) == expect[i] && s.holder[i].grant == expect[i]);
}
static void empty_all(void) {
    for (int h = 0; h < (int)XZ_STEM_HOLDERS; h++) if (status().holder[h].joined) {
        want(h, XZ_STEM_IDLE, 0, 0);
        while (count[h]) give(h);
        xz_stem_pool_leave(h);
    }
    struct xz_stem_pool_status s = status();
    assert(!s.mapped && !s.spare && !ledger_held && !ledger_pending);
}

static void scripted(void) {
    int a = xz_stem_pool_join(), b = xz_stem_pool_join(), c = xz_stem_pool_join(), d = xz_stem_pool_join();
    assert(a == 0 && b == 1 && c == 2 && d == 3 && xz_stem_pool_join() == -1);
    xz_stem_pool_leave(c); xz_stem_pool_leave(d);
    assert(xz_stem_pool_join() == 2); xz_stem_pool_leave(2);
    assert(status().limit == XZ_STEM_POOL_EXTENTS && status().cap == XZ_STEM_POOL_EXTENTS);
    /* Two five-and-a-half-minute tracks (37 segments) that cannot both be whole: 28 each. */
    want(a, XZ_STEM_PRELOAD, 37, 1); want(b, XZ_STEM_PRELOAD, 37, 2);
    assert(xz_stem_pool_grant(a) == 28 && xz_stem_pool_grant(b) == 28);
    settle();
    assert(count[a] == 28 && count[b] == 28 && status().mapped == 56 && ledger_reserves == 56);
    /* At the grant, take gives nothing and refuses nothing. */
    assert(!xz_stem_pool_take(a) && !xz_stem_pool_refusal());
    /* Deck B engages: it is held whole and A gives back; the extents move without a new allocation. */
    want(b, XZ_STEM_ENGAGED, 37, 3);
    assert(xz_stem_pool_grant(b) == 37 && xz_stem_pool_grant(a) == 19);
    assert(!xz_stem_pool_take(b)); /* nothing free until A gives */
    give(a);
    assert(status().spare == 1 && status().mapped == 56); /* kept: B is below its grant */
    settle();
    assert(count[a] == 19 && count[b] == 37 && status().mapped == 56 && ledger_reserves == 56 && !ledger_releases);
    /* A given-back extent nobody is short of goes back to the system at once. */
    want(b, XZ_STEM_ENGAGED, 30, 3);
    settle();
    assert(count[b] == 30 && count[a] == 26 && status().mapped == 56);
    want(a, XZ_STEM_PRELOAD, 20, 1);
    give(a);
    assert(status().spare == 0 && status().mapped == 55 && ledger_releases == 1);
    settle();
    assert(count[a] == 20 && status().mapped == 50);

    /* Tracks first: a refused track load lowers the cap 8 below what is mapped, for 30 s. */
    track_refusals++;
    want(a, XZ_STEM_PRELOAD, 20, 1);
    assert(status().cap == 42);
    settle();
    assert(status().mapped == 42 && count[a] + count[b] == 42);
    clock_ms += 29000; xz_stem_pool_service();
    assert(status().cap == 42);
    want(a, XZ_STEM_PRELOAD, 37, 1);
    settle();
    assert(status().mapped == 42);
    clock_ms += 1000; xz_stem_pool_service();
    assert(status().cap == XZ_STEM_POOL_EXTENTS);
    settle();
    assert(count[a] == 26 && count[b] == 30 && status().mapped == 56);

    /* A refused allocation holds the cap at what is mapped for 5 s, and says why. */
    want(b, XZ_STEM_ENGAGED, 20, 3);
    settle();
    assert(count[b] == 20 && count[a] == 36 && status().mapped == 56);
    want(a, XZ_STEM_PRELOAD, 37, 1);
    settle();
    assert(count[a] == 36);
    want(b, XZ_STEM_ENGAGED, 10, 3);
    settle();
    assert(count[b] == 10 && count[a] == 37 && status().mapped == 47);
    want(b, XZ_STEM_ENGAGED, 18, 3);
    refuse_with = 2;
    assert(!xz_stem_pool_take(b) && xz_stem_pool_refusal() == 2);
    /* Held at 47: the engaged deck is still held whole, from what the other deck gives back. */
    assert(status().cap == 47 && xz_stem_pool_grant(b) == 18 && xz_stem_pool_grant(a) == 29);
    assert(!xz_stem_pool_take(b) && !xz_stem_pool_refusal());
    refuse_with = 0;
    settle();
    assert(count[b] == 18 && count[a] == 29 && status().mapped == 47 && ledger_reserves == 70);
    clock_ms += 4999; xz_stem_pool_service(); assert(status().cap == 47);
    clock_ms += 1; xz_stem_pool_service(); assert(status().cap == XZ_STEM_POOL_EXTENTS);
    settle();
    assert(count[b] == 18 && count[a] == 37 && status().mapped == 55);
    empty_all();
    printf("PASS pool scripted: joins, grants after each want, hand-over without allocation, release when nobody is short, "
           "tracks first for 30 s, refusal backoff for 5 s, empty at the end\n");
}

static void limited(void) {
    setenv("XZ_MODS_STEM_POOL_EXTENTS", "10", 1);
    int a = xz_stem_pool_join();
    assert(status().limit == 10);
    want(a, XZ_STEM_ENGAGED, 37, 1);
    settle();
    assert(count[a] == 10 && status().mapped == 10);
    /* A holder that leaves holding extents loses them for good: never handed on, never freed under a reader. */
    int b = xz_stem_pool_join();
    want(b, XZ_STEM_PRELOAD, 37, 2);
    give(a); give(a);
    count[a] = 0;
    xz_stem_pool_leave(a);
    assert(status().lost == 8 && status().spare == 2);
    settle();
    assert(count[b] == 2 && status().mapped == 10);
    unsetenv("XZ_MODS_STEM_POOL_EXTENTS");
    want(b, XZ_STEM_IDLE, 0, 0);
    while (count[b]) give(b);
    xz_stem_pool_leave(b);
    assert(status().mapped == 8 && status().lost == 8);
    printf("PASS pool limit from XZ_MODS_STEM_POOL_EXTENTS, abandoned extents never reused\n");
}

static uint32_t seed = 12345;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static void randomized(void) {
    uint32_t order = 100;
    unsigned steps = 0;
    for (unsigned n = 0; n < 200000; n++) {
        int h = (int)(next() % XZ_STEM_HOLDERS);
        int joined = (int)status().holder[h].joined;
        switch (next() % 9) {
        case 0: if (!joined) { int got = xz_stem_pool_join(); assert(got >= 0); } break;
        case 1: if (joined) { want(h, XZ_STEM_IDLE, 0, 0); while (count[h]) give(h); xz_stem_pool_leave(h); } break;
        case 2: if (joined) want(h, (uint8_t)(1 + next() % 3), (uint16_t)(next() % 60), order++); break;
        case 3: case 4: if (joined) take(h); break;
        case 5: if (count[h]) give(h); break;
        case 6: if (!(next() % 8)) track_refusals++; xz_stem_pool_service(); break;
        case 7: refuse_with = next() % 6 == 0 ? (int)(1 + next() % 4) : 0; clock_ms += next() % 3000; break;
        case 8: settle(); break;
        }
        check();
        steps++;
    }
    refuse_with = 0; clock_ms += 60000; xz_stem_pool_service();
    empty_all();
    printf("PASS pool random: %u steps, every extent accounted for after each\n", steps);
}

int main(void) {
    scripted();
    randomized();
    limited();
    return 0;
}
