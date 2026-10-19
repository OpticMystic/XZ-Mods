/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_ANALYSIS_H
#define XZ_SHELL_ANALYSIS_H
/* Track -> rekordbox analysis folder for one USB, for the FLAGSHIP shell's waveforms.

   ui/layered_wave_source.c keeps one thread-local index for the last root it saw and rebuilds it
   (opening every ANLZ0000.DAT on the stick) whenever the root changes or a lookup misses for 15 s.
   With deck 1 on USB1 and deck 2 on USB2 every wave load rebuilt it. This index is built once per
   mounted USB on the shell worker, kept per USB, and rebuilt only when that USB's export changes or
   a lookup misses (at most every XZ_SHANA_MISS_REBUILD_MS). It stores the PPTH path hash and the
   folder name relative to PIONEER/USBANLZ, and a hit is confirmed by reading that folder's DAT PPTH.
   Worker thread only; no locking. */
#include <stddef.h>
#include <stdint.h>
#include "ui/layered_wave_source.h"

#define XZ_SHANA_MISS_REBUILD_MS 30000u
struct xz_shana;
/* root: the folder that holds PIONEER (e.g. /media/usb1/sda2). Returns 0 and *out (possibly with no
   entries when the stick has no USBANLZ). *files: DAT/EXT files read. */
int xz_shana_build(const char *root, struct xz_shana **out, uint32_t *files);
void xz_shana_free(struct xz_shana *);
const char *xz_shana_root(const struct xz_shana *);
uint32_t xz_shana_count(const struct xz_shana *);
/* track: the path below root, starting "/Contents/". Writes the absolute analysis folder. 0 = found. */
int xz_shana_find(const struct xz_shana *, const char *track, char *dir, size_t capacity);
/* Reads PWV7 from dir/ANLZ0000.2EX, as xz_wave_load_three_band does after its lookup. */
int xz_shana_three_band(const char *dir, struct xz_wave_load *out);
/* Splits an absolute deck path at "/Contents/": root and track (pointing into source). */
int xz_shana_split(const char *source, char *root, size_t capacity, const char **track);
#endif
