/* Artwork supplied by VJ OpticMystic. Local prototype assets. */
#ifndef XZ_GLYPH_ART_H
#define XZ_GLYPH_ART_H
#include <stdint.h>
#define XZ_GLYPH_BG_W 400
#define XZ_GLYPH_BG_H 188
#define XZ_GLYPH_CAT_SIZE 96
extern const uint16_t xz_glyph_background[3][XZ_GLYPH_BG_W*XZ_GLYPH_BG_H];
extern const uint16_t xz_glyph_cat_rgb[XZ_GLYPH_CAT_SIZE*XZ_GLYPH_CAT_SIZE];
extern const uint8_t xz_glyph_cat_alpha[XZ_GLYPH_CAT_SIZE*XZ_GLYPH_CAT_SIZE];
extern const uint16_t xz_glyph_cow_rgb[96*96];
extern const uint8_t xz_glyph_cow_alpha[96*96];
#endif
