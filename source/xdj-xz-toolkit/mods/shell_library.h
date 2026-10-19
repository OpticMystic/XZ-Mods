/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_LIBRARY_H
#define XZ_SHELL_LIBRARY_H
/* One USB's export.pdb, read with mods/pdb, for the FLAGSHIP shell's BROWSE,
   PLAYLIST, SEARCH and INFO screens.

   xz_shlib_open copies every track's display fields and the playlist and
   history trees into the library, so window and INFO lookups never touch the
   reader. List building (xz_shlib_build) reuses the reader's arena and must
   run on one thread (the runtime worker); window and info lookups are
   read-only and may run on another thread while a build runs. Portable C11:
   files are read with stdio. */
#include <stddef.h>
#include <stdint.h>
#include "shell/shell_browse.h"
#include "shell/shell_info.h"
#include "shell_playlists.h"

#define XZ_SHLIB_PATH_DEPTH 8
struct xz_shlib;
struct xz_shlib_track {
 uint32_t id, artist_id, album_id, genre_id, key_id, bpm_x100, duration_ms, bitrate;
 uint8_t rating, color_id;
 uint32_t title, artist, album, genre, label, key, file_name, date_added; /* offsets into the text pool */
 uint32_t file_path;   /* the full path below the USB root, e.g. /Contents/Artist/Album/track.mp3 */
};
/* The track filter, applied on the worker over the whole list (tracks only; folders stay). Zero = none. */
struct xz_shlib_filter {
 int on;
 int32_t bpm_min_x100, bpm_max_x100;    /* both 0: no BPM limit */
 int rating_min;                        /* 0: any */
 uint32_t colors;                       /* bit (colour id - 1), shell_browse.h XZ_SHELL_TAG_COLOR_*; 0: any */
 char master_key[32];                   /* non-empty: only keys xz_shell_key_match marks GREEN or AMBER */
};
/* arena_size: the reader arena for list builds (see shell_runtime.c for the deck size). */
int xz_shlib_open(const char *path, size_t arena_size, struct xz_shlib **out);
void xz_shlib_free(struct xz_shlib *);
uint32_t xz_shlib_track_count(const struct xz_shlib *);
const struct xz_shlib_track *xz_shlib_track(const struct xz_shlib *, uint32_t id);
const char *xz_shlib_text(const struct xz_shlib *, uint32_t offset);

/* One list level. depth 0 is the category top; path[i] is the id opened at level i+1. */
struct xz_shlib_request {
 enum xz_shell_screen screen;          /* BROWSE, PLAYLIST or SEARCH */
 enum xz_shell_browse_category category;
 int depth;
 uint32_t path[XZ_SHLIB_PATH_DEPTH];
 char query[XZ_SHELL_BROWSE_QUERY];
 int sort_set, sort_column, sort_descending;
 struct xz_shlib_filter filter;
 const struct xz_shpl_set *edits;       /* playlist edits merged into PLAYLIST lists; NULL = none. Read only */
};
struct xz_shlib_list {
 enum xz_shell_screen screen;
 enum xz_shell_browse_category category;
 int depth, folders;                    /* folders: rows are folders (groups, playlist folders) */
 uint32_t list_id;
 int32_t total;
 uint8_t *kind;                         /* enum xz_shell_browse_row_kind per row */
 uint32_t *id, *name;                   /* row id; folder name offset into names */
 char *names; size_t names_used, names_cap;
 uint32_t level_name, parent_name;      /* offsets into names; parent_name only when has_parent */
 int has_parent;
 int sort_column, sort_descending;
 struct xz_shell_browse_letter letters[27];
 int letter_count;
};
/* Categories xz_shlib_build serves. */
uint32_t xz_shlib_categories(void);
/* 0 on success. A failure leaves *out NULL and the caller shows the list as unknown. */
int xz_shlib_build(struct xz_shlib *, const struct xz_shlib_request *, struct xz_shlib_list **out);
void xz_shlib_list_free(struct xz_shlib_list *);
/* Fills up to XZ_SHELL_BROWSE_WINDOW rows from start; returns the count. on_deck[d]: deck d's loaded
   track id from this library, 0 = none or unknown (on_deck_known says which decks are known). */
int xz_shlib_window(const struct xz_shlib *, const struct xz_shlib_list *, int32_t start,
                    const uint32_t on_deck[2], unsigned on_deck_known, struct xz_shell_browse_row rows[XZ_SHELL_BROWSE_WINDOW]);
/* INFO record for one track; returns 0 when the id is not in the library. */
int xz_shlib_info(const struct xz_shlib *, uint32_t track_id, enum xz_shell_source source, struct xz_shell_info_track *out);
uint32_t xz_shlib_color_rgb(unsigned color_id);
/* Columns the rows carry and every list can sort by (bit per enum xz_shell_browse_column). */
uint32_t xz_shlib_sort_keys(void);
/* Worker only (uses the reader): a stick playlist's tracks in stick order, malloc'd (free it). */
int xz_shlib_playlist_tracks(struct xz_shlib *, uint32_t id, uint32_t **tracks, uint32_t *count);
/* A stick playlist's (or folder's) name, "" when unknown. */
const char *xz_shlib_playlist_name(const struct xz_shlib *, uint32_t id);
/* Worker only: the playlists (not folders) merged with edits, for ADD TO PLAYLIST. Names are copied. */
struct xz_shlib_playlist_ref { uint32_t id; char name[XZ_SHELL_NAME_MAX]; int32_t tracks; };
int xz_shlib_playlist_refs(struct xz_shlib *, const struct xz_shpl_set *, struct xz_shlib_playlist_ref *out, int capacity);
#endif
