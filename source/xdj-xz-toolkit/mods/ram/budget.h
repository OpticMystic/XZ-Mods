/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_LEDGER_H
#define XZ_RAM_LEDGER_H
#include <stdint.h>
#include "cache.h"
enum xz_ram_allocation { XZ_RAM_ORIGINAL, XZ_RAM_ASSETS };
enum xz_ram_admission { XZ_RAM_ADMITTED, XZ_RAM_BAD_SIZE, XZ_RAM_FULL, XZ_RAM_LOW_MEMORY };
/* Move-only: zero the old owner after transfer. Release only after final unmap/close. */
struct xz_ram_ticket { uint64_t bytes; unsigned assets, held; };
struct xz_ram_usage { uint64_t original_pending, original_held, assets_pending, assets_held; };
uint64_t xz_ram_round(uint64_t);
enum xz_ram_admission xz_ram_reserve(struct xz_ram_ticket *, enum xz_ram_allocation, uint64_t);
void xz_ram_publish(struct xz_ram_ticket *);
void xz_ram_return(struct xz_ram_ticket *);
void xz_ram_usage(struct xz_ram_usage *);
/* Diagnostic: the bytes the ledger counts as available now. Reads /proc/meminfo: never from a real-time thread. */
uint64_t xz_ram_available_now(void);
/* Serializes original-file materialization, never held by a playback/UI worker. */
void xz_ram_copy_lock(int manual);
void xz_ram_copy_unlock(void);
/* Test injection, installed before consumers start. NULL restores sysinfo. */
void xz_ram_free_sampler(uint64_t (*)(void));
/* Existing prepared-stream ABI. Each input is page-rounded by the pin owner. */
/* Track loads come first. A deck that starts loading a track calls begin(); background copies of stem
 * files ask busy() and stand back while it is true, so they never share the USB with a loading track. */
#define XZ_RAM_TRACK_LOAD_QUIET_MS 6000u
/* Six seconds is a guess at how long a load takes. Whoever can read the stock player's load record calls
 * loading_seen() for as long as a deck is still loading; busy() then stays true until two seconds after the
 * last sighting, and for at most a minute after begin() so a deck stuck in that state cannot hold stems off
 * for good. */
#define XZ_RAM_TRACK_LOADING_HOLD_MS 2000u
#define XZ_RAM_TRACK_LOADING_LIMIT_MS 60000u
void xz_ram_track_load_begin(void);
void xz_ram_track_loading_seen(void);
int xz_ram_track_load_busy(void);
/* The USB has an order of use: a loading track, then the RAM copy of a playing track's own file, then stem
 * work. busy() is what stem work asks: it is also true while an original-file copy runs or waits its turn,
 * because a deck plays from the USB until that copy is done and must not share the stick with a 100 MB stem
 * copy. The original-file copy itself asks window(), the track-load part alone. */
int xz_ram_track_load_window(void);
/* The two worker cores have an order too. Workers that copy a track's own file run at this nice value, above
 * stem workers (nice 10, audio/overcue_stream.c) and below the system's helpers. They used to run at
 * SCHED_IDLE, which never ran at all while a stem worker was busy on the same core. */
#define XZ_RAM_COPY_NICE 5
int xz_ram_assets_reserve(uint64_t);
/* For a store that can be given back at once (the whole-track stem store while it is being built): admitted
 * against the reserve alone, without the unused RAM-track budget kept free in front of it. Its owner must
 * watch xz_ram_track_refusals() and return the memory when the count moves: tracks come first. */
int xz_ram_assets_reserve_yielding(uint64_t);
uint32_t xz_ram_track_refusals(void);
void xz_ram_assets_publish(uint64_t);
void xz_ram_assets_cancel(uint64_t);
void xz_ram_assets_release(uint64_t);
#endif
