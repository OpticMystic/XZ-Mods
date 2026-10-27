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
    int (*step)(void *, unsigned, struct xz_stem_media *, int); /* -1: uncertain rollback, stop this reader */
    void (*retire)(void *, unsigned, struct xz_stem_media *);
    void (*status)(void *, unsigned, struct xz_ram_status *);
};
struct xz_ram_extra;
/* route is the authoritative live route epoch, zero while down; generation is a copied atomic source observation. */
struct xz_ram_extra *xz_ram_extra_create(void *context, uint32_t (*route)(void *), uint64_t (*generation)(void *, unsigned));
void xz_ram_extra_destroy(struct xz_ram_extra *);
struct xz_ram_player_service xz_ram_extra_player_service(struct xz_ram_extra *);
void xz_ram_extra_status(struct xz_ram_extra *, unsigned, struct xz_ram_status *);
enum xz_ram_result xz_ram_extra_command(struct xz_ram_extra *, unsigned, enum xz_ram_command, const struct xz_ram_track_key *);
#endif
