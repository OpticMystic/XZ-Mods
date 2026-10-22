/* SPDX-License-Identifier: MIT */
#ifndef XZ_SYNC_MASTER_H
#define XZ_SYNC_MASTER_H
#include <stdint.h>

/* Logical indices match the shell. Native wire/source IDs stay inside the bridge. */
enum xz_master_deck {
 XZ_MASTER_UNKNOWN=-3, XZ_MASTER_NONE=-2, XZ_MASTER_EXTERNAL=-1,
 XZ_MASTER_DECK1=0, XZ_MASTER_DECK2=1, XZ_MASTER_DECK3=2, XZ_MASTER_DECK4=3
};
struct xz_master_intent {
 int32_t deck;
 uint32_t epoch;
 uint32_t explicit_choice;
};
struct xz_master_observation {
 int32_t deck;
 uint32_t revision,observed_ms,operator_epoch,request_epoch,lease_generation;
 uint32_t route_epoch,load_revision,transport_generation;
};
/* Copies one coherent confirmed observation without native I/O or player locks.
 * A failed copy returns zero and sets the output deck to UNKNOWN. */
typedef int (*xz_master_observer)(struct xz_master_observation *);
#endif
