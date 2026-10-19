/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_PLAYLISTS_H
#define XZ_SHELL_PLAYLISTS_H
/* Playlist edits made from the FLAGSHIP shell, kept in a sidecar file on the same stick
   (<root>/XZMODS/playlists.txt), never in PIONEER/rekordbox/export.pdb.

   A set holds one record per playlist the shell changed: a stick playlist renamed, deleted or given its
   own track order, or a playlist created here. Lists are built from the stick's export merged with this
   set (shell_library.c). Pure C11 apart from xz_shpl_load/xz_shpl_save, which use stdio and POSIX
   (fsync, rename) so a write is all-or-nothing. */
#include <stddef.h>
#include <stdint.h>
#include "shell/shell.h"

/* Ids of playlists created here, far above any id rekordbox assigns. */
#define XZ_SHPL_FIRST_CREATED 0x7f000000u
#define XZ_SHPL_TRACKS_MAX 10000u
struct xz_shpl {
 uint32_t id, parent;
 int deleted, created, renamed, has_tracks;
 char name[XZ_SHELL_NAME_MAX];
 uint32_t *tracks; uint32_t count, cap;
};
struct xz_shpl_set { struct xz_shpl *p; uint32_t n, cap, next_id; };

void xz_shpl_init(struct xz_shpl_set *);
void xz_shpl_free(struct xz_shpl_set *);
int xz_shpl_copy(struct xz_shpl_set *dst, const struct xz_shpl_set *src);
const struct xz_shpl *xz_shpl_find(const struct xz_shpl_set *, uint32_t id);
/* Applies one edit. The track list of a stick playlist the set does not hold yet comes in base/base_count
   (ignored for created playlists and for CREATE/RENAME/DELETE). base_name: that playlist's stick name, for
   records that keep it. Returns 0 (and *created for CREATE) or -1 when the edit does not apply. */
int xz_shpl_apply(struct xz_shpl_set *, const struct xz_shell_playlist_edit *, const uint32_t *base, uint32_t base_count,
                  const char *base_name, uint32_t *created);
/* The set as text and back. Text: a header line, then per record "p id parent flags name" and
   "t id track track ...". Unknown lines are skipped. */
char *xz_shpl_serialize(const struct xz_shpl_set *, size_t *length);
int xz_shpl_parse(struct xz_shpl_set *, const char *text, size_t length);
/* root/XZMODS/playlists.txt. load: 0 also when the file is absent (an empty set). save: temp file, fsync,
   rename; -1 with errno set (EROFS, ENOSPC...) when the stick refused it. */
int xz_shpl_load(struct xz_shpl_set *, const char *root);
int xz_shpl_save(const struct xz_shpl_set *, const char *root);
#endif
