/* SPDX-License-Identifier: MIT */
#ifndef XZ_DECK_VIEW_H
#define XZ_DECK_VIEW_H
/* DECK VIEW (design section 4): how many decks the play screen shows and in what order. The values are
   enum xz_deck_layout (deck_layout.h): 0 2 DECKS, 1 4 DECKS in number order 1 2 3 4, 2 4 DECKS in mixer
   order 3 1 2 4. It changes only the screen: sound, mixer channels and which deck each side controls stay.

   Shared by both interfaces: the words, the top-bar button and the three-picture picker, drawn and hit
   tested from one style per interface. Pure: no IO, no allocation, no clock; callers pass what changes.
   ready means decks 3 and 4 play now (the CH3/CH4 route is up); while it is clear the picture greys them. */
#include "ui_skin.h"
#include "deck_layout.h"

struct xz_dv_rect { int x, y, w, h; };
/* One interface's look. Every colour is 0xRRGGBB. */
struct xz_deck_view_style {
 int classic;                         /* rounded CLASSIC shapes; MODERN is square */
 int theme;                           /* text face theme: atlas text in both interfaces */
 struct xz_dv_rect button;            /* the top-bar button as drawn */
 int button_touch_h;                  /* its touch target runs from y 0 down to this */
 struct xz_dv_rect picker;            /* the picker panel */
 uint32_t button_fill, button_edge, button_open_fill, button_open_edge;
 uint32_t ink, dim, off, amber, green, accent;
 uint32_t panel_fill, panel_edge, tile_fill, tile_sel_fill, tile_edge, tile_sel_edge, focus;
};
extern const struct xz_deck_view_style xz_deck_view_modern, xz_deck_view_classic;

/* Words. name: "2 DECKS" / "4 DECKS"; sub: "DECKS 1 2", "NUMBER ORDER 1 2 3 4", "MIXER ORDER 3 1 2 4". */
const char *xz_deck_view_name(int view);
const char *xz_deck_view_sub(int view);
/* A tile's status line: "LIKE THE PLAYER ITSELF" (DIM), "3 AND 4 NOT CONNECTED" (AMBER) or
   "DECKS 3 AND 4 READY" (GREEN). */
enum xz_deck_view_tone { XZ_DECK_VIEW_TONE_DIM, XZ_DECK_VIEW_TONE_AMBER, XZ_DECK_VIEW_TONE_GREEN };
const char *xz_deck_view_status(int view, int ready, enum xz_deck_view_tone *tone);
/* Deck numbers top to bottom as the play screen draws its lanes (1-based); returns 2 or 4. */
int xz_deck_view_lanes(int view, unsigned char out[4]);
/* An invalid stored value reads as 2 DECKS. */
static inline int xz_deck_view_valid(int view) { return (unsigned)view<XZ_LAYOUT_COUNT?view:XZ_LAYOUT_TWO; }

/* The top-bar button: lane glyph, DECKS, the deck numbers in screen order (3 and 4 grey while not ready),
   a down triangle, and an amber dot while a four-deck view waits for decks 3 and 4. pair names the decks a
   2 DECKS view shows when the sides drive decks 3 or 4 (bit 0: left on 3, bit 1: right on 4; both interfaces
   show that pair, DECK 3 and 4 in their colours). open draws it lit (the picker is showing); focused draws
   the encoder ring. Writes only the button rectangle. */
void xz_deck_view_button_draw(struct xz_canvas c, const struct xz_deck_view_style *s, int view, int ready, int pair, int open, int focused);
/* The button's touch target: its columns, from y 0 to button_touch_h. */
int xz_deck_view_button_hit(const struct xz_deck_view_style *s, int x, int y);

/* The picker: header, three tiles (a mini play screen, the name, a radio, the sub-line and the status
   line). view is the chosen value (blue radio), focus the encoder's tile or -1. Writes only the panel
   and its one-pixel shadow. */
enum { XZ_DECK_VIEW_TILES = 3, XZ_DECK_VIEW_PICKER_TIMEOUT_MS = 6000 };
enum { XZ_DECK_VIEW_HIT_OUTSIDE = -2, XZ_DECK_VIEW_HIT_PANEL = -1 }; /* tiles are 0..2 */
struct xz_dv_rect xz_deck_view_tile(const struct xz_deck_view_style *s, int tile);
int xz_deck_view_picker_hit(const struct xz_deck_view_style *s, int x, int y);
void xz_deck_view_picker_draw(struct xz_canvas c, const struct xz_deck_view_style *s, int view, int ready, int focus);
/* The encoder in the picker: turn moves the focus one tile per detent, stopping at the ends. */
int xz_deck_view_focus_step(int focus, int delta);
/* A miniature play screen of a view (the picker's tile pictures), for other pages to reuse. */
void xz_deck_view_mini_screen(struct xz_canvas c, const struct xz_deck_view_style *s, int x, int y, int w, int h, int view, int ready);

/* Shown once per route-up while DECK VIEW is 2 DECKS (decision D2). */
#define XZ_DECK_VIEW_READY_NOTICE "DECKS 3 AND 4 ARE READY. TAP DECKS AT THE TOP FOR FOUR DECKS."
/* The same notice while SCREEN > DECK VIEW BUTTON hides the DECKS button: it points to MODS instead. */
#define XZ_DECK_VIEW_READY_NOTICE_HIDDEN "DECKS 3 AND 4 ARE READY. DECK VIEW IN MODS SHOWS FOUR DECKS."
/* Both interfaces share one latch, so a route-up raises one notice whichever interface sees it first.
   ready_revision is xz_extra_route_status.ready_revision; 0 (never up) and an already taken edge give 0.
   Lock-free; the only state in this module. */
int xz_deck_view_ready_notice_take(uint32_t ready_revision);
/* 2 DECKS shows the decks the two sides control. When a side switches (SHIFT pressed twice, SHORTCUT > DECK
   SELECT, a panel's DECK SELECT) while the route is up and 2 DECKS shows, both interfaces say where it went
   for this long: "LEFT: DECK 3", "RIGHT: DECK 2", or both sides when both changed at once. before and after
   are pairs as the button's; NULL when no side changed. */
#define XZ_DECK_VIEW_PAIR_NOTICE_MS 2000u
const char *xz_deck_view_pair_notice(unsigned before, unsigned after);
/* Shown when the route comes up under the decks 3 and 4 card (design 5.1, after the wipe). */
#define XZ_DECK_VIEW_SUCCESS_NOTICE "DECKS 3 AND 4 ARE READY"
/* CLASSIC's banner for a route-up. The draw thread calls this once the route is up and its ready revision
   is known: it takes the shared latch and, the first time for that revision, fills out. 2 DECKS: the
   ready notice from now for 3.5 s. A four-deck view: the success notice for 2.5 s once the card's READY
   hold and wipe end, so the banner never covers the card. */
struct xz_deck_view_notice { const char *text; uint32_t from_ms, ms; };
int xz_deck_view_route_notice(int view, uint32_t ready_revision, uint32_t now_ms, struct xz_deck_view_notice *out);
/* The notice's text at now_ms, or NULL outside its window (one clock domain, wrapping at 32 bits). */
const char *xz_deck_view_notice_text(const struct xz_deck_view_notice *, uint32_t now_ms);
#endif
