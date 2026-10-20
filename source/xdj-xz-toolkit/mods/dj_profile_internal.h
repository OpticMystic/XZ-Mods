/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_PROFILE_INTERNAL_H
#define XZ_DJ_PROFILE_INTERNAL_H
/* What dj_profile.c shares with dj_profile_write.c and dj_folders.c. Not part of the runtime API. */
#include <stddef.h>
#include <stdint.h>
#include "dj_profile.h"
#include "dj_folders.h"
struct xz_djp_str { const char *s; size_t n; };
int xz_djp_utf8_valid(struct xz_djp_str v);
/* Confined files below root: no links, directories as ancestors, bounded regular file. */
enum { XZ_DJP_READ_OK = 0, XZ_DJP_READ_MISSING = 1, XZ_DJP_READ_INVALID = 2, XZ_DJP_READ_IO = -1 };
int xz_djp_confined_open(const char *root, const char *relative, uint64_t max, int *fd, uint64_t *size);
int xz_djp_confined_read(const char *root, const char *relative, size_t max, unsigned char *buf, size_t *len);

#define XZ_DJP_MAX_JSON 65536
#define XZ_DJP_MAX_STICK_JSON 16384
#define XZ_DJP_STICK_FILE "DJPROFILE.json"
#define XZ_DJP_OLD_INDEX "DJPROFILE/VJTOOLS/index.json"
#define XZ_DJP_OLD_CARD "VJ.Tools/Branding/profile.json"
/* One candidate profile folder as the reader judged it. */
struct xz_djp_folder {
    struct xz_dj_candidate c;
    int level;                     /* -1 not a profile, 0, 1 */
    char json[XZ_DJ_NAME_MAX];     /* profile.json as the stick spells it, "" when there is none */
    int json_valid;                /* profile.json itself is a valid document */
    char used[XZ_DJ_NAME_MAX];     /* the document level 1 came from (profile.json or profile.previous.json) */
    struct xz_dj_profile view;     /* what the reader shows for it */
};
struct xz_djp_scan {
    char tops[2][XZ_DJ_NAME_MAX];
    struct xz_djp_folder *folder; int folders;   /* every candidate, xz_dj_candidates order */
    int order[XZ_DJ_MAX_PROFILES], shown;        /* the 1.0 profiles in profile order (indices into folder) */
    int active;                                  /* index into order of the 1.0 active profile, -1 */
    char stick_file[1024];         /* stick-relative DJPROFILE.json when it is valid, else "" */
    int stick_file_present;                      /* a file by that name exists, valid or not */
    struct xz_dj_profile *old; int olds;         /* old-store profiles not yet in a profile folder, contract order */
    int old_index_valid;                         /* DJPROFILE/VJTOOLS/index.json is valid */
    char old_active[XZ_DJ_ID_MAX];               /* its "active" */
    struct xz_dj_stick list;                     /* what xz_dj_stick_read returns */
};
/* 0 read, -1 I/O error. buf holds XZ_DJ_LOGO_MAX_BYTES + 1. Free with xz_djp_scan_free. */
int xz_djp_scan(const char *root, struct xz_djp_scan *s, unsigned char *buf);
void xz_djp_scan_free(struct xz_djp_scan *s);
/* 0 when the 1.0 document tree d (in folder dir) keeps every rule check_djprofile.py checks, except the
   ID3 tag of a profile track copy. Reads the files it lists. logo receives the primary logo file (""
   when the document has no logos); tile (XZ_DJ_TILE_BYTES + 1 bytes), when non-NULL, the verified
   xdj-xz tile and tile_file its file ("" when none). -1 invalid, -2 I/O error. */
struct xz_dj_jv;
int xz_djp_check_document(const char *root, const char *dir, const struct xz_dj_jv *d, char logo[1024], char tile_file[1024],
    unsigned char *tile, unsigned char *buf);
/* Section 6 resolution over a list: ids first, then folder name keys. -1 when nothing matches. */
int xz_djp_resolve(const struct xz_dj_stick *list, const char *ref, size_t n);
/* Rewrites DJPROFILE/VJTOOLS/index.json so active = dj_id, splicing only the "active" and "revision"
   values so unknown fields and formatting survive (index.json.tmp, fsync, rename). 0 written, 1 already
   active, 2 not listed, -1 invalid index or I/O failure (the old file is intact). */
int xz_djp_old_index_set_active(const char *root, const char *dj_id);
#endif
