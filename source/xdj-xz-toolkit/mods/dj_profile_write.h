/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_PROFILE_WRITE_H
#define XZ_DJ_PROFILE_WRITE_H
/* DJ Profile writer and logo decoder for the XZ (docs/architecture/dj-profile-standard.md section 4).
   Mirrors publish_profile in native/rust-app/crates/dj-profiles/src/profile.rs. */
#include "dj_profile.h"
#define XZ_DJ_LOGO_MAX_BYTES (8u*1024u*1024u)
#define XZ_DJ_IMAGE_MAX_PIXELS (4096u*4096u)
#define XZ_DJ_IMAGE_LIST_MAX 96
#define XZ_DJ_IMAGE_PATH 256
enum xz_dj_image_result {
    XZ_DJ_IMAGE_OK, XZ_DJ_IMAGE_UNREADABLE, XZ_DJ_IMAGE_FILE_TOO_BIG, XZ_DJ_IMAGE_NOT_IMAGE,
    XZ_DJ_IMAGE_TOO_MANY_PIXELS, XZ_DJ_IMAGE_NO_MEMORY
};
/* Lists PNG/JPEG files (.png .jpg .jpeg, any case) on a stick, relative to root, at most `max`.
   Images directly inside DJ folders (DJPROFILE/<name>/ and DJPROFILES/<name>/, any case, not
   VJTOOLS; see dj_folders.h) come first. Then files in root and in directories up to 3 levels below
   it (root/a/b/c/logo.png is found, root/a/b/c/d/logo.png is not). Each group is sorted
   case-insensitively. Skips names starting with '.', PIONEER, DJPROFILE, DJPROFILES, VJ.Tools,
   CDJMODS, "System Volume Information", $RECYCLE.BIN (any case) in that second walk, symlinks, and
   files over XZ_DJ_LOGO_MAX_BYTES or under 8 bytes. Paths longer than XZ_DJ_IMAGE_PATH-1 are
   skipped. Returns the count (>=0) or -1 when root is unreadable. */
int xz_dj_image_list(const char *root, char out[][XZ_DJ_IMAGE_PATH], int max);
/* Decodes a PNG/JPEG file (regular file, no symlink, <= 8 MiB) and renders the 160x160 RGB565 tile
   the way profile.rs render_xz_tile does: alpha blended over black, fitted inside the square with
   Rust's rounding for the fitted size, centred over black. Shrinking is an exact area average,
   enlarging is bilinear (Rust uses Catmull-Rom, so pixels differ slightly from a Rust-made tile).
   tile[] holds host-order RGB565 values; the file on the stick is little-endian.
   Refuses images over XZ_DJ_IMAGE_MAX_PIXELS before decoding. Optional width/height receive the
   source size. */
enum xz_dj_image_result xz_dj_image_tile(const char *path, uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE], int *width, int *height);
/* What the XZ can change. Strings are UTF-8. */
struct xz_dj_draft {
    char dj_id[XZ_DJ_ID_MAX];  /* "" = create a new profile (a fresh lowercase UUID v4) */
    char name[641];            /* 1..160 printable characters, not blank (Rust validate_name) */
    char tagline[321];         /* 0..80 printable characters */
    char logo[1024];           /* absolute path of a new PNG/JPEG logo, "" = keep the current logos (none for a new profile) */
    int remove_logo;           /* 1 = drop every logo (monogram); invalid together with a logo path */
    char accent[8];            /* "#rrggbb" set only when the profile has no accent yet, "" = leave it */
};
enum xz_dj_write_result {
    XZ_DJ_WRITE_OK, XZ_DJ_WRITE_INVALID, XZ_DJ_WRITE_FULL /* 16 profiles */, XZ_DJ_WRITE_IMAGE,
    XZ_DJ_WRITE_IO, XZ_DJ_WRITE_CHANGED /* stick changed under us */
};
/* Creates or updates one profile on the stick at root: a fresh immutable bundle (staged in
   ".stage-<hex>" next to it, then renamed; an existing bundle with the same id is verified, not
   rewritten), profile.previous.json, profile.json (temporary file, fsync, rename), index.json last.
   Updating keeps every field of the existing profile.json the Rust struct knows (note, accent,
   aliases, links, page_url, secondary logos copied into the new bundle, labels and notes); unknown
   fields are dropped as Rust's re-serialisation drops them. An existing profile.json that is missing
   or invalid is replaced by a fresh one. A new logo replaces the preferred logo
   ("assets/001-preferred_logo.<png|jpg>", label "Preferred logo") and renders the tile; a kept
   preferred logo keeps a copy of its tile; otherwise there is no tile. Index: Rust listed_index
   (a stick without a valid index gets one with active = this id; a new id is appended without
   changing active; a renamed entry is updated; a 17th profile is refused before anything is
   written). Every written file is re-read and checked (size, SHA-256) before index.json is
   replaced; when a later step fails, profile.json is restored and index.json is untouched.
   `out_dj_id` (optional, XZ_DJ_ID_MAX bytes) receives the id. Writes only below
   <root>/DJPROFILE/VJTOOLS/. Not thread-safe against another writer on the same stick. */
enum xz_dj_write_result xz_dj_profile_write(const char *root, const struct xz_dj_draft *draft, char *out_dj_id);
/* Reads the editable fields of one listed profile into a draft (logo = "", remove_logo = 0).
   0 ok, -1 not listed, unreadable or invalid. */
int xz_dj_draft_load(const char *root, const char *dj_id, struct xz_dj_draft *out);
#endif
