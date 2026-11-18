/* SPDX-License-Identifier: MIT */
/* The stem pool arbiter (stem_arbiter.h): the design's table of examples, row by row, and its properties over
 * random inputs. */
#include "../stem_arbiter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error The arbiter test needs active assertions
#endif

#define E XZ_STEM_ENGAGED
#define P XZ_STEM_PRELOAD
struct row { unsigned pool, count; uint8_t klass[2]; uint16_t need[2], expect[2]; const char *rule; };
static const struct row table[] = {
    {56, 2, {P, P}, {26, 26}, {26, 26}, "both fit"},
    {56, 2, {P, P}, {37, 37}, {28, 28}, "3"},
    {56, 2, {E, P}, {37, 37}, {37, 19}, "2, then 3"},
    {56, 2, {P, E}, {37, 37}, {19, 37}, "2 (class, not position)"},
    {56, 2, {E, E}, {37, 37}, {37, 19}, "2: the first to engage stays whole"},
    {56, 2, {E, P}, {300, 26}, {30, 26}, "3: a premix takes what the short track leaves"},
    {56, 2, {P, P}, {300, 26}, {30, 26}, "3"},
    {56, 2, {E, P}, {60, 37}, {28, 28}, "3: neither can be whole"},
    {56, 1, {E, 0}, {26, 0}, {26, 0}, "alone"},
    {12, 2, {E, P}, {26, 26}, {12, 0}, "1: the second floor does not fit; 3 gives the first the rest"},
};

static uint32_t seed = 0x9e3779b9u;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static unsigned total(const uint16_t *g) { unsigned t = 0; for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) t += g[i]; return t; }
static unsigned effective_need(const struct xz_stem_want *w) { return w->klass == XZ_STEM_IDLE ? 0u : w->need; }

static void pinned_rows(void) {
    for (unsigned n = 0; n < sizeof(table) / sizeof(table[0]); n++) {
        const struct row *r = &table[n];
        /* The holders sit at every pair of indices: the outcome may depend on order, never on the slot. */
        for (unsigned a = 0; a < XZ_STEM_HOLDERS; a++) for (unsigned b = 0; b < XZ_STEM_HOLDERS; b++) {
            if (a == b && r->count == 2) continue;
            struct xz_stem_want want[XZ_STEM_HOLDERS] = {{0}};
            for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) want[i] = (struct xz_stem_want){XZ_STEM_IDLE, 50, 8, 1000 + i};
            unsigned slot[2] = {a, b};
            for (unsigned h = 0; h < r->count; h++) want[slot[h]] = (struct xz_stem_want){r->klass[h], r->need[h], 8, 10 + h};
            uint16_t grant[XZ_STEM_HOLDERS];
            xz_stem_arbitrate(want, r->pool, grant);
            for (unsigned h = 0; h < r->count; h++) {
                if (grant[slot[h]] != r->expect[h]) {
                    fprintf(stderr, "row %u (%s), slots %u/%u: holder %u got %u, expected %u\n", n, r->rule, a, b, h, grant[slot[h]], r->expect[h]);
                    assert(0);
                }
            }
            for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) if (i != a && (r->count == 1 || i != b)) assert(!grant[i]);
            if (r->count == 1) break;
        }
        printf("row %u pool %u: %s %u, %s %u -> %u, %u (%s)\n", n, r->pool, r->klass[0] == E ? "E" : "P", r->need[0],
               r->count > 1 ? (r->klass[1] == E ? "E" : "P") : "-", r->need[1], r->expect[0], r->expect[1], r->rule);
    }
}

static void random_want(struct xz_stem_want *w, int distinct) {
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) {
        w[i].klass = (uint8_t)(next() % 4);
        w[i].need = (uint16_t)(next() % 5 == 0 ? next() % 400 : next() % 70);
        w[i].floor = (uint16_t)(next() % 4 == 0 ? next() % 40 : 8);
        w[i].order = distinct ? i * 7 + 1 + (next() % 3) * 100 : next() % 3;
    }
    if (distinct) for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) for (unsigned j = 0; j < i; j++) if (w[i].order == w[j].order) w[i].order += 1000;
}

static void properties(void) {
    unsigned cases = 0, pool_falls = 0;
    for (unsigned trial = 0; trial < 200000; trial++) {
        struct xz_stem_want want[XZ_STEM_HOLDERS];
        random_want(want, trial & 1);
        unsigned pool = next() % 200;
        uint16_t grant[XZ_STEM_HOLDERS], again[XZ_STEM_HOLDERS], more[XZ_STEM_HOLDERS];
        uint8_t steps[XZ_STEM_HOLDERS], steps_more[XZ_STEM_HOLDERS];
        xz_stem_arbitrate_steps(want, pool, grant, steps);
        /* Bounded by the pool and by each need. IDLE gets nothing. */
        assert(total(grant) <= pool);
        for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) assert(grant[i] <= effective_need(&want[i]));
        /* A pure function: the same inputs give the same grants. */
        xz_stem_arbitrate(want, pool, again);
        assert(!memcmp(grant, again, sizeof(grant)));
        /* A larger pool never lowers the total, and never lowers any grant while every holder passes the same
           all-or-nothing steps (rules 1, 2 and 4). */
        unsigned bigger = pool + 1 + next() % 60;
        xz_stem_arbitrate_steps(want, bigger, more, steps_more);
        assert(total(more) >= total(grant));
        if (!memcmp(steps, steps_more, sizeof(steps))) for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) assert(more[i] >= grant[i]);
        else for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) pool_falls += more[i] < grant[i];
        /* Raising a holder's class never lowers its own grant. A raise also takes a new, later claim, and the
           max-min step breaks ties between equal grants by claim: that alone can cost the raised holder the one
           extent of a remainder, never more. */
        for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) {
            if (want[i].klass == XZ_STEM_ENGAGED) continue;
            struct xz_stem_want raised[XZ_STEM_HOLDERS];
            for (int latest = 0; latest < 2; latest++) {
                memcpy(raised, want, sizeof(raised));
                raised[i].klass++;
                if (latest) raised[i].order = 100000;
                xz_stem_arbitrate(raised, pool, again);
                if (again[i] + (unsigned)latest < grant[i]) {
                    fprintf(stderr, "raise holder %u class %u->%u lowered %u -> %u (pool %u, claims last %d)\n", i, want[i].klass,
                            raised[i].klass, grant[i], again[i], pool, latest);
                    for (unsigned k = 0; k < XZ_STEM_HOLDERS; k++)
                        fprintf(stderr, "  %u: class %u need %u floor %u order %u grant %u -> %u\n", k, want[k].klass, want[k].need,
                                want[k].floor, want[k].order, grant[k], again[k]);
                    assert(0);
                }
            }
        }
        /* Ties go to order, never to the slot: with distinct orders, moving the holders between slots moves the
           grants with them. */
        if (trial & 1) {
            unsigned perm[XZ_STEM_HOLDERS] = {0, 1, 2, 3};
            for (unsigned i = XZ_STEM_HOLDERS - 1; i > 0; i--) { unsigned j = next() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
            struct xz_stem_want moved[XZ_STEM_HOLDERS];
            for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) moved[perm[i]] = want[i];
            xz_stem_arbitrate(moved, pool, again);
            for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) assert(again[perm[i]] == grant[i]);
        }
        cases++;
    }
    /* The documented exception, pinned: a larger pool lets rule 2 hold the engaged deck whole. */
    struct xz_stem_want two[XZ_STEM_HOLDERS] = {{E, 37, 8, 1}, {P, 37, 8, 2}, {0}, {0}};
    uint16_t at40[XZ_STEM_HOLDERS], at56[XZ_STEM_HOLDERS];
    xz_stem_arbitrate(two, 40, at40); xz_stem_arbitrate(two, 56, at56);
    assert(at40[0] == 20 && at40[1] == 20 && at56[0] == 37 && at56[1] == 19);
    /* The remainder tie, pinned: pool 28, grants 9, 9, 10; the holder with 10 engages and claims last. */
    struct xz_stem_want tie[XZ_STEM_HOLDERS] = {{P, 59, 8, 1}, {E, 32, 8, 2}, {XZ_STEM_SPARE, 44, 8, 2}, {P, 43, 8, 0}};
    uint16_t before[XZ_STEM_HOLDERS], after[XZ_STEM_HOLDERS];
    xz_stem_arbitrate(tie, 28, before);
    tie[3].klass = E; xz_stem_arbitrate(tie, 28, after);
    assert(before[3] == 10 && after[3] == 10);
    tie[3].order = 100000; xz_stem_arbitrate(tie, 28, after);
    assert(after[3] == 9);
    printf("%u random cases: bounded, pure, total monotone in the pool, per-holder monotone within the same steps, "
           "class raise never hurts (a later claim loses at most a one-extent tie), slot-independent "
           "(%u grants fell across a step change, as documented)\n", cases, pool_falls);
}

int main(void) {
    pinned_rows();
    properties();
    puts("PASS stem arbiter");
    return 0;
}
