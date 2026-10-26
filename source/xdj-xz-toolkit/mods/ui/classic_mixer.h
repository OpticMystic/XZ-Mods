/* SPDX-License-Identifier: MIT */
#ifndef XZ_CLASSIC_MIXER_H
#define XZ_CLASSIC_MIXER_H
#include "../shell/shell.h"
#include "../extra_decks.h"
#include <stddef.h>
#include "deck_layout.h"
/* Parent integration: native must contain two guarded shell snapshots, including
   XZ_SHELL_F_AUTO_CUE/auto_cue. No native reads occur here. Overview inputs are
   immutable, validated 300x34 RGB565 caches, including their original markers.
   Only overview rectangles answer seeking; titles/status are not buttons.
   Physical controls and the parent's DECK3/4 detail overlay own other actions. */
struct xz_classic_mixer_view {
 const struct xz_shell_deck *native;
 const struct xz_extra_decks *extra;
 uint32_t now_ms;
 int theme;
 int layout;
 const uint16_t *native_overview[2];
};
struct xz_classic_mixer_rect { int x,y,w,h; };
/* Geometry is header-only so touch routing needs no rendering closure. */
/* Logical deck 0..3, or -1 outside x0..799/y311..479. */
static inline int xz_classic_mixer_channel_at(int layout,int x,int y){return layout!=XZ_LAYOUT_TWO&&x>=0&&x<800&&y>=311&&y<480?xz_deck_layout_order(layout)[x/200]:-1;}
/* Absolute screen box; invalid logical deck returns an empty box. */
static inline struct xz_classic_mixer_rect xz_classic_mixer_overview_rect(int layout,int d){int s=-1;if(layout!=XZ_LAYOUT_TWO)for(int i=0;i<4;i++)if(xz_deck_layout_order(layout)[i]==d)s=i;return s<0?(struct xz_classic_mixer_rect){0,0,0,0}:(struct xz_classic_mixer_rect){s*200+6,440,188,34};}
/* Clamp x to the overview endpoints. Invalid deck/duration returns -1. */
static inline int32_t xz_classic_mixer_seek_ms(int layout,int d,int x,int32_t total){struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(layout,d);if(!r.w||total<=0)return -1;int64_t p=(int64_t)x-r.x;if(p<0)p=0;if(p>=r.w)p=r.w-1;return (int32_t)((int64_t)p*total/(r.w-1));}
/* Native decks only: local 300x34 coordinate, only for points inside the box. */
static inline int xz_classic_mixer_native_point(int layout,int d,int x,int y,int *nx,int *ny){struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(layout,d);if(d<0||d>1||!nx||!ny||x<r.x||x>=r.x+r.w||y<r.y||y>=r.y+r.h)return 0;*nx=(x-r.x)*299/(r.w-1);*ny=y-r.y;return 1;}
/* Returns 1 only when drawn. Requires full 800x480 capacity; no writes outside
   x0..799,y311..479 or to stride padding. No allocation, IO or model mutation. */
int xz_classic_mixer_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_classic_mixer_view *view);
#endif
