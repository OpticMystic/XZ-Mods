/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "dj_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OUTPUT (1u << 20)
struct block { struct block *next; size_t used, cap; unsigned char data[]; };
struct xz_dj_json { struct block *blocks; int failed; };

static void *grab(struct xz_dj_json *j, size_t n) {
    n = (n + 7) & ~(size_t)7;
    struct block *b = j->blocks;
    if (!b || b->cap - b->used < n) {
        size_t cap = n > 16384 ? n : 16384;
        if (!(b = malloc(sizeof(*b) + cap))) { j->failed = 1; return NULL; }
        b->next = j->blocks; b->used = 0; b->cap = cap; j->blocks = b;
    }
    void *p = b->data + b->used; b->used += n; return p;
}
static char *keep(struct xz_dj_json *j, const char *s, size_t n) {
    char *p = grab(j, n + 1);
    if (p) { if (n) memcpy(p, s, n); p[n] = 0; }
    return p;
}
struct xz_dj_json *xz_dj_json_new(void) { return calloc(1, sizeof(struct xz_dj_json)); }
void xz_dj_json_free(struct xz_dj_json *j) {
    if (!j) return;
    for (struct block *b = j->blocks, *next; b; b = next) { next = b->next; free(b); }
    free(j);
}
int xz_dj_json_failed(const struct xz_dj_json *j) { return !j || j->failed; }
struct xz_dj_jv *xz_dj_json_value(struct xz_dj_json *j, enum xz_dj_jtype type) {
    struct xz_dj_jv *v = grab(j, sizeof(*v));
    if (v) { memset(v, 0, sizeof(*v)); v->type = type; v->s = ""; }
    return v;
}
struct xz_dj_jv *xz_dj_json_string(struct xz_dj_json *j, const char *s, size_t n) {
    struct xz_dj_jv *v = xz_dj_json_value(j, XZ_DJ_JSTR); char *copy = keep(j, s, n);
    if (!v || !copy) return NULL;
    v->s = copy; v->n = n; return v;
}
struct xz_dj_jv *xz_dj_json_text(struct xz_dj_json *j, const char *s) { return xz_dj_json_string(j, s, strlen(s)); }
struct xz_dj_jv *xz_dj_json_number(struct xz_dj_json *j, int64_t value) {
    char text[24]; int n = snprintf(text, sizeof(text), "%lld", (long long)value);
    struct xz_dj_jv *v = xz_dj_json_value(j, XZ_DJ_JNUM); char *copy = keep(j, text, (size_t)n);
    if (!v || !copy) return NULL;
    v->s = copy; v->n = (size_t)n; return v;
}
static void attach(struct xz_dj_jv *parent, struct xz_dj_jv *v) {
    v->next = NULL;
    if (parent->last) parent->last->next = v; else parent->child = v;
    parent->last = v; parent->count++;
}
struct xz_dj_jv *xz_dj_json_copy(struct xz_dj_json *j, const struct xz_dj_jv *v) {
    if (!v) return NULL;
    struct xz_dj_jv *o = xz_dj_json_value(j, v->type);
    if (!o) return NULL;
    if (v->type == XZ_DJ_JSTR || v->type == XZ_DJ_JNUM || v->type == XZ_DJ_JBOOL) {
        char *s = keep(j, v->s, v->n); if (!s) return NULL;
        o->s = s; o->n = v->n;
    }
    for (const struct xz_dj_jv *c = v->child; c; c = c->next) {
        struct xz_dj_jv *k = xz_dj_json_copy(j, c);
        if (!k) return NULL;
        if (c->key) { char *key = keep(j, c->key, c->key_n); if (!key) return NULL; k->key = key; k->key_n = c->key_n; }
        attach(o, k);
    }
    return o;
}
static int key_is(const struct xz_dj_jv *v, const char *key) {
    size_t n = strlen(key); return v->key && v->key_n == n && !memcmp(v->key, key, n);
}
struct xz_dj_jv *xz_dj_json_get(const struct xz_dj_jv *object, const char *key) {
    if (!object || object->type != XZ_DJ_JOBJ) return NULL;
    for (struct xz_dj_jv *c = object->child; c; c = c->next) if (key_is(c, key)) return c;
    return NULL;
}
int xz_dj_json_is(const struct xz_dj_jv *v, const char *s) {
    size_t n = strlen(s); return v && v->type == XZ_DJ_JSTR && v->n == n && !memcmp(v->s, s, n);
}
int xz_dj_json_whole(const struct xz_dj_jv *v, int64_t *out) {
    if (!v || v->type != XZ_DJ_JNUM || !v->n) return -1;
    const char *s = v->s; size_t i = s[0] == '-', n = v->n; uint64_t value = 0;
    if (i == n) return -1;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        if (value > (UINT64_C(1) << 62) / 10) return -1;
        value = value * 10 + (uint64_t)(s[i] - '0');
    }
    if (value > (UINT64_C(1) << 62)) return -1;
    if (out) *out = s[0] == '-' ? -(int64_t)value : (int64_t)value;
    return 0;
}
void xz_dj_json_put(struct xz_dj_json *j, struct xz_dj_jv *object, const char *key, struct xz_dj_jv *value) {
    if (!object || !value || object->type != XZ_DJ_JOBJ) { if (j) j->failed = 1; return; }
    char *name = keep(j, key, strlen(key));
    if (!name) return;
    value->key = name; value->key_n = strlen(key);
    for (struct xz_dj_jv **at = &object->child; *at; at = &(*at)->next) {
        if (*at == value || !key_is(*at, key)) continue;
        struct xz_dj_jv *old = *at;
        value->next = old->next; *at = value;
        if (object->last == old) object->last = value;
        return;
    }
    attach(object, value);
}
void xz_dj_json_push(struct xz_dj_jv *array, struct xz_dj_jv *value) { if (array && value) { value->key = NULL; value->key_n = 0; attach(array, value); } }
void xz_dj_json_push_front(struct xz_dj_jv *array, struct xz_dj_jv *value) {
    if (!array || !value) return;
    value->key = NULL; value->key_n = 0; value->next = array->child; array->child = value;
    if (!array->last) array->last = value;
    array->count++;
}
void xz_dj_json_unlink(struct xz_dj_jv *parent, struct xz_dj_jv *child) {
    if (!parent) return;
    struct xz_dj_jv *prev = NULL;
    for (struct xz_dj_jv **at = &parent->child; *at; prev = *at, at = &(*at)->next) {
        if (*at != child) continue;
        *at = child->next;
        if (parent->last == child) parent->last = prev;
        parent->count--; child->next = NULL; return;
    }
}
void xz_dj_json_remove(struct xz_dj_jv *object, const char *key) {
    struct xz_dj_jv *v;
    while ((v = xz_dj_json_get(object, key))) xz_dj_json_unlink(object, v);
}

/* --- parsing --- */
struct parser { struct xz_dj_json *j; const char *s; size_t n, p; int rules, depth, max_depth; };
static void ws(struct parser *q) { while (q->p < q->n && (q->s[q->p] == ' ' || q->s[q->p] == '\t' || q->s[q->p] == '\n' || q->s[q->p] == '\r')) q->p++; }
static int hexv(unsigned char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static int hex4(struct parser *q, uint32_t *v) {
    if (q->n - q->p < 4) return -1;
    *v = 0;
    for (int k = 0; k < 4; k++) { int h = hexv((unsigned char)q->s[q->p++]); if (h < 0) return -1; *v = *v * 16 + (uint32_t)h; }
    return 0;
}
/* One UTF-8 sequence of valid input starting at s[i] (n bytes in all); its length, or 0 when invalid. */
static size_t utf8_step(const unsigned char *s, size_t i, size_t n) {
    unsigned c = s[i]; size_t k; uint32_t v, min;
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) { k = 1; v = c & 0x1f; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { k = 2; v = c & 0x0f; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { k = 3; v = c & 0x07; min = 0x10000; }
    else return 0;
    if (i + k >= n) return 0;
    for (size_t m = 1; m <= k; m++) { if ((s[i + m] & 0xc0) != 0x80) return 0; v = v << 6 | (s[i + m] & 0x3f); }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
    return k + 1;
}
/* At the opening quote. Decodes into the arena. */
static int string(struct parser *q, const char **out, size_t *out_n) {
    size_t start = ++q->p, cap = 0;
    for (size_t p = start; ; p++) { if (p >= q->n) return -1; if (q->s[p] == '"') { cap = p - start; break; } if (q->s[p] == '\\') p++; }
    char *o = grab(q->j, cap + 1); size_t n = 0;
    if (!o) return -1;
    while (q->p < q->n) {
        unsigned char c = (unsigned char)q->s[q->p];
        if (c == '"') { q->p++; o[n] = 0; *out = o; *out_n = n; return 0; }
        if (c < 0x20) return -1;
        if (c != '\\') {
            size_t k = utf8_step((const unsigned char *)q->s, q->p, q->n);
            if (!k) return -1;
            memcpy(o + n, q->s + q->p, k); n += k; q->p += k; continue;
        }
        if (++q->p >= q->n) return -1;
        c = (unsigned char)q->s[q->p++];
        const char *named = strchr("\"\\/bfnrt", c), *with = "\"\\/\b\f\n\r\t";
        if (c && named) { o[n++] = with[named - "\"\\/bfnrt"]; continue; }
        if (c != 'u') return -1;
        uint32_t v, low;
        if (hex4(q, &v)) return -1;
        if (v >= 0xdc00 && v <= 0xdfff) return -1;
        if (v >= 0xd800 && v <= 0xdbff) {
            if (q->n - q->p < 6 || q->s[q->p] != '\\' || q->s[q->p + 1] != 'u') return -1;
            q->p += 2;
            if (hex4(q, &low) || low < 0xdc00 || low > 0xdfff) return -1;
            v = 0x10000 + ((v - 0xd800) << 10) + (low - 0xdc00);
        }
        if (v < 0x80) o[n++] = (char)v;
        else if (v < 0x800) { o[n++] = (char)(0xc0 | v >> 6); o[n++] = (char)(0x80 | (v & 63)); }
        else if (v < 0x10000) { o[n++] = (char)(0xe0 | v >> 12); o[n++] = (char)(0x80 | (v >> 6 & 63)); o[n++] = (char)(0x80 | (v & 63)); }
        else { o[n++] = (char)(0xf0 | v >> 18); o[n++] = (char)(0x80 | (v >> 12 & 63)); o[n++] = (char)(0x80 | (v >> 6 & 63)); o[n++] = (char)(0x80 | (v & 63)); }
    }
    return -1;
}
static int digit(char c) { return c >= '0' && c <= '9'; }
static int number(struct parser *q, struct xz_dj_jv *v) {
    size_t start = q->p, p = q->p; const char *s = q->s;
    if (p < q->n && s[p] == '-') p++;
    if (p >= q->n || !digit(s[p])) return -1;
    if (s[p] == '0') p++; else while (p < q->n && digit(s[p])) p++;
    int whole = 1;
    if (p < q->n && s[p] == '.') { whole = 0; p++; if (p >= q->n || !digit(s[p])) return -1; while (p < q->n && digit(s[p])) p++; }
    if (p < q->n && (s[p] == 'e' || s[p] == 'E')) {
        whole = 0; p++;
        if (p < q->n && (s[p] == '+' || s[p] == '-')) p++;
        if (p >= q->n || !digit(s[p])) return -1;
        while (p < q->n && digit(s[p])) p++;
    }
    v->s = keep(q->j, s + start, p - start); v->n = p - start; q->p = p;
    if (!v->s) return -1;
    if (q->rules & XZ_DJ_JSON_PROFILE) {
        int64_t value;
        if (!whole || xz_dj_json_whole(v, &value) || value > (INT64_C(1) << 53) || value < -(INT64_C(1) << 53)) return -1;
    }
    return 0;
}
static int value(struct parser *q, struct xz_dj_jv **out);
static int container(struct parser *q, struct xz_dj_jv *v, char close) {
    if (++q->depth > q->max_depth) return -1;
    q->p++; ws(q);
    if (q->p < q->n && q->s[q->p] == close) { q->p++; q->depth--; return 0; }
    for (;;) {
        const char *key = NULL; size_t key_n = 0;
        if (close == '}') {
            if (q->p >= q->n || q->s[q->p] != '"' || string(q, &key, &key_n)) return -1;
            if (q->rules & XZ_DJ_JSON_PROFILE) {
                for (size_t i = 0; i < key_n; i++) if ((unsigned char)key[i] >= 0x80) return -1;
                for (struct xz_dj_jv *c = v->child; c; c = c->next) if (c->key_n == key_n && !memcmp(c->key, key, key_n)) return -1;
            }
            ws(q);
            if (q->p >= q->n || q->s[q->p] != ':') return -1;
            q->p++;
        }
        struct xz_dj_jv *item;
        if (value(q, &item)) return -1;
        item->key = key; item->key_n = key_n; attach(v, item);
        ws(q);
        if (q->p < q->n && q->s[q->p] == ',') { q->p++; ws(q); continue; }
        if (q->p < q->n && q->s[q->p] == close) { q->p++; q->depth--; return 0; }
        return -1;
    }
}
static int literal(struct parser *q, const char *word) {
    size_t n = strlen(word);
    if (q->n - q->p < n || memcmp(q->s + q->p, word, n)) return -1;
    q->p += n; return 0;
}
static int value(struct parser *q, struct xz_dj_jv **out) {
    ws(q);
    if (q->p >= q->n) return -1;
    char c = q->s[q->p];
    struct xz_dj_jv *v = xz_dj_json_value(q->j, c == '{' ? XZ_DJ_JOBJ : c == '[' ? XZ_DJ_JARR : c == '"' ? XZ_DJ_JSTR :
        c == 't' || c == 'f' ? XZ_DJ_JBOOL : c == 'n' ? XZ_DJ_JNULL : XZ_DJ_JNUM);
    if (!v) return -1;
    *out = v;
    if (c == '{' || c == '[') return container(q, v, c == '{' ? '}' : ']');
    if (c == '"') return string(q, &v->s, &v->n);
    if (c == 't') { v->s = "true"; v->n = 4; return literal(q, "true"); }
    if (c == 'f') { v->s = "false"; v->n = 5; return literal(q, "false"); }
    if (c == 'n') return literal(q, "null");
    return number(q, v);
}
struct xz_dj_json *xz_dj_json_parse(const char *text, size_t n, int rules, struct xz_dj_jv **root) {
    if (root) *root = NULL;
    if (!text) return NULL;
    struct xz_dj_json *j = xz_dj_json_new();
    if (!j) return NULL;
    struct parser q = { j, text, n, 0, rules, 0, rules & XZ_DJ_JSON_PROFILE ? 8 : 16 };
    struct xz_dj_jv *v = NULL;
    int bad = value(&q, &v);
    ws(&q);
    if (bad || q.p != n || j->failed) { xz_dj_json_free(j); return NULL; }
    if (root) *root = v;
    return j;
}

/* --- writing --- */
struct out { char *b; size_t n, cap; int err; };
static void raw(struct out *o, const char *s, size_t n) {
    if (o->err) return;
    if (o->n + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        while (o->n + n + 1 > cap) cap *= 2;
        char *b = cap <= MAX_OUTPUT ? realloc(o->b, cap) : NULL;
        if (!b) { o->err = 1; return; }
        o->b = b; o->cap = cap;
    }
    memcpy(o->b + o->n, s, n); o->n += n; o->b[o->n] = 0;
}
static void put_s(struct out *o, const char *s) { raw(o, s, strlen(s)); }
static void quoted(struct out *o, const char *s, size_t n) {
    raw(o, "\"", 1);
    size_t from = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i]; char e[8]; const char *with = NULL;
        switch (c) {
        case '"': with = "\\\""; break; case '\\': with = "\\\\"; break; case '\b': with = "\\b"; break;
        case '\f': with = "\\f"; break; case '\n': with = "\\n"; break; case '\r': with = "\\r"; break; case '\t': with = "\\t"; break;
        default: if (c < 0x20) { snprintf(e, sizeof(e), "\\u%04x", c); with = e; }
        }
        if (!with) continue;
        raw(o, s + from, i - from); put_s(o, with); from = i + 1;
    }
    raw(o, s + from, n - from); raw(o, "\"", 1);
}
static void newline(struct out *o, int style, int depth) {
    if (style != XZ_DJ_JSON_PRETTY) return;
    raw(o, "\n", 1);
    for (int i = 0; i < depth; i++) raw(o, "  ", 2);
}
static int by_key(const void *a, const void *b) {
    const struct xz_dj_jv *x = *(const struct xz_dj_jv *const *)a, *y = *(const struct xz_dj_jv *const *)b;
    size_t n = x->key_n < y->key_n ? x->key_n : y->key_n; int c = memcmp(x->key, y->key, n);
    return c ? c : x->key_n < y->key_n ? -1 : x->key_n > y->key_n;
}
static void emit(struct out *o, const struct xz_dj_jv *v, int style, int depth) {
    if (o->err) return;
    if (depth > 64) { o->err = 1; return; }
    switch (v->type) {
    case XZ_DJ_JNULL: put_s(o, "null"); return;
    case XZ_DJ_JBOOL: raw(o, v->s, v->n); return;
    case XZ_DJ_JSTR: quoted(o, v->s, v->n); return;
    case XZ_DJ_JNUM: {
        int64_t whole; char text[24];
        if (style == XZ_DJ_JSON_CANONICAL && !xz_dj_json_whole(v, &whole)) { snprintf(text, sizeof(text), "%lld", (long long)whole); put_s(o, text); }
        else raw(o, v->s, v->n);
        return;
    }
    default: break;
    }
    int object = v->type == XZ_DJ_JOBJ;
    raw(o, object ? "{" : "[", 1);
    if (!v->count) { raw(o, object ? "}" : "]", 1); return; }
    const struct xz_dj_jv **items = malloc(sizeof(*items) * (size_t)v->count); int count = 0;
    if (!items) { o->err = 1; return; }
    for (const struct xz_dj_jv *c = v->child; c && count < v->count; c = c->next) items[count++] = c;
    if (object && style == XZ_DJ_JSON_CANONICAL) qsort(items, (size_t)count, sizeof(*items), by_key);
    for (int i = 0; i < count; i++) {
        if (i) raw(o, ",", 1);
        newline(o, style, depth + 1);
        if (object) { quoted(o, items[i]->key, items[i]->key_n); put_s(o, style == XZ_DJ_JSON_PRETTY ? ": " : ":"); }
        emit(o, items[i], style, depth + 1);
    }
    free(items);
    newline(o, style, depth);
    raw(o, object ? "}" : "]", 1);
}
char *xz_dj_json_write(const struct xz_dj_jv *v, int style, size_t *n) {
    struct out o = { 0 };
    if (n) *n = 0;
    if (!v) return NULL;
    emit(&o, v, style, 0);
    if (o.err || !o.b) { free(o.b); return NULL; }
    if (n) *n = o.n;
    return o.b;
}
int xz_dj_json_same(const struct xz_dj_jv *a, const struct xz_dj_jv *b) {
    size_t an, bn; char *x = xz_dj_json_write(a, XZ_DJ_JSON_CANONICAL, &an), *y = xz_dj_json_write(b, XZ_DJ_JSON_CANONICAL, &bn);
    int same = x && y && an == bn && !memcmp(x, y, an);
    free(x); free(y); return same;
}
