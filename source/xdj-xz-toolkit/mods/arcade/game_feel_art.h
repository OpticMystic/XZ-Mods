/* Artist-owned local prototype assets: VJ OpticMystic. */
#ifndef XZ_GAME_FEEL_ART_H
#define XZ_GAME_FEEL_ART_H
#include <stdint.h>
#define XZ_FEEL_CAT_FRAMES 8
#define XZ_FEEL_CAT_SIZE 128
#define XZ_FEEL_CAT_ANCHOR_X 72
#define XZ_FEEL_CAT_ANCHOR_Y 118
#define XZ_FEEL_COW_FRAMES 4
#define XZ_FEEL_COW_SIZE 96
#define XZ_FEEL_SCENERY_FRAMES 4
#define XZ_FEEL_SCENERY_SIZE 192
#define XZ_FEEL_BG_W 768
#define XZ_FEEL_BG_H 216
extern const uint16_t xz_feel_cat_rgb[8][128*128];
extern const uint8_t xz_feel_cat_alpha[8][128*128];
extern const uint16_t xz_feel_cow_rgb[4][96*96];
extern const uint8_t xz_feel_cow_alpha[4][96*96];
extern const uint16_t xz_feel_scenery_rgb[4][192*192];
extern const uint8_t xz_feel_scenery_alpha[4][192*192];
extern const uint16_t xz_feel_background[3][768*216];
#endif
