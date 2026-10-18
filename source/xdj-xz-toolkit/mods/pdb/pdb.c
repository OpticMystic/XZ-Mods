/* SPDX-License-Identifier: MIT
 * Independent implementation of the DeviceSQL format documented at
 * https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html
 * Page/string rules cross-checked with XZ-Mods mods/key/catalog.c (MIT).
 */
#include "pdb.h"
#include <stdalign.h>
#include <string.h>
#include <limits.h>
#include "unicode_fold.h"

#define TABLES 20u
#define CACHE 64u
#define HEAP 40u
struct table { uint32_t first, last; int present; };
struct cache { uint32_t id, type, page, start, end; int valid; };
struct pdb {
    pdb_read_fn read;
    void *user;
    uint64_t file_size;
    unsigned char *arena, *page[2], *seen[2], *bad;
    size_t size, used, floor, scratch, bits;
    uint32_t page_size, pages, loaded[2];
    struct table tables[TABLES];
    struct cache cache[CACHE];
    pdb_stats stats;
    int busy;
};
static const pdb_text empty = {"", 0};
#ifdef PDB_BOUNDS_CHECK
#include <assert.h>
/* Test-only instrumentation. Every integer and DeviceSQL string read must lie
 * inside one of the two page buffers or the most recent small header read. */
static const pdb *checked_db;
static const unsigned char *checked_header;
static size_t checked_header_size;
static int contains_range(const void *base, size_t length, const void *p, size_t n) {
    uintptr_t a = (uintptr_t)base, b = (uintptr_t)p;
    return base && b >= a && b - a <= length && n <= length - (b - a);
}
static void check_read(const void *p, size_t n) {
    assert(checked_db);
    assert(contains_range(checked_db->page[0], checked_db->page_size, p, n) ||
           contains_range(checked_db->page[1], checked_db->page_size, p, n) ||
           contains_range(checked_header, checked_header_size, p, n));
}
#define CHECK_READ(p, n) check_read((p), (n))
#define CHECK_ARENA(d) assert((d)->used <= (d)->scratch && (d)->scratch <= (d)->size)
#else
#define CHECK_READ(p, n) ((void)0)
#define CHECK_ARENA(d) ((void)0)
#endif
static uint16_t u16(const unsigned char *p) { CHECK_READ(p, 2); return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }
static uint32_t u32(const unsigned char *p) {
    CHECK_READ(p, 4);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void *alloc(pdb *d, size_t n) {
    CHECK_ARENA(d);
    size_t a = alignof(max_align_t), pad = (a - d->used % a) % a;
    if (pad > d->scratch - d->used || n > d->scratch - d->used - pad) return NULL;
    void *p = d->arena + d->used + pad;
    d->used += pad + n;
    return p;
}
static void *temporary(pdb *d, size_t n) {
    CHECK_ARENA(d);
    if (n > d->scratch - d->used) return NULL;
    d->scratch -= n;
    return d->arena + d->scratch;
}
static int read_at(pdb *d, uint64_t at, void *p, size_t n) {
    if (at > d->file_size || n > d->file_size - at) return PDB_EFORMAT;
#ifdef PDB_BOUNDS_CHECK
    if (p != d->page[0] && p != d->page[1]) { checked_header = p; checked_header_size = n; }
#endif
    if (d->read(d->user, at, p, n)) { d->stats.io_errors++; return PDB_EIO; }
    return PDB_OK;
}
static int bit_get(const unsigned char *p, uint32_t n) { return (p[n / 8] >> (n % 8)) & 1; }
static void bit_set(unsigned char *p, uint32_t n) { p[n / 8] |= (unsigned char)(1u << (n % 8)); }
static void corrupt(pdb *d, uint32_t n) {
    if (n < d->pages && !bit_get(d->bad, n)) { bit_set(d->bad, n); d->stats.corrupt_pages++; }
}
static int load(pdb *d, unsigned which, uint32_t n) {
    if (!n || n >= d->pages) return PDB_EFORMAT;
    if (d->loaded[which] == n) return PDB_OK;
    d->loaded[which] = 0;
    int rc = read_at(d, (uint64_t)n * d->page_size, d->page[which], d->page_size);
    if (!rc) d->loaded[which] = n;
    return rc;
}

/* Decode one validated scalar; ASCII strings reject high bytes, UTF-16 rejects
 * odd lengths and unpaired surrogates. Output size is computed before writing. */
static int string_bytes(const unsigned char *p, size_t n, size_t *head, size_t *bytes, int *wide) {
    if (!n) return PDB_EFORMAT;
    CHECK_READ(p, n);
    *wide = 0;
    if (p[0] & 1u) {
        if (p[0] < 3) return PDB_EFORMAT;
        *head = 1; *bytes = (p[0] >> 1) - 1u;
    } else {
        if (n < 4 || (p[0] != 0x40 && p[0] != 0x90)) return PDB_EFORMAT;
        size_t total = u16(p + 1);
        if (total < 4) return PDB_EFORMAT;
        *head = 4; *bytes = total - 4; *wide = p[0] == 0x90;
    }
    if (*head > n || *bytes > n - *head || (*wide && (*bytes & 1u))) return PDB_EFORMAT;
    return PDB_OK;
}
static int scalar(const unsigned char *p, size_t n, size_t *at, int wide, uint32_t *ch) {
    CHECK_READ(p, n);
    if (*at >= n) return PDB_EFORMAT;
    if (!wide) { *ch = p[(*at)++]; return *ch <= 127 ? 0 : PDB_EFORMAT; }
    if (n - *at < 2) return PDB_EFORMAT;
    *ch = u16(p + *at); *at += 2;
    if (*ch >= 0xd800 && *ch <= 0xdbff) {
        if (n - *at < 2) return PDB_EFORMAT;
        uint32_t low = u16(p + *at); *at += 2;
        if (low < 0xdc00 || low > 0xdfff) return PDB_EFORMAT;
        *ch = 0x10000u + ((*ch - 0xd800u) << 10) + low - 0xdc00u;
    } else if (*ch >= 0xdc00 && *ch <= 0xdfff) return PDB_EFORMAT;
    return PDB_OK;
}
static size_t utf8_write(char *p, uint32_t c) {
    if (c < 0x80) { p[0] = (char)c; return 1; }
    if (c < 0x800) { p[0] = (char)(0xc0u | c >> 6); p[1] = (char)(0x80u | (c & 63)); return 2; }
    if (c < 0x10000) {
        p[0] = (char)(0xe0u | c >> 12); p[1] = (char)(0x80u | ((c >> 6) & 63));
        p[2] = (char)(0x80u | (c & 63)); return 3;
    }
    p[0] = (char)(0xf0u | c >> 18); p[1] = (char)(0x80u | ((c >> 12) & 63));
    p[2] = (char)(0x80u | ((c >> 6) & 63)); p[3] = (char)(0x80u | (c & 63)); return 4;
}
static int decode(pdb *d, const unsigned char *p, size_t n, pdb_text *out) {
    size_t head, bytes, at = 0, length = 0;
    int wide, rc = string_bytes(p, n, &head, &bytes, &wide);
    if (rc) return rc;
    while (at < bytes) {
        uint32_t c;
        if (scalar(p + head, bytes, &at, wide, &c)) return PDB_EFORMAT;
        length += c < 0x80 ? 1u : c < 0x800 ? 2u : c < 0x10000 ? 3u : 4u;
    }
    if (!out) return PDB_OK;
    if (!length) { *out = empty; return PDB_OK; }
    char *text = temporary(d, length + 1);
    if (!text) return PDB_ENOMEM;
    at = 0; size_t pos = 0;
    while (at < bytes) {
        uint32_t c = 0;
        (void)scalar(p + head, bytes, &at, wide, &c);
        pos += utf8_write(text + pos, c);
    }
    text[pos] = 0; *out = (pdb_text){text, pos};
    return PDB_OK;
}
static size_t slots(const unsigned char *p) { return u32(p + 24) & 0x1fffu; }
static int present(const pdb *d, const unsigned char *p, size_t slot) {
    size_t end = d->page_size - slot / 16 * 36;
    return (u16(p + end - 4) >> (slot % 16)) & 1;
}
static size_t start(const pdb *d, const unsigned char *p, size_t slot) {
    size_t end = d->page_size - slot / 16 * 36;
    return HEAP + u16(p + end - 6 - slot % 16 * 2);
}
static size_t row_end(const pdb *d, const unsigned char *p, size_t at) {
    size_t count = slots(p), end = d->page_size - (count + 15) / 16 * 36;
    for (size_t i = 0; i < count; ++i) if (present(d, p, i)) {
        size_t next = start(d, p, i);
        if (next > at && next < end) end = next;
    }
    return end;
}
/* Layout offsets are schema offsets, not the shifted offsets in the old key
 * catalog: track artist/id = 0x44/0x48, artwork/key = 0x1c/0x20. */
static int name_location(uint32_t type, const unsigned char *r, size_t n, uint32_t *id, size_t *at) {
    size_t minimum;
    switch (type) {
    case 2:
        minimum = 10; if (n < minimum) return PDB_EFORMAT;
        *id = u32(r + 4);
        if (u16(r) & 4) { minimum = 12; if (n < minimum) return PDB_EFORMAT; *at = u16(r + 10); }
        else *at = r[9];
        break;
    case 3:
        minimum = 22; if (n < minimum) return PDB_EFORMAT;
        *id = u32(r + 12);
        if (u16(r) & 4) { minimum = 24; if (n < minimum) return PDB_EFORMAT; *at = u16(r + 22); }
        else *at = r[21];
        break;
    case 5: minimum = 8; if (n < minimum) return PDB_EFORMAT; *id = u32(r); *at = 8; break;
    case 6: minimum = 8; if (n < minimum) return PDB_EFORMAT; *id = u16(r + 5); *at = 8; break;
    case 7: minimum = 20; if (n < minimum) return PDB_EFORMAT; *id = u32(r + 12); *at = 20; break;
    default: minimum = 4; if (n < minimum) return PDB_EFORMAT; *id = u32(r); *at = 4; break;
    }
    return *at < minimum || *at >= n ? PDB_EFORMAT : PDB_OK;
}
static int validate_row(const unsigned char *r, size_t n, uint32_t type) {
    if (type == 0) {
        if (n < 0x88 || u16(r) != 0x24 || r[0x59] > 5) return PDB_EFORMAT;
        const unsigned fields[] = {10, 17, 20};
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
            size_t at = u16(r + 0x5e + fields[i] * 2);
            if (at < 0x88 || at >= n || decode(NULL, r + at, n - at, NULL)) return PDB_EFORMAT;
        }
    } else if (type == 8 || type == 12) {
        if (n < 12) return PDB_EFORMAT;
    } else {
        uint32_t id; size_t at;
        if (name_location(type, r, n, &id, &at) || decode(NULL, r + at, n - at, NULL)) return PDB_EFORMAT;
    }
    return PDB_OK;
}
static int page_valid(pdb *d, const unsigned char *p, uint32_t number, uint32_t type) {
    if (u32(p + 4) != number || u32(p + 8) != type) return 0;
    if (p[27] & 0x40) return 1;
    size_t count = slots(p), groups = (count + 15) / 16;
    if (groups * 36 > d->page_size - HEAP) return 0;
    size_t end = d->page_size - groups * 36;
    for (size_t i = 0; i < count; ++i) if (present(d, p, i)) {
        size_t at = start(d, p, i);
        if (at >= end) return 0;
        for (size_t j = 0; j < i; ++j) if (present(d, p, j) && start(d, p, j) == at) return 0;
        if (validate_row(p + at, row_end(d, p, at) - at, type)) return 0;
    }
    return 1;
}
typedef int (*row_fn)(pdb *, const unsigned char *, size_t, uint32_t, size_t, void *);
static int walk(pdb *d, uint32_t type, unsigned which, row_fn fn, void *user) {
    if (!d->tables[type].present) return PDB_OK;
    memset(d->seen[which], 0, d->bits);
    uint32_t number = d->tables[type].first, last = d->tables[type].last;
    if (!number && !last) return PDB_OK;
    for (;;) {
        if (!number || number >= d->pages || last >= d->pages) { corrupt(d, 0); break; }
        if (bit_get(d->seen[which], number)) { corrupt(d, number); break; }
        bit_set(d->seen[which], number);
        if (load(d, which, number)) { corrupt(d, number); break; }
        const unsigned char *p = d->page[which];
        uint32_t next = u32(p + 12);
        if (!page_valid(d, p, number, type)) corrupt(d, number);
        else if (!(p[27] & 0x40)) {
            for (size_t i = 0; i < slots(p); ++i) if (present(d, p, i)) {
                size_t at = start(d, p, i), end = row_end(d, p, at);
                if (!which) d->scratch = d->size;
                int rc = fn(d, p + at, end - at, number, at, user);
                if (rc) return rc;
            }
        }
        if (number == last) break;
        /* Never follow an untrusted link into a different table. */
        if (u32(p + 4) != number || u32(p + 8) != type) break;
        number = next;
    }
    return PDB_OK;
}
int pdb_open(pdb **out, pdb_read_fn read, void *user, uint64_t file_size, void *arena, size_t arena_size) {
    if (!out) return PDB_EINVAL;
    *out = NULL;
    if (!read || !arena || file_size < 28) return PDB_EINVAL;
    size_t a = alignof(max_align_t), pad = (a - (uintptr_t)arena % a) % a;
    if (pad > arena_size || sizeof(pdb) > arena_size - pad) return PDB_ENOMEM;
    pdb *d = (pdb *)((unsigned char *)arena + pad);
    memset(d, 0, sizeof(*d));
    d->arena = (unsigned char *)d; d->size = arena_size - pad;
    d->used = sizeof(*d); d->scratch = d->size;
    d->read = read; d->user = user; d->file_size = file_size;
#ifdef PDB_BOUNDS_CHECK
    checked_db = d; checked_header = NULL; checked_header_size = 0;
#endif
    unsigned char h[28];
    int rc = read_at(d, 0, h, sizeof(h));
    if (rc) return rc;
    uint32_t length = u32(h + 4), tables = u32(h + 8);
    if (u32(h) || length < 512 || length > 65536 || (length & (length - 1)) ||
        !tables || tables > (length - 28u) / 16u || file_size % length || file_size / length > UINT32_MAX)
        return PDB_EFORMAT;
    d->page_size = length; d->pages = (uint32_t)(file_size / length);
    d->bits = (size_t)(d->pages / 8u) + (d->pages % 8u != 0);
    for (unsigned i = 0; i < 2; ++i) {
        d->page[i] = alloc(d, length); d->seen[i] = alloc(d, d->bits);
        if (!d->page[i] || !d->seen[i]) return PDB_ENOMEM;
    }
    d->bad = alloc(d, d->bits);
    if (!d->bad) return PDB_ENOMEM;
    memset(d->bad, 0, d->bits);
    for (uint32_t i = 0; i < tables; ++i) {
        unsigned char p[16];
        rc = read_at(d, 28u + (uint64_t)i * 16u, p, sizeof(p));
        if (rc) return rc;
        uint32_t type = u32(p);
        if (type >= TABLES || (type > 8 && type != 11 && type != 12 && type != 13)) continue;
        if (d->tables[type].present) return PDB_EFORMAT;
        d->tables[type] = (struct table){u32(p + 8), u32(p + 12), 1};
    }
    d->floor = d->used; *out = d;
    return PDB_OK;
}
struct find { uint32_t type, id, page, start, end; pdb_text text; };
static int find_row(pdb *d, const unsigned char *r, size_t n, uint32_t page, size_t at, void *user) {
    struct find *f = user; uint32_t id; size_t offset;
    if (name_location(f->type, r, n, &id, &offset) || id != f->id) return 0;
    f->page = page; f->start = (uint32_t)at; f->end = (uint32_t)(at + n);
    int rc = decode(d, r + offset, n - offset, &f->text);
    return rc ? rc : PDB_STOP;
}
static int lookup(pdb *d, uint32_t type, uint32_t id, pdb_text *out) {
    *out = empty;
    if (!id) return PDB_OK;
    struct cache *c = &d->cache[((id * 2654435761u) ^ type) % CACHE];
    if (c->valid && c->id == id && c->type == type) {
        d->stats.cache_hits++;
        if (!c->page) return PDB_OK;
        if (load(d, 1, c->page)) { corrupt(d, c->page); return PDB_OK; }
        uint32_t key; size_t at;
        const unsigned char *r = d->page[1] + c->start;
        if (name_location(type, r, c->end - c->start, &key, &at)) return PDB_EFORMAT;
        return decode(d, r + at, c->end - c->start - at, out);
    }
    d->stats.cache_misses++;
    struct find f = {type, id, 0, 0, 0, {"", 0}};
    int rc = walk(d, type, 1, find_row, &f);
    if (rc < 0) return rc;
    *c = (struct cache){id, type, f.page, f.start, f.end, 1};
    *out = f.text;
    return PDB_OK;
}
struct tracks { pdb_track_fn fn; void *user; };
static int track_row(pdb *d, const unsigned char *r, size_t n, uint32_t page, size_t at, void *user) {
    (void)page; (void)at;
    struct tracks *ctx = user;
    pdb_track t;
    memset(&t, 0, sizeof(t));
    t.id = u32(r + 0x48); t.artist_id = u32(r + 0x44); t.album_id = u32(r + 0x40);
    t.genre_id = u32(r + 0x3c); t.key_id = u32(r + 0x20); t.label_id = u32(r + 0x28);
    t.artwork_id = u32(r + 0x1c); t.bpm_x100 = u32(r + 0x38);
    t.track_number = u32(r + 0x34); t.bitrate = u32(r + 0x30); t.sample_rate = u32(r + 8);
    t.duration_seconds = u16(r + 0x54); t.color_id = r[0x58]; t.rating = r[0x59];
    const unsigned fields[] = {17, 20, 10};
    pdb_text *texts[] = {&t.title, &t.file_path, &t.date_added};
    for (size_t i = 0; i < 3; ++i) {
        size_t offset = u16(r + 0x5e + fields[i] * 2);
        int rc = decode(d, r + offset, n - offset, texts[i]);
        if (rc) return rc;
    }
    const uint32_t types[] = {2, 3, 1, 5, 4, 6, 13};
    const uint32_t ids[] = {t.artist_id, t.album_id, t.genre_id, t.key_id, t.label_id, t.color_id, t.artwork_id};
    pdb_text *names[] = {&t.artist, &t.album, &t.genre, &t.key, &t.label, &t.color, &t.artwork_path};
    for (size_t i = 0; i < 7; ++i) {
        int rc = lookup(d, types[i], ids[i], names[i]);
        if (rc) return rc;
    }
    return ctx->fn(ctx->user, &t) ? PDB_STOP : PDB_OK;
}
static int tracks(pdb *d, pdb_track_fn fn, void *user) {
    struct tracks ctx = {fn, user};
    return walk(d, 0, 0, track_row, &ctx);
}
static int enter(pdb *d) {
    if (!d) return PDB_EINVAL;
    if (d->busy) return PDB_EBUSY;
#ifdef PDB_BOUNDS_CHECK
    checked_db = d;
#endif
    d->busy = 1; d->scratch = d->size;
    return PDB_OK;
}
static int leave(pdb *d, int rc) { d->busy = 0; d->scratch = d->size; return rc; }
int pdb_each_track(pdb *d, pdb_track_fn fn, void *user) {
    if (!fn) return PDB_EINVAL;
    int rc = enter(d); if (rc) return rc;
    return leave(d, tracks(d, fn, user));
}
struct playlists { pdb_playlist_fn fn; void *user; int history; };
static int playlist_row(pdb *d, const unsigned char *r, size_t n, uint32_t page, size_t at, void *user) {
    (void)page; (void)at;
    struct playlists *ctx = user; pdb_playlist p;
    memset(&p, 0, sizeof(p));
    size_t offset = ctx->history ? 4u : 20u;
    p.id = u32(r + (ctx->history ? 0u : 12u));
    if (!ctx->history) { p.parent_id = u32(r); p.sort_order = u32(r + 8); p.is_folder = u32(r + 16) != 0; }
    int rc = decode(d, r + offset, n - offset, &p.name);
    return rc ? rc : ctx->fn(ctx->user, &p) ? PDB_STOP : PDB_OK;
}
static int each_playlist(pdb *d, pdb_playlist_fn fn, void *user, int history) {
    if (!fn) return PDB_EINVAL;
    int rc = enter(d); if (rc) return rc;
    struct playlists ctx = {fn, user, history};
    return leave(d, walk(d, history ? 11u : 7u, 0, playlist_row, &ctx));
}
int pdb_each_playlist(pdb *d, pdb_playlist_fn fn, void *user) { return each_playlist(d, fn, user, 0); }
int pdb_each_history(pdb *d, pdb_playlist_fn fn, void *user) { return each_playlist(d, fn, user, 1); }

/* No qsort: libc implementations may allocate. Heapsort uses constant stack. */
typedef int (*compare_fn)(const void *, const void *);
static void swap(unsigned char *a, unsigned char *b, size_t size) {
    for (size_t i = 0; i < size; ++i) { unsigned char t = a[i]; a[i] = b[i]; b[i] = t; }
}
static void sift(unsigned char *p, size_t root, size_t n, size_t size, compare_fn cmp) {
    while (root < n / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < n && cmp(p + child * size, p + (child + 1) * size) < 0) child++;
        if (cmp(p + root * size, p + child * size) >= 0) break;
        swap(p + root * size, p + child * size, size); root = child;
    }
}
static void sort(void *p, size_t n, size_t size, compare_fn cmp) {
    unsigned char *bytes = p;
    for (size_t i = n / 2; i; --i) sift(bytes, i - 1, n, size, cmp);
    for (size_t i = n; i > 1; --i) { swap(bytes, bytes + (i - 1) * size, size); sift(bytes, 0, i - 1, size, cmp); }
}
static int compare_u32(uint32_t a, uint32_t b) { return (a > b) - (a < b); }
static int entry_compare(const void *a, const void *b) {
    const pdb_entry *x = a, *y = b;
    int rc = compare_u32(x->sort_order, y->sort_order);
    return rc ? rc : compare_u32(x->track_id, y->track_id);
}
struct entries { uint32_t id; int history; pdb_entry *out; size_t count, capacity; };
static int entry_row(pdb *d, const unsigned char *r, size_t n, uint32_t page, size_t at, void *user) {
    (void)d; (void)n; (void)page; (void)at;
    struct entries *ctx = user;
    if (u32(r + (ctx->history ? 4u : 8u)) != ctx->id) return PDB_OK;
    if (ctx->out) {
        if (ctx->count >= ctx->capacity) return PDB_EFORMAT;
        ctx->out[ctx->count] = (pdb_entry){u32(r + (ctx->history ? 0u : 4u)), u32(r + (ctx->history ? 8u : 0u))};
    }
    ctx->count++;
    return PDB_OK;
}
int pdb_playlist_entries(pdb *d, uint32_t id, int history, const pdb_entry **out, size_t *count) {
    if (!out || !count) return PDB_EINVAL;
    *out = NULL; *count = 0;
    int rc = enter(d); if (rc) return rc;
    size_t mark = d->used;
    struct entries ctx = {id, history != 0, NULL, 0, 0};
    uint32_t type = history ? 12u : 8u;
    rc = walk(d, type, 0, entry_row, &ctx);
    if (!rc && ctx.count) {
        if (ctx.count > SIZE_MAX / sizeof(pdb_entry)) rc = PDB_ENOMEM;
        else ctx.out = alloc(d, ctx.count * sizeof(pdb_entry));
        if (!ctx.out) rc = PDB_ENOMEM;
        if (!rc) { ctx.capacity = ctx.count; ctx.count = 0; rc = walk(d, type, 0, entry_row, &ctx); }
    }
    if (!rc) { sort(ctx.out, ctx.count, sizeof(pdb_entry), entry_compare); *out = ctx.out; *count = ctx.count; }
    else d->used = mark;
    return leave(d, rc);
}

/* UTF-8 external query validation and Unicode case folding use no locale or
 * host wchar_t. The checked-in table includes expanding folds such as ss. */
static int next_utf8(pdb_text s, size_t *at, uint32_t *out) {
    if (*at >= s.len) return 0;
    unsigned char c = (unsigned char)s.data[(*at)++];
    if (c < 128) { *out = c; return 1; }
    unsigned need; uint32_t value, min;
    if (c >= 0xc2 && c <= 0xdf) { need = 1; value = c & 31u; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { need = 2; value = c & 15u; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { need = 3; value = c & 7u; min = 0x10000; }
    else return -1;
    if (need > s.len - *at) return -1;
    for (unsigned i = 0; i < need; ++i) {
        c = (unsigned char)s.data[(*at)++];
        if ((c & 0xc0u) != 0x80u) return -1;
        value = (value << 6) | (c & 63u);
    }
    if (value < min || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return -1;
    *out = value; return 1;
}
struct folded { pdb_text text; size_t at; uint32_t pending[3]; unsigned next, count; };
static int folded_next(struct folded *f, uint32_t *c) {
    if (f->next < f->count) { *c = f->pending[f->next++]; return 1; }
    int rc = next_utf8(f->text, &f->at, c);
    if (rc <= 0) return rc;
    size_t lo = 0, hi = sizeof(fold_table) / sizeof(fold_table[0]);
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (fold_table[mid].from < *c) lo = mid + 1; else hi = mid; }
    if (lo < sizeof(fold_table) / sizeof(fold_table[0]) && fold_table[lo].from == *c) {
        const struct fold_map *m = &fold_table[lo];
        memcpy(f->pending, m->to, sizeof(m->to)); f->count = m->count; f->next = 1; *c = m->to[0];
    } else { f->count = 0; f->next = 0; }
    return 1;
}
static int text_compare(pdb_text a, pdb_text b) {
    struct folded x = {a, 0, {0}, 0, 0}, y = {b, 0, {0}, 0, 0};
    uint32_t cx = 0, cy = 0;
    for (;;) {
        int nx = folded_next(&x, &cx), ny = folded_next(&y, &cy);
        if (!nx || !ny) return (nx > ny) - (nx < ny);
        if (cx != cy) return compare_u32(cx, cy);
    }
}
static int index_compare(const void *a, const void *b) {
    const pdb_index_entry *x = a, *y = b;
    int rc = text_compare(x->name, y->name);
    if (!rc) rc = compare_u32(x->group_id, y->group_id);
    if (!rc) rc = text_compare(x->title, y->title);
    return rc ? rc : compare_u32(x->track_id, y->track_id);
}
static int numeric_compare(const void *a, const void *b) {
    const pdb_index_entry *x = a, *y = b;
    int rc = compare_u32(x->group_id, y->group_id);
    if (!rc) rc = text_compare(x->title, y->title);
    return rc ? rc : compare_u32(x->track_id, y->track_id);
}
static int save_text(pdb *d, pdb_text s, pdb_text *out) {
    if (!s.len) { *out = empty; return PDB_OK; }
    char *p = alloc(d, s.len + 1);
    if (!p) return PDB_ENOMEM;
    memcpy(p, s.data, s.len); p[s.len] = 0; *out = (pdb_text){p, s.len};
    return PDB_OK;
}
struct index_context { pdb *db; pdb_category category; pdb_index_entry *out; size_t count, capacity; int error; };
static int index_track(void *user, const pdb_track *t) {
    struct index_context *ctx = user;
    if (!ctx->out) { ctx->count++; return 0; }
    if (ctx->count >= ctx->capacity) { ctx->error = PDB_EFORMAT; return 1; }
    pdb_text name = empty; uint32_t group = 0;
    switch (ctx->category) {
    case PDB_ARTIST: name = t->artist; group = t->artist_id; break;
    case PDB_ALBUM: name = t->album; group = t->album_id; break;
    case PDB_TRACK: name = t->title; group = t->id; break;
    case PDB_GENRE: name = t->genre; group = t->genre_id; break;
    case PDB_KEY: name = t->key; group = t->key_id; break;
    case PDB_BPM: group = t->bpm_x100; break;
    case PDB_RATING: group = t->rating; break;
    case PDB_COLOR: name = t->color; group = t->color_id; break;
    }
    pdb_index_entry *e = &ctx->out[ctx->count++];
    e->track_id = t->id; e->group_id = group;
    ctx->error = save_text(ctx->db, name, &e->name);
    if (!ctx->error) {
        if (ctx->category == PDB_TRACK) e->title = e->name;
        else ctx->error = save_text(ctx->db, t->title, &e->title);
    }
    return ctx->error != 0;
}
int pdb_build_index(pdb *d, pdb_category category, pdb_index *out) {
    if (!out || category < PDB_ARTIST || category > PDB_COLOR) return PDB_EINVAL;
    *out = (pdb_index){NULL, 0};
    int rc = enter(d); if (rc) return rc;
    size_t mark = d->used;
    struct index_context ctx = {d, category, NULL, 0, 0, 0};
    rc = tracks(d, index_track, &ctx);
    d->scratch = d->size;
    if (!rc && ctx.count) {
        if (ctx.count > SIZE_MAX / sizeof(pdb_index_entry)) rc = PDB_ENOMEM;
        else ctx.out = alloc(d, ctx.count * sizeof(pdb_index_entry));
        if (!ctx.out) rc = PDB_ENOMEM;
        if (!rc) { ctx.capacity = ctx.count; ctx.count = 0; rc = tracks(d, index_track, &ctx); }
    }
    if (ctx.error) rc = ctx.error;
    if (!rc) {
        sort(ctx.out, ctx.count, sizeof(pdb_index_entry), category >= PDB_BPM ? numeric_compare : index_compare);
        *out = (pdb_index){ctx.out, ctx.count};
    } else d->used = mark;
    return leave(d, rc);
}
static int whitespace(uint32_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
        (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
static int contains(pdb_text text, pdb_text term) {
    struct folded scan = {text, 0, {0}, 0, 0}; uint32_t c;
    for (;;) {
        struct folded candidate = scan, needle = {term, 0, {0}, 0, 0};
        uint32_t a = 0, b = 0; int nb;
        while ((nb = folded_next(&needle, &b)) > 0) {
            if (folded_next(&candidate, &a) <= 0 || a != b) break;
        }
        if (!nb) return 1;
        if (folded_next(&scan, &c) <= 0) return 0;
    }
}
static int matches(const pdb_track *t, pdb_text query) {
    size_t at = 0;
    while (at < query.len) {
        uint32_t c = 0; size_t begin = at;
        (void)next_utf8(query, &at, &c);
        if (whitespace(c)) continue;
        size_t end = at;
        while (at < query.len) {
            size_t prior = at;
            (void)next_utf8(query, &at, &c);
            if (whitespace(c)) { end = prior; break; }
            end = at;
        }
        pdb_text term = {query.data + begin, end - begin};
        if (!contains(t->title, term) && !contains(t->artist, term) && !contains(t->album, term)) return 0;
    }
    return 1;
}
struct search { pdb_text query; uint32_t *ids; size_t count, capacity; int error; };
static int search_track(void *user, const pdb_track *t) {
    struct search *s = user;
    if (!matches(t, s->query)) return 0;
    if (s->ids) {
        if (s->count >= s->capacity) { s->error = PDB_EFORMAT; return 1; }
        s->ids[s->count] = t->id;
    }
    s->count++;
    return 0;
}
int pdb_search(pdb *d, pdb_text query, const uint32_t **ids, size_t *count) {
    if (!ids || !count || (!query.data && query.len)) return PDB_EINVAL;
    *ids = NULL; *count = 0;
    size_t at = 0; uint32_t c;
    while (at < query.len) if (next_utf8(query, &at, &c) < 0) return PDB_EINVAL;
    int rc = enter(d); if (rc) return rc;
    size_t mark = d->used;
    struct search s = {query, NULL, 0, 0, 0};
    rc = tracks(d, search_track, &s);
    d->scratch = d->size;
    if (!rc && s.count) {
        if (s.count > SIZE_MAX / sizeof(uint32_t)) rc = PDB_ENOMEM;
        else s.ids = alloc(d, s.count * sizeof(uint32_t));
        if (!s.ids) rc = PDB_ENOMEM;
        if (!rc) { s.capacity = s.count; s.count = 0; rc = tracks(d, search_track, &s); }
    }
    if (s.error) rc = s.error;
    if (!rc) { *ids = s.ids; *count = s.count; } else d->used = mark;
    return leave(d, rc);
}
size_t pdb_mark(const pdb *d) { return d ? d->used : 0; }
int pdb_reset(pdb *d, size_t mark) {
    if (!d || mark < d->floor || mark > d->used) return PDB_EINVAL;
    if (d->busy) return PDB_EBUSY;
    d->used = mark; return PDB_OK;
}
size_t pdb_memory_used(const pdb *d) { return d ? d->used : 0; }
pdb_stats pdb_get_stats(const pdb *d) { const pdb_stats zero = {0, 0, 0, 0}; return d ? d->stats : zero; }
