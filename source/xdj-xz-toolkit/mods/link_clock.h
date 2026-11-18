/* SPDX-License-Identifier: MIT */
#ifndef XZ_LINK_CLOCK_H
#define XZ_LINK_CLOCK_H
/* The unit's tempo master right now, as one shape for the Ableton Link policy (link_policy.h), and the
   builders that make it from the stock master clock or from a DECK 3/4 view.
   Pure: no clock, I/O, allocation or locks. */
#include <stdint.h>
#include "sync_master.h"
#include "sync_native.h"
#include "djlink/djl.h"

enum xz_link_source {
 XZ_LINK_SOURCE_NONE,
 XZ_LINK_SOURCE_DECK1, XZ_LINK_SOURCE_DECK2,  /* stock decks */
 XZ_LINK_SOURCE_DECK3, XZ_LINK_SOURCE_DECK4,  /* software decks */
 XZ_LINK_SOURCE_EXTERNAL                      /* a networked player is master; not sent in this version */
};

struct xz_link_master {
 int valid;
 int source;               /* enum xz_link_source */
 double bpm;               /* effective, pitch included */
 double bar_phase;         /* 0..4 beats into the bar at at_us */
 uint64_t at_us;           /* CLOCK_MONOTONIC */
 uint32_t uncertainty_us;
 int playing;
 int manual;               /* scratching, jog bend or platter held: hold, do not chase */
 uint32_t epoch;           /* changes on load, seek, loop jump or master change, where the source can tell */
};

void xz_link_master_none(struct xz_link_master *out);
/* From the cached stock master snapshot (xz_extra_native_sync_clock). bar_phase is the engine's; it is
   heard output_latency_us later, so at_us includes that offset. */
void xz_link_master_from_stock(const struct xz_sync_native_snapshot *s, struct xz_link_master *out);
/* From a software deck's Pro DJ Link facts (djlink_facts.h). deck_index 0 is DECK 3. Bar phase comes from the
   first beat ahead of the heard playhead and the grid tempo. manual is the platter contact; uncertainty_us is
   the caller's bound on the heard anchor time; transport_generation joins the epoch so a seek or loop jump
   the deck reports starts a new one. */
void xz_link_master_from_facts(int deck_index, const struct djl_deck_facts *f, int manual, uint32_t uncertainty_us,
                               uint32_t transport_generation, struct xz_link_master *out);

/* Which deck is tempo master: the observation the Jog LCD trusts (xz_sync_link_runtime_master_observe),
   when it is current (named deck or NONE/EXTERNAL, nonzero revision and operator epoch, at most
   XZ_MASTER_MAX_AGE_MS old). Without a current observation (the master bridge is not installed) the stock
   clock's own master names deck 1 or 2. Returns enum xz_master_deck; XZ_MASTER_UNKNOWN when neither says. */
int xz_link_pick_master(const struct xz_master_observation *observation, int observed, uint64_t now_us,
                        const struct xz_sync_native_snapshot *stock, int stock_ok);
/* The master shape for a picked deck: the stock builder for decks 1/2 (the snapshot must name that deck),
   the software deck's shape for decks 3/4 (soft[0] DECK 3, soft[1] DECK 4, built on the snapshot path),
   and "none" otherwise. An EXTERNAL master gives source EXTERNAL with valid 0. */
void xz_link_master_select(int deck, const struct xz_sync_native_snapshot *stock, int stock_ok,
                           const struct xz_link_master soft[2], struct xz_link_master *out);
#endif
