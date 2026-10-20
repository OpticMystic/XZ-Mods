/* SPDX-License-Identifier: MIT */
/* Until the DECK 3 / DECK 4 players exist: no route, every input refused, both interfaces keep their
   two-deck screens. The sides the input layer publishes are kept, so the snapshot shows them once a
   route reports up. */
#include "extra_decks.h"
#include <string.h>

static int left_side, right_side;

void xz_extra_decks_publish_sides(int left_on_3, int right_on_4) {
 __atomic_store_n(&left_side, !!left_on_3, __ATOMIC_RELEASE);
 __atomic_store_n(&right_side, !!right_on_4, __ATOMIC_RELEASE);
}

struct xz_extra_route xz_extra_decks_route(void) {
 struct xz_extra_route route = {0, 0};
 return route;
}

int xz_extra_decks_snapshot(struct xz_extra_decks *out) {
 if(!out) return 0;
 memset(out, 0, sizeof *out);
 out->route_up = xz_extra_decks_route().route_up;
 out->left_on_3 = out->route_up && __atomic_load_n(&left_side, __ATOMIC_ACQUIRE);
 out->right_on_4 = out->route_up && __atomic_load_n(&right_side, __ATOMIC_ACQUIRE);
 return out->route_up;
}

int xz_extra_deck_input(const struct xz_extra_deck_event *event) {
 (void)event;
 return 0;
}
