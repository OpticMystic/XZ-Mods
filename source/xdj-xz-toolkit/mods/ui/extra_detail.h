/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_EXTRA_DETAIL_H
#define XZ_UI_EXTRA_DETAIL_H
/* DECK 3 / DECK 4 detail on the classic four-deck surface (FOUR_DECK.md). Touching a software deck's lane
   opens it over the surface's left half, the older part of every lane, so the playhead at column 268
   and everything after it stay visible. It says in words what the plate abbreviates, carries the
   side's DECK SELECT and a whole-track needle-search strip, and closes from its CLOSE key or the same
   lane. Geometry, hit testing and the needle rule are inline so the touch thread needs nothing else;
   drawing is extra_detail.c, called from the four-deck frame only. Surface coordinates throughout:
   (0,0) is screen (131,18). */
#include "../extra_decks.h"
#include "wave_viewport.h"
#include <stddef.h>
#include <stdint.h>

#define XZ_EXTRA_DETAIL_W 266            /* columns 0..265; 268 is the playhead */
#define XZ_EXTRA_DETAIL_H XZ_WAVE_HEIGHT

enum xz_extra_detail_hit {
 XZ_EXTRA_DETAIL_OUTSIDE=-1,
 XZ_EXTRA_DETAIL_PANEL=0,             /* inert panel area: owned, does nothing */
 XZ_EXTRA_DETAIL_CLOSE,
 XZ_EXTRA_DETAIL_NATIVE_SIDE,         /* this side's controls to DECK 1 (left) or DECK 2 (right) */
 XZ_EXTRA_DETAIL_EXTRA_SIDE,          /* this side's controls to the panel's DECK 3 or DECK 4 */
 XZ_EXTRA_DETAIL_OVERVIEW,            /* whole-track strip: needle search while paused */
 XZ_EXTRA_DETAIL_HITS
};
struct xz_extra_detail_box { int x,y,w,h; };
static inline struct xz_extra_detail_box xz_extra_detail_box(int hit){
 switch(hit){
 case XZ_EXTRA_DETAIL_CLOSE:return (struct xz_extra_detail_box){200,6,62,24};
 case XZ_EXTRA_DETAIL_NATIVE_SIDE:return (struct xz_extra_detail_box){96,32,82,28};
 case XZ_EXTRA_DETAIL_EXTRA_SIDE:return (struct xz_extra_detail_box){180,32,82,28};
 case XZ_EXTRA_DETAIL_OVERVIEW:return (struct xz_extra_detail_box){6,217,254,24};
 case XZ_EXTRA_DETAIL_PANEL:return (struct xz_extra_detail_box){0,0,XZ_EXTRA_DETAIL_W,XZ_EXTRA_DETAIL_H};
 default:return (struct xz_extra_detail_box){0,0,0,0};
 }
}
static inline int xz_extra_detail_inside(struct xz_extra_detail_box b,int x,int y){
 return x>=b.x&&y>=b.y&&x<b.x+b.w&&y<b.y+b.h;
}
/* What a surface point of the open panel is. */
static inline int xz_extra_detail_hit(int x,int y){
 if(!xz_extra_detail_inside(xz_extra_detail_box(XZ_EXTRA_DETAIL_PANEL),x,y))return XZ_EXTRA_DETAIL_OUTSIDE;
 for(int hit=XZ_EXTRA_DETAIL_CLOSE;hit<XZ_EXTRA_DETAIL_HITS;hit++)
  if(xz_extra_detail_inside(xz_extra_detail_box(hit),x,y))return hit;
 return XZ_EXTRA_DETAIL_PANEL;
}
/* Track position under strip column x (clamped to the strip), and the strip column of a position. */
static inline int32_t xz_extra_detail_seek_ms(int x,int32_t total_ms){
 struct xz_extra_detail_box b=xz_extra_detail_box(XZ_EXTRA_DETAIL_OVERVIEW);
 if(total_ms<=0)return 0;
 int dx=x-b.x;if(dx<0)dx=0;else if(dx>b.w-1)dx=b.w-1;
 return (int32_t)((int64_t)dx*total_ms/(b.w-1));
}
static inline int xz_extra_detail_column(int32_t ms,int32_t total_ms){
 struct xz_extra_detail_box b=xz_extra_detail_box(XZ_EXTRA_DETAIL_OVERVIEW);
 if(total_ms<=0)return b.x;
 if(ms<0)ms=0;else if(ms>total_ms)ms=total_ms;
 return b.x+(int)((int64_t)ms*(b.w-1)/total_ms);
}

/* One line the panel's status row shows for a refused touch; the touch thread raises it. */
enum xz_extra_detail_notice {
 XZ_EXTRA_DETAIL_NOTICE_NONE,
 XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING,  /* NEEDLE LOCK: stock default, never while playing */
 XZ_EXTRA_DETAIL_NOTICE_NEEDLE_HELD,     /* released platter coast is still playing queued audio */
 XZ_EXTRA_DETAIL_NOTICE_NO_TRACK,
 XZ_EXTRA_DETAIL_NOTICE_NO_LENGTH,       /* the track's length is not known yet */
 XZ_EXTRA_DETAIL_NOTICE_REFUSED,         /* the player did not take the search */
 XZ_EXTRA_DETAIL_NOTICE_SELECT_FAILED,   /* DECK SELECT refused (players or route not available) */
 XZ_EXTRA_DETAIL_NOTICE_UNAVAILABLE,     /* this runtime has no DECK 3/4 players */
 XZ_EXTRA_DETAIL_NOTICES
};
/* Whether a needle search may move deck d now: NONE, or the notice saying why not. Conservative, as the
   FLAGSHIP shell: playing, unknown play state and released coast stay locked. Physical TOP hold may search. */
static inline int xz_extra_detail_seek_refusal(const struct xz_extra_deck *d){
 uint32_t playing=XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING),total=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL);
 if(!d||!d->loaded)return XZ_EXTRA_DETAIL_NOTICE_NO_TRACK;
 if(!(d->known&total)||d->total_ms<=0)return XZ_EXTRA_DETAIL_NOTICE_NO_LENGTH;
 if(!(d->known&playing)||d->playing)return XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING;
 if(d->held_playing&&!d->platter_contact)return XZ_EXTRA_DETAIL_NOTICE_NEEDLE_HELD;
 return XZ_EXTRA_DETAIL_NOTICE_NONE;
}
/* The status-row text for a notice about deck number 3 or 4, into out. */
const char *xz_extra_detail_notice_text(int notice,unsigned number,char *out,size_t size);

struct xz_extra_detail_view {
 uint32_t now_ms;  /* the players' monotonic clock */
 int notice;       /* enum xz_extra_detail_notice to show now, NONE for the player's notice or the hint */
};
/* Draws deck index 0 (DECK 3) or 1 (DECK 4) into columns 0..XZ_EXTRA_DETAIL_W-1 of a 536-wide surface.
   Writes only the panel rectangle. Fixed cost: no allocation, no IO, bounded overview sampling. */
int xz_extra_detail_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_extra_decks *,int deck,
 const struct xz_extra_detail_view *);

/* The mod's part of one four-deck frame, after the native lanes have shrunk into their slots
   (xz_four_layout_pixels): both DECK 3/4 lanes, the native lanes' deck tags, then the open panel. The
   runtime and the offline previews both draw through this. detail is the panel's deck index or -1. */
struct xz_extra_lane_marks; /* extra_lane.h; marks: XZ_EXTRA_DECKS of them, or NULL */
struct xz_four_frame {
 const struct xz_extra_decks *decks;
 int order;                 /* enum xz_four_order */
 uint32_t now_ms, column_us;
 unsigned background;       /* the native column background mode */
 int detail, notice;        /* open panel (-1 none) and its enum xz_extra_detail_notice */
 int placeholders;          /* DECK 3 and 4 not connected: native tags lit, their lanes a dark tint (route_card.h
                               covers them after the theme); decks may be NULL */
};
int xz_four_frame_draw(uint16_t *pixels,size_t count,const struct xz_four_frame *,struct xz_extra_lane_marks *marks);
/* After a theme pass over the drawn frame: the deck number boxes (deck_colour.h) again, in their own colours,
   where xz_four_frame_draw put them for the same frame. */
void xz_four_identity_draw(uint16_t *pixels,size_t count,const struct xz_four_frame *);
#endif
