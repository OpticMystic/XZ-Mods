/* SPDX-License-Identifier: MIT */
#include "pdb.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

static unsigned char arena[512 * 1024];
static int read_file(void *user, uint64_t at, void *out, size_t length) {
    FILE *f = user;
    /* Portable C11 adapter only. The reader itself supports uint64_t offsets. */
    if (at > LONG_MAX || fseek(f, (long)at, SEEK_SET)) return -1;
    return fread(out, 1, length, f) == length ? 0 : -1;
}
static void text(pdb_text s) {
    putchar('"');
    for (size_t i = 0; i < s.len; ++i) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32) printf("\\u%04x", (unsigned)c);
        else putchar(c);
    }
    putchar('"');
}
static int track(void *user, const pdb_track *t) {
    int *first = user;
    if (!*first) putchar(',');
    *first = 0;
    printf("{\"id\":%u,\"bpm_x100\":%u,\"duration\":%u,\"sample_rate\":%u,"
           "\"rating\":%u,\"color_id\":%u,\"track_number\":%u,\"artwork_id\":%u,\"bitrate\":%u",
           (unsigned)t->id, (unsigned)t->bpm_x100, (unsigned)t->duration_seconds, (unsigned)t->sample_rate,
           (unsigned)t->rating, (unsigned)t->color_id, (unsigned)t->track_number,
           (unsigned)t->artwork_id, (unsigned)t->bitrate);
    const char *names[] = {"title", "artist", "album", "genre", "key", "label", "color", "file_path", "artwork_path", "date_added"};
    const pdb_text values[] = {t->title, t->artist, t->album, t->genre, t->key, t->label,
                              t->color, t->file_path, t->artwork_path, t->date_added};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) { printf(",\"%s\":", names[i]); text(values[i]); }
    putchar('}');
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 2; }
    long length = ftell(f);
    if (length < 0) { fclose(f); return 2; }
    pdb *db;
    int rc = pdb_open(&db, read_file, f, (uint64_t)length, arena, sizeof(arena));
    if (!rc) { int first = 1; putchar('['); rc = pdb_each_track(db, track, &first); puts("]"); }
    fclose(f);
    return rc ? 1 : 0;
}
