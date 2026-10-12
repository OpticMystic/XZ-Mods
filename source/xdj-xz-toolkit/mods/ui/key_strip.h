/* SPDX-License-Identifier: MIT */
#ifndef XZ_UI_KEY_STRIP_H
#define XZ_UI_KEY_STRIP_H
/* Always-visible key readout and browse-table header, drawn on the final 800x480
   frame (the same pass as the MODS badge, which is proven on the LCD).

   Both surfaces draw only into reserved bounds of a stock surface, and only when
   that stock surface is actually on screen. Each draw first samples stock pixels
   just outside its own bounds and must find the stock background there (theme-
   mapped). A frame without the stock surface draws nothing. */
#include <stddef.h>
#include <stdint.h>

/* Main play: replace only the Beat Countdown slot beneath the native key.
   Browse: compact rows in the spare table-header space, never in track titles. */
enum { XZ_KEY_STRIP_PLAY=1, XZ_KEY_STRIP_BROWSE=2 };
struct xz_key_strip_box { int x,y,w,h,kind; };
int xz_key_strip_area(const uint16_t *,size_t,int theme,int deck,struct xz_key_strip_box *);

struct xz_key_strip_deck {
    int valid;          /* native key snapshot exists for a loaded track */
    char original[8];   /* stock-format original key, e.g. "4A" */
    char effective[8];  /* what the deck plays now: shift + tempo, e.g. "9A" */
    int cents;          /* detune left after the semitone, -50..50 */
    int moved;          /* effective key differs from the original */
    int synced;         /* last KEY SYNC still holds */
    int sync_master;    /* reference deck for the other deck's KEY SYNC */
    char error[40];     /* inline actionable error; "" when none */
};

/* 1 when both deck title bars are on screen (stock pixels at the sample points). */
int xz_key_strip_decks_present(const uint16_t *pixels, size_t stride, int theme, int deck);
/* Returns the drawn width, 0 when nothing was drawn. */
int xz_key_strip_render_deck(uint16_t *pixels, size_t stride, int theme, int deck,
    const struct xz_key_strip_deck *d, int enabled);

/* Browse table header: the stock title bar y 3..32 between the page title and the
   STEMS/MODS buttons. Two header cells name the stock MENU click choices over the
   columns they change: SORT over the title column, COLUMN over the right column. */
enum { XZ_BROWSE_HEAD_SORT, XZ_BROWSE_HEAD_COLUMN, XZ_BROWSE_HEAD_COUNT };
enum { XZ_BROWSE_HEAD_Y = 4, XZ_BROWSE_HEAD_H = 25, XZ_BROWSE_HEAD_LEFT = 300, XZ_BROWSE_HEAD_RIGHT = 668 };
struct xz_browse_head_cell { int x, w; };
static inline struct xz_browse_head_cell xz_browse_head_cell(int which) {
    return which == XZ_BROWSE_HEAD_SORT ? (struct xz_browse_head_cell){ 380, 124 }
                                        : (struct xz_browse_head_cell){ 512, 156 };
}
struct xz_browse_head {
    int interactive;  /* a native menu operation is linked; else the cells are labels only */
    int pressed;      /* -1, or the cell under a held finger */
    const char *notice; /* short red line after a failed tap, or NULL */
};
int xz_browse_head_present(const uint16_t *pixels, size_t stride, int theme);
int xz_browse_head_render(uint16_t *pixels, size_t stride, int theme, const struct xz_browse_head *h);
/* -1 or XZ_BROWSE_HEAD_SORT / XZ_BROWSE_HEAD_COLUMN. */
int xz_browse_head_hit(int x, int y);

/* Inline Shift+Sync failure text: what to do, in a few words. */
enum xz_key_sync_failure {
    XZ_KEY_SYNC_OK, XZ_KEY_SYNC_NO_ENGINE, XZ_KEY_SYNC_NO_KEY, XZ_KEY_SYNC_NO_MASTER_KEY,
    XZ_KEY_SYNC_IS_MASTER, XZ_KEY_SYNC_NEED_MASTER, XZ_KEY_SYNC_RANGE
};
void xz_key_sync_failure_text(enum xz_key_sync_failure, int deck, char out[40]);
#endif
