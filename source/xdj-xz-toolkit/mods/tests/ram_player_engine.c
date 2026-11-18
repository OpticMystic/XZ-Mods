/* Deterministic admission cost for the RAM PCM comparison, not a CPU benchmark. */
#define _GNU_SOURCE
#include <stdint.h>
static uint64_t ram_render_us = 1000000;
static uint64_t ram_render_clock(void) { return ram_render_us += 10; }
#define XZ_XP_RENDER_CLOCK_US ram_render_clock
#include "../extra_player.c"
/* test_ram_player.c: where a deck is cued, where its reader has read in the file (stem_media.h), and whether its
   sample cache holds the cue. */
uint64_t ram_player_reader(struct xz_xp_players *p, int d, uint64_t *cue, int *cue_cached);
uint64_t ram_player_reader(struct xz_xp_players *p, int d, uint64_t *cue, int *cue_cached) {
 const struct deck *k = &p->deck[d];
 *cue = k->cue;
 *cue_cached = k->r->cached && k->cue >= k->r->begin && k->cue < k->r->begin + CACHE;
 return xz_stem_media_source_offset(k->r->media);
}
