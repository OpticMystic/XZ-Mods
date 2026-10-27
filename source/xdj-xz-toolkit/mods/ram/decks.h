/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_DECKS_H
#define XZ_RAM_DECKS_H
#include "status.h"
#include "budget.h"
void xz_ram_deck_status(unsigned logical_deck, struct xz_ram_status *);
enum xz_ram_result xz_ram_deck_command(unsigned logical_deck, enum xz_ram_command, const struct xz_ram_track_key *);
#endif
