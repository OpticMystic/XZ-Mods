/* SPDX-License-Identifier: MIT */
#ifndef XZ_DJ_JSON_H
#define XZ_DJ_JSON_H
/* A JSON value tree for DJ Profile documents. Parsing can enforce the document rules of DJ Profile 1.0
   section 5; writing keeps every member in its order (section 5.5) and can emit RFC 8785 canonical JSON
   for documents that keep those rules. One arena owns every node and string of a tree. */
#include <stddef.h>
#include <stdint.h>
enum xz_dj_jtype { XZ_DJ_JNULL, XZ_DJ_JBOOL, XZ_DJ_JNUM, XZ_DJ_JSTR, XZ_DJ_JARR, XZ_DJ_JOBJ };
struct xz_dj_jv {
    enum xz_dj_jtype type;
    const char *key; size_t key_n;   /* member name when the parent is an object */
    const char *s; size_t n;         /* JSTR decoded UTF-8; JNUM the number as written; JBOOL "true" or "false" */
    struct xz_dj_jv *child, *last, *next;
    int count;                       /* members or elements */
};
struct xz_dj_json;
/* DJ Profile 1.0 section 5: valid UTF-8 throughout, no byte-order mark, nested at most 8 levels, member
   names ASCII and unique per object, numbers whole and within -2^53..2^53. Without it: RFC 8259 grammar
   only, nested at most 16 levels, strings decoded (lone surrogates and invalid UTF-8 in strings refused). */
#define XZ_DJ_JSON_PROFILE 1
/* NULL when the text breaks the rules or memory runs out. The root may be any value. */
struct xz_dj_json *xz_dj_json_parse(const char *text, size_t n, int rules, struct xz_dj_jv **root);
struct xz_dj_json *xz_dj_json_new(void);
void xz_dj_json_free(struct xz_dj_json *j);
/* 1 once any allocation of this tree failed; writes then return NULL. */
int xz_dj_json_failed(const struct xz_dj_json *j);

struct xz_dj_jv *xz_dj_json_get(const struct xz_dj_jv *object, const char *key);
int xz_dj_json_is(const struct xz_dj_jv *v, const char *s);       /* a string equal to s */
int xz_dj_json_whole(const struct xz_dj_jv *v, int64_t *out);      /* 0 when v is a whole number in int64 */

/* New detached values in j's arena. */
struct xz_dj_jv *xz_dj_json_value(struct xz_dj_json *j, enum xz_dj_jtype type);
struct xz_dj_jv *xz_dj_json_string(struct xz_dj_json *j, const char *s, size_t n);
struct xz_dj_jv *xz_dj_json_text(struct xz_dj_json *j, const char *s);   /* NUL-terminated */
struct xz_dj_jv *xz_dj_json_number(struct xz_dj_json *j, int64_t v);
struct xz_dj_jv *xz_dj_json_copy(struct xz_dj_json *j, const struct xz_dj_jv *v);  /* deep, from any tree */
/* object[key] = value: replaces the member in place (keeping its position) or appends it. */
void xz_dj_json_put(struct xz_dj_json *j, struct xz_dj_jv *object, const char *key, struct xz_dj_jv *value);
void xz_dj_json_push(struct xz_dj_jv *array, struct xz_dj_jv *value);
void xz_dj_json_push_front(struct xz_dj_jv *array, struct xz_dj_jv *value);
void xz_dj_json_remove(struct xz_dj_jv *object, const char *key);
/* Removes one element or member, found by identity. */
void xz_dj_json_unlink(struct xz_dj_jv *parent, struct xz_dj_jv *child);

/* PRETTY is Python's json.dumps(indent=2, ensure_ascii=False) and serde_json's to_vec_pretty;
   COMPACT the same with separators (',', ':'); CANONICAL is COMPACT with members sorted by name and
   whole numbers normalised, which is RFC 8785 for documents that keep DJ Profile's rules. */
enum { XZ_DJ_JSON_PRETTY, XZ_DJ_JSON_COMPACT, XZ_DJ_JSON_CANONICAL };
/* malloc'd and NUL-terminated, *n its length; NULL when memory runs out or the output passes 1 MiB. */
char *xz_dj_json_write(const struct xz_dj_jv *v, int style, size_t *n);
/* Equal canonical forms. */
int xz_dj_json_same(const struct xz_dj_jv *a, const struct xz_dj_jv *b);
#endif
