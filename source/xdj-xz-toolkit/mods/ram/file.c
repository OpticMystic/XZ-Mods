/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "file.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
enum xz_ram_reason xz_ram_file_prepare(struct xz_ram_file *f, int source,
    const struct xz_media_identity *identity, const struct xz_ram_copy *copy) {
    struct xz_media_identity before, after;
    if (!xz_media_identify(source, &before) || !xz_media_same(&before, identity)) return XZ_RAM_SOURCE_CHANGED;
    enum xz_ram_admission a = xz_ram_reserve(&f->ticket, XZ_RAM_ORIGINAL, before.bytes);
    if (a != XZ_RAM_ADMITTED) return a == XZ_RAM_BAD_SIZE ? XZ_RAM_TOO_LARGE :
        a == XZ_RAM_FULL ? XZ_RAM_BUDGET_FULL : XZ_RAM_FREE_LOW;
    f->bytes = (uint32_t)before.bytes;
    if (copy->cancelled(copy->context)) return XZ_RAM_INTERRUPTED;
    char name[] = "/dev/shm/xz-ram-track-XXXXXX";
    f->fd = mkstemp(name);
    if (f->fd < 0) return XZ_RAM_PIN_FAILED;
    unlink(name);
    if (fcntl(f->fd, F_SETFD, FD_CLOEXEC) || posix_fallocate(f->fd, 0, f->bytes)) return XZ_RAM_PIN_FAILED;
    void *map = mmap(NULL, f->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd, 0);
    if (map == MAP_FAILED) return XZ_RAM_PIN_FAILED;
    f->mapping = map;
    if (mlock(map, f->bytes)) return XZ_RAM_PIN_FAILED;
    if (xz_ram_copy_all(map, f->bytes, copy) || copy->cancelled(copy->context)) return XZ_RAM_INTERRUPTED;
    if (!xz_media_identify(source, &after) || !xz_media_same(&before, &after)) return XZ_RAM_SOURCE_CHANGED;
    if (mprotect(map, f->bytes, PROT_READ) || !xz_media_identify(f->fd, &f->identity)) return XZ_RAM_PIN_FAILED;
    return XZ_RAM_OK;
}
void xz_ram_file_dispose(struct xz_ram_file *f) {
    if (f->fd >= 0) close(f->fd);
    if (f->mapping) { munlock(f->mapping, f->bytes); munmap((void *)f->mapping, f->bytes); }
    xz_ram_return(&f->ticket);
    *f = (struct xz_ram_file)XZ_RAM_FILE_INIT;
}
