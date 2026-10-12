/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_CACHE_H
#define XZ_RAM_CACHE_H
#include <stddef.h>
#include <stdint.h>

#define XZ_RAM_TRACK_LIMIT (64u * 1024u * 1024u)
#define XZ_RAM_TOTAL_LIMIT (128u * 1024u * 1024u)
#define XZ_RAM_RESERVE (64u * 1024u * 1024u)
struct xz_ram_copy {
    void *context;
    /* Return bytes copied, or -1. Reads must not move the decoder's cursor. */
    int (*read)(void *, uint32_t offset, void *, uint32_t count);
    int (*cancelled)(void *);
    void (*progress)(void *, uint32_t copied);
};
int xz_ram_copy_all(unsigned char *destination, uint32_t bytes,
                    const struct xz_ram_copy *);
int xz_ram_budget(uint64_t bytes, uint64_t allocated, uint64_t free_ram);
/* A complete immutable compressed file, addressed by the stock stream cursor. */
size_t xz_ram_read(const unsigned char *, uint32_t bytes, int64_t *position,
                   void *destination, size_t count);
#endif
