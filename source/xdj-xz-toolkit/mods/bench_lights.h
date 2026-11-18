/* SPDX-License-Identifier: MIT */
#ifndef XZ_BENCH_LIGHTS_H
#define XZ_BENCH_LIGHTS_H
/* Private bench operations 24..25: the hardware MASTER / BEAT SYNC lights and the master state behind them, and
   presses of MASTER, BEAT SYNC and the deck-select gesture on a side, for unattended checks on the unit
   (master-lights/master_lights_check.py). Reachable only through the opt-in bench socket (xz_bench_control.h,
   XZ_MODS_TEST_CONTROL=1); never from the interface. Same pattern as bench_keys.h: a pure core over an injected
   environment, then the firmware 1.26 adapter whose stock addresses are checked by code hashes.

   Wire (struct xz_bench_request / xz_bench_result):
   24 LIGHTS  deck 4. a0 page.
              Page 0, a summary:
               a0 one byte per lamp, L-MASTER (bits 0-7), L-SYNC, R-MASTER, R-SYNC (bits 24-31), from the LED list
                  the panel is sent (uif::LedManager +48): bit 7 read, bit 0 a record exists, bits 1-3 its state
                  word (0 off, 1 on, 2 flash), bit 4 the mods' force flag.
               a1 byte 0 TotalMainIf master number (the UI's master player: 1/2 stock decks, 3/4 DECK 3/4, -1 none),
                  byte 1 the engine's master (SyncMaster id: 0/1 decks 1/2, 2/3 Link players 3/4, -1 none),
                  0x7f in either when it could not be read;
                  byte 2 the Pro DJ Link sentinel: bit 0/1 deck 1/2 is master, bit 2/3 deck 1/2 SYNC on, bit 7 read;
                  byte 3 which deck each side shows: bits 0-3 left (1 or 3), bits 4-7 right (2 or 4), 0 unknown.
               a2 byte 0 the engine's SYNC flag for deck 1/2 (bits 0/1), bit 7 read;
                  byte 1 the stock deck status the lights are drawn from (ui::Player +0x20b): MASTER deck 1/2
                  (bits 0/1), SYNC (bits 2/3), out-of-sync flash (bits 4/5), bit 7 read;
                  byte 2 the master the Link bridge observes (enum xz_master_deck + 4: 1 unknown, 2 none,
                  3 external, 4..7 DECK 1..4);
                  byte 3 master_lights.c: bit 0 UI number part on, bit 1 record part on, bit 2 owns the number,
                  bits 4-7 the number it last wrote + 2.
               a3 byte 0 DECK 3: bit 0 loaded, 1 master, 2 SYNC on, 3 SYNC flashing (kept offset), 7 read;
                  byte 1 DECK 4 the same; bits 16-31 records master_lights.c added (low 16 bits).
              Pages 1..4, one lamp (L-MASTER, L-SYNC, R-MASTER, R-SYNC) as the panel gets it:
               a0 state word (+0x10), a1 brightness word (+0x14), a2 flash period ms (+0x1c),
               a3 RGB (+0x28..0x2a) | force flag << 24 | record exists << 28 | read << 30.
   25 PRESS   deck 0 left side, 1 right side. a0 1 MASTER, 2 BEAT SYNC, 3 the deck-select gesture (SHIFT twice);
              a1 the deck (1..4) the side must show before the press, 0 for any (refused with -ESTALE otherwise).
              Each key is a press and its release through the stock key path, exactly as the panel reader sends it.
              Reply a0 keys sent (press and release count once), a1 the side's deck before. */
#include <stdint.h>
#include "xz_bench_control.h"

enum { XZ_BL_OP_LIGHTS = 24, XZ_BL_OP_PRESS = 25 };
#define XZ_BL_OP_FIRST 24u
#define XZ_BL_OP_LAST 25u
#define XZ_BL_OP_MASK ((1u << XZ_BL_OP_LIGHTS) | (1u << XZ_BL_OP_PRESS))
enum xz_bl_press { XZ_BL_PRESS_MASTER = 1, XZ_BL_PRESS_SYNC = 2, XZ_BL_PRESS_DECK_SELECT = 3 };

/* Stock key codes, firmware 1.26 (evidence in master-lights/REPORT.md). Channel is the side + 1. */
#define XZ_BL_KEY_MASTER 0x4312u
#define XZ_BL_KEY_SYNC 0x4313u
#define XZ_BL_KEY_SHIFT 0x4103u
#define XZ_BL_OP_KEY_PRESS 0u
#define XZ_BL_OP_KEY_RELEASE 2u

struct xz_bl_lamp { int present, forced; uint32_t state, brightness, period, rgb; };
struct xz_bl_view {
 int lamps_known; struct xz_bl_lamp lamp[4];        /* L-MASTER, L-SYNC, R-MASTER, R-SYNC */
 int ui_known; int32_t ui_master;
 int engine_known; int32_t engine_master;
 int sentinel_known, sentinel_master[2], sentinel_sync[2];
 int engine_sync_known, engine_sync[2];
 int stat_known, stat_master[2], stat_sync[2], stat_flash[2];
 int side_deck[2];
 int32_t bridge_actual;                              /* enum xz_master_deck */
 int soft_known[2], soft_loaded[2], soft_master[2], soft_sync[2], soft_offset[2];
 int ml_ui_ready, ml_insert_ready, ml_owned; int32_t ml_written, ml_inserted;
};
void xz_bl_encode(const struct xz_bl_view *, int32_t page, struct xz_bench_result *);

struct xz_bl_env {
 void *ctx;
 int (*send_key)(void *ctx, unsigned code, unsigned operation, unsigned channel); /* <0 refused */
 int (*side_deck)(void *ctx, int side);                                            /* 1..4, <0 unknown */
 int (*keys_available)(void *ctx);
};
/* op 25 core: checks, then the key taps. */
void xz_bl_press(const struct xz_bl_env *, const struct xz_bench_request *, struct xz_bench_result *);
/* The adapter's entry point for ops 24..25 (bench_adapter.h). */
void xz_bench_lights_apply(const struct xz_bench_request *, struct xz_bench_result *);
#endif
