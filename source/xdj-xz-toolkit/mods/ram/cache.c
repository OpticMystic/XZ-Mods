/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include "cache.h"
#include <string.h>
#include <time.h>
/* ram/budget.h; absent in the pure tests of this file. */
extern int xz_ram_track_load_window(void) __attribute__((weak));

int xz_ram_budget(uint64_t bytes, uint64_t allocated, uint64_t free_ram)
{
    return bytes && bytes <= XZ_RAM_TRACK_LIMIT &&
        allocated <= XZ_RAM_TOTAL_LIMIT && bytes <= XZ_RAM_TOTAL_LIMIT - allocated &&
        free_ram >= XZ_RAM_RESERVE && bytes <= free_ram - XZ_RAM_RESERVE;
}

/* The first piece not yet copied at or after `from`, wrapping round; `pieces` when none is missing. */
static uint32_t missing_from(const unsigned char *have, uint32_t pieces, uint32_t from)
{
    for (uint32_t n = 0; n < pieces; ++n) {
        uint32_t piece = from + n < pieces ? from + n : from + n - pieces;
        if (!(have[piece >> 3] & (1u << (piece & 7u)))) return piece;
    }
    return pieces;
}

int xz_ram_copy_all(unsigned char *destination, uint32_t bytes,
                    const struct xz_ram_copy *copy)
{
    if (!destination || !bytes || bytes > XZ_RAM_TRACK_LIMIT || !copy ||
        !copy->read || !copy->cancelled) return -1;
    unsigned char have[XZ_RAM_TRACK_LIMIT / XZ_RAM_COPY_PIECE / 8u];
    memset(have, 0, sizeof(have));
    uint32_t pieces = bytes / XZ_RAM_COPY_PIECE + (bytes % XZ_RAM_COPY_PIECE != 0), next = 0, copied = 0;
    for (uint32_t done = 0; done < pieces;) {
        if (copy->cancelled(copy->context)) return -2;
        uint32_t at = copy->wanted ? copy->wanted(copy->context) : UINT32_MAX;
        int placed = at != UINT32_MAX && at < bytes;
        uint32_t piece = missing_from(have, pieces, placed ? at / XZ_RAM_COPY_PIECE : next);
        /* A track that is loading has the USB to itself, but for the pieces a playing consumer is about to
           read: those it would otherwise fetch itself, behind the same loading track, and wait for them.
           Wait in short steps so a cancel, and the consumer moving on, are still seen. */
        if (xz_ram_track_load_window && xz_ram_track_load_window()) {
            uint32_t lead = placed && copy->lead ? copy->lead(copy->context) : 0;
            uint64_t start = (uint64_t)piece * XZ_RAM_COPY_PIECE;
            if (!lead || piece < at / XZ_RAM_COPY_PIECE || start >= (uint64_t)at + lead) {
                struct timespec wait = {0, 20000000}; nanosleep(&wait, NULL); continue;
            }
        }
        uint32_t offset = piece * XZ_RAM_COPY_PIECE, end = bytes - offset > XZ_RAM_COPY_PIECE ? offset + XZ_RAM_COPY_PIECE : bytes;
        while (offset < end) {
            int read = copy->read(copy->context, offset, destination + offset, end - offset);
            if (read <= 0 || (uint32_t)read > end - offset) return -1;
            offset += (uint32_t)read; copied += (uint32_t)read;
            if (copy->progress) copy->progress(copy->context, copied);
            if (offset < end && copy->cancelled(copy->context)) return -2;
        }
        have[piece >> 3] |= (unsigned char)(1u << (piece & 7u));
        ++done; next = piece + 1u < pieces ? piece + 1u : 0;
    }
    return copy->cancelled(copy->context) ? -2 : 0;
}

size_t xz_ram_read(const unsigned char *data, uint32_t bytes, int64_t *position,
                   void *destination, size_t count)
{
    if (!data || !position || !destination || *position < 0 ||
        (uint64_t)*position >= bytes) return 0;
    size_t remaining = bytes - (uint32_t)*position;
    if (count > remaining) count = remaining;
    memcpy(destination, data + (size_t)*position, count);
    *position += (int64_t)count;
    return count;
}
