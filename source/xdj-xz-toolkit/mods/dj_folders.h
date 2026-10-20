/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_FOLDERS_H
#define XZ_DJ_FOLDERS_H
/* The DJ Profile 1.0 stick layout as plain file reads, shared by the reader and the writer
   (spec/djprofile/DJPROFILE.md sections 3, 4, 5.2 and 11; check_djprofile.py is the reference). */
#include "dj_profile.h"
#include "dj_name_key.h"
#define XZ_DJ_FOLDER_ID "folder:"      /* prefix of the in-memory id of a level 0 profile */
#define XZ_DJ_LOGO_MIN_BYTES 8u
#define XZ_DJ_LOGO_MAX_BYTES (8u*1024u*1024u)
#define XZ_DJ_LOGO_MAX_SIDE 4096u
#define XZ_DJ_LOGO_MAX_PIXELS (4u*1024u*1024u)
#define XZ_DJ_NAME_MAX 256              /* one file name with its NUL */
#define XZ_DJ_KEY_MAX XZ_DJ_NAME_KEY_CAP(XZ_DJ_NAME_MAX)

/* Fixed names (section 3): A-Z equal a-z, everything else exact. */
int xz_dj_fold_equal(const char *a, const char *b);
/* The top folders as the stick spells them: [0] DJPROFILE, [1] DJPROFILES, "" when absent. Real
   directories only; a name starting with '.' never matches. */
void xz_dj_tops(const char *root, char tops[2][XZ_DJ_NAME_MAX]);
struct xz_dj_candidate {
    char dir[2 * XZ_DJ_NAME_MAX];  /* stick-relative, "DJPROFILE/DJ Kai" */
    const char *name;              /* the folder name, inside dir */
    char key[XZ_DJ_KEY_MAX];       /* its name key */
    int top;                       /* 0 DJPROFILE, 1 DJPROFILES */
};
/* Candidate profile folders in check_djprofile.py's order: DJPROFILE's directories sorted by name key,
   then DJPROFILES'; a name key seen before counts once. Skips names starting with '.', links,
   non-directories and VJTOOLS (the pre-1.0 store). malloc'd; *n entries; NULL with *n 0 when there are none. */
struct xz_dj_candidate *xz_dj_candidates(const char *root, int *n);
/* The file in stick-relative dir named `name` under ASCII folding: a regular file, not a link, not a
   dot-file. Writes the stick's spelling to out. 0 when found. */
int xz_dj_fixed(const char *root, const char *dir, const char *name, char out[XZ_DJ_NAME_MAX]);

enum { XZ_DJ_IMAGE_PNG = 1, XZ_DJ_IMAGE_JPEG = 2 };
struct xz_dj_image_info { int kind; uint32_t width, height; int plain; };
/* Kind and size from the header (section 5.2); plain = baseline JPEG or 8-bit non-interlaced PNG.
   0 when recognised. */
int xz_dj_image_info(const unsigned char *data, size_t n, struct xz_dj_image_info *o);
/* 0 when the n bytes are a usable logo: 8 B to 8 MiB, PNG or JPEG, at most 4096 px a side and 4 MP. */
int xz_dj_image_usable(const unsigned char *data, size_t n, struct xz_dj_image_info *o);
/* The level 0 logo of a stick-relative profile folder (section 4): logo.png, logo.jpg, logo.jpeg, then
   the other PNG/JPEG files by name key, the first usable one. buf holds XZ_DJ_LOGO_MAX_BYTES + 1.
   0 and its name in out when found, 1 when none, -1 on an I/O error. */
int xz_dj_level0_logo(const char *root, const char *dir, char out[XZ_DJ_NAME_MAX], unsigned char *buf);
/* Section 3 profile folder names: 1..160 bytes and at most 100 UTF-16 units, not blank, no control
   characters or < > : " / \ | ? *, no trailing space or dot. 0 valid. */
int xz_dj_valid_folder_name(const char *s, size_t n);
/* Section 5 names: 1..max bytes of valid UTF-8, not blank, no control characters. 0 valid. */
int xz_dj_valid_name(const char *s, size_t n, size_t max);

/* The rekordbox Background Color of the stick (section 11): byte +9 of the djdbProperty row (table
   type 19 of PIONEER/rekordbox/export.pdb) whose present bit is set; superseded rows stay in the heap.
   1 Pink, 2 Red, 3 Orange, 4 Yellow, 5 Green, 6 Aqua, 7 Blue, 8 Purple. 0 and 0xRRGGBB when set, 1 when
   the stick has none (or no export), -1 when the file is unreadable or not an export. */
int xz_dj_stick_color(const char *root, uint32_t *rgb);
/* The RGB of colour id 1..8, 0 otherwise. */
uint32_t xz_dj_color_rgb(int color_id);
#endif
