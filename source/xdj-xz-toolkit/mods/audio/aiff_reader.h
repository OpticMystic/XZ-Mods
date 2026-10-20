/* SPDX-License-Identifier: MIT
 * Bounded AIFF / AIFF-C PCM reader. Format: Apple Inside Macintosh: Sound,
 * AIFF/AIFF-C chunks (COMM, SSND, FVER). No compressed-codec fallback. */
#ifndef XZ_AIFF_READER_H
#define XZ_AIFF_READER_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct xz_aiff {
 FILE *file;
 uint64_t data, frames;
 uint32_t rate;
 unsigned channels, bits, little, floating;
};
static uint32_t xz_aiff_u32(const unsigned char *b) {
 return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static unsigned xz_aiff_u16(const unsigned char *b) { return (unsigned)b[0] << 8 | b[1]; }
static int xz_aiff_seek(struct xz_aiff *a, uint64_t frame) {
 if (frame > a->frames) return 0;
 uint64_t offset = a->data + frame * a->channels * (a->bits / 8);
 return !fseeko(a->file, (int64_t)offset, SEEK_SET);
}
static int xz_aiff_open(struct xz_aiff *a, const char *path) {
 unsigned char h[24];
 memset(a, 0, sizeof *a);
 a->file = fopen(path, "rb");
 if (!a->file) return 0;
 if (fseeko(a->file, 0, SEEK_END)) return 0;
 int64_t bytes = ftello(a->file);
 if (bytes < 12 || fseeko(a->file, 0, SEEK_SET) || fread(h, 1, 12, a->file) != 12 ||
     memcmp(h, "FORM", 4)) return 0;
 int compressed = !memcmp(h + 8, "AIFC", 4);
 if (!compressed && memcmp(h + 8, "AIFF", 4)) return 0;
 uint64_t end = 8u + (uint64_t)xz_aiff_u32(h + 4);
 if (end < 12 || end > (uint64_t)bytes) return 0;
 int common = 0, sound = 0, version = 0;
 uint64_t data_bytes = 0;
 unsigned chunks = 0;
 for (uint64_t at = 12; at < end;) {
  if (++chunks > 4096) return 0;
  if (end - at < 8 || fseeko(a->file, (int64_t)at, SEEK_SET) ||
      fread(h, 1, 8, a->file) != 8) return 0;
  uint32_t size = xz_aiff_u32(h + 4);
  uint64_t next = at + 8u + size + (size & 1u);
  if (next > end) return 0;
  if (!memcmp(h, "COMM", 4)) {
   unsigned need = compressed ? 23u : 18u;
   if (common++ || size < need || fread(h, 1, need, a->file) != need) return 0;
   a->channels = xz_aiff_u16(h);
   a->frames = xz_aiff_u32(h + 2);
   a->bits = xz_aiff_u16(h + 6);
   unsigned exponent = xz_aiff_u16(h + 8);
   uint64_t mantissa = (uint64_t)xz_aiff_u32(h + 10) << 32 | xz_aiff_u32(h + 14);
   if (exponent & 0x8000u || exponent == 0x7fffu || !(mantissa >> 63)) return 0;
   double rate = ldexp((double)mantissa, (int)exponent - 16383 - 63);
   if (!isfinite(rate) || rate < 32000 || rate > 96000 || fabs(rate - floor(rate + .5)) > .001) return 0;
   a->rate = (uint32_t)(rate + .5);
   if (compressed) {
    if ((uint32_t)23 + h[22] > size) return 0;
    if (!memcmp(h + 18, "sowt", 4)) a->little = 1;
    else if (!memcmp(h + 18, "fl32", 4) || !memcmp(h + 18, "FL32", 4)) a->floating = 1;
    else if (memcmp(h + 18, "NONE", 4) && memcmp(h + 18, "twos", 4)) return 0;
   }
   if (a->channels != 2 || !a->frames ||
       (a->bits != 8 && a->bits != 16 && a->bits != 24 && a->bits != 32) ||
       (a->floating && a->bits != 32)) return 0;
  } else if (!memcmp(h, "SSND", 4)) {
   if (sound++ || size < 8 || fread(h, 1, 8, a->file) != 8) return 0;
   uint32_t offset = xz_aiff_u32(h);
   if (offset > size - 8) return 0;
   a->data = at + 16 + offset;
   data_bytes = size - 8 - offset;
  } else if (!memcmp(h, "FVER", 4)) {
   if (version++ || size != 4 || fread(h, 1, 4, a->file) != 4 || xz_aiff_u32(h) != 0xa2805140u) return 0;
  }
  at = next;
 }
 if (!common || !sound || (compressed && !version) ||
     a->frames * a->channels * (a->bits / 8) > data_bytes) return 0;
 return xz_aiff_seek(a, 0);
}
static uint64_t xz_aiff_read(struct xz_aiff *a, size_t frames, float *out) {
 unsigned char buffer[4096];
 unsigned width = a->bits / 8, frame_bytes = width * a->channels;
 size_t done = 0;
 while (done < frames) {
  size_t block = frames - done;
  if (block > sizeof buffer / frame_bytes) block = sizeof buffer / frame_bytes;
  if (fread(buffer, frame_bytes, block, a->file) != block) return done;
  for (size_t i = 0; i < block * 2; ++i) {
   const unsigned char *b = buffer + i * width;
   uint32_t value = 0;
   for (unsigned j = 0; j < width; ++j)
    value = value << 8 | b[a->little ? width - j - 1 : j];
   float sample;
   if (a->floating) memcpy(&sample, &value, sizeof sample);
   else {
    int64_t signed_value = value;
    if ((uint64_t)value & ((uint64_t)1 << (a->bits - 1))) signed_value -= (int64_t)((uint64_t)1 << a->bits);
    sample = (float)((double)signed_value / (double)((uint64_t)1 << (a->bits - 1)));
   }
   if (!isfinite(sample)) return done;
   out[(done * 2) + i] = sample;
  }
  done += block;
 }
 return done;
}
#endif
