/* SPDX-License-Identifier: MIT */
#ifndef XZ_PDB_H
#define XZ_PDB_H
#include <stddef.h>
#include <stdint.h>

typedef struct pdb pdb;
typedef struct { const char *data; size_t len; } pdb_text;
/* Return zero only after filling exactly length bytes. No read exceeds file_size. */
typedef int (*pdb_read_fn)(void *user, uint64_t offset, void *bytes, size_t length);
enum { PDB_OK = 0, PDB_STOP = 1, PDB_EINVAL = -1, PDB_ENOMEM = -2,
       PDB_EIO = -3, PDB_EFORMAT = -4, PDB_EBUSY = -5 };
typedef struct {
    uint32_t id, artist_id, album_id, genre_id, key_id, label_id, artwork_id;
    uint32_t bpm_x100, track_number, bitrate, sample_rate;
    uint16_t duration_seconds;
    uint8_t rating, color_id;
    pdb_text title, artist, album, genre, key, label, color, file_path, artwork_path, date_added;
} pdb_track;
typedef struct {
    uint32_t id, parent_id, sort_order;
    int is_folder;
    pdb_text name;
} pdb_playlist;
typedef struct { uint32_t track_id, sort_order; } pdb_entry;
typedef struct { uint64_t corrupt_pages, io_errors, cache_hits, cache_misses; } pdb_stats;
typedef int (*pdb_track_fn)(void *, const pdb_track *);
typedef int (*pdb_playlist_fn)(void *, const pdb_playlist *);
typedef enum {
    PDB_ARTIST, PDB_ALBUM, PDB_TRACK, PDB_GENRE, PDB_KEY,
    PDB_BPM, PDB_RATING, PDB_COLOR
} pdb_category;
/* One entry per track, grouped/sorted by category. Group ID is the side-table ID;
 * numeric categories use the numeric value. Ties sort by folded title then ID. */
typedef struct { uint32_t track_id, group_id; pdb_text name, title; } pdb_index_entry;
typedef struct { const pdb_index_entry *entries; size_t count; } pdb_index;

/* arena may have any alignment; db and every dynamic byte live in it. The file
 * must remain immutable until the caller discards db. No close or allocation. */
int pdb_open(pdb **out, pdb_read_fn read, void *user, uint64_t file_size,
             void *arena, size_t arena_size);
/* Track/tree strings expire when the callback returns. Nonzero callback result
 * stops iteration with PDB_STOP. Operations on one db are not reentrant. */
int pdb_each_track(pdb *db, pdb_track_fn fn, void *user);
int pdb_each_playlist(pdb *db, pdb_playlist_fn fn, void *user);
int pdb_each_history(pdb *db, pdb_playlist_fn fn, void *user);
/* Results persist until pdb_reset. Missing tables/playlists produce empty lists.
 * ENOMEM rolls back the entire result; partial success is never reported. */
int pdb_playlist_entries(pdb *db, uint32_t id, int history,
                         const pdb_entry **entries, size_t *count);
int pdb_build_index(pdb *db, pdb_category category, pdb_index *out);
/* Unicode default casefold, AND of whitespace-delimited substring terms. Each
 * term may match a different field among title/artist/album. No normalization. */
int pdb_search(pdb *db, pdb_text query, const uint32_t **ids, size_t *count);
size_t pdb_mark(const pdb *db);
int pdb_reset(pdb *db, size_t mark);
size_t pdb_memory_used(const pdb *db);
pdb_stats pdb_get_stats(const pdb *db);
#endif
