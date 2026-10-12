/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "cache.h"
#include <string.h>

int xz_ram_budget(uint64_t bytes, uint64_t allocated, uint64_t free_ram)
{
    return bytes && bytes <= XZ_RAM_TRACK_LIMIT &&
        allocated <= XZ_RAM_TOTAL_LIMIT && bytes <= XZ_RAM_TOTAL_LIMIT - allocated &&
        free_ram >= XZ_RAM_RESERVE && bytes <= free_ram - XZ_RAM_RESERVE;
}

int xz_ram_copy_all(unsigned char *destination, uint32_t bytes,
                    const struct xz_ram_copy *copy)
{
    if (!destination || !bytes || bytes > XZ_RAM_TRACK_LIMIT || !copy ||
        !copy->read || !copy->cancelled) return -1;
    for (uint32_t offset = 0; offset < bytes;) {
        if (copy->cancelled(copy->context)) return -2;
        uint32_t count = bytes - offset;
        if (count > 32768) count = 32768;
        int read = copy->read(copy->context, offset, destination + offset, count);
        if (read <= 0 || (uint32_t)read > count) return -1;
        offset += (uint32_t)read;
        if (copy->progress) copy->progress(copy->context, offset);
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
