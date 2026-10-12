/* SPDX-License-Identifier: MIT */
#ifndef XZ_FOREIGN_STEMS_H
#define XZ_FOREIGN_STEMS_H
#include <stddef.h>
#include <stdint.h>
struct xz_foreign_stems;
enum xz_foreign_format { XZ_FOREIGN_AZ = 1, XZ_FOREIGN_RX3 = 2 };
/* Worker-only, read-only adapters. Roles are drums, other, vocals, bass.
 * A successful open proves track identity and a complete four-part bundle.
 * Native playback must still measure alignment before using any samples. */
struct xz_foreign_stems *xz_foreign_open(const char *source, char *error, size_t capacity);
void xz_foreign_close(struct xz_foreign_stems *);
uint32_t xz_foreign_rate(const struct xz_foreign_stems *);
uint64_t xz_foreign_frames(const struct xz_foreign_stems *);
unsigned xz_foreign_stem_count(const struct xz_foreign_stems *);
enum xz_foreign_format xz_foreign_format(const struct xz_foreign_stems *);
int xz_foreign_reference(struct xz_foreign_stems *, int64_t first, size_t frames, float *stereo);
int xz_foreign_read(struct xz_foreign_stems *, unsigned mask, int64_t first, size_t frames, float *stereo);
#endif
