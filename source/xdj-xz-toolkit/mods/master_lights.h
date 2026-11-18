/* SPDX-License-Identifier: MIT */
#ifndef XZ_MASTER_LIGHTS_H
#define XZ_MASTER_LIGHTS_H
/* The hardware MASTER and BEAT SYNC lights when DECK 3 or DECK 4 is involved (firmware 1.26).

   How stock lights them (ui::Player::checkLedStat 0x27f074, fed by PlayerInnards::getStat 0x28a378):
   - MASTER (LED id 5) is lit when TotalMainIf::getSyncMaster() (TotalMainIf+0x34, the UI's master player
     number) equals the deck's channel (1 or 2). The stock screen's MASTER label reads the same bit
     (StatWatcher::getPlaySyncMaster 0x249f44). Only TotalMainIf_SetLinkSyncMaster (0x9f560, called by the
     Pro DJ Link sentinel task when its master changes) writes that number; it also moves the engine.
   - BEAT SYNC (id 4) is lit from the engine's own SYNC flag (PlayEngine::isSyncOn), flashing when out of
     beat sync.
   - The LED list is rebuilt on every refresh and holds a record only for a lamp stock lights
     (LedStat::setLedState appends, LedManager::refStates clears).

   What went wrong with DECK 3/4:
   1. A DECK 3/4 MASTER goes into the engine directly (sync_link_runtime.c, SyncMaster::setLinkMaster).
      The UI master number kept the old stock master, so its MASTER light (and screen label) stayed on.
   2. On a side showing DECK 3/4 the mods could only rewrite records stock had made for the stock deck
      underneath, so a DECK 3/4 MASTER or SYNC lamp lit only when that stock deck had the same lamp lit.

   The fix: (1) once a DECK 3/4 master is observed, the UI master number follows the engine's real master
   (3/4 for the software decks, 1/2 for the stock decks, -1 for none) until the sentinel writes its own value;
   (2) a lit DECK 3/4 lamp with no record gets one from stock's own LedStat::setLedState, as checkLedStat
   would have made it. Nothing here runs on the audio thread or takes a lock. */
#include <stddef.h>
#include <stdint.h>
#include "extra_decks.h"
#include "sync_master.h"

/* ---- 1. The UI master number (TotalMainIf+0x34) ---- */
#define XZ_ML_NO_TARGET INT32_MIN
/* Player number for an observed master: DECK 1..4 -> 1..4, none -> -1 (stock's own encoding of "no master",
   TotalMainIf_SetLinkSyncMaster maps 0 to -1). Unknown or external (a real network player): XZ_ML_NO_TARGET. */
int32_t xz_ml_ui_number(int32_t actual_deck);
struct xz_ml_ui { int owned; int32_t written; };
/* One step. actual_deck: the engine's observed master (enum xz_master_deck), XZ_MASTER_UNKNOWN when not fresh.
   current: TotalMainIf+0x34 now. Takes ownership when a DECK 3/4 master is observed; while it owns the number
   it mirrors the engine's master; it gives ownership up as soon as the number holds anything it did not write
   (the sentinel changed the master). Returns 1 with *write set when the number must be written. */
int xz_ml_ui_step(struct xz_ml_ui *, int32_t actual_deck, int32_t current, int32_t *write);

/* ---- 2. Missing lamp records on a side showing DECK 3/4 ---- */
#define XZ_ML_RECORD_BYTES 0x2cu
#define XZ_ML_ID_BEAT_SYNC 4u
#define XZ_ML_ID_MASTER 5u
/* The refresh list stock builds (uif::LedStat inside uif::LedManager +72), firmware 1.26:
   +8 count (u16), +12 records (44 bytes each), +16 ids (u16, 60), +18 slots per id (u16, 2),
   +20 index (u16 per slot, record number + 1, 0 = none); slot = id * 2 + channel - 1. Capacity is
   4400 bytes = 100 records (UiObjectManager::init 0x2936c0). */
#define XZ_ML_STAT_IDS 60u
#define XZ_ML_STAT_SLOTS 2u
#define XZ_ML_STAT_CAPACITY 100u
#define XZ_ML_STAT_HEADROOM 4u   /* never fill the last records: stock appends after the hook for other servers */
struct xz_ml_stat { uint16_t *count; unsigned char *records; uint16_t ids, slots; uint16_t *index; };
/* Stock's record writer: append (id, channel) lit, as checkLedStat calls it. Returns nothing, like stock. */
typedef void (*xz_ml_append_fn)(void *ctx, unsigned id, unsigned channel);
/* Which lamps (bit 0 BEAT SYNC, bit 1 MASTER) the software deck shows lit or flashing (xz_extra_lamp). */
unsigned xz_ml_lit_lamps(const struct xz_extra_deck *);
/* Appends a record for each lit lamp the list does not carry for this channel, through append, after checking
   the list's shape, its index and its spare room; verifies each append (count + 1, index points at a record
   with that id and channel). Returns the number appended, or -1 when the list is not what firmware 1.26
   builds (the caller then switches insertion off). */
int xz_ml_insert_missing(const struct xz_ml_stat *, unsigned channel, const struct xz_extra_deck *,
                         xz_ml_append_fn append, void *ctx);

/* ---- Runtime (firmware 1.26 adapter, master_lights.c) ---- */
/* Checks the stock code once; 0 when both parts may run, a negative value when they stay off. */
int xz_master_lights_start(void);
void xz_master_lights_stop(void);
/* LED refresh thread, once per refresh (native_led.c): the UI master number. */
void xz_master_lights_ui_tick(void);
/* LED refresh thread, for a side showing a software deck, before its lamps are written (native_led.c).
   packet is the uif::LedStat the stock callback received. Returns records appended. */
int xz_master_lights_insert(void *packet, unsigned channel, const struct xz_extra_deck *deck);
/* Read-out for the bench (bench_lights.c). */
struct xz_ml_status { int ui_ready, insert_ready, owned; int32_t written, inserted, refused; };
void xz_master_lights_status(struct xz_ml_status *);
#endif
