/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "games_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned enabled;

void xz_games_decide(void)
{
    const char *choice = getenv("XZ_MODS_GAMES");
    int on = 0;
    if (choice && (choice[0] == '0' || choice[0] == '1') && !choice[1]) on = choice[0] == '1';
    else {
        /* One look at the stick, at start, from the starting thread. Never from a drawing or audio path. */
        /* Installed means the desktop app copied a game: it writes VJTOOLS/Games/<game>/game.json
           (builder/games.py). VJTOOLS/games-enabled turns the built-in games on without one. */
        static const char *const marks[] = {"games-enabled", "Games/Doom/game.json", "Games/ChexQuest/game.json",
                                            "Games/Freedoom/game.json"};
        const char *usb = getenv("XZ_MODS_USB");
        char path[1100];
        for (unsigned i = 0; usb && usb[0] == '/' && !on && i < sizeof(marks) / sizeof(marks[0]); i++)
            if (snprintf(path, sizeof(path), "%s/VJTOOLS/%s", usb, marks[i]) < (int)sizeof(path)) on = !access(path, F_OK);
    }
    __atomic_store_n(&enabled, on ? 1u : 0u, __ATOMIC_RELEASE);
}

int xz_games_enabled(void)
{
    return (int)__atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
}
