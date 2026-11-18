/* SPDX-License-Identifier: MIT */
#ifndef XZ_STEM_ARBITER_H
#define XZ_STEM_ARBITER_H
/* Who holds how much of the stem pool: a pure function of the holders' wants and the pool size, in extents.
 *
 * Rules, in this order (floor is clamped to need):
 *  1. Floors: ENGAGED holders, then PRELOAD holders, by order. A floor is granted whole or not at all.
 *  2. Each ENGAGED holder that got its floor, by order, is raised to its need if what is left covers all of it:
 *     a deck whose stems are in use and whose track fits is held whole, and the first to engage keeps it.
 *  3. Max-min over the ENGAGED and PRELOAD holders that got their floor: what is left raises the smallest grants
 *     first, each up to its need. A holder refused its floor stays at 0.
 *  4. SPARE holders from what is still left: floor whole or nothing, then max-min up to need.
 *  5. IDLE gets nothing.
 * Not "engaged takes everything": a premix can never be held whole, and a nine-minute bubble is worth little more
 * than a five-minute one while the other deck would lose its whole track for it.
 *
 * Rules 1 and 2 are all-or-nothing, so one holder's grant can fall when the pool grows (pool 40: E 37, P 37 gets
 * 20, 20; pool 56 gets 37, 19). The total granted never falls. */
#include <stdint.h>

enum xz_stem_class { XZ_STEM_IDLE, XZ_STEM_SPARE, XZ_STEM_PRELOAD, XZ_STEM_ENGAGED };
/* need and floor in extents; order: lower is the earlier claim. */
struct xz_stem_want { uint8_t klass; uint16_t need, floor; uint32_t order; };
#define XZ_STEM_HOLDERS 4u

static inline int xz_stem_first(const struct xz_stem_want *want, unsigned a, unsigned b) {
    return want[a].order != want[b].order ? want[a].order < want[b].order : a < b;
}

/* Max-min: one extent at a time to the smallest grant below its need, ties to the earlier claim. */
static inline unsigned xz_stem_fill(const struct xz_stem_want *want, const unsigned *need, const uint8_t *member,
                                    unsigned left, uint16_t *grant) {
    while (left) {
        unsigned pick = XZ_STEM_HOLDERS;
        for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) {
            if (!member[i] || grant[i] >= need[i]) continue;
            if (pick == XZ_STEM_HOLDERS || grant[i] < grant[pick] || (grant[i] == grant[pick] && xz_stem_first(want, i, pick))) pick = i;
        }
        if (pick == XZ_STEM_HOLDERS) break;
        grant[pick]++; left--;
    }
    return left;
}

/* Which all-or-nothing steps each holder passed: XZ_STEM_GOT_FLOOR (rules 1 and 4), XZ_STEM_GOT_WHOLE (rule 2). */
#define XZ_STEM_GOT_FLOOR 1u
#define XZ_STEM_GOT_WHOLE 2u
static inline void xz_stem_arbitrate_steps(const struct xz_stem_want want[XZ_STEM_HOLDERS], unsigned pool,
                                           uint16_t grant[XZ_STEM_HOLDERS], uint8_t steps[XZ_STEM_HOLDERS]) {
    unsigned by[XZ_STEM_HOLDERS], need[XZ_STEM_HOLDERS], floor[XZ_STEM_HOLDERS], left = pool;
    uint8_t member[XZ_STEM_HOLDERS] = {0}, spare[XZ_STEM_HOLDERS] = {0};
    for (unsigned i = 0; i < XZ_STEM_HOLDERS; i++) {
        grant[i] = 0; by[i] = i; steps[i] = 0;
        need[i] = want[i].klass == XZ_STEM_IDLE || want[i].klass > XZ_STEM_ENGAGED ? 0u : want[i].need;
        floor[i] = want[i].floor < need[i] ? want[i].floor : need[i];
    }
    for (unsigned i = 1; i < XZ_STEM_HOLDERS; i++)
        for (unsigned j = i; j > 0 && xz_stem_first(want, by[j], by[j - 1]); j--) { unsigned t = by[j]; by[j] = by[j - 1]; by[j - 1] = t; }
    static const uint8_t floors[] = {XZ_STEM_ENGAGED, XZ_STEM_PRELOAD};
    for (unsigned k = 0; k < 2; k++)
        for (unsigned n = 0; n < XZ_STEM_HOLDERS; n++) {
            unsigned i = by[n];
            if (want[i].klass != floors[k] || !need[i] || floor[i] > left) continue;
            grant[i] = (uint16_t)floor[i]; left -= floor[i]; member[i] = 1; steps[i] |= XZ_STEM_GOT_FLOOR;
        }
    for (unsigned n = 0; n < XZ_STEM_HOLDERS; n++) {
        unsigned i = by[n];
        if (want[i].klass != XZ_STEM_ENGAGED || !member[i] || need[i] - grant[i] > left) continue;
        left -= need[i] - grant[i]; grant[i] = (uint16_t)need[i]; steps[i] |= XZ_STEM_GOT_WHOLE;
    }
    left = xz_stem_fill(want, need, member, left, grant);
    for (unsigned n = 0; n < XZ_STEM_HOLDERS; n++) {
        unsigned i = by[n];
        if (want[i].klass != XZ_STEM_SPARE || !need[i] || floor[i] > left) continue;
        grant[i] = (uint16_t)floor[i]; left -= floor[i]; spare[i] = 1; steps[i] |= XZ_STEM_GOT_FLOOR;
    }
    xz_stem_fill(want, need, spare, left, grant);
}
static inline void xz_stem_arbitrate(const struct xz_stem_want want[XZ_STEM_HOLDERS], unsigned pool,
                                     uint16_t grant[XZ_STEM_HOLDERS]) {
    uint8_t steps[XZ_STEM_HOLDERS];
    xz_stem_arbitrate_steps(want, pool, grant, steps);
}
#endif
