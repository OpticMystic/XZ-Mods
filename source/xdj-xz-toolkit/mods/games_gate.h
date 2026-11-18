/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_GAMES_GATE_H
#define XZ_GAMES_GATE_H
/* Games and apps are an optional extra, off unless installed.
 *
 * The runtime is a DJ tool first. With games compiled in and always armed, game code ran where it could cost
 * a performance: Kart looked at every button and jog packet inside the stock control-surface threads and kept
 * a heartbeat thread, the Doom bridge and the app host started with the player, and the display thread once
 * asked the USB stick for the Doom WAD on every frame (display hitches of up to 280 ms). None of that is worth
 * having on a unit whose owner never opens a game.
 *
 * Installed means: the file VJTOOLS/games-enabled exists on the MODS USB when the player starts (the
 * installer writes it when the owner chooses to install games), or XZ_MODS_GAMES=1 is set for a test.
 * XZ_MODS_GAMES=0 forces them off. The decision is made once; until then games are off.
 *
 * Off means: no game bridge or game thread is started, the control-surface path makes no game call, nothing
 * probes the stick for game files, and the game entries in the interface say they are not installed. */
void xz_games_decide(void);
int xz_games_enabled(void);
/* A source file built alone for a host test has no gate: there the old always-on behaviour stays. */
#pragma weak xz_games_enabled
#define XZ_GAMES_ON() (!xz_games_enabled || xz_games_enabled())
#endif
