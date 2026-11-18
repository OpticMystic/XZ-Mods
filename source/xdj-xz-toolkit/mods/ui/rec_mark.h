/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_REC_MARK_H
#define XZ_UI_REC_MARK_H
/* The REC mark: a small pill in the top band of every screen that says the unit's own recorder is
   writing a file, and for how long. One look, two answers: "recording now" (the red dot) and "this
   has been going a long while" (the time, and after XZ_REC_LONG_MS the whole pill turns red and the
   dot blinks).

   Where it sits (every place is right-anchored, so the pill grows leftwards and nothing it does
   moves a control):
   - MODERN PLAY, two and four decks, every stem layout, BEAT FX lit or not: its own slot between the
     BEAT FX strip and DECKS, x 502..571 (shell_play.c), there whether or not the unit records. It shows
     the dot and the clock; the BEAT FX strip and every control keep their places in every state.
   - MODERN's other screens (BROWSE, INFO, SOURCE, UTILITY, SHORTCUT, MODS settings): the header slot
     left of X, x 674..741, which every shell screen keeps clear (shell.h XZ_SHELL_HEADER_X).
   - CLASSIC, every stock-drawn screen: the top band (top_bar.h), ending two pixels before DECKS's
     column (x 160); it reaches to x 0 while VJ.TOOLS is hidden and starts after it otherwise.
   - The MODS panel, both interfaces: the header, ending at x XZ_REC_PANEL_RIGHT.
   Nothing draws while the recorder is idle or its state is unknown: an unknown state makes no claim.

   Motion: no animation of its own. The pill changes at most twice a second (the clock each second,
   the long state's dot each half second) and never asks for a frame: the MODERN shell and the MODS
   panel redraw on their own cadence, and the CLASSIC band paints with every stock frame. Pure C11:
   no I/O, threads or allocation. */
#include "ui_skin.h"
#include "top_bar.h"
#include "../recorder_state.h"
#include "rec_reading.h"
#include <stddef.h>
#include <stdint.h>

/* A bench build may pass -DXZ_REC_LONG_AFTER_MS=60000u to see the long state after a minute. */
#ifndef XZ_REC_LONG_AFTER_MS
#define XZ_REC_LONG_AFTER_MS (60u*60u*1000u)
#endif
enum {
 XZ_REC_LONG_MS=XZ_REC_LONG_AFTER_MS, /* calm for the first hour, insistent after it */
 XZ_REC_BLINK_MS=500u,           /* the long state's dot: lit for the first half of every second */
 XZ_REC_EXTRAPOLATE_MS=5000u,    /* a reading's clock runs on for at most this long, then holds */
 XZ_REC_HOLD_MS=3000u,           /* failed reads shorter than this keep the last known reading */
 XZ_REC_PANEL_RIGHT=492, XZ_REC_PANEL_Y=10, XZ_REC_PANEL_H=20, XZ_REC_PANEL_ROOM=140,
 XZ_REC_SHELL_H=20,              /* MODERN top bar and header slot */
 XZ_REC_BAND_RIGHT=160,          /* CLASSIC: DECKS starts at 162 (deck_view.c) */
 XZ_REC_BAND_VJ_LEFT=114,        /* CLASSIC: VJ.TOOLS ends at 111 (top_bar.h) */
 XZ_REC_BAND_W=120               /* the widest band pill: columns 40..159 at most */
};
_Static_assert(XZ_REC_BAND_RIGHT==162-2, "the band pill ends two pixels before DECKS (deck_view.c classic button x 162)");

enum xz_rec_level { XZ_REC_NONE, XZ_REC_ON, XZ_REC_LONG };

/* What the pill shows at one instant. */
struct xz_rec_view {
 enum xz_rec_level level;
 int time_known;
 uint32_t elapsed_ms;
 int dot_lit;
};

/* The runtime side: turns successive xz_recorder_state results into readings. The clock is the
   session's, from the press of REC until it stops, so it runs on through a TRACK MARK or the stock
   3-hour split (each starts a new file, a new generation and a new elapsed_ms, recorder/SEAM.md while
   recording stays 1). The session's start is now - elapsed_ms at the first recording reading. Exact
   when this tracker saw the recorder idle before; met mid-file (the mods started during a recording),
   it is the current file's start, a lower bound that leaves out earlier split files. No clock at all
   (elapsed_known 0) until a reading gives a non-zero elapsed_ms or the start was seen. A reading that
   cannot tell (known 0: the seam's two snapshots disagreed, a guard failed) keeps the last known reading
   for XZ_REC_HOLD_MS, so one failed read neither hides the mark nor restarts the clock; after that the
   state is unknown and nothing draws. */
struct xz_rec_tracker { uint32_t start_ms, known_at_ms; uint8_t active, saw_idle, timed; struct xz_rec_reading last; };
struct xz_rec_reading xz_rec_observe(struct xz_rec_tracker *, const struct xz_recorder_state *, uint32_t now_ms);

struct xz_rec_view xz_rec_view_at(const struct xz_rec_reading *, uint32_t now_ms);
/* "0:03", "59:59", "1:12:40"; holds at "99:59:59". */
void xz_rec_clock(char out[12], uint32_t ms);

/* Parts the pill can carry beyond the dot. */
enum { XZ_REC_PART_WORD=1u, XZ_REC_PART_TIME=2u };
/* The pill's width for parts at height h with the clock at elapsed_ms. Clock digits are tabular, so the
   width changes only when the form does (MM:SS to H:MM:SS at one hour, with the long state). */
int xz_rec_width(struct xz_canvas c, unsigned parts, int h, uint32_t elapsed_ms);
/* The richest parts that fit room: dot, REC and clock; then dot and clock; then dot and REC; then the
   dot alone. The clock is left out while v->time_known is 0. */
unsigned xz_rec_parts(struct xz_canvas c, int room, int h, const struct xz_rec_view *v);
/* The width xz_rec_draw will paint for v in room, 0 when it paints nothing. */
int xz_rec_box(struct xz_canvas c, int room, int h, const struct xz_rec_view *v);
/* Draws the pill for v with its right edge at right, top y, height h, using at most room columns.
   Returns the pill's left edge, or right when nothing was drawn. Paints only inside its own box. */
int xz_rec_draw(struct xz_canvas c, int right, int y, int h, int room, const struct xz_rec_view *v);

/* CLASSIC top band. Stock redraws only its dirty regions, so a pill that stops (recording ended) would
   stay on the black band: every stock-drawn frame first restores the stock pixels under the last paint,
   but only if that paint is still on screen (fresh stock pixels always win, as the LOAD ERR badge,
   transient_notice.h), then draws. Only the pill's own box is saved: rows 0..XZ_TOP_BAR_H-1, at most
   XZ_REC_BAND_W columns ending at XZ_REC_BAND_RIGHT. The paint is remembered by a checksum, not a copy,
   which halves the save (4.3 KB). vj_shown: VJ.TOOLS is on the band this frame. Returns the pill's width. */
struct xz_rec_band_pixels {
 uint16_t base[XZ_TOP_BAR_H][XZ_REC_BAND_W];
 uint32_t paint_sum;
 int valid, x, w;                /* the columns of the last paint */
};
void xz_rec_band_restore(struct xz_rec_band_pixels *, uint16_t *pixels, size_t stride);
int xz_rec_band_draw(uint16_t *pixels, size_t stride, int theme, int vj_shown, const struct xz_rec_view *, struct xz_rec_band_pixels *);
#endif
