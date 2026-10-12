/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_PROFILE_H
#define XZ_DJ_PROFILE_H
/* DJ Profile USB reader and active-profile switcher (docs/architecture/dj-profile-standard.md).
   Validation mirrors native/rust-app/crates/dj-profiles/src/profile.rs. */
#include <stddef.h>
#include <stdint.h>
#define XZ_DJ_MAX_PROFILES 16
#define XZ_DJ_TILE_SIDE 160
#define XZ_DJ_TILE_BYTES (160*160*2)
#define XZ_DJ_THUMB 40           /* thumbnail side, RGB565 */
#define XZ_DJ_ID_MAX 129         /* 128 + NUL */
struct xz_dj_profile {
    char dj_id[XZ_DJ_ID_MAX];
    char name[641];     /* full dj_name (160 characters of up to 4 UTF-8 bytes) */
    char tagline[321];  /* full tagline (80 characters), "" when absent */
    char page_url[512]; /* "" when absent or longer than 511 bytes */
    uint32_t accent;    /* 0xRRGGBB */
    int has_accent;
    int has_thumb;      /* thumb holds a verified 40x40 downscale of xz_tile */
    int damaged;        /* listed in index.json but profile.json missing/invalid/dj_id mismatch; name comes from the index */
    uint16_t thumb[XZ_DJ_THUMB*XZ_DJ_THUMB]; /* RGB565, row-major */
    char tile_path[512];     /* stick-relative path of the verified xz_tile, "" when has_thumb is 0 */
    char document_path[256]; /* stick-relative profile.json this came from, "" when damaged */
    int folder;              /* a folder profile (dj_folders.h): no profile.json, the logo is logo_path */
    char logo_path[256];     /* stick-relative PNG/JPEG of a folder profile, "" otherwise */
};
struct xz_dj_stick {
    int count;      /* 0..16 */
    int active;     /* index into profile[], -1 when count==0 */
    int legacy;     /* 1: single profile from VJ.Tools/Branding/profile.json (cannot switch) */
    struct xz_dj_profile profile[XZ_DJ_MAX_PROFILES];
};
/* Pure parsers on in-memory JSON (no I/O). 0 valid, -1 invalid. Same rules as profile.rs. */
int xz_dj_index_valid(const char *json, size_t n);
int xz_dj_profile_valid(const char *json, size_t n);
/* Reads <root>/DJPROFILE/VJTOOLS/index.json and each listed profiles/<dj_id>/profile.json, then the tile.
   Falls back to <root>/VJ.Tools/Branding/profile.json when the index is missing or invalid.
   Returns 0 when count>=1, 1 when the stick has no profile, -1 on an I/O error worth retrying.
   Never lists directories; opens fixed paths only. Files over 64 KiB are invalid. */
int xz_dj_stick_read(const char *root, struct xz_dj_stick *out);
/* Rewrites index.json so active = dj_id, with a fresh revision ("YYYY-MM-DDTHH-MM-SSZ-xxxx", UTC, 4 lowercase hex).
   Splices only the "active" and "revision" string values in the original bytes so unknown fields and
   formatting survive. Re-validates the result before writing. Writes index.json.tmp, fsync, rename over
   index.json, fsync the directory. Returns 0 written, 1 already active (nothing written), -1 error
   (not listed, invalid index, I/O failure; the old index.json is intact). */
int xz_dj_stick_set_active(const char *root, const char *dj_id);
#endif
