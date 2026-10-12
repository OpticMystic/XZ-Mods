/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_FOLDERS_H
#define XZ_DJ_FOLDERS_H
/* What a DJ sets up on a stick without any special software
   (docs/architecture/dj-profile-standard.md section 7):
   - folder profiles: DJPROFILE/<DJ name>/ or DJPROFILES/<DJ name>/ holding a PNG or JPEG logo;
   - the stick colour rekordbox saves in PIONEER/rekordbox/export.pdb (Background Color). */
#include "dj_profile.h"
#define XZ_DJ_FOLDER_ID "folder:"   /* prefix of the in-memory id of a folder profile */

/* Appends the stick's folder profiles to `stick` after what xz_dj_stick_read found, sorted by name
   (case-insensitive), up to XZ_DJ_MAX_PROFILES in all. The top folder is DJPROFILE or DJPROFILES in
   any case; both are read, DJPROFILE first. Inside it every directory except VJTOOLS and names
   starting with '.' is a candidate: its name is the DJ name (1..160 characters, not blank, no
   control characters) and it needs a PNG or JPEG directly inside (regular file, no symlink,
   8 bytes..8 MiB). The logo is logo.png, logo.jpg or logo.jpeg (any case) when present, otherwise
   the first image by case-insensitive name. A folder whose name matches a profile already in
   `stick` (case-insensitive) or an earlier folder is skipped. Each added profile has folder = 1,
   dj_id = XZ_DJ_FOLDER_ID + 16 hex of SHA-256(name), the stick-relative logo_path, and no thumb
   (the caller decodes the logo). A stick with no other profile gets the first folder as active.
   Returns how many were added. */
int xz_dj_folder_profiles(const char *root, struct xz_dj_stick *stick);

/* The rekordbox Background Color of the stick: COLORID of the djdbProperty row (table type 19) in
   PIONEER/rekordbox/export.pdb. 1 Pink, 2 Red, 3 Orange, 4 Yellow, 5 Green, 6 Aqua, 7 Blue,
   8 Purple. Returns 0 and the colour as 0xRRGGBB, 1 when the stick has no colour set (or no
   export), -1 when the file is unreadable or not a rekordbox export. Reads the header and the
   table's pages only. */
int xz_dj_stick_color(const char *root, uint32_t *rgb);
/* The RGB shown for a rekordbox colour id 1..8, 0 otherwise. */
uint32_t xz_dj_color_rgb(int color_id);
#endif
