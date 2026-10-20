/* SPDX-License-Identifier: MIT */
/* The DJ Profile 1.0 stick layout (dj_folders.h) and the rekordbox stick colour.
   Usage: test_dj_folders <empty scratch directory> <stick with two pre-1.0 profiles> */
#define _POSIX_C_SOURCE 200809L
#include "dj_folders.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
/* Writes `bytes` (or a tiny PNG) at root/relative, making the folders on the way. */
static const unsigned char tiny_png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 4, 0, 0, 0, 4, 8, 2, 0, 0, 0, 0, 0, 0, 0 };
static void put(const char *root, const char *relative, const void *bytes, size_t n) {
    char path[2048]; snprintf(path, sizeof(path), "%s/%s", root, relative);
    for (char *p = path + strlen(root) + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
    FILE *f = fopen(path, "wb"); assert(f);
    if (bytes) fwrite(bytes, 1, n, f); else fwrite(tiny_png, 1, sizeof(tiny_png), f);
    fclose(f);
}
static void folder(const char *root, const char *relative) {
    char path[2048]; snprintf(path, sizeof(path), "%s/%s", root, relative);
    for (char *p = path + strlen(root) + 1;; p++) if (*p == '/' || !*p) { char c = *p; *p = 0; mkdir(path, 0755); if (!c) break; *p = c; }
}
/* A minimal export.pdb: header page, the property table's index page, then its data page with two rows
   of which only `live` (0 or 1) has its present bit set; colours are first and second. */
static void pdb(const char *root, int first, int second, int live, int data_flags) {
    static unsigned char file[3 * 4096];
    memset(file, 0, sizeof(file));
    unsigned char *h = file;
    h[4] = 0x00; h[5] = 0x10;                    /* page size 4096 */
    h[8] = 1;                                    /* one table */
    unsigned char *t = h + 0x1c; t[0] = 19; t[4] = 3; t[8] = 1; t[12] = 2;
    unsigned char *index = file + 4096; index[4] = 1; index[8] = 19; index[12] = 2; index[0x1b] = 0x64;
    unsigned char *data = file + 8192; data[4] = 2; data[8] = 19; data[12] = 3; data[0x1b] = (unsigned char)data_flags;
    data[0x18] = 2;                              /* two rows in the heap */
    data[4096 - 4] = (unsigned char)(1 << live); /* present bits of the first row group */
    data[4096 - 6] = 0; data[4096 - 8] = 12;     /* row offsets from 0x28 */
    data[0x28] = 0x80; data[0x29] = 0x02; data[0x28 + 9] = (unsigned char)first;
    data[0x34] = 0x80; data[0x35] = 0x02; data[0x34 + 9] = (unsigned char)second;
    data[0x28 + 8] = 4;                          /* byte +8 is not the colour */
    put(root, "PIONEER/rekordbox/export.pdb", file, sizeof(file));
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    snprintf(scratch, sizeof(scratch), "%s", argv[1]);
    static struct xz_dj_stick s;
    static unsigned char buf[XZ_DJ_LOGO_MAX_BYTES + 1];

    /* Candidates: DJPROFILE's folders by name key, then DJPROFILES'; dot names, links, files and VJTOOLS skipped. */
    const char *a = at(scratch, "a"); folder(scratch, "a");
    put(a, "DJPROFILES/Zed/b.png", NULL, 0); put(a, "DJPROFILES/Abe/logo.png", NULL, 0); put(a, "DJPROFILES/dj kai/logo.png", NULL, 0);
    put(a, "djprofile/DJ Kai/logo.png", NULL, 0); put(a, "djprofile/mia/logo.png", NULL, 0); put(a, "djprofile/VJTOOLS/logo.png", NULL, 0);
    put(a, "djprofile/.hidden/logo.png", NULL, 0); put(a, "djprofile/stray.png", NULL, 0); folder(a, "djprofile/Empty");
    assert(!symlink("mia", at(a, "djprofile/Linked")));
    char tops[2][XZ_DJ_NAME_MAX]; xz_dj_tops(a, tops); assert(!strcmp(tops[0], "djprofile") && !strcmp(tops[1], "DJPROFILES"));
    int n; struct xz_dj_candidate *c = xz_dj_candidates(a, &n);
    static const char *const order[] = { "djprofile/DJ Kai", "djprofile/Empty", "djprofile/mia", "DJPROFILES/Abe", "DJPROFILES/Zed" };
    assert(n == 5);
    for (int i = 0; i < n; i++) assert(!strcmp(c[i].dir, order[i]) && c[i].name == strchr(c[i].dir, '/') + 1);
    free(c);
    char name[XZ_DJ_NAME_MAX];
    put(a, "djprofile/mia/PROFILE.JSON", "{}", 2);
    assert(!xz_dj_fixed(a, "djprofile/mia", "profile.json", name) && !strcmp(name, "PROFILE.JSON"));
    assert(xz_dj_fixed(a, "djprofile/mia", "profile.previous.json", name));

    /* Level 0 logos: logo.png, logo.jpg, logo.jpeg, then the rest by name key; the first usable one. */
    put(a, "djprofile/Pick/flyer.png", NULL, 0); put(a, "djprofile/Pick/Logo.JPEG", "\xff\xd8\xff\xc0\x00\x11\x08\x00\x04\x00\x04\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01\xff\xd9", 23);
    assert(!xz_dj_level0_logo(a, "djprofile/Pick", name, buf) && !strcmp(name, "Logo.JPEG"));
    unsigned char big[33]; memcpy(big, tiny_png, sizeof(big)); big[18] = 0x10; big[19] = 0x01;   /* 4097 px wide */
    put(a, "djprofile/Pick/logo.png", big, sizeof(big));
    assert(!xz_dj_level0_logo(a, "djprofile/Pick", name, buf) && !strcmp(name, "Logo.JPEG"));
    assert(xz_dj_level0_logo(a, "djprofile/Empty", name, buf) == 1);
    struct xz_dj_image_info info;
    assert(!xz_dj_image_info(tiny_png, sizeof(tiny_png), &info) && info.kind == XZ_DJ_IMAGE_PNG && info.width == 4 && info.plain);
    assert(xz_dj_image_usable(big, sizeof(big), &info) && info.width == 4097);

    /* Folder names (section 3). */
    assert(!xz_dj_valid_folder_name("DJ Kai", 6) && !xz_dj_valid_folder_name("Beyonc\xc3\xa9", 8));
    assert(xz_dj_valid_folder_name("a:b", 3) && xz_dj_valid_folder_name("trailing.", 9) && xz_dj_valid_folder_name("space ", 6) && xz_dj_valid_folder_name("   ", 3));
    char longest[200]; memset(longest, 'x', 100); longest[100] = 0; assert(!xz_dj_valid_folder_name(longest, 100));
    longest[100] = 'x'; longest[101] = 0; assert(xz_dj_valid_folder_name(longest, 101));

    /* Next to the pre-1.0 store: a hand-made folder named like an old profile is listed once (contract 0.1 step 3). */
    const char *b = argv[2];
    put(b, "DJPROFILES/dj KAI/logo.png", NULL, 0);
    put(b, "DJPROFILES/Mia Moon/logo.png", NULL, 0);
    assert(xz_dj_stick_read(b, &s) == 0 && s.count == 3);
    assert(!strcmp(s.profile[0].name, "Mia Moon") && s.profile[0].kind == XZ_DJ_LEVEL0 && !strcmp(s.profile[0].logo_path, "DJPROFILES/Mia Moon/logo.png"));
    assert(!strncmp(s.profile[0].dj_id, XZ_DJ_FOLDER_ID, strlen(XZ_DJ_FOLDER_ID)) && strlen(s.profile[0].dj_id) == strlen(XZ_DJ_FOLDER_ID) + 16);
    assert(s.profile[1].kind == XZ_DJ_OLD && !strcmp(s.profile[1].name, "DJ Kai") && s.profile[2].kind == XZ_DJ_OLD);
    assert(s.active == 2 && !strcmp(xz_dj_profile_ref(&s.profile[0]), "Mia Moon") && !strcmp(xz_dj_profile_ref(&s.profile[1]), "djp_kai"));

    /* The rekordbox Background Color: byte +9 of the live djdbProperty row. */
    uint32_t rgb = 1;
    const char *k = at(scratch, "c"); folder(scratch, "c");
    assert(xz_dj_stick_color(k, &rgb) == 1 && rgb == 0);              /* no export */
    pdb(k, 5, 7, 1, 0x34); assert(xz_dj_stick_color(k, &rgb) == 0 && rgb == 0x1e50f0);   /* the stale first row is skipped */
    pdb(k, 1, 7, 0, 0x34); assert(xz_dj_stick_color(k, &rgb) == 0 && rgb == 0xf870f8);
    pdb(k, 0, 0, 1, 0x34); assert(xz_dj_stick_color(k, &rgb) == 1 && rgb == 0);   /* none chosen */
    pdb(k, 3, 9, 1, 0x34); assert(xz_dj_stick_color(k, &rgb) == 1);
    pdb(k, 7, 7, 1, 0x64); assert(xz_dj_stick_color(k, &rgb) == 1);                /* no data page */
    put(k, "PIONEER/rekordbox/export.pdb", "not a database at all, just text that is long enough", 52);
    assert(xz_dj_stick_color(k, &rgb) == -1);
    assert(xz_dj_color_rgb(0) == 0 && xz_dj_color_rgb(8) == 0x9828f0 && xz_dj_color_rgb(9) == 0);

    puts("PASS DJ folders: candidates in reference order (dot/link/file/VJTOOLS skipped, DJPROFILE first), fixed names folded, level 0 logo rank and limits, folder names, an old profile's hand-made folder listed once, stick colour from the live row's byte +9");
    return 0;
}
