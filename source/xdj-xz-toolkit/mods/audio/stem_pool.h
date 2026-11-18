/* SPDX-License-Identifier: MIT */
#ifndef XZ_STEM_POOL_H
#define XZ_STEM_POOL_H
/* One pool of stem memory shared by every stem store.
 *
 * The pool hands out extents: 6 MiB each, the memory behind one segment of a store (32 chunks x 3 singles x
 * 64 KiB). It owns them (on the unit a RAM-disk file and a low-zone mapping each, as the whole-track store had),
 * their RAM-ledger charge, and the table of holders. stem_arbiter.h decides how many extents each holder may hold.
 *
 * Worker and control threads only, never a real-time thread: take, give, leave and service may allocate, unmap
 * and wait. want and grant are cheap and make no system call. */
#include <stddef.h>
#include <stdint.h>
#include "stem_arbiter.h"

#define XZ_STEM_EXTENT_BYTES (6u * 1024u * 1024u)
/* 56 extents are 336 MiB: the whole stem part of the address zone (ram/lowzone.h XZ_LOWZONE_STORE_CEILING). */
#define XZ_STEM_POOL_EXTENTS 56u
/* Tracks first: a refused track load lowers the limit this far below what is mapped, for this long. */
#define XZ_STEM_POOL_TRACK_STEP 8u
#define XZ_STEM_POOL_TRACK_HOLD_MS 30000u
/* A refused allocation holds the limit at what is mapped for this long. */
#define XZ_STEM_POOL_REFUSAL_HOLD_MS 5000u

struct xz_stem_pool_status {
    unsigned limit;   /* configured: XZ_STEM_POOL_EXTENTS, or XZ_MODS_STEM_POOL_EXTENTS when lower */
    unsigned cap;     /* what the arbiter shares now: the limit, lowered for tracks or after a refusal */
    unsigned mapped;  /* extents allocated from the system, held or spare */
    unsigned spare;   /* mapped, held by nobody, kept for a holder below its grant */
    unsigned lost;    /* mapped, left behind by a holder that left without giving it back; never reused */
    struct { unsigned joined, held, grant, need, klass, order; } holder[XZ_STEM_HOLDERS];
};

int xz_stem_pool_join(void);                                    /* holder 0..3, or -1 */
void xz_stem_pool_leave(int holder);                            /* the holder has given everything back */
void xz_stem_pool_want(int holder, const struct xz_stem_want *); /* re-arbitrates */
unsigned xz_stem_pool_grant(int holder);                        /* atomic read */
/* One extent, or NULL: the holder is at its grant, nothing is free yet, or the system refused (see refusal). */
void *xz_stem_pool_take(int holder);
void xz_stem_pool_give(int holder, void *extent);
/* Why the calling thread's last take allocated nothing: 0 none, 1 RAM ledger, 2 RAM-disk space, 3 RAM-disk file,
 * 4 address zone. */
int xz_stem_pool_refusal(void);
/* Each worker pass: ends the timed limits and returns spare extents nobody is below its grant for. */
void xz_stem_pool_service(void);
void xz_stem_pool_status(struct xz_stem_pool_status *);
/* Free room on the RAM disk in MiB, for refusal messages (0 when unknown and on host builds). */
unsigned xz_stem_pool_disk_free_mib(void);
#endif
