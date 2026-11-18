/* SPDX-License-Identifier: MIT */
#ifndef XZ_ROUTE_CARD_H
#define XZ_ROUTE_CARD_H
/* Decks 3 and 4 not connected (design section 5): the card a four-deck DECK VIEW shows in the lanes of
   decks 3 and 4 while their route is not up. Row A sits in deck 3's lane: a picture of the loopback
   cable route and the two steps to connect it. Row B sits in deck 4's lane: the state, one or two plain
   lines, RESCAN (or TRY AGAIN) and TWO DECKS. In number order the lanes touch and the rows join into
   one card; in mixer order they are two strips with the same content. Decks 1 and 2 are never covered.

   Every state comes from the runtime's xz_extra_route_status (state, fail_part, reason); the card never
   invents one. The picture is the cable explainer (cable_anim.h): the unit in true perspective with the
   loopback cable, playing its loop while the DJ has a cable to fix (not detected, failed, lost), showing
   its main shot with the signal running along the cable while the route comes up, tinted by the state,
   the failing part red and marked. The loop starts on the main shot when the picture starts moving (its
   clock is kept in the cache), and 60 s after the card appears or after the last input it plays on to the
   main shot and rests there; the rest of the motion is a pure function of the frame clock. The still card is drawn once
   per state into a caller-owned cache and copied each frame; the picture is redrawn into the cache's own
   copy only when its frame changes, at the caller's step. Without a cache the picture is the earlier
   flat drawing, still.
   Pure: no IO, no allocation, no clock of its own. */
#include "ui_skin.h"
#include "../extra_decks.h"
#include "cable_anim.h"

enum xz_route_card_kind {
 XZ_ROUTE_CARD_NOT_DETECTED, XZ_ROUTE_CARD_SCANNING, XZ_ROUTE_CARD_STARTING, XZ_ROUTE_CARD_WAITING,
 XZ_ROUTE_CARD_READY, XZ_ROUTE_CARD_FAILED, XZ_ROUTE_CARD_LOST, XZ_ROUTE_CARD_NO_ENGINE, XZ_ROUTE_CARD_KINDS
};
/* One interface's card. Offsets are from the card's left edge and the top of the row they sit in. */
struct xz_route_card_rect { int x, y, w, h; };
struct xz_route_card_style {
 int classic, theme;
 int x, w;                                    /* card columns */
 int pitch, row_h, accent_h;                  /* lane-to-lane distance; a row's height; top accent */
 struct xz_route_card_rect picture;           /* row A */
 int head_x, head_dy, step_dy[2];             /* row A: headline and the two steps */
 int dot_dx, dot_dy, dot_r, title_dx, title_dy, line_dx, line_dy[2]; /* row B text */
 struct xz_route_card_rect primary, two;      /* row B buttons */
 int labels;                                  /* "USB 1" / "REAR USB" in the picture */
 uint32_t fill, edge, rule, ink, text, step_text, amber, blue, green, red, white, grey;
 uint32_t primary_fill, primary_ink, scan_fill, scan_track, two_fill, two_edge, two_ink, focus;
 uint32_t body, body_edge;                    /* the unit in the picture */
};
extern const struct xz_route_card_style xz_route_card_modern, xz_route_card_classic;

/* What the card shows this frame. Times are the caller's frame clock (one domain, wrapping at 32 bits). */
struct xz_route_card_input {
 int state, fail_part;      /* enum xz_extra_route_state / xz_extra_route_part */
 const char *reason;        /* the runtime's one sentence, may be NULL */
 uint32_t now_ms;
 uint32_t rest_from_ms;     /* card appeared, its state changed, or the last touch / encoder input */
 uint32_t ready_at_ms;      /* the route came up under the card (state READY), 0 when not */
 int focus;                 /* encoder focus: XZ_ROUTE_CARD_HIT_PRIMARY / _TWO, or -1 */
 uint32_t step_ms;          /* the picture's frame step: 0 = XZ_ROUTE_CARD_FRAME_MS; more while a native
                               deck plays or frames run late (the picture then changes less often) */
};
/* Hit results. PRIMARY is RESCAN or TRY AGAIN; TWO is TWO DECKS; CARD is the rest of the card. */
enum { XZ_ROUTE_CARD_HIT_NONE=-1, XZ_ROUTE_CARD_HIT_CARD=0, XZ_ROUTE_CARD_HIT_PRIMARY=1, XZ_ROUTE_CARD_HIT_TWO=2 };

/* The card's still picture (everything but the motion), one buffer per row, cached by the caller between
   frames: drawn at a change of state, focus or sentence, copied every frame, the motion drawn over it. */
#define XZ_ROUTE_CARD_ROW_MAX (702*78)
#define XZ_ROUTE_CARD_PIC_MAX (262*68)
struct xz_route_card_cache {
 uint32_t key; uint16_t row[2][XZ_ROUTE_CARD_ROW_MAX];
 uint32_t pic_key, pic_base;  /* the picture's frame, and its state and part */
 struct xz_cable_clock pic_clock;  /* its loop's clock */
 uint16_t pic[XZ_ROUTE_CARD_PIC_MAX]; uint8_t pic_cov[XZ_ROUTE_CARD_PIC_MAX];
};

enum xz_route_card_kind xz_route_card_kind(int state);
/* The success hold and wipe after the route comes up under the card. */
enum { XZ_ROUTE_CARD_READY_HOLD_MS=700, XZ_ROUTE_CARD_WIPE_FRAMES=8, XZ_ROUTE_CARD_WIPE_MS=240,
       XZ_ROUTE_CARD_REST_MS=60000, XZ_ROUTE_CARD_FRAME_MS=16 };
/* The most pixel operations one picture frame may cost (cable_anim.h budget): a frame over it is skipped and
   the last one stays. Host bench and qemu numbers: cable-anim/RESEARCH.md. */
#define XZ_ROUTE_CARD_PIC_BUDGET 90000u
/* Whether the card shows at all this frame: always while the route is not ready, and for the success
   hold and wipe after ready_at_ms. */
int xz_route_card_visible(const struct xz_route_card_input *);
/* The column left of which the live lanes already show during the wipe (the card's x before it). */
int xz_route_card_wipe_x(const struct xz_route_card_style *, const struct xz_route_card_input *);
/* Which buttons this state offers: PRIMARY when RESCAN / TRY AGAIN can be pressed, TWO always but READY. */
int xz_route_card_offers(const struct xz_route_card_input *, int hit);
/* The encoder's focus order: actionable buttons left to right; returns how many (0..2). */
int xz_route_card_focus_order(const struct xz_route_card_input *, int out[2]);
/* 0 when nothing on the card moves at now_ms; else the frame period the motion wants (the picture's step). */
uint32_t xz_route_card_motion_ms(const struct xz_route_card_input *);
/* Draws the card for the lanes whose tops are ya (deck 3) and yb (deck 4), in canvas coordinates; the rows
   join when yb == ya + pitch. cache may be NULL (the picture is then drawn directly). Writes only the card. */
void xz_route_card_draw(struct xz_canvas c, const struct xz_route_card_style *, int ya, int yb,
 const struct xz_route_card_input *, struct xz_route_card_cache *cache);
/* What the point is on the card drawn with the same ya, yb and input. */
int xz_route_card_hit(const struct xz_route_card_style *, int ya, int yb, const struct xz_route_card_input *, int x, int y);
/* The card's rectangles in canvas coordinates: rows A and B (equal when joined counts as one), buttons. */
struct xz_route_card_rect xz_route_card_row(const struct xz_route_card_style *, int ya, int yb, int row);
struct xz_route_card_rect xz_route_card_button(const struct xz_route_card_style *, int yb, int hit);
#endif
