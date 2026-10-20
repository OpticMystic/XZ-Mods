/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_PROFILE_WRITE_H
#define XZ_DJ_PROFILE_WRITE_H
/* DJ Profile 1.0 writer and logo decoder for the XZ (XZ-Mods spec/djprofile/DJPROFILE.md section 7;
   VJToolsLibrary docs/architecture/dj-profile-standard.md section 0.3 for sticks written before 1.0). */
#include "dj_profile.h"
#define XZ_DJ_IMAGE_MAX_PIXELS (4096u*4096u)   /* the largest image decoded; larger logos are scaled to 4 MP */
#define XZ_DJ_IMAGE_LIST_MAX 96
#define XZ_DJ_IMAGE_PATH 256
enum xz_dj_image_result {
    XZ_DJ_IMAGE_OK, XZ_DJ_IMAGE_UNREADABLE, XZ_DJ_IMAGE_FILE_TOO_BIG, XZ_DJ_IMAGE_NOT_IMAGE,
    XZ_DJ_IMAGE_TOO_MANY_PIXELS, XZ_DJ_IMAGE_NO_MEMORY
};
/* Lists PNG/JPEG files (.png .jpg .jpeg, any case) on a stick, relative to root, at most `max`.
   Images directly inside DJ folders (DJPROFILE/<name>/ and DJPROFILES/<name>/, any case, not
   VJTOOLS) come first. Then files in root and in directories up to 3 levels below
   it (root/a/b/c/logo.png is found, root/a/b/c/d/logo.png is not). Each group is sorted
   case-insensitively. Skips names starting with '.', PIONEER, DJPROFILE, DJPROFILES, VJ.Tools,
   CDJMODS, "System Volume Information", $RECYCLE.BIN (any case) in that second walk, symlinks, and
   files over 8 MiB or under 8 bytes. Paths longer than XZ_DJ_IMAGE_PATH-1 are skipped. Returns the
   count (>=0) or -1 when root is unreadable. */
int xz_dj_image_list(const char *root, char out[][XZ_DJ_IMAGE_PATH], int max);
/* Decodes a PNG/JPEG file (regular file, no symlink, <= 8 MiB) and renders the 160x160 RGB565 tile:
   alpha blended over black, fitted inside the square, centred over black. Shrinking is an exact area
   average, enlarging is bilinear. tile[] holds host-order RGB565 values; the file on the stick is
   little-endian. Refuses images over XZ_DJ_IMAGE_MAX_PIXELS before decoding. Optional width/height
   receive the source size. */
enum xz_dj_image_result xz_dj_image_tile(const char *path, uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE], int *width, int *height);
/* What the XZ can change. Strings are UTF-8. */
struct xz_dj_draft {
    char dj_id[XZ_DJ_ID_MAX];  /* the profile's id, "" for a level 0 folder (folder) or a new profile */
    char folder[512];          /* stick-relative profile folder when dj_id is "", "" for a new profile */
    char name[641];            /* 1..160 bytes, not blank, no control characters */
    char tagline[321];         /* 0..240 bytes, no control characters */
    char logo[1024];           /* absolute path of a new PNG/JPEG logo, "" = keep the current logo */
    int remove_logo;           /* 1 = the initial on the accent colour replaces every logo; invalid with a logo path */
    char accent[8];            /* "#rrggbb" stored only when the profile has no accent yet, "" = leave it */
};
enum xz_dj_write_result {
    XZ_DJ_WRITE_OK, XZ_DJ_WRITE_INVALID, XZ_DJ_WRITE_FULL /* 16 profiles */, XZ_DJ_WRITE_IMAGE,
    XZ_DJ_WRITE_IO, XZ_DJ_WRITE_CHANGED /* stick changed under us */
};
/* Creates or updates one profile folder on the stick at root (spec section 7). A stick that still has
   the old store is moved to profile folders first (contract 0.3; the old store is never deleted).
   Assets first under new names (the primary logo logo.png or logo.jpg is replaced in place, a logo over
   4096 px a side or 4 MP is scaled down and written as PNG, the 160x160 rgb565le tile rendition for
   "xdj-xz" is rendered from it), then profile.previous.json, profile.json, the folder renamed after the
   name, DJPROFILE.json last, then the files the replaced document listed and the new one does not are
   deleted. Every member the writer does not change is kept. A profile without a logo gets its initial on
   its accent colour as logo.png. A hand-made logo.png/jpg/jpeg that differs from the new primary logo
   becomes logo-original.<ext>. Writing what the stick already holds writes nothing. `out_dj_id`
   (optional, XZ_DJ_ID_MAX bytes) receives the id. Writes only below DJPROFILE/ (DJPROFILES/ when that is
   the stick's only top folder). Not thread-safe against another writer on the same stick. */
enum xz_dj_write_result xz_dj_profile_write(const char *root, const struct xz_dj_draft *draft, char *out_dj_id);
/* The editable fields of the profile `reference` (an id, or a level 0 folder name) into a draft
   (logo = "", remove_logo = 0). 0 ok, -1 not found or unreadable. */
int xz_dj_draft_load(const char *root, const char *reference, struct xz_dj_draft *out);
#endif
