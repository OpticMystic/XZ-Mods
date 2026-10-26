/* SPDX-License-Identifier: MIT */
#ifndef XZ_ROUTE_CARD_H
#define XZ_ROUTE_CARD_H
/* Decks 3 and 4 not connected (design section 5): the card a four-deck DECK VIEW shows in the lanes of
   decks 3 and 4 while their route is not up. Row A sits in deck 3's lane: a picture of the loopback
   cable route and the two steps to connect it. Row B sits in deck 4's lane: the state, one or two plain
   lines, RESCAN (or TRY AGAIN) and TWO DECKS. In number order the lanes touch and the rows join into
   one card; in mixer order they are two strips with the same content. Decks 1 and 2 are never covered.

   Every state comes from the runtime's xz_extra_route_status (state, fail_part, reason); the card never
   invents one. Motion is mild and cheap: a three-dot pulse along the cable, a ring and a sweep while
   scanning, all a pure function of the frame clock, resting 60 s after the card appears or after the
   last input. The still card is drawn once per state into a caller-owned cache and copied each frame;
   only the moving parts are drawn on top.
   Pure: no IO, no allocation, no clock of its own. */
#include "ui_skin.h"
#include "../extra_decks.h"

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
};
/* Hit results. PRIMARY is RESCAN or TRY AGAIN; TWO is TWO DECKS; CARD is the rest of the card. */
enum { XZ_ROUTE_CARD_HIT_NONE=-1, XZ_ROUTE_CARD_HIT_CARD=0, XZ_ROUTE_CARD_HIT_PRIMARY=1, XZ_ROUTE_CARD_HIT_TWO=2 };

/* The card's still picture (everything but the motion), one buffer per row, cached by the caller between
   frames: drawn at a change of state, focus or sentence, copied every frame, the motion drawn over it. */
#define XZ_ROUTE_CARD_ROW_MAX (702*78)
struct xz_route_card_cache { uint32_t key; uint16_t row[2][XZ_ROUTE_CARD_ROW_MAX]; };

enum xz_route_card_kind xz_route_card_kind(int state);
/* The success hold and wipe after the route comes up under the card. */
enum { XZ_ROUTE_CARD_READY_HOLD_MS=700, XZ_ROUTE_CARD_WIPE_FRAMES=8, XZ_ROUTE_CARD_WIPE_MS=240,
       XZ_ROUTE_CARD_REST_MS=60000, XZ_ROUTE_CARD_FRAME_MS=50 };
/* Whether the card shows at all this frame: always while the route is not ready, and for the success
   hold and wipe after ready_at_ms. */
int xz_route_card_visible(const struct xz_route_card_input *);
/* The column left of which the live lanes already show during the wipe (the card's x before it). */
int xz_route_card_wipe_x(const struct xz_route_card_style *, const struct xz_route_card_input *);
/* Which buttons this state offers: PRIMARY when RESCAN / TRY AGAIN can be pressed, TWO always but READY. */
int xz_route_card_offers(const struct xz_route_card_input *, int hit);
/* The encoder's focus order: actionable buttons left to right; returns how many (0..2). */
int xz_route_card_focus_order(const struct xz_route_card_input *, int out[2]);
/* 0 when nothing on the card moves at now_ms; else the frame period the motion wants (20 fps). */
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
