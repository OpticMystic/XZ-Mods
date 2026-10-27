/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_FILE_H
#define XZ_RAM_FILE_H
#include "budget.h"
#include "status.h"
#include "../audio/stem_media.h"
struct xz_ram_file {
    int fd;
    const unsigned char *mapping;
    uint32_t bytes;
    struct xz_media_identity identity;
    struct xz_ram_ticket ticket;
};
#define XZ_RAM_FILE_INIT { .fd = -1 }
/* Background-only. Failure still requires dispose. Source descriptor stays caller-owned. */
enum xz_ram_reason xz_ram_file_prepare(struct xz_ram_file *, int source,
    const struct xz_media_identity *, const struct xz_ram_copy *);
void xz_ram_file_dispose(struct xz_ram_file *);
#endif
