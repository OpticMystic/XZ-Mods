/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_folders.h"
#include "dj_profile_internal.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fold(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int xz_dj_fold_equal(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (fold((unsigned char)*a) != fold((unsigned char)*b)) return 0;
    return !*a && !*b;
}
static int join(char *out, size_t cap, const char *a, const char *b) { return snprintf(out, cap, "%s/%s", a, b) >= (int)cap; }
static int real_dir(const char *path) { struct stat st; return !lstat(path, &st) && S_ISDIR(st.st_mode); }

void xz_dj_tops(const char *root, char tops[2][XZ_DJ_NAME_MAX]) {
    tops[0][0] = tops[1][0] = 0;
    DIR *d = root ? opendir(root) : NULL; struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        int k = xz_dj_fold_equal(e->d_name, "DJPROFILE") ? 0 : xz_dj_fold_equal(e->d_name, "DJPROFILES") ? 1 : -1;
        char path[4096];
        if (k < 0 || tops[k][0] || strlen(e->d_name) >= XZ_DJ_NAME_MAX || join(path, sizeof(path), root, e->d_name) || !real_dir(path)) continue;
        memcpy(tops[k], e->d_name, strlen(e->d_name) + 1);
    }
    closedir(d);
}
struct sorted { struct xz_dj_candidate c; int order; };
static int by_key(const void *a, const void *b) {
    const struct sorted *x = a, *y = b; int c = strcmp(x->c.key, y->c.key);
    return c ? c : x->order - y->order;
}
struct xz_dj_candidate *xz_dj_candidates(const char *root, int *count) {
    char tops[2][XZ_DJ_NAME_MAX]; struct sorted *all = NULL; int n = 0, cap = 0;
    *count = 0;
    xz_dj_tops(root, tops);
    for (int k = 0; k < 2; k++) {
        char top[4096]; DIR *d; struct dirent *e; int first = n;
        if (!tops[k][0] || join(top, sizeof(top), root, tops[k]) || !(d = opendir(top))) continue;
        while ((e = readdir(d)) && n < 1024) {
            char path[4096]; size_t len = strlen(e->d_name);
            if (e->d_name[0] == '.' || xz_dj_fold_equal(e->d_name, "VJTOOLS") || len >= XZ_DJ_NAME_MAX ||
                join(path, sizeof(path), top, e->d_name) || !real_dir(path)) continue;
            if (n == cap) {
                int grown = cap ? cap * 2 : 16; struct sorted *more = realloc(all, sizeof(*all) * (size_t)grown);
                if (!more) break;
                all = more; cap = grown;
            }
            struct sorted *s = &all[n];
            if (snprintf(s->c.dir, sizeof(s->c.dir), "%s/%s", tops[k], e->d_name) >= (int)sizeof(s->c.dir) ||
                xz_dj_name_key(e->d_name, len, s->c.key, sizeof(s->c.key)) < 0) continue;
            s->c.name = NULL; s->c.top = k; s->order = n; n++;
        }
        closedir(d);
        if (n - first > 1) qsort(all + first, (size_t)(n - first), sizeof(*all), by_key);
    }
    struct xz_dj_candidate *out = n ? malloc(sizeof(*out) * (size_t)n) : NULL;
    int kept = 0;
    for (int i = 0; out && i < n; i++) {
        int seen = 0;
        for (int j = 0; j < kept && !seen; j++) seen = !strcmp(out[j].key, all[i].c.key);
        if (seen) continue;
        out[kept] = all[i].c;
        out[kept].name = strchr(out[kept].dir, '/') + 1;
        kept++;
    }
    free(all);
    if (out && !kept) { free(out); out = NULL; }
    *count = kept;
    return out;
}
int xz_dj_fixed(const char *root, const char *dir, const char *name, char out[XZ_DJ_NAME_MAX]) {
    char path[4096]; DIR *d; struct dirent *e; int found = -1;
    if (join(path, sizeof(path), root, dir) || !(d = opendir(path))) return -1;
    while (found && (e = readdir(d))) {
        char file[4096]; struct stat st;
        if (e->d_name[0] == '.' || !xz_dj_fold_equal(e->d_name, name) || strlen(e->d_name) >= XZ_DJ_NAME_MAX ||
            join(file, sizeof(file), path, e->d_name) || lstat(file, &st) || !S_ISREG(st.st_mode)) continue;
        memcpy(out, e->d_name, strlen(e->d_name) + 1); found = 0;
    }
    closedir(d);
    return found;
}

/* --- images (section 5.2) --- */
static uint32_t be32(const unsigned char *b) { return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]; }
static unsigned be16(const unsigned char *b) { return (unsigned)b[0] << 8 | b[1]; }
int xz_dj_image_info(const unsigned char *data, size_t n, struct xz_dj_image_info *o) {
    memset(o, 0, sizeof(*o));
    if (n >= 29 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8) && !memcmp(data + 12, "IHDR", 4)) {
        o->kind = XZ_DJ_IMAGE_PNG; o->width = be32(data + 16); o->height = be32(data + 20);
        o->plain = data[24] == 8 && data[28] == 0;
        return 0;
    }
    if (n < 2 || data[0] != 0xff || data[1] != 0xd8) return -1;
    for (size_t at = 2; at + 9 < n;) {
        if (data[at] != 0xff) return -1;
        unsigned marker = data[at + 1];
        if (marker == 0xd8 || marker == 0x01 || marker == 0xff || (marker >= 0xd0 && marker <= 0xd7)) { at += marker == 0xff ? 1 : 2; continue; }
        if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) || (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
            o->kind = XZ_DJ_IMAGE_JPEG; o->height = be16(data + at + 5); o->width = be16(data + at + 7);
            o->plain = marker == 0xc0 || marker == 0xc1;
            return 0;
        }
        at += 2 + be16(data + at + 2);
    }
    return -1;
}
int xz_dj_image_usable(const unsigned char *data, size_t n, struct xz_dj_image_info *o) {
    struct xz_dj_image_info scratch; if (!o) o = &scratch;
    if (n < XZ_DJ_LOGO_MIN_BYTES || n > XZ_DJ_LOGO_MAX_BYTES || xz_dj_image_info(data, n, o)) return -1;
    return o->width && o->height && o->width <= XZ_DJ_LOGO_MAX_SIDE && o->height <= XZ_DJ_LOGO_MAX_SIDE &&
        (uint64_t)o->width * o->height <= XZ_DJ_LOGO_MAX_PIXELS ? 0 : -1;
}
static int image_suffix(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && dot != name && (xz_dj_fold_equal(dot, ".png") || xz_dj_fold_equal(dot, ".jpg") || xz_dj_fold_equal(dot, ".jpeg"));
}
struct image { char name[XZ_DJ_NAME_MAX]; char key[XZ_DJ_KEY_MAX]; int rank; };
static int by_rank(const void *a, const void *b) {
    const struct image *x = a, *y = b;
    return x->rank != y->rank ? x->rank - y->rank : strcmp(x->key, y->key);
}
int xz_dj_level0_logo(const char *root, const char *dir, char out[XZ_DJ_NAME_MAX], unsigned char *buf) {
    static const char *const first[] = { "logo.png", "logo.jpg", "logo.jpeg" };
    char path[4096]; DIR *d; struct dirent *e; struct image *found = NULL; int n = 0, cap = 0, result = 1;
    if (join(path, sizeof(path), root, dir) || !(d = opendir(path))) return -1;
    while ((e = readdir(d)) && n < 4096) {
        char file[4096]; struct stat st; size_t len = strlen(e->d_name);
        if (e->d_name[0] == '.' || !image_suffix(e->d_name) || len >= XZ_DJ_NAME_MAX || join(file, sizeof(file), path, e->d_name) ||
            lstat(file, &st) || !S_ISREG(st.st_mode)) continue;
        if (n == cap) {
            int grown = cap ? cap * 2 : 8; struct image *more = realloc(found, sizeof(*found) * (size_t)grown);
            if (!more) break;
            found = more; cap = grown;
        }
        memcpy(found[n].name, e->d_name, len + 1);
        if (xz_dj_name_key(e->d_name, len, found[n].key, sizeof(found[n].key)) < 0) continue;
        found[n].rank = 3;
        for (int k = 0; k < 3; k++) if (xz_dj_fold_equal(e->d_name, first[k])) found[n].rank = k;
        n++;
    }
    closedir(d);
    if (n > 1) qsort(found, (size_t)n, sizeof(*found), by_rank);
    for (int i = 0; i < n && result == 1; i++) {
        char relative[2 * 4096]; size_t got;
        if (snprintf(relative, sizeof(relative), "%s/%s", dir, found[i].name) >= (int)sizeof(relative)) continue;
        int rc = xz_djp_confined_read(root, relative, XZ_DJ_LOGO_MAX_BYTES, buf, &got);
        if (rc == XZ_DJP_READ_IO) result = -1;
        else if (rc == XZ_DJP_READ_OK && !xz_dj_image_usable(buf, got, NULL)) { memcpy(out, found[i].name, strlen(found[i].name) + 1); result = 0; }
    }
    free(found);
    return result;
}

/* --- names (sections 3 and 5) --- */
static uint32_t next_cp(const unsigned char *s, size_t n, size_t *i) {
    unsigned c = s[*i]; size_t k = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
    uint32_t v = k == 0 ? c : c & (k == 3 ? 7u : k == 2 ? 15u : 31u);
    for (size_t j = 1; j <= k && *i + j < n; j++) v = v << 6 | (s[*i + j] & 63);
    *i += k + 1; return v;
}
/* Python's str.isspace(), which strip() uses. */
static int space(uint32_t c) {
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
        c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
static int control(uint32_t c) { return c < 0x20 || (c >= 0x7f && c <= 0x9f); }
int xz_dj_valid_name(const char *s, size_t n, size_t max) {
    struct xz_djp_str v = { s, n };
    if (!n || n > max || xz_djp_utf8_valid(v)) return -1;
    int blank = 1;
    for (size_t i = 0; i < n;) { uint32_t c = next_cp((const unsigned char *)s, n, &i); if (control(c)) return -1; if (!space(c)) blank = 0; }
    return blank ? -1 : 0;
}
int xz_dj_valid_folder_name(const char *s, size_t n) {
    if (xz_dj_valid_name(s, n, 160) || s[n - 1] == ' ' || s[n - 1] == '.') return -1;
    size_t units = 0;
    for (size_t i = 0; i < n;) { uint32_t c = next_cp((const unsigned char *)s, n, &i); units += c >= 0x10000 ? 2 : 1; if (c < 0x80 && strchr("<>:\"/\\|?*", (int)c) && c) return -1; }
    return units > 100 ? -1 : 0;
}

/* --- rekordbox stick colour (section 11) --- */
uint32_t xz_dj_color_rgb(int id) {
    static const uint32_t rgb[9] = { 0, 0xf870f8, 0xf87070, 0xffa064, 0xf8e550, 0x1ee12b, 0x10e4dc, 0x1e50f0, 0x9828f0 };
    return id >= 1 && id <= 8 ? rgb[id] : 0;
}
static uint32_t u32le(const unsigned char *b) { return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static unsigned u16le(const unsigned char *b) { return b[0] | (unsigned)b[1] << 8; }
static int read_at(int fd, void *buf, size_t n, uint64_t at) {
    size_t got = 0;
    while (got < n) { ssize_t r = pread(fd, (char *)buf + got, n - got, (off_t)(at + got)); if (r <= 0) return -1; got += (size_t)r; }
    return 0;
}
/* The page offset of the row the row index marks present (rows start 80 02), or 0. The last one wins,
   as in builder/dj_profile.py _live_property_row. */
static size_t live_row(const unsigned char *page, uint32_t page_size) {
    unsigned count = u32le(page + 0x18) & 0x1fff; size_t live = 0;
    for (unsigned group = 0; group < (count + 15) / 16; group++) {
        if ((int64_t)page_size - (int64_t)group * 36 - 36 < 0x28) break;
        size_t end = page_size - group * 36;
        unsigned present = u16le(page + end - 4), rows = count - group * 16 < 16 ? count - group * 16 : 16;
        for (unsigned index = 0; index < rows; index++) {
            if (!(present >> index & 1)) continue;
            size_t start = 0x28 + u16le(page + end - 6 - 2 * index);
            if (start + 12 <= page_size && page[start] == 0x80 && page[start + 1] == 0x02) live = start;
        }
    }
    return live;
}
#define PDB_PATH "PIONEER/rekordbox/export.pdb"
#define PROPERTY_TABLE 19
int xz_dj_stick_color(const char *root, uint32_t *rgb) {
    int fd = -1; uint64_t size = 0;
    if (rgb) *rgb = 0;
    int rc = xz_djp_confined_open(root, PDB_PATH, (uint64_t)1 << 32, &fd, &size);
    if (rc == XZ_DJP_READ_MISSING) return 1;
    if (rc != XZ_DJP_READ_OK) return -1;
    int result = -1; unsigned char head[0x1c + 16 * 64], *page = NULL;
    if (size < sizeof(head) || read_at(fd, head, sizeof(head), 0)) goto done;
    uint32_t page_size = u32le(head + 4), tables = u32le(head + 8);
    if (u32le(head) || page_size < 0x40 || page_size > 65536 || !tables || tables > 64) goto done;
    uint32_t first = 0, last = 0; int listed = 0;
    for (uint32_t i = 0; i < tables && !listed; i++) {
        const unsigned char *t = head + 0x1c + 16 * i;
        if (u32le(t) == PROPERTY_TABLE) { first = u32le(t + 8); last = u32le(t + 12); listed = 1; }
    }
    if (!listed || !(page = malloc(page_size))) goto done;
    uint32_t at = first; result = 1;
    for (int hops = 0; hops < 64; hops++) {
        if ((uint64_t)at * page_size + page_size > size || read_at(fd, page, page_size, (uint64_t)at * page_size)) { result = -1; break; }
        if (u32le(page + 4) != at || u32le(page + 8) != PROPERTY_TABLE) { result = -1; break; }
        size_t row = page[0x1b] & 0x40 ? 0 : live_row(page, page_size);
        if (row) {
            uint32_t colour = xz_dj_color_rgb(page[row + 9]);
            if (colour) { if (rgb) *rgb = colour; result = 0; }
            break;
        }
        if (at == last) break;
        at = u32le(page + 12);
    }
done:
    free(page); close(fd); return result;
}
