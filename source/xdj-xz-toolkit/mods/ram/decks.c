/* SPDX-License-Identifier: MIT */
#include "decks.h"
#include "runtime.h"
#include "../extra_decks.h"
#include <stdio.h>
#include <string.h>
void xz_ram_deck_status(unsigned deck, struct xz_ram_status *s) {
    memset(s, 0, sizeof(*s));
    if (deck < 2) xz_ram_status((int)deck, s);
    else if (deck < 4) xz_extra_decks_ram_status(deck - 2, s);
    if (!s->supported) { s->reason = XZ_RAM_NO_ENGINE;
        snprintf(s->message, sizeof(s->message), "This loader does not include the RAM engine."); }
}
enum xz_ram_result xz_ram_deck_command(unsigned deck, enum xz_ram_command command, const struct xz_ram_track_key *expected) {
    if (deck >= 4) return XZ_RAM_REFUSED;
    if (deck >= 2) return (enum xz_ram_result)xz_extra_decks_ram_command(deck - 2, command, expected);
    struct xz_ram_status s; xz_ram_status((int)deck, &s);
    if (!expected || expected->session != s.key.session) return XZ_RAM_STALE;
    if (command == XZ_RAM_LOAD && (s.state == XZ_RAM_READY || s.state == XZ_RAM_DETACHED)) return XZ_RAM_ALREADY_HELD;
    if (command == XZ_RAM_LOAD && !s.can_request) return XZ_RAM_REFUSED;
    return xz_ram_native_command((int)deck, command, expected->session);
}
