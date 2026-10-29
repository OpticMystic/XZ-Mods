/* Host-only deterministic clock. All other bridge functions retain their source. */
#define xz_game_milliseconds unused_wall_milliseconds
#include "../doom/native_bridge.c"
#undef xz_game_milliseconds
static uint32_t nav_ms;
void nav_clock_set(uint32_t ms){nav_ms=ms;}
uint32_t xz_game_milliseconds(void){return nav_ms;}
