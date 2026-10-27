/* SPDX-License-Identifier: MIT */
#ifndef XZ_BAND_COLORS_H
#define XZ_BAND_COLORS_H
#include "stem_colors.h"
/* The 3-BAND waveform's colours, one choice per band. 0 STANDARD keeps each renderer's own colour (MODERN's
   theme_flagship.h XZ_FLAGSHIP_WAVE_*, CLASSIC's and decks 3 and 4's layered_wave.c); 1..8 are the stem
   palette's fixed colours, so a band and a stem can share a colour by name. Stored numbers never change. */
enum xz_wave_band { XZ_BAND_LOW, XZ_BAND_MID, XZ_BAND_HIGH, XZ_BANDS };
#define XZ_BAND_COLOR_CHOICES XZ_STEM_COLOR_CHOICES
static inline uint32_t xz_band_palette_color(int choice,uint32_t standard) {
 return xz_stem_palette_color(choice,standard);
}
static inline const char *xz_band_palette_name(int choice) {
 return choice>0&&choice<XZ_BAND_COLOR_CHOICES?xz_stem_palette_name(choice):"STANDARD";
}
#endif
