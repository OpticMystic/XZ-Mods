/* Deterministic admission cost for the RAM PCM comparison, not a CPU benchmark. */
#define _GNU_SOURCE
#include <stdint.h>
static uint64_t ram_render_us = 1000000;
static uint64_t ram_render_clock(void) { return ram_render_us += 10; }
#define XZ_XP_RENDER_CLOCK_US ram_render_clock
#include "../extra_player.c"
