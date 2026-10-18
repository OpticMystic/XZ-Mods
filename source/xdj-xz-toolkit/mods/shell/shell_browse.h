#ifndef XZ_SHELL_BROWSE_H
#define XZ_SHELL_BROWSE_H
/* BROWSE, PLAYLIST, TAG LIST and SEARCH: one list engine over one model slot
   (xz_shell_model.browse). The runtime fills the slot for the list the shell
   shows (xz_shell_screen) and sets `screen` to say which list the snapshot
   describes; rows from another list never draw under the wrong title.

   The list is windowed. The runtime publishes the total row count (or leaves
   it unknown) and up to XZ_SHELL_BROWSE_WINDOW rows starting at window_start.
   Rows outside the window are never invented: they draw blank and the shell
   asks for them with a BROWSE / REQUEST_WINDOW action.

   Everything the runtime executes comes back as an action:
   - XZ_SHELL_ACT_LOAD   deck, id = row id (only with XZ_SHELL_CAP_LOAD;
                          without it the LOAD control emits HAND_OFF so the
                          stock browser takes over). The shell returns to PLAY.
   - XZ_SHELL_ACT_BROWSE id = enum xz_shell_browse_op, value as listed there.
   - XZ_SHELL_ACT_HAND_OFF id = the list screen, for blocked categories.
   The search text lives in the shell (xz_shell_browse_query); SEARCH_QUERY
   says it changed. */
#include "shell.h"

#define XZ_SHELL_BROWSE_WINDOW 32   /* rows a REQUEST_WINDOW asks for, and the most a snapshot needs */
#define XZ_SHELL_BROWSE_COLUMNS 6
#define XZ_SHELL_BROWSE_QUERY 64    /* search buffer bytes, NUL included */
#define XZ_SHELL_TAG_LIST_MAX 100

enum xz_shell_browse_op {
 XZ_SHELL_BROWSE_REQUEST_WINDOW=1, /* value = first row wanted; XZ_SHELL_BROWSE_WINDOW rows */
 XZ_SHELL_BROWSE_OPEN,             /* value = folder row id: enter it */
 XZ_SHELL_BROWSE_UP,               /* one level up */
 XZ_SHELL_BROWSE_TOP,              /* to the current category's top level */
 XZ_SHELL_BROWSE_CATEGORY,         /* value = enum xz_shell_browse_category */
 XZ_SHELL_BROWSE_SORT,             /* value = enum xz_shell_browse_column | descending<<8 */
 XZ_SHELL_BROWSE_SEARCH_QUERY      /* value = byte length of xz_shell_browse_query() */
};

enum xz_shell_browse_category {
 XZ_SHELL_CAT_ARTIST, XZ_SHELL_CAT_ALBUM, XZ_SHELL_CAT_TRACK, XZ_SHELL_CAT_KEY, XZ_SHELL_CAT_BPM,
 XZ_SHELL_CAT_GENRE, XZ_SHELL_CAT_RATING, XZ_SHELL_CAT_COLOR, XZ_SHELL_CAT_PLAYLIST, XZ_SHELL_CAT_FOLDER,
 XZ_SHELL_CAT_HISTORY, XZ_SHELL_CAT_MATCHING, XZ_SHELL_CAT_COUNT
};
enum xz_shell_browse_column {
 XZ_SHELL_COL_NUMBER, XZ_SHELL_COL_TITLE, XZ_SHELL_COL_ARTIST, XZ_SHELL_COL_BPM, XZ_SHELL_COL_KEY,
 XZ_SHELL_COL_RATING, XZ_SHELL_COL_COLOR, XZ_SHELL_COL_TIME, XZ_SHELL_COL_COUNT
};
enum xz_shell_browse_row_kind { XZ_SHELL_ROW_FOLDER, XZ_SHELL_ROW_TRACK };
/* One bit per row readout. kind and id are always valid for a row in the window. */
enum xz_shell_browse_row_field {
 XZ_SHELL_RF_TITLE, XZ_SHELL_RF_ARTIST, XZ_SHELL_RF_BPM, XZ_SHELL_RF_KEY, XZ_SHELL_RF_RATING,
 XZ_SHELL_RF_COLOR, XZ_SHELL_RF_DURATION, XZ_SHELL_RF_PLAYED, XZ_SHELL_RF_PREVIEW, XZ_SHELL_RF_ON_DECK,
 XZ_SHELL_RF_COUNT
};
struct xz_shell_browse_row {
 uint32_t known;                        /* bit per enum xz_shell_browse_row_field */
 enum xz_shell_browse_row_kind kind;
 uint32_t id;                           /* track id, or the folder/playlist id OPEN takes */
 const char *title, *artist, *key;
 int32_t bpm_x100;
 int key_matches_master;
 int rating;                            /* 0..5 */
 uint32_t color;                        /* colour tag 0xRRGGBB, 0 = none */
 int32_t duration_ms;
 int played;
 const struct xz_shell_column *preview; /* small overview, bands 0..255 */
 uint32_t preview_count;
 uint8_t on_deck;                       /* bit d: loaded on deck d */
};

enum xz_shell_browse_field {
 XZ_SHELL_BF_SOURCE, XZ_SHELL_BF_SOURCE_COLOR, XZ_SHELL_BF_CATEGORY, XZ_SHELL_BF_LEVEL, XZ_SHELL_BF_PARENT,
 XZ_SHELL_BF_LIST, XZ_SHELL_BF_TOTAL, XZ_SHELL_BF_COLUMNS, XZ_SHELL_BF_SORT, XZ_SHELL_BF_COUNT
};
/* Alphabet jump index: the first row whose sort key starts with letter. */
struct xz_shell_browse_letter { char letter; int32_t row; };
struct xz_shell_browse_model {
 uint32_t known;                        /* bit per enum xz_shell_browse_field */
 enum xz_shell_screen screen;           /* the list this snapshot describes */
 const char *source_name;               /* BF_SOURCE, e.g. "USB1" */
 uint32_t source_color;                 /* BF_SOURCE_COLOR, the device's background colour */
 uint32_t category_known;               /* bit per category: shown in the rail */
 uint32_t category_available;           /* bit per category: the shell can open it (else hand-off) */
 enum xz_shell_browse_category category;/* BF_CATEGORY: the current one */
 const char *level_name;                /* BF_LEVEL: current folder, or the category at its top */
 int depth;                             /* BF_LEVEL: 0 = category top level */
 const char *parent_name;               /* BF_PARENT: the one level above */
 uint32_t list_id;                      /* BF_LIST: stable id of this list level (cursor memory) */
 int32_t total;                         /* BF_TOTAL */
 int32_t window_start;
 int window_count;                      /* rows[0..window_count) are rows window_start.. */
 const struct xz_shell_browse_row *rows;
 enum xz_shell_browse_column columns[XZ_SHELL_BROWSE_COLUMNS]; /* BF_COLUMNS, in display order */
 int column_count;
 enum xz_shell_browse_column sort_column; /* BF_SORT */
 int sort_descending;
 const struct xz_shell_browse_letter *letters; /* NULL: encoder hold jumps by page */
 int letter_count;
};

/* Geometry for the runtime's hit tests and the host tests. Some regions
   depend on the model (columns) and on shell state (PREVIEW, INFO, keyboard). */
enum xz_shell_browse_region {
 XZ_SHELL_BR_SOURCE, XZ_SHELL_BR_BACK, XZ_SHELL_BR_FIELD, XZ_SHELL_BR_PREVIEW, XZ_SHELL_BR_INFO,
 XZ_SHELL_BR_RAIL,      /* index = visible rail slot */
 XZ_SHELL_BR_COLUMN,    /* index = column slot, left to right */
 XZ_SHELL_BR_LIST,
 XZ_SHELL_BR_ROW,       /* index = visible row */
 XZ_SHELL_BR_LOAD,      /* in the touched row; index 0 = LOAD (active deck), 1/2 = deck 1/2 */
 XZ_SHELL_BR_INFO_PANE,
 XZ_SHELL_BR_KEY,       /* index = keyboard key */
 XZ_SHELL_BR_STRIP      /* index = deck */
};
struct xz_shell_rect xz_shell_browse_rect(const struct xz_shell *, const struct xz_shell_model *, enum xz_shell_browse_region, int index);
int xz_shell_browse_rows_visible(const struct xz_shell *);
/* Keyboard: XZ_SHELL_BROWSE_KEYS keys; the label of key i. */
#define XZ_SHELL_BROWSE_KEYS 40
const char *xz_shell_browse_key_label(int index);

/* BACK hook for BROWSE, PLAYLIST, TAG LIST and SEARCH: 1 when the shown list
   is below its category's top level, so BACK should climb one level instead
   of leaving the list. No side effects: the screen's input, given the BACK
   key event, emits BROWSE/UP and restores the parent level's cursor. */
int xz_shell_browse_back(struct xz_shell *, const struct xz_shell_model *);
/* The shell's search text. Valid until the next input. */
const char *xz_shell_browse_query(void);
/* The touched row (LOAD chip), or -1. */
int xz_shell_browse_touched(void);
/* Forget all list state: memory, toggles, query. Host tests only. */
void xz_shell_browse_reset(void);
#endif
