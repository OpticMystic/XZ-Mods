/* SPDX-License-Identifier: MIT */
/* Folder profiles and the rekordbox stick colour (dj_folders.h).
   Usage: test_dj_folders <empty scratch directory> <stick with two VJTOOLS profiles> */
#define _POSIX_C_SOURCE 200809L
#include "dj_folders.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef NDEBUG
#error "folder profile acceptance requires assertions; compile with -UNDEBUG"
#endif

static char scratch[1024];
static const char *at(const char *root, const char *relative) {
    static char buffers[8][2048]; static int next; char *out = buffers[next++ & 7];
    snprintf(out, 2048, "%s/%s", root, relative); return out;
}
/* Writes `bytes` (or 10 filler bytes) at root/relative, making the folders on the way. */
static void put(const char *root, const char *relative, const void *bytes, size_t n) {
    char path[2048]; snprintf(path, sizeof(path), "%s/%s", root, relative);
    for (char *p = path + strlen(root) + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
    FILE *f = fopen(path, "wb"); assert(f);
    if (bytes) fwrite(bytes, 1, n, f); else fputs("0123456789", f);
    fclose(f);
}
static void folder(const char *root, const char *relative) {
    char path[2048]; snprintf(path, sizeof(path), "%s/%s", root, relative);
    for (char *p = path + strlen(root) + 1;; p++) if (*p == '/' || !*p) { char c = *p; *p = 0; mkdir(path, 0755); if (!c) break; *p = c; }
}
/* A minimal export.pdb: header page, the property table's index page, then its data page. */
static void pdb(const char *root, int colour, int data_flags) {
    static unsigned char file[3 * 4096];
    memset(file, 0, sizeof(file));
    unsigned char *h = file;
    h[4] = 0x00; h[5] = 0x10;                    /* page size 4096 */
    h[8] = 1;                                    /* one table */
    unsigned char *t = h + 0x1c; t[0] = 19; t[4] = 3; t[8] = 1; t[12] = 2;
    unsigned char *index = file + 4096; index[4] = 1; index[8] = 19; index[12] = 2; index[0x1b] = 0x64;
    unsigned char *data = file + 8192; data[4] = 2; data[8] = 19; data[12] = 3; data[0x1b] = (unsigned char)data_flags;
    data[0x28] = 0x80; data[0x29] = 0x02; data[0x28 + 8] = (unsigned char)colour;
    put(root, "PIONEER/rekordbox/export.pdb", file, sizeof(file));
}
static int named(const struct xz_dj_stick *s, int i, const char *name, const char *logo) {
    return i < s->count && !strcmp(s->profile[i].name, name) && s->profile[i].folder && !strcmp(s->profile[i].logo_path, logo) &&
        !strncmp(s->profile[i].dj_id, XZ_DJ_FOLDER_ID, strlen(XZ_DJ_FOLDER_ID)) && strlen(s->profile[i].dj_id) == strlen(XZ_DJ_FOLDER_ID) + 16;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    snprintf(scratch, sizeof(scratch), "%s", argv[1]);
    static struct xz_dj_stick s;

    /* A stick with only hand-made folders: no special software, the first folder is active. */
    const char *a = at(scratch, "a"); folder(scratch, "a");
    put(a, "DJPROFILES/Zed/b.png", NULL, 0); put(a, "DJPROFILES/Zed/a.JPG", NULL, 0);
    put(a, "DJPROFILES/DJ Kai/flyer.png", NULL, 0); put(a, "DJPROFILES/DJ Kai/Logo.PNG", NULL, 0);
    put(a, "DJPROFILES/.hidden/x.png", NULL, 0);
    put(a, "DJPROFILES/Notes/readme.txt", NULL, 0);
    folder(a, "DJPROFILES/Empty");
    put(a, "DJPROFILES/Deep/sub/logo.png", NULL, 0);       /* the logo must sit directly in the folder */
    put(a, "DJPROFILES/Tiny/logo.png", "1234", 4);          /* under 8 bytes */
    put(a, "DJPROFILES/stray.png", NULL, 0);                /* a file, not a DJ folder */
    put(a, "DJPROFILES/   /logo.png", NULL, 0);             /* blank name */
    assert(!symlink("Zed", at(a, "DJPROFILES/Linked")));
    assert(xz_dj_stick_read(a, &s) == 1 && s.count == 0);
    assert(xz_dj_folder_profiles(a, &s) == 2 && s.count == 2 && s.active == 0);
    assert(named(&s, 0, "DJ Kai", "DJPROFILES/DJ Kai/Logo.PNG") && named(&s, 1, "Zed", "DJPROFILES/Zed/a.JPG"));
    assert(!s.profile[0].has_thumb && !s.profile[0].tile_path[0] && !s.profile[0].document_path[0]);
    char kai[XZ_DJ_ID_MAX]; snprintf(kai, sizeof(kai), "%s", s.profile[0].dj_id);
    assert(strcmp(kai, s.profile[1].dj_id));

    /* DJPROFILE and DJPROFILES in any case: DJPROFILE's copy of a name wins, VJTOOLS is never a DJ. */
    put(a, "djprofile/dj kai/me.jpeg", NULL, 0);
    put(a, "djprofile/VJTOOLS/logo.png", NULL, 0);
    put(a, "djprofile/Mia Moon/logo.jpg", NULL, 0);
    assert(xz_dj_stick_read(a, &s) == 1 && xz_dj_folder_profiles(a, &s) == 3);
    assert(named(&s, 0, "dj kai", "djprofile/dj kai/me.jpeg") && named(&s, 1, "Mia Moon", "djprofile/Mia Moon/logo.jpg") &&
        named(&s, 2, "Zed", "DJPROFILES/Zed/a.JPG"));
    /* The id comes from the name, so it is the same on every read. */
    assert(strcmp(s.profile[0].dj_id, kai));
    snprintf(kai, sizeof(kai), "%s", s.profile[0].dj_id);
    assert(xz_dj_stick_read(a, &s) == 1 && xz_dj_folder_profiles(a, &s) == 3 && !strcmp(s.profile[0].dj_id, kai));

    /* Next to VJTOOLS profiles: a folder named like one is skipped and the index keeps choosing. */
    const char *b = argv[2];
    put(b, "DJPROFILES/dj KAI/logo.png", NULL, 0);
    put(b, "DJPROFILES/Mia Moon/logo.png", NULL, 0);
    assert(xz_dj_stick_read(b, &s) == 0 && s.count == 2);
    int active = s.active;
    assert(xz_dj_folder_profiles(b, &s) == 1 && s.count == 3 && s.active == active);
    assert(!s.profile[0].folder && !s.profile[1].folder && named(&s, 2, "Mia Moon", "DJPROFILES/Mia Moon/logo.png"));
    /* Sixteen in all. */
    static struct xz_dj_stick full; full.count = XZ_DJ_MAX_PROFILES - 1; full.active = 0;
    for (int i = 0; i < full.count; i++) snprintf(full.profile[i].name, sizeof(full.profile[i].name), "P%d", i);
    assert(xz_dj_folder_profiles(a, &full) == 1 && full.count == XZ_DJ_MAX_PROFILES && full.active == 0);
    assert(xz_dj_folder_profiles(a, &full) == 0);

    /* The rekordbox Background Color in export.pdb. */
    uint32_t rgb = 1;
    const char *c = at(scratch, "c"); folder(scratch, "c");
    assert(xz_dj_stick_color(c, &rgb) == 1 && rgb == 0);              /* no export */
    pdb(c, 7, 0x34); assert(xz_dj_stick_color(c, &rgb) == 0 && rgb == 0x1e50f0);
    pdb(c, 1, 0x34); assert(xz_dj_stick_color(c, &rgb) == 0 && rgb == 0xf870f8);
    pdb(c, 0, 0x34); assert(xz_dj_stick_color(c, &rgb) == 1 && rgb == 0);   /* none chosen */
    pdb(c, 9, 0x34); assert(xz_dj_stick_color(c, &rgb) == 1);
    pdb(c, 7, 0x64); assert(xz_dj_stick_color(c, &rgb) == 1);                /* no data page */
    put(c, "PIONEER/rekordbox/export.pdb", "not a database at all, just text that is long enough", 52);
    assert(xz_dj_stick_color(c, &rgb) == -1);
    assert(xz_dj_color_rgb(0) == 0 && xz_dj_color_rgb(8) == 0x9828f0 && xz_dj_color_rgb(9) == 0);

    puts("PASS DJ folders: folder-only stick, logo choice, hidden/empty/deep/tiny/blank/symlink skipped, DJPROFILE over DJPROFILES, VJTOOLS never a DJ, stable ids, VJTOOLS names win, 16 cap, rekordbox colour");
    return 0;
}
