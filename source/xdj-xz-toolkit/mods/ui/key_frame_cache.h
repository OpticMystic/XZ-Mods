/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_FRAME_CACHE_H
#define XZ_KEY_FRAME_CACHE_H
#include "key_strip.h"
#include <string.h>

/* Owned by one presentation thread. A busy model publisher must not expose
   the native countdown for one flip; replay the last coherent key snapshot. */
struct xz_key_frame_cache {
    int valid,theme,enabled;
    struct xz_key_strip_deck deck[2];
};
static inline void xz_key_frame_cache_store(struct xz_key_frame_cache *cache,
        int theme,int enabled,int takeover,const struct xz_key_strip_deck deck[2]) {
    cache->theme=theme;cache->enabled=!!enabled&&!takeover;
    memcpy(cache->deck,deck,sizeof(cache->deck));cache->valid=1;
}
static inline int xz_key_frame_cache_draw(const struct xz_key_frame_cache *cache,
        uint16_t *pixels,size_t stride) {
    if(!cache->valid||!cache->enabled)return 0;
    int drawn=0;
    for(int deck=0;deck<2;deck++)
        drawn|=xz_key_strip_render_deck(pixels,stride,cache->theme,deck,&cache->deck[deck],1)>0;
    return drawn;
}
#endif
