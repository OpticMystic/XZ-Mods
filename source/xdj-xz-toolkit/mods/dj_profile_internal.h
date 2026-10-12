/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_PROFILE_INTERNAL_H
#define XZ_DJ_PROFILE_INTERNAL_H
/* Helpers dj_profile.c shares with dj_profile_write.c. Not part of the runtime API. */
#include <stddef.h>
#include <stdint.h>
struct xz_djp_str { const char *s; size_t n; };
/* Strict serde_json-compatible document (64 KiB max). Token 0 is the root object. */
struct xz_djp_json;
struct xz_djp_json *xz_djp_json_open(const char *text, size_t n);
void xz_djp_json_close(struct xz_djp_json *j);
/* Value tokens of the named keys of an object (-1 absent). -1 on a duplicate known key. */
int xz_djp_json_fields(struct xz_djp_json *j, int object, const char *const *names, int k, int *out);
/* Decoded string; an absent token (-1) is "", null is an error. */
int xz_djp_json_string(struct xz_djp_json *j, int t, struct xz_djp_str *o);
int xz_djp_json_null(struct xz_djp_json *j, int t);       /* absent or null */
int xz_djp_json_u64(struct xz_djp_json *j, int t, uint64_t *o);
int xz_djp_json_array(struct xz_djp_json *j, int t);      /* element count, 0 when absent, -1 otherwise */
int xz_djp_json_next(struct xz_djp_json *j, int t);       /* the token after t's whole value */
/* profile.rs validators: 0 valid. */
int xz_djp_utf8_valid(struct xz_djp_str v);
int xz_djp_valid_id(struct xz_djp_str v);
int xz_djp_valid_label(struct xz_djp_str v, size_t max_chars);  /* validate_label */
int xz_djp_valid_text(struct xz_djp_str v, size_t max_chars);   /* tagline: <= max chars, no control */
/* Confined files below root: no links, directories as ancestors, bounded regular file. */
enum { XZ_DJP_READ_OK = 0, XZ_DJP_READ_MISSING = 1, XZ_DJP_READ_INVALID = 2, XZ_DJP_READ_IO = -1 };
int xz_djp_confined_open(const char *root, const char *relative, uint64_t max, int *fd, uint64_t *size);
int xz_djp_confined_read(const char *root, const char *relative, size_t max, unsigned char *buf, size_t *len);
#endif
