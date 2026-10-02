/* SPDX-License-Identifier: MIT */
#ifndef XZ_LAYERED_WAVE_H
#define XZ_LAYERED_WAVE_H
#include <stdint.h>
#include <stddef.h>

/* What the three bytes of each 1/150 s sample mean, and how they are drawn. */
enum xz_layered_kind {
 XZ_LAYERED_THREE_BAND=0, /* rekordbox PWV7 low/mid/high, CDJ-3000 colours */
 XZ_LAYERED_STEMS=1       /* drums/harmonics/vocals amplitudes, theme stem colours */
};
struct xz_layered_data {
 const uint8_t *bands;
 uint32_t count,normalization;
 unsigned kind;
 uint16_t colors[3]; /* RGB565 per stem, used by XZ_LAYERED_STEMS */
};
/* Heights in rows above the centre line, computed once per column. */
struct xz_layered_col {uint8_t h[3],valid;};

/* Draw the native upper half. Stock subsequently mirrors it and adds markers. */
int xz_layered_column(uint16_t *pixels,size_t stride,unsigned width,unsigned height,
 unsigned x,unsigned center,unsigned extent,const struct xz_layered_data *,
 uint32_t first,uint32_t end,unsigned flags,unsigned monochrome_background,struct xz_layered_col *out);
/* Layer heights for samples [first,end) scaled to extent rows (overview: extent 34, no centre line). */
int xz_layered_heights(const struct xz_layered_data *,uint32_t first,uint32_t end,unsigned extent,struct xz_layered_col *out);
/* The layer colour at d rows above the centre, or -1 where the column shows background. */
int xz_layered_pixel(const struct xz_layered_data *,const struct xz_layered_col *,unsigned d);
uint64_t xz_layered_path_hash(const char *path);
#endif
