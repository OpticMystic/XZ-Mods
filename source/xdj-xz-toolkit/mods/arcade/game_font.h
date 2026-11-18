/* Rasterized from bundled OFL font; see ../ui/fonts/OFL.txt. */
#ifndef XZ_GAME_FONT_H
#define XZ_GAME_FONT_H
#include <stdint.h>
struct xz_game_font_glyph { uint32_t offset; uint16_t width,height; int16_t x,y; uint16_t advance; };
extern const struct xz_game_font_glyph xz_game_font_glyphs[4][224];
extern const uint8_t xz_game_font_alpha[];
extern const uint8_t xz_game_font_ascent[4],xz_game_font_height[4],xz_game_font_size[4];
#endif
