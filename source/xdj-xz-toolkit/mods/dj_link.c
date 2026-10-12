/* SPDX-License-Identifier: MIT */
/* The DJ Profile online-page link (spec section 6) and its QR code.
   The fragment mirrors encodeProfileFragment in packages/creative-graph/src/dj-profile.ts:
   p1.<base64url(deflate-raw(JSON.stringify({i,n,t?,a?,l?})))>. The XZ has no compressor, so the
   deflate stream is stored blocks only; any inflater (DecompressionStream included) reads it. */
#include "dj_link.h"

#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#define JSMN_STRICT
#include "audio/vendor/jsmn/jsmn.h"
#include "vendor/qrcodegen/qrcodegen.h"

#define PAGE_PREFIX "https://vj.tools/dj#p1."
#define MAX_JSON 65536
#define MAX_LINKS 12
#define ID_MAX 128
#define NAME_MAX_CHARS 160
#define TAGLINE_MAX_CHARS 80
#define KIND_MAX_CHARS 40
#define URL_MAX_UNITS 2048
#define STORED_BLOCK_MAX 65535u

struct link { char *kind, *url; };
struct profile {
    char *id, *name, *tagline, *accent, *page_url;
    struct link links[MAX_LINKS];
    int nlinks;
};

/* --- Growable byte buffer --- */
struct buf { char *p; size_t n, cap; int err; };

static void put(struct buf *b, const void *data, size_t n) {
    if (b->err) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (b->n + n + 1 > cap) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) { b->err = 1; return; }
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void puts_(struct buf *b, const char *s) { put(b, s, strlen(s)); }

/* --- Unicode on valid UTF-8 --- */
static uint32_t next_cp(const char *s, size_t *i) {
    const unsigned char *u = (const unsigned char *)s;
    unsigned c = u[*i];
    if (c < 0x80) { (*i)++; return c; }
    int k = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
    uint32_t v = c & (0x3fu >> k);
    for (int j = 1; j <= k; j++) v = v << 6 | (u[*i + (size_t)j] & 0x3f);
    *i += (size_t)k + 1;
    return v;
}
static int utf8_valid(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = s[i];
        size_t k;
        uint32_t v, min;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xc2 && c <= 0xdf) { k = 1; v = c & 0x1f; min = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { k = 2; v = c & 0x0f; min = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { k = 3; v = c & 0x07; min = 0x10000; }
        else return 0;
        if (i + k >= n) return 0;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xc0) != 0x80) return 0;
            v = v << 6 | (s[i + j] & 0x3f);
        }
        if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
        i += k + 1;
    }
    return 1;
}
/* The TS CONTROL class: C0, DEL and C1. */
static int is_control(uint32_t c) { return c < 0x20 || (c >= 0x7f && c <= 0x9f); }
/* JavaScript's WhiteSpace + LineTerminator: what String.prototype.trim and \s remove. */
static int is_js_space(uint32_t c) {
    return (c >= 0x09 && c <= 0x0d) || c == 0x20 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}

/* --- The page's field rules (readFragment) --- */
static int id_ok(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > ID_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}
/* At most `max` code points, no controls; `nonblank` also needs one non-space character. */
static int text_ok(const char *s, size_t max, int nonblank) {
    size_t count = 0, i = 0, n = strlen(s);
    int blank = 1;
    while (i < n) {
        uint32_t c = next_cp(s, &i);
        if (is_control(c)) return 0;
        if (!is_js_space(c)) blank = 0;
        count++;
    }
    return count <= max && !(nonblank && blank);
}
static int accent_ok(const char *s) {
    if (strlen(s) != 7 || s[0] != '#') return 0;
    for (int i = 1; i < 7; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}
/* httpsUrl: https://, at most 2048 UTF-16 units, no whitespace or controls, a non-empty host.
   The WHATWG URL parser is approximated by the host check. */
static int https_ok(const char *s) {
    size_t n = strlen(s), units = 0, i = 0;
    if (n < 8 || memcmp(s, "https://", 8)) return 0;
    while (i < n) {
        uint32_t c = next_cp(s, &i);
        if (is_control(c) || is_js_space(c)) return 0;
        units += c >= 0x10000 ? 2 : 1;
    }
    if (units > URL_MAX_UNITS) return 0;
    size_t start = 8, end = start;
    while (end < n && !strchr("/?#\\", s[end])) end++;
    for (size_t k = start; k < end; k++)
        if (s[k] == '@') start = k + 1; /* userinfo */
    size_t host_end = start;
    if (host_end < end && s[host_end] == '[') {
        while (host_end < end && s[host_end] != ']') host_end++;
        if (host_end == end) return 0;
        host_end++;
    } else {
        while (host_end < end && s[host_end] != ':') host_end++;
    }
    if (host_end == start) return 0;
    for (size_t k = start; k < host_end; k++)
        if (strchr("<>^|%", s[k])) return 0;
    for (size_t k = host_end + (host_end < end && s[host_end] == ':'); k < end; k++)
        if (host_end < end && (s[k] < '0' || s[k] > '9')) return 0; /* port: digits only */
    return 1;
}

/* --- JSON reading (lenient: only the fields used) --- */
struct doc { const char *text; jsmntok_t *tok; int count; };

static int hexv(unsigned char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static int tok_skip(const struct doc *d, int t) {
    int end = d->tok[t].end;
    while (++t < d->count && d->tok[t].start < end) {}
    return t;
}
static int is_null(const struct doc *d, int t) {
    return t >= 0 && d->tok[t].type == JSMN_PRIMITIVE && d->text[d->tok[t].start] == 'n';
}
/* The value token for key in object, the last one when repeated (JSON.parse), else -1. */
static int field(const struct doc *d, int object, const char *key) {
    int found = -1, t = object + 1;
    size_t kn = strlen(key);
    for (int f = 0; f < d->tok[object].size && t + 1 < d->count; f++) {
        const jsmntok_t *k = &d->tok[t];
        if (k->type == JSMN_STRING && (size_t)(k->end - k->start) == kn && !memcmp(d->text + k->start, key, kn)) found = t + 1;
        t = tok_skip(d, t + 1);
    }
    return found;
}
/* Decoded string for token t (malloc'd, NUL-terminated), NULL when not a valid UTF-8 string
   (lone surrogates included) or when it holds NUL. */
static char *str_dup(const struct doc *d, int t) {
    if (t < 0 || d->tok[t].type != JSMN_STRING) return NULL;
    const char *s = d->text;
    int p = d->tok[t].start, end = d->tok[t].end;
    char *out = malloc((size_t)(end - p) + 1);
    size_t n = 0;
    if (!out) return NULL;
    while (p < end) {
        unsigned c = (unsigned char)s[p++];
        if (c != '\\') { out[n++] = (char)c; continue; }
        c = (unsigned char)s[p++];
        switch (c) {
        case 'b': out[n++] = '\b'; continue;
        case 'f': out[n++] = '\f'; continue;
        case 'n': out[n++] = '\n'; continue;
        case 'r': out[n++] = '\r'; continue;
        case 't': out[n++] = '\t'; continue;
        case 'u': break;
        default: out[n++] = (char)c; continue;
        }
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) v = v * 16 + (uint32_t)hexv((unsigned char)s[p++]);
        if (v >= 0xdc00 && v <= 0xdfff) goto bad;
        if (v >= 0xd800 && v <= 0xdbff) {
            if (p + 6 > end || s[p] != '\\' || s[p + 1] != 'u') goto bad;
            uint32_t low = 0;
            for (int k = 0; k < 4; k++) {
                int h = hexv((unsigned char)s[p + 2 + k]);
                if (h < 0) goto bad;
                low = low * 16 + (uint32_t)h;
            }
            if (low < 0xdc00 || low > 0xdfff) goto bad;
            p += 6;
            v = 0x10000 + ((v - 0xd800) << 10) + (low - 0xdc00);
        }
        if (v == 0) goto bad;
        if (v < 0x80) out[n++] = (char)v;
        else if (v < 0x800) { out[n++] = (char)(0xc0 | v >> 6); out[n++] = (char)(0x80 | (v & 63)); }
        else if (v < 0x10000) {
            out[n++] = (char)(0xe0 | v >> 12); out[n++] = (char)(0x80 | (v >> 6 & 63)); out[n++] = (char)(0x80 | (v & 63));
        } else {
            out[n++] = (char)(0xf0 | v >> 18); out[n++] = (char)(0x80 | (v >> 12 & 63));
            out[n++] = (char)(0x80 | (v >> 6 & 63)); out[n++] = (char)(0x80 | (v & 63));
        }
    }
    if (!utf8_valid((const unsigned char *)out, n) || memchr(out, 0, n)) goto bad;
    out[n] = 0;
    return out;
bad:
    free(out);
    return NULL;
}
/* An optional string field that passes `ok`, else NULL (absent, null, wrong type or rejected). */
static char *optional(const struct doc *d, int object, const char *key, int (*ok)(const char *)) {
    int t = field(d, object, key);
    if (t < 0 || is_null(d, t)) return NULL;
    char *s = str_dup(d, t);
    if (s && !ok(s)) { free(s); s = NULL; }
    return s;
}
static int name_ok(const char *s) { return text_ok(s, NAME_MAX_CHARS, 1); }
static int tagline_ok(const char *s) { return text_ok(s, TAGLINE_MAX_CHARS, 0); }
static int kind_ok(const char *s) { return text_ok(s, KIND_MAX_CHARS, 1); }

static void profile_free(struct profile *p) {
    free(p->id); free(p->name); free(p->tagline); free(p->accent); free(p->page_url);
    for (int i = 0; i < p->nlinks; i++) { free(p->links[i].kind); free(p->links[i].url); }
    memset(p, 0, sizeof(*p));
}
static int profile_read(const char *text, size_t n, struct profile *p) {
    memset(p, 0, sizeof(*p));
    if (!text || n == 0 || n > MAX_JSON) return -1;
    jsmn_parser parser;
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, text, n, NULL, 0);
    if (count <= 0) return -1;
    struct doc d = { text, calloc((size_t)count, sizeof(jsmntok_t)), 0 };
    if (!d.tok) return -1;
    jsmn_init(&parser);
    d.count = jsmn_parse(&parser, text, n, d.tok, (unsigned)count);
    int rc = -1;
    if (d.count != count || d.tok[0].type != JSMN_OBJECT) goto done;
    p->id = optional(&d, 0, "dj_id", id_ok);
    p->name = optional(&d, 0, "dj_name", name_ok);
    if (!p->id || !p->name) goto done;
    p->tagline = optional(&d, 0, "tagline", tagline_ok);
    p->accent = optional(&d, 0, "accent", accent_ok);
    p->page_url = optional(&d, 0, "page_url", https_ok);
    int links = field(&d, 0, "links");
    if (links >= 0 && d.tok[links].type == JSMN_ARRAY) {
        int t = links + 1;
        for (int i = 0; i < d.tok[links].size && p->nlinks < MAX_LINKS; i++, t = tok_skip(&d, t)) {
            if (d.tok[t].type != JSMN_OBJECT) continue;
            char *kind = optional(&d, t, "kind", kind_ok), *url = optional(&d, t, "url", https_ok);
            if (kind && url) { p->links[p->nlinks].kind = kind; p->links[p->nlinks++].url = url; }
            else { free(kind); free(url); }
        }
    }
    rc = 0;
done:
    free(d.tok);
    if (rc) profile_free(p);
    return rc;
}

/* --- Encoding --- */
/* JSON.stringify for a well-formed string. */
static void json_string(struct buf *b, const char *s) {
    static const char hex[] = "0123456789abcdef";
    put(b, "\"", 1);
    for (const unsigned char *u = (const unsigned char *)s; *u; u++) {
        unsigned char c = *u;
        if (c == '"') puts_(b, "\\\"");
        else if (c == '\\') puts_(b, "\\\\");
        else if (c == '\b') puts_(b, "\\b");
        else if (c == '\f') puts_(b, "\\f");
        else if (c == '\n') puts_(b, "\\n");
        else if (c == '\r') puts_(b, "\\r");
        else if (c == '\t') puts_(b, "\\t");
        else if (c < 0x20) {
            char e[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
            put(b, e, 6);
        } else put(b, &c, 1);
    }
    put(b, "\"", 1);
}
static void fragment_json(struct buf *b, const struct profile *p, int nlinks, int tagline) {
    puts_(b, "{\"i\":");
    json_string(b, p->id);
    puts_(b, ",\"n\":");
    json_string(b, p->name);
    if (tagline && p->tagline) { puts_(b, ",\"t\":"); json_string(b, p->tagline); }
    if (p->accent) { puts_(b, ",\"a\":"); json_string(b, p->accent); }
    if (nlinks > 0) {
        puts_(b, ",\"l\":[");
        for (int i = 0; i < nlinks; i++) {
            puts_(b, i ? ",[" : "[");
            json_string(b, p->links[i].kind);
            put(b, ",", 1);
            json_string(b, p->links[i].url);
            put(b, "]", 1);
        }
        put(b, "]", 1);
    }
    put(b, "}", 1);
}
static size_t stored_size(size_t n) {
    size_t blocks = n ? (n + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX : 1;
    return n + 5 * blocks;
}
/* Raw DEFLATE of stored blocks (RFC 1951 3.2.4); out holds stored_size(n). Returns bytes written. */
static size_t stored_deflate(const uint8_t *in, size_t n, uint8_t *out) {
    size_t o = 0, at = 0;
    do {
        size_t len = n - at > STORED_BLOCK_MAX ? STORED_BLOCK_MAX : n - at;
        out[o++] = at + len == n ? 0x01 : 0x00; /* BFINAL, BTYPE=00; the rest of the byte is padding */
        out[o++] = (uint8_t)(len & 0xff);
        out[o++] = (uint8_t)(len >> 8);
        out[o++] = (uint8_t)(~len & 0xff);
        out[o++] = (uint8_t)((~len >> 8) & 0xff);
        if (len) memcpy(out + o, in + at, len);
        o += len;
        at += len;
    } while (at < n);
    return o;
}
/* base64url without padding; out holds (4n+2)/3 + 1. Returns length. */
static size_t base64url(const uint8_t *in, size_t n, char *out) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0, i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = alphabet[v >> 18]; out[o++] = alphabet[v >> 12 & 63];
        out[o++] = alphabet[v >> 6 & 63]; out[o++] = alphabet[v & 63];
    }
    if (n - i == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = alphabet[v >> 18]; out[o++] = alphabet[v >> 12 & 63];
    } else if (n - i == 2) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8;
        out[o++] = alphabet[v >> 18]; out[o++] = alphabet[v >> 12 & 63]; out[o++] = alphabet[v >> 6 & 63];
    }
    out[o] = 0;
    return o;
}
/* PAGE_PREFIX + base64url(stored(json)), malloc'd; NULL on allocation failure. */
static char *fragment_link(const struct profile *p, int nlinks, int tagline, size_t *len) {
    struct buf json = { 0 };
    fragment_json(&json, p, nlinks, tagline);
    if (json.err) { free(json.p); return NULL; }
    size_t raw_cap = stored_size(json.n);
    uint8_t *raw = malloc(raw_cap);
    size_t prefix = strlen(PAGE_PREFIX);
    char *link = malloc(prefix + (4 * raw_cap + 2) / 3 + 1);
    if (!raw || !link) { free(json.p); free(raw); free(link); return NULL; }
    size_t raw_n = stored_deflate((const uint8_t *)json.p, json.n, raw);
    memcpy(link, PAGE_PREFIX, prefix);
    *len = prefix + base64url(raw, raw_n, link + prefix);
    free(json.p);
    free(raw);
    return link;
}
/* Fits XZ_DJ_QR_VERSION at ECC M in byte mode. A fixed mask: only the fit is asked. */
static int fits_qr(const char *text, size_t n) {
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(XZ_DJ_QR_VERSION)], qr[sizeof(temp)];
    if (n > XZ_DJ_LINK_MAX || n > sizeof(temp)) return 0;
    memcpy(temp, text, n);
    return qrcodegen_encodeBinary(temp, n, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, XZ_DJ_QR_VERSION,
                                  qrcodegen_Mask_0, false);
}
static int emit(const char *link, size_t n, char *out, size_t cap) {
    if (!out || n + 1 > cap) return -1;
    memcpy(out, link, n);
    out[n] = 0;
    return (int)n;
}

int xz_dj_link_from_profile(const char *profile_json, size_t n, char *out, size_t cap, int *dropped) {
    struct profile p;
    if (dropped) *dropped = 0;
    if (profile_read(profile_json, n, &p)) return -1;
    int rc = -1;
    if (p.page_url) {
        size_t len = strlen(p.page_url);
        if (fits_qr(p.page_url, len)) { rc = emit(p.page_url, len, out, cap); goto done; }
    }
    int nlinks = p.nlinks, tagline = p.tagline != NULL, gone = 0;
    for (;;) {
        size_t len = 0;
        char *link = fragment_link(&p, nlinks, tagline, &len);
        if (!link) break;
        int last = nlinks == 0 && !tagline;
        if (last || fits_qr(link, len)) {
            rc = emit(link, len, out, cap);
            free(link);
            break;
        }
        free(link);
        if (nlinks > 0) nlinks--;
        else tagline = 0;
        gone++;
    }
    if (dropped) *dropped = gone;
done:
    profile_free(&p);
    return rc;
}

int xz_dj_qr(const char *text, int max_version, uint8_t *modules, size_t cap) {
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX], qr[qrcodegen_BUFFER_LEN_MAX];
    if (!text || !modules || max_version < qrcodegen_VERSION_MIN) return 0;
    if (max_version > qrcodegen_VERSION_MAX) max_version = qrcodegen_VERSION_MAX;
    size_t n = strlen(text);
    if (n > (size_t)qrcodegen_BUFFER_LEN_FOR_VERSION(max_version)) return 0;
    memcpy(temp, text, n);
    if (!qrcodegen_encodeBinary(temp, n, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, max_version, qrcodegen_Mask_AUTO,
                                true))
        return 0;
    int side = qrcodegen_getSize(qr);
    if ((size_t)side * (size_t)side > cap) return 0;
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) modules[(size_t)y * (size_t)side + (size_t)x] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
    return side;
}
