/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error PDB acceptance requires active assertions
#endif
#include "pdb.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

struct source { const unsigned char *bytes; size_t length, calls; int fail; };
struct guarded {
    unsigned char before[32];
    alignas(max_align_t) unsigned char arena[512 * 1024];
    unsigned char after[32];
};
static struct guarded memory;
static unsigned char original[512 * 1024], mutated[512 * 1024], tiny[16384];
static uint32_t random_state = 0x91c4fa37;
static uint32_t rng(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5;
    return random_state;
}
static void guards(void) {
    memset(memory.before, 0xa5, sizeof(memory.before)); memset(memory.after, 0x5a, sizeof(memory.after));
}
static void check_guards(void) {
    for (size_t i = 0; i < 32; ++i) { assert(memory.before[i] == 0xa5); assert(memory.after[i] == 0x5a); }
}
static int read_memory(void *user, uint64_t at, void *out, size_t n) {
    struct source *s = user;
    assert(at <= s->length); assert(n <= s->length - at);
    s->calls++;
    if (s->fail) return -1;
    memcpy(out, s->bytes + (size_t)at, n);
    return 0;
}
static void text_ok(pdb_text s) {
    assert(s.data != NULL);
    assert(s.len <= 3 * 65536u);
    assert(s.data[s.len] == 0);
    /* Output must be valid, shortest-form UTF-8, including supplementary planes. */
    for (size_t i = 0; i < s.len;) {
        unsigned char c = (unsigned char)s.data[i++];
        if (c < 128) continue;
        size_t count; uint32_t cp, min;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; cp = c & 31u; min = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; cp = c & 15u; min = 0x800; }
        else { assert(c >= 0xf0 && c <= 0xf4); count = 3; cp = c & 7u; min = 0x10000; }
        assert(count <= s.len - i);
        for (size_t j = 0; j < count; ++j) {
            c = (unsigned char)s.data[i++]; assert((c & 0xc0u) == 0x80u); cp = (cp << 6) | (c & 63u);
        }
        assert(cp >= min && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff));
    }
}
static int equal(pdb_text s, const char *text) { return s.len == strlen(text) && !memcmp(s.data, text, s.len); }
static int inspect_track(void *user, const pdb_track *t) {
    size_t *count = user; (*count)++;
    const pdb_text texts[] = {t->title, t->artist, t->album, t->genre, t->key, t->label,
                             t->color, t->file_path, t->artwork_path, t->date_added};
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i) text_ok(texts[i]);
    assert(t->rating <= 5);
    return 0;
}
struct track_context { pdb *db; size_t count; };
static int expected_track(void *user, const pdb_track *t) {
    struct track_context *ctx = user;
    inspect_track(&ctx->count, t);
    assert(pdb_each_track(ctx->db, inspect_track, &ctx->count) == PDB_EBUSY);
    assert(pdb_reset(ctx->db, pdb_mark(ctx->db)) == PDB_EBUSY);
    assert(t->id == ctx->count * 101);
    assert(t->bitrate == 320 && t->track_number == ctx->count && t->sample_rate == 48000);
    assert(equal(t->date_added, "2026-10-18") && equal(t->label, "Independent"));
    if (t->id == 101) {
        assert(equal(t->title, "Alpha Night") && equal(t->artist, "Night Artist"));
        assert(equal(t->album, "First Album") && equal(t->genre, "House") && equal(t->key, "8A"));
        assert(equal(t->color, "Red") && equal(t->artwork_path, "/PIONEER/Artwork/a.jpg"));
        assert(equal(t->file_path, "/Contents/Alpha Night.mp3"));
        assert(t->bpm_x100 == 12800 && t->duration_seconds == 240 && t->rating == 5 && t->color_id == 1 && t->artwork_id == 40);
    } else if (t->id == 202) {
        assert(equal(t->title, "Été 東京 🎵") && equal(t->artist, "Björk 日本"));
        assert(equal(t->album, "夏 Album") && equal(t->genre, "Électronique") && equal(t->key, "F♯m"));
        assert(equal(t->color, "青") && equal(t->artwork_path, "/PIONEER/Artwork/夏.jpg"));
        assert(equal(t->file_path, "/Contents/Été 東京 🎵.flac"));
        assert(t->bpm_x100 == 12345 && t->duration_seconds == 301 && t->rating == 3 && t->color_id == 2 && t->artwork_id == 41);
    } else if (t->id == 303) {
        assert(t->title.len == 161 && !memcmp(t->title.data, "beta ", 5));
        assert(equal(t->artist, "Night Artist") && !t->artwork_path.len);
        assert(t->bpm_x100 == 9000 && t->duration_seconds == 99 && t->rating == 0);
    } else if (t->id == 404) {
        assert(equal(t->title, "Straße") && !t->artist.len && !t->album.len && !t->genre.len && !t->key.len && !t->color.len);
    } else assert(0);
    return 0;
}
static int stop_track(void *user, const pdb_track *t) { (void)user; (void)t; return 1; }
static int inspect_playlist(void *user, const pdb_playlist *p) {
    size_t *count = user; (*count)++; text_ok(p->name);
    return 0;
}
static int expected_playlist(void *user, const pdb_playlist *p) {
    size_t *count = user; (*count)++; text_ok(p->name);
    switch (p->id) {
    case 1000: assert(p->is_folder && p->parent_id == 0 && p->sort_order == 1 && equal(p->name, "Sets")); break;
    case 1001: assert(p->is_folder && p->parent_id == 1000 && p->sort_order == 2 && equal(p->name, "夏")); break;
    case 2000: assert(!p->is_folder && p->parent_id == 1000 && p->sort_order == 3 && equal(p->name, "Opening")); break;
    case 2001: assert(!p->is_folder && p->parent_id == 1001 && p->sort_order == 1 && equal(p->name, "日本")); break;
    case 2002: assert(!p->is_folder && p->parent_id == 0 && p->sort_order == 4 && equal(p->name, "Empty")); break;
    default: assert(0);
    }
    return 0;
}
static void search(pdb *d, const char *query, const uint32_t *expected, size_t wanted) {
    size_t mark = pdb_mark(d), count; const uint32_t *ids;
    assert(pdb_search(d, (pdb_text){query, strlen(query)}, &ids, &count) == PDB_OK);
    assert(count == wanted);
    if (wanted) assert(!memcmp(ids, expected, wanted * sizeof(uint32_t)));
    assert(pdb_reset(d, mark) == PDB_OK);
}
static void acceptance(struct source *s, int corrupt) {
    pdb *d;
    guards();
    assert(pdb_open(&d, read_memory, s, s->length, memory.arena, sizeof(memory.arena)) == PDB_OK);
    size_t mark = pdb_mark(d);
    struct track_context ctx = {d, 0};
    assert(pdb_each_track(d, expected_track, &ctx) == PDB_OK && ctx.count == 4);
    assert(pdb_memory_used(d) == mark);
    assert(pdb_get_stats(d).corrupt_pages == (uint64_t)corrupt);
    assert(pdb_each_track(d, stop_track, NULL) == PDB_STOP);
    size_t count = 0;
    assert(pdb_each_playlist(d, expected_playlist, &count) == PDB_OK && count == 5);
    count = 0; assert(pdb_each_history(d, inspect_playlist, &count) == PDB_OK && count == 1);
    const pdb_entry *entries;
    assert(pdb_playlist_entries(d, 2000, 0, &entries, &count) == PDB_OK && count == 4);
    const uint32_t entry_ids[] = {202, 303, 101, 101};
    for (size_t i = 0; i < count; ++i) assert(entries[i].track_id == entry_ids[i] && entries[i].sort_order == i + 1);
    assert(pdb_reset(d, mark) == PDB_OK);
    assert(pdb_playlist_entries(d, 2002, 0, &entries, &count) == PDB_OK && count == 0);
    assert(pdb_playlist_entries(d, 3000, 1, &entries, &count) == PDB_OK && count == 2);
    assert(entries[0].track_id == 101 && entries[1].track_id == 202);
    assert(pdb_reset(d, mark) == PDB_OK);
    const uint32_t orders[8][4] = {
        {404, 202, 101, 303}, {404, 101, 303, 202}, {101, 303, 404, 202}, {404, 101, 303, 202},
        {404, 101, 303, 202}, {303, 202, 101, 404}, {303, 202, 404, 101}, {404, 101, 303, 202}
    };
    for (int category = PDB_ARTIST; category <= PDB_COLOR; ++category) {
        pdb_index index;
        assert(pdb_build_index(d, (pdb_category)category, &index) == PDB_OK && index.count == 4);
        for (size_t i = 0; i < index.count; ++i) {
            assert(index.entries[i].track_id == orders[category][i]);
            text_ok(index.entries[i].name); text_ok(index.entries[i].title);
        }
        assert(pdb_reset(d, mark) == PDB_OK);
    }
    const uint32_t night[] = {101, 303}, japanese[] = {202}, strasse[] = {404}, all[] = {101, 202, 303, 404};
    search(d, "NIGHT first", night, 2); search(d, "éTÉ BJÖRK 東京", japanese, 1);
    search(d, "album　日本", japanese, 1); search(d, "STRASSE", strasse, 1);
    search(d, "\t \r\n", all, 4); search(d, "does-not-exist", NULL, 0);
    const uint32_t *bad_ids;
    assert(pdb_search(d, (pdb_text){"\xc0\x80", 2}, &bad_ids, &count) == PDB_EINVAL);
    assert(pdb_get_stats(d).cache_hits > 0 && pdb_get_stats(d).cache_misses > 0);
    assert(pdb_reset(d, 0) == PDB_EINVAL);
    check_guards();
    printf("PASS metadata, UTF-8, 8 indexes, search, playlists/history, corruption=%d; base arena=%zu bytes\n", corrupt, mark);
}
static void exercise(struct source *s, size_t arena_size) {
    guards(); pdb *d = NULL;
    int rc = pdb_open(&d, read_memory, s, s->length, memory.arena, arena_size);
    if (!rc) {
        size_t count = 0, mark = pdb_mark(d);
        rc = pdb_each_track(d, inspect_track, &count); assert(rc == PDB_OK || rc == PDB_ENOMEM);
        count = 0; rc = pdb_each_playlist(d, inspect_playlist, &count); assert(rc == PDB_OK || rc == PDB_ENOMEM);
        count = 0; rc = pdb_each_history(d, inspect_playlist, &count); assert(rc == PDB_OK || rc == PDB_ENOMEM);
        for (int category = PDB_ARTIST; category <= PDB_COLOR; ++category) {
            pdb_index index;
            rc = pdb_build_index(d, (pdb_category)category, &index);
            assert(rc == PDB_OK || rc == PDB_ENOMEM);
            if (rc) { assert(index.count == 0 && !index.entries); assert(pdb_mark(d) == mark); }
            assert(pdb_reset(d, mark) == PDB_OK);
        }
        const uint32_t *ids;
        rc = pdb_search(d, (pdb_text){"a", 1}, &ids, &count);
        assert(rc == PDB_OK || rc == PDB_ENOMEM); assert(pdb_reset(d, mark) == PDB_OK);
        const pdb_entry *entries;
        rc = pdb_playlist_entries(d, 2000, 0, &entries, &count);
        assert(rc == PDB_OK || rc == PDB_ENOMEM); assert(pdb_reset(d, mark) == PDB_OK);
    } else assert(rc == PDB_EFORMAT || rc == PDB_ENOMEM || rc == PDB_EINVAL || rc == PDB_EIO);
    check_guards();
}
static void limits(struct source *s) {
    pdb *d = (pdb *)(uintptr_t)1;
    assert(pdb_open(&d, read_memory, s, s->length, tiny, 1) == PDB_ENOMEM && d == NULL);
    assert(pdb_open(&d, NULL, s, s->length, tiny, sizeof(tiny)) == PDB_EINVAL);
    struct source failed = *s; failed.fail = 1;
    assert(pdb_open(&d, read_memory, &failed, s->length, memory.arena, sizeof(memory.arena)) == PDB_EIO);
    /* Arena exhaustion must not publish partial indexes; preserve old results. */
    assert(pdb_open(&d, read_memory, s, s->length, memory.arena + 1, sizeof(memory.arena) - 1) == PDB_OK);
    size_t base = pdb_mark(d); pdb_index first, next;
    assert(pdb_build_index(d, PDB_TRACK, &first) == PDB_OK);
    unsigned loops = 0;
    int result;
    while ((result = pdb_build_index(d, PDB_TRACK, &next)) == PDB_OK) assert(++loops < 2000);
    assert(result == PDB_ENOMEM);
    assert(next.count == 0 && !next.entries);
    assert(first.count == 4 && equal(first.entries[0].name, "Alpha Night"));
    assert(pdb_reset(d, base) == PDB_OK);
    /* Every arena size around the open/track/index boundaries is exercised. */
    for (size_t size = 1; size < 18000; size += 97) exercise(s, size);
    puts("PASS arena exhaustion rollback, alignment, I/O failure, callback bounds");
}
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}
static void corruptions(struct source *s) {
    uint32_t page_size = get32(s->bytes + 4), data_page = get32(s->bytes + 28 + 8) + 1;
    for (unsigned mode = 0; mode < 10; ++mode) {
        memcpy(mutated, s->bytes, s->length);
        unsigned char *p = mutated + (size_t)data_page * page_size;
        size_t expected = 0;
        switch (mode) {
        case 0: p[24] = 0xff; p[25] = 0x1f; break; /* oversized row index */
        case 1: p[page_size - 6] = 0xff; p[page_size - 5] = 0xff; break;
        case 2: p[40 + 0x5e + 17 * 2] = 1; p[40 + 0x5e + 17 * 2 + 1] = 0; break;
        case 3: p[40 + 0x59] = 6; break; /* rating cannot escape 0..5 */
        case 4: put32(p + 8, 13); break; /* wrong table */
        case 5: put32(p + 4, data_page + 1); break; /* wrong page number */
        case 6: { /* valid long header, body extends beyond this row */
            size_t off = (size_t)p[40 + 0x5e + 17 * 2] | (size_t)p[40 + 0x5e + 17 * 2 + 1] << 8;
            p[40 + off] = 0x40; p[40 + off + 1] = 0xff; p[40 + off + 2] = 0xff;
            break;
        }
        case 7: { /* lone UTF-16 high surrogate */
            size_t off = (size_t)p[40 + 0x5e + 17 * 2] | (size_t)p[40 + 0x5e + 17 * 2 + 1] << 8;
            unsigned char bad[] = {0x90, 6, 0, 0, 0, 0xd8};
            memcpy(p + 40 + off, bad, sizeof(bad)); break;
        }
        case 8: /* cycle back to index page; do not re-emit any track */
            put32(mutated + 28 + 12, data_page + 1);
            put32(p + 12, data_page - 1); expected = 4; break;
        case 9: /* bad link out of file */
            put32(mutated + 28 + 12, data_page + 1);
            put32(p + 12, 0xffffffffu); expected = 4; break;
        }
        struct source input = {mutated, s->length, 0, 0}; pdb *d;
        assert(pdb_open(&d, read_memory, &input, input.length, memory.arena, sizeof(memory.arena)) == PDB_OK);
        size_t count = 0;
        assert(pdb_each_track(d, inspect_track, &count) == PDB_OK && count == expected);
        assert(pdb_get_stats(d).corrupt_pages == 1);
        count = 0; assert(pdb_each_track(d, inspect_track, &count) == PDB_OK && count == expected);
        assert(pdb_get_stats(d).corrupt_pages == 1); /* distinct pages, not scan count */
    }
    puts("PASS malformed indexes/offsets/strings/surrogates, wrong-table pages, cycles, broken links, distinct-page accounting");
}
static void fuzz(struct source *s) {
    size_t length = s->length;
    for (unsigned iteration = 0; iteration < 2000; ++iteration) {
        memcpy(mutated, s->bytes, length);
        unsigned changes = 1 + rng() % 16;
        for (unsigned j = 0; j < changes; ++j) mutated[rng() % length] ^= (unsigned char)(1u << (rng() % 8));
        struct source input = {mutated, length, 0, 0};
        if (iteration % 10 == 0) input.length = rng() % length;
        exercise(&input, iteration % 7 == 0 ? 12000 : sizeof(memory.arena));
    }
    puts("PASS fuzz: 2000 deterministic random mutations/truncations; file bounds + arena guards + UTF-8 assertions");
}
int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) return 2;
    FILE *f = fopen(argv[1], "rb"); assert(f);
    size_t length = fread(original, 1, sizeof(original), f);
    assert(!ferror(f) && feof(f)); fclose(f);
    struct source input = {original, length, 0, 0};
    int is_corrupt = argc >= 3 && !strcmp(argv[2], "corrupt");
    acceptance(&input, is_corrupt);
    if (argc >= 3 && !strcmp(argv[argc - 1], "fuzz")) {
        limits(&input);
        if (!is_corrupt) corruptions(&input);
        fuzz(&input);
    }
    return 0;
}
