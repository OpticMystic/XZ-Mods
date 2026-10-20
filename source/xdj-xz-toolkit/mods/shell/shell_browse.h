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
   - XZ_SHELL_ACT_PLAYLIST id = enum xz_shell_playlist_op, details in xz_shell.edit
                          (shell.h); only with XZ_SHELL_CAP_PLAYLIST_EDIT.
   The search text lives in the shell (xz_shell_browse_query); SEARCH_QUERY
   says it changed.

   Panels (shell.h struct xz_shell_panel) slide over the list's right side:
   INFO follows the highlighted row; MENU sorts (SORT, by the keys in
   sort_keys), picks the columns (COLUMNS, from column_choices) and edits
   playlists; FILTER builds xz_shell.filter. A panel row the model gives no
   data for is left out, never drawn dead. */
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
 XZ_SHELL_BROWSE_SEARCH_QUERY,     /* value = byte length of xz_shell_browse_query() */
 XZ_SHELL_BROWSE_SET_COLUMNS,      /* value = bit per enum xz_shell_browse_column to show, in enum order; TITLE always */
 XZ_SHELL_BROWSE_FILTER            /* value = 1 apply xz_shell.filter (shell.h), 0 clear it; needs XZ_SHELL_CAP_FILTER */
};

enum xz_shell_browse_category {
 XZ_SHELL_CAT_ARTIST, XZ_SHELL_CAT_ALBUM, XZ_SHELL_CAT_TRACK, XZ_SHELL_CAT_KEY, XZ_SHELL_CAT_BPM,
 XZ_SHELL_CAT_GENRE, XZ_SHELL_CAT_RATING, XZ_SHELL_CAT_COLOR, XZ_SHELL_CAT_PLAYLIST, XZ_SHELL_CAT_FOLDER,
 XZ_SHELL_CAT_HISTORY, XZ_SHELL_CAT_MATCHING, XZ_SHELL_CAT_COUNT
};
enum xz_shell_browse_column {
 XZ_SHELL_COL_NUMBER, XZ_SHELL_COL_TITLE, XZ_SHELL_COL_ARTIST, XZ_SHELL_COL_BPM, XZ_SHELL_COL_KEY,
 XZ_SHELL_COL_RATING, XZ_SHELL_COL_COLOR, XZ_SHELL_COL_TIME, XZ_SHELL_COL_ALBUM, XZ_SHELL_COL_GENRE,
 XZ_SHELL_COL_DATE_ADDED, XZ_SHELL_COL_COUNT
};
/* Colour tags in their stock order, for xz_shell_filter.colors bits and the colour rows. */
enum xz_shell_tag_color {
 XZ_SHELL_TAG_COLOR_PINK, XZ_SHELL_TAG_COLOR_RED, XZ_SHELL_TAG_COLOR_ORANGE, XZ_SHELL_TAG_COLOR_YELLOW,
 XZ_SHELL_TAG_COLOR_GREEN, XZ_SHELL_TAG_COLOR_AQUA, XZ_SHELL_TAG_COLOR_BLUE, XZ_SHELL_TAG_COLOR_PURPLE,
 XZ_SHELL_TAG_COLOR_COUNT
};
enum xz_shell_browse_row_kind { XZ_SHELL_ROW_FOLDER, XZ_SHELL_ROW_TRACK };
/* One bit per row readout. kind and id are always valid for a row in the window. */
enum xz_shell_browse_row_field {
 XZ_SHELL_RF_TITLE, XZ_SHELL_RF_ARTIST, XZ_SHELL_RF_BPM, XZ_SHELL_RF_KEY, XZ_SHELL_RF_RATING,
 XZ_SHELL_RF_COLOR, XZ_SHELL_RF_DURATION, XZ_SHELL_RF_PLAYED, XZ_SHELL_RF_PREVIEW, XZ_SHELL_RF_ON_DECK,
 XZ_SHELL_RF_ALBUM, XZ_SHELL_RF_GENRE, XZ_SHELL_RF_DATE_ADDED,
 XZ_SHELL_RF_EDITED,                    /* the playlist (folder row) or its order comes from the shell's sidecar edits */
 XZ_SHELL_RF_TAGGED,                    /* tagged: the track is on this device's TAG LIST */
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
 const char *album, *genre;
 int32_t date_added;                    /* yyyymmdd */
 int tagged;                            /* RF_TAGGED */
};
/* One memory point of the highlighted track for the CUE/LOOP LIST (XZ manual pp.81-82). */
struct xz_shell_cue_entry {
 int loop;                              /* a stored loop: ms to end_ms */
 int32_t ms, end_ms;
 uint32_t color;                        /* 0xRRGGBB, 0 = none */
 const char *comment;                   /* NULL: the time stands alone */
};
/* A playlist ADD TO PLAYLIST can put the highlighted track in (merged: stick playlists with sidecar edits). */
struct xz_shell_playlist_ref { uint32_t id; const char *name; int32_t tracks; };
/* A MY TAG the FILTER panel offers. */
struct xz_shell_my_tag { uint32_t id; const char *name; };
/* What the shown list lets the shell edit (BF_EDIT). */
enum xz_shell_browse_edit {
 XZ_SHELL_EDIT_PLAYLISTS=1u, /* the rows are playlists: NEW, RENAME and DELETE apply */
 XZ_SHELL_EDIT_TRACKS=2u     /* the rows are the tracks of playlist edit_playlist: REMOVE and MOVE apply */
};

enum xz_shell_browse_field {
 XZ_SHELL_BF_SOURCE, XZ_SHELL_BF_SOURCE_COLOR, XZ_SHELL_BF_CATEGORY, XZ_SHELL_BF_LEVEL, XZ_SHELL_BF_PARENT,
 XZ_SHELL_BF_LIST, XZ_SHELL_BF_TOTAL, XZ_SHELL_BF_COLUMNS, XZ_SHELL_BF_SORT,
 XZ_SHELL_BF_SORT_KEYS, XZ_SHELL_BF_COLUMN_CHOICES, XZ_SHELL_BF_EDIT, XZ_SHELL_BF_PLAYLISTS, XZ_SHELL_BF_MY_TAGS,
 XZ_SHELL_BF_CUES,                      /* cues: the highlighted track's memory points (cue_track says which) */
 XZ_SHELL_BF_COUNT
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
 uint32_t sort_keys;                    /* BF_SORT_KEYS: bit per column this list can sort by (MENU > SORT) */
 uint32_t column_choices;               /* BF_COLUMN_CHOICES: bit per column the rows can show (MENU > COLUMNS) */
 uint32_t edit;                         /* BF_EDIT: enum xz_shell_browse_edit bits */
 uint32_t edit_playlist;                /* BF_EDIT with XZ_SHELL_EDIT_TRACKS: the playlist shown */
 const struct xz_shell_playlist_ref *playlists; /* BF_PLAYLISTS: ADD TO PLAYLIST targets */
 int playlist_count;
 const struct xz_shell_my_tag *my_tags; /* BF_MY_TAGS: FILTER > MY TAG */
 int my_tag_count;
 uint32_t cue_track;                    /* BF_CUES: the track id the cues belong to */
 const struct xz_shell_cue_entry *cues;
 int cue_count;
};
/* The list MENU panel, by list (newer players' context menus, XZ manual pp.83-89):
   any track row       ADD TO TAG LIST / REMOVE FROM TAG LIST (RF_TAGGED), CUE/LOOP LIST (BF_CUES for that
                       track), MARK PLAYED / MARK UNPLAYED (RF_PLAYED, HISTORY_EDIT)
   folder or playlist  ADD ALL TO TAG LIST (TAG_EDIT)
   TAG LIST            CREATE PLAYLIST (names it, then PL_FROM_TAG_LIST), REMOVE ALL TRACKS (push twice)
   HISTORY top level   DELETE HISTORY on a history row, DELETE ALL HISTORIES (each push twice)
   On TAG LIST the TAG TRACK key held removes the highlighted track; TAG TRACK short on a list adds or
   removes it by RF_TAGGED. */

/* Geometry for the runtime's hit tests and the host tests. Some regions
   depend on the model (columns) and on shell state (PREVIEW, INFO, keyboard). */
enum xz_shell_browse_region {
 XZ_SHELL_BR_SOURCE, XZ_SHELL_BR_BACK, XZ_SHELL_BR_FIELD, XZ_SHELL_BR_PREVIEW, XZ_SHELL_BR_INFO,
 XZ_SHELL_BR_RAIL,      /* index = visible rail slot */
 XZ_SHELL_BR_COLUMN,    /* index = column slot, left to right */
 XZ_SHELL_BR_LIST,
 XZ_SHELL_BR_ROW,       /* index = visible row */
 XZ_SHELL_BR_LOAD,      /* in the touched row; index 0 = LOAD (active deck), 1.. = deck 1 to the model's deck count */
 XZ_SHELL_BR_INFO_PANE,
 XZ_SHELL_BR_KEY,       /* index = keyboard key */
 XZ_SHELL_BR_STRIP,     /* index = deck: its half of the status strip, or its quarter in the four-deck view */
 XZ_SHELL_BR_FILTER,    /* the FILTER header tile */
 XZ_SHELL_BR_FILTER_CHIP, /* the active-filter chip; a touch clears the filter */
 XZ_SHELL_BR_PANEL,     /* the open slide-over panel */
 XZ_SHELL_BR_PANEL_ROW  /* index = visible panel row */
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
/* Highlight as drawn, including a remembered cursor. Pure: safe for press-time probes. */
const struct xz_shell_browse_row *xz_shell_browse_highlight(const struct xz_shell *, const struct xz_shell_model *);
/* The shell's search text. Valid until the next input. */
const char *xz_shell_browse_query(void);
/* The touched row (LOAD chip), or -1. */
int xz_shell_browse_touched(void);
/* Forget all list state: memory, toggles, query. Host tests only. */
void xz_shell_browse_reset(void);
#endif
