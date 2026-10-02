/* SPDX-License-Identifier: MIT */
#ifndef XZ_LAYERED_WAVE_SOURCE_H
#define XZ_LAYERED_WAVE_SOURCE_H
#include <stddef.h>
#include <stdint.h>

/* Worker-thread loaders. They read analysis already on a rekordbox USB and
 * never write: 3-band from the track's own ANLZ0000.2EX, stems from the
 * per-stem analysis an OverCue bundle carries. No preparation step. */
struct xz_wave_load {uint8_t *bands;uint32_t count,normalization;unsigned kind;};

/* source is the deck reader's absolute path, e.g. /media/usb1/sda2/Contents/... */
int xz_wave_load_three_band(const char *source,struct xz_wave_load *out);
int xz_wave_load_stems(const char *source,struct xz_wave_load *out);
void xz_wave_load_free(struct xz_wave_load *);

/* Exposed for tests. */
int xz_anlz_section(const uint8_t *file,size_t n,const char tag[4],uint32_t entry_size,size_t *entries,uint32_t *count);
int xz_anlz_track_path(const uint8_t *file,size_t n,char *out,size_t capacity);
uint32_t xz_wave_normalization(const uint8_t *bands,uint32_t count,unsigned kind);
/* Forget the cached USBANLZ index (tests, or after a USB is removed). */
void xz_wave_index_reset(void);
#endif
