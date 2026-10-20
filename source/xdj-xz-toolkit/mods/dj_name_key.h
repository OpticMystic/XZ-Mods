/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_NAME_KEY_H
#define XZ_DJ_NAME_KEY_H
/* DJ Profile 1.0 name keys (section 3.1). Mirrors name_key() in XZ-Mods spec/djprofile/tools/check_djprofile.py. */
#include <stddef.h>
#define XZ_DJ_NAME_KEY_CAP(n) (3*(n)+1)
/* Name key (DJ Profile 1.0 section 3.1) of n bytes of UTF-8 at s: NFC, then A-Z folded to a-z. Bytes that are not
   valid UTF-8 pass through unchanged. Writes a NUL-terminated key to out and returns its length, or -1 when it needs
   more than cap bytes (XZ_DJ_NAME_KEY_CAP(n) always suffices) or working memory for a long name cannot be allocated. */
int xz_dj_name_key(const char *s, size_t n, char *out, size_t cap);
/* Byte order of the name keys of two NUL-terminated names, like strcmp; equal keys return 0. */
int xz_dj_name_key_compare(const char *a, const char *b);
#endif
