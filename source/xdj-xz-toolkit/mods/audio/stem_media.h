/* SPDX-License-Identifier: MIT */
#ifndef XZ_STEM_MEDIA_H
#define XZ_STEM_MEDIA_H
#include <stddef.h>
#include <stdint.h>
struct xz_stem_media;
/* Worker-only seekable stereo decoder. Reads are normalized float PCM with
 * zero padding outside the file. MP3 untrimmed selects the RX3 decoder grid. */
struct xz_stem_media *xz_stem_media_open(const char *path, int untrimmed);
void xz_stem_media_close(struct xz_stem_media *);
uint32_t xz_stem_media_rate(const struct xz_stem_media *);
uint64_t xz_stem_media_frames(const struct xz_stem_media *);
int xz_stem_media_read(struct xz_stem_media *, int64_t first, size_t frames, float *stereo);
/* Where the decoder's last read of its source file ended, plus one; 0 before the first read. Owner's thread. */
uint64_t xz_stem_media_source_offset(const struct xz_stem_media *);
/* Stable file ownership, called only between decoder reads by its owner. */
struct xz_media_identity {
    uint64_t device, inode, bytes;
    int64_t modified, changed, modified_ns, changed_ns;
};
int xz_media_identify(int fd, struct xz_media_identity *);
int xz_media_same(const struct xz_media_identity *, const struct xz_media_identity *);
int xz_stem_media_source_snapshot(struct xz_stem_media *, struct xz_media_identity *, int *owned_fd);
/* A close-on-exec duplicate of the source descriptor, or -1. Unlike the snapshot it asks the file system nothing
 * (an fstat on the stick waits behind the stick's whole request queue), so a real-time thread may call it. */
int xz_stem_media_source_dup(struct xz_stem_media *);
/* 1 replaced, 0 unchanged, -1 rollback failed: retain both backings until unload.
 * old_fd is always caller-owned when nonnegative, including failure.
 * expected names the file the reader must still be on. It may be NULL when the caller made its copy from a
 * duplicate of this reader's own descriptor: then the reader is on that file by construction, and nothing is
 * asked of the file system (an fstat on the stick waits behind the stick's whole request queue, and the deck's
 * real-time thread makes this call). Nor does it close anything: the reader's previous descriptor comes back as
 * old_fd, for the caller to close off the real-time thread (a close on the stick is a FLUSH request that waits
 * behind the stick's queue). The reader's descriptor number changes (glibc). */
int xz_stem_media_replace_file(struct xz_stem_media *, const struct xz_media_identity *expected,
                               int prepared_fd, int *old_fd);
#endif
