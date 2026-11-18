/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_EXTRA_H
#define XZ_RAM_EXTRA_H
#include "status.h"
struct xz_stem_media;
/* Installed before player workers start. Callbacks run on the owning deck/lifecycle thread.
 * retire consumes media; only the background service closes it after that transfer. */
struct xz_ram_player_service {
    void *context;
    void (*invalidate)(void *, unsigned);
    void (*bind)(void *, unsigned, struct xz_stem_media *, unsigned, uint32_t, uint64_t, const char *, unsigned);
    /* Every pass of the deck's worker. use: XZ_RAM_STEP_* below; a loaded track is copied to RAM without being
     * asked. -1: uncertain rollback, stop this reader. */
    int (*step)(void *, unsigned, struct xz_stem_media *, int use);
    void (*retire)(void *, unsigned, struct xz_stem_media *);
    void (*status)(void *, unsigned, struct xz_ram_status *);
    /* The deck's stick is stopped or pulled (usb_release.h), on the deck's worker. 1: the deck plays from its
     * complete, attached RAM copy and keeps playing; the session closes every descriptor of the stick it still
     * holds, on its own worker, and never opens the stick again for this track. 0: no attached copy. */
    int (*release)(void *, unsigned, struct xz_stem_media *);
    /* Where the deck's reader is in its file, from the deck's worker on every pass: the offset its last read
     * of the source ended at, in 4 KiB units, plus one (0: not known). One atomic store, nothing else. While the
     * track still plays from the stick the service keeps the megabyte after that place read ahead (ram/extra.c,
     * "Leading"). */
    void (*reading)(void *, unsigned, uint32_t);
};
#define XZ_RAM_STEP_PLAYING 1 /* the deck is sounding: what it reads next must not wait for another deck's load */
#define XZ_RAM_STEP_LOADED 2  /* a track is on the deck */
/* Test probe, NULL in the runtime: called for every stretch the worker reads ahead by itself. */
extern void (*xz_ram_extra_lead_probe)(unsigned deck, uint64_t from, uint64_t bytes);
struct xz_ram_extra;
/* route is the authoritative live route epoch, zero while down; generation is a copied atomic source observation. */
struct xz_ram_extra *xz_ram_extra_create(void *context, uint32_t (*route)(void *), uint64_t (*generation)(void *, unsigned));
void xz_ram_extra_destroy(struct xz_ram_extra *);
struct xz_ram_player_service xz_ram_extra_player_service(struct xz_ram_extra *);
void xz_ram_extra_status(struct xz_ram_extra *, unsigned, struct xz_ram_status *);
enum xz_ram_result xz_ram_extra_command(struct xz_ram_extra *, unsigned, enum xz_ram_command, const struct xz_ram_track_key *);
/* The stick in USB slot 1 or 2 is going, from the thread that began the stop, before the decks hear of it: an
 * attached copy of a track from it is never handed back to the stick (a pending RAM release is dropped). */
void xz_ram_extra_source_gone(struct xz_ram_extra *, unsigned slot);
#endif
