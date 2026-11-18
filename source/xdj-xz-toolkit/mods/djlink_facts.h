/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJLINK_FACTS_H
#define XZ_DJLINK_FACTS_H
/* DECK 3 or DECK 4 as the xz-djlink helper announces it on Pro DJ Link (djlink/djl.h struct djl_deck_facts).
   Pure: no clock, IO, allocation or locks, so the snapshot path of a real-time thread may call it. */
#include <stdint.h>
#include "extra_decks.h"
#include "djlink/djl.h"

/* deck_index 0 is DECK 3. d comes from a held snapshot, so its grid pointer is valid for the call.
   route_up stands in for on-air until the mixer's on-air state is readable.
   master is the current tempo-master observation (xz_sync_link_runtime_master_observe), NULL when none;
   the deck is master only under the rule the Jog LCD uses: fresh, naming this deck, same load fences.
   now_mono_us is CLOCK_MONOTONIC. */
void xz_djlink_facts_from_deck(int deck_index, const struct xz_extra_deck *d, int route_up,
                               const struct xz_master_observation *master, uint64_t now_mono_us,
                               struct djl_deck_facts *out);
/* Changes on every accepted load; 0 while the deck is empty or still loading. */
uint32_t xz_djlink_content_epoch(const struct xz_extra_deck *d);
#endif
