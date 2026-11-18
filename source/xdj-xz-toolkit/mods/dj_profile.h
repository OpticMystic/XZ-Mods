/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_PROFILE_H
#define XZ_DJ_PROFILE_H
/* DJ Profile 1.0 USB reader (XZ-Mods spec/djprofile/DJPROFILE.md; tools/check_djprofile.py is the
   reference reader) plus the store written before 1.0, read as VJToolsLibrary
   docs/architecture/dj-profile-standard.md section 0.1 says. */
#include <stddef.h>
#include <stdint.h>
#define XZ_DJ_MAX_PROFILES 16
#define XZ_DJ_TILE_SIDE 160
#define XZ_DJ_TILE_BYTES (160*160*2)
#define XZ_DJ_THUMB 40           /* thumbnail side, RGB565 */
#define XZ_DJ_ID_MAX 129         /* 128 + NUL */
enum xz_dj_kind {
    XZ_DJ_LEVEL0,  /* a profile folder with a logo and no usable profile.json */
    XZ_DJ_LEVEL1,  /* a profile folder with a valid profile.json (or profile.previous.json) */
    XZ_DJ_OLD      /* DJPROFILE/VJTOOLS or VJ.Tools/Branding, not yet moved to a profile folder */
};
struct xz_dj_profile {
    enum xz_dj_kind kind;
    char dj_id[XZ_DJ_ID_MAX]; /* the id; level 0: XZ_DJ_FOLDER_ID + 16 hex of SHA-256(folder name), in memory only */
    char name[641];
    char tagline[321];  /* "" when absent */
    char page_url[512]; /* the first link of kind "page" (old store: page_url), "" when absent or over 511 bytes */
    uint32_t accent;    /* 0xRRGGBB */
    int has_accent;
    int has_thumb;      /* thumb holds a verified 40x40 downscale of the xdj-xz tile rendition */
    int damaged;        /* old store only: listed in index.json, its profile.json missing/invalid; name from the index */
    uint16_t thumb[XZ_DJ_THUMB*XZ_DJ_THUMB]; /* RGB565, row-major */
    char dir[512];            /* stick-relative profile folder, "DJPROFILE/DJ Kai"; "" for the old store */
    char tile_path[1024];     /* stick-relative verified 160x160 rgb565le tile, "" when has_thumb is 0 */
    char logo_path[1024];     /* stick-relative primary logo, "" when there is none */
    char document_path[1024];  /* stick-relative document this came from, "" at level 0 and when damaged */
};
struct xz_dj_stick {
    int count;      /* 0..16 */
    int active;     /* index into profile[], -1 when count==0 */
    struct xz_dj_profile profile[XZ_DJ_MAX_PROFILES];
};
/* The profile reference DJPROFILE.json uses (spec section 6): the id, or the folder name at level 0. */
const char *xz_dj_profile_ref(const struct xz_dj_profile *p);
/* Pre-1.0 documents on in-memory JSON (no I/O). 0 valid, -1 invalid. Same rules as profile.rs. */
int xz_dj_index_valid(const char *json, size_t n);
int xz_dj_profile_valid(const char *json, size_t n);
/* Reads every profile on the stick at root: the 1.0 profiles in profile order with the active one
   (spec sections 3 to 6, 16 at most), then the old store's profiles that are not yet profile folders
   (contract 0.1). Lists directories. The tile rendition (for "xdj-xz", rgb565le, 160x160) becomes the
   thumbnail. Returns 0 when count>=1, 1 when the stick has no profile, -1 on an I/O error worth retrying. */
int xz_dj_stick_read(const char *root, struct xz_dj_stick *out);
/* Makes `reference` (an id, or a level 0 folder name) the stick's active profile. Moves an old store to
   profile folders first (it never deletes it), then writes DJPROFILE/DJPROFILE.json with a new active and
   revision, keeping every other member, and the old index.json's "active" in place when it lists that id.
   Returns 0 written, 1 already active (nothing written), -1 error (unknown reference, I/O failure).
   Implemented in dj_profile_write.c. */
int xz_dj_stick_set_active(const char *root, const char *reference);
#endif
