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
#endif
