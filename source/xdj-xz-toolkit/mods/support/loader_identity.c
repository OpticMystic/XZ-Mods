/* SPDX-License-Identifier: MIT */
#if !defined(_WIN32)
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#endif
#include "loader_identity.h"
#include "crockford.h"
#define JSMN_STATIC
#define JSMN_STRICT
#include "../audio/vendor/jsmn/jsmn.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#if defined(_WIN32)
#define O_IDENTITY_FLAGS (O_RDONLY | _O_BINARY)
#else
#define O_IDENTITY_FLAGS (O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
#endif

void xz_loader_identity_clear(struct xz_loader_identity *out) {
    if (!out) return;
    volatile unsigned char *p = (volatile unsigned char *)out;
    for (size_t i = 0; i < sizeof *out; i++) p[i] = 0;
}

static int set(struct xz_loader_identity *out, int state) {
    xz_loader_identity_clear(out);
    out->state = state;
    return state;
}

static int is_key(const char *json, const jsmntok_t *t, const char *name) {
    size_t n = strlen(name);
    return t->type == JSMN_STRING && (size_t)(t->end - t->start) == n && !memcmp(json + t->start, name, n);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* An ID exactly as the schema writes it: the prefix, a hyphen, four, a hyphen, four, all canonical Crockford. */
static int canonical_id(const char *value, size_t n, char prefix, char out[12]) {
    return n == 11 && !xz_crockford_ref(value, n, prefix, out) && !memcmp(out, value, 11) ? 0 : -1;
}
static int digits(const char *s, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0; return 1; }
/* YYYY-MM-DDTHH:MM:SS.mmmZ */
static int timestamp(const char *s, size_t n) {
    return n == 24 && digits(s, 4) && s[4] == '-' && digits(s + 5, 2) && s[7] == '-' && digits(s + 8, 2) && s[10] == 'T' &&
           digits(s + 11, 2) && s[13] == ':' && digits(s + 14, 2) && s[16] == ':' && digits(s + 17, 2) && s[19] == '.' &&
           digits(s + 20, 3) && s[23] == 'Z';
}
/* unknown, or MAJOR.MINOR.PATCH with an optional -prerelease and +build of [a-z0-9.]. */
static int version_text(const char *s, size_t n) {
    if (n > 64) return 0;
    if (n == 7 && !memcmp(s, "unknown", 7)) return 1;
    size_t i = 0;
    for (int part = 0; part < 3; part++) {
        size_t start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        if (i == start) return 0;
        if (part < 2) { if (i >= n || s[i] != '.') return 0; i++; }
    }
    for (int part = 0; part < 2; part++) {
        if (i < n && s[i] == (part ? '+' : '-')) {
            size_t start = ++i;
            while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.')) i++;
            if (i == start) return 0;
        }
    }
    return i == n;
}

int xz_loader_identity_parse(const char *json, size_t len, struct xz_loader_identity *out) {
    if (!out) return XZ_LOADER_INVALID;
    if (!json) return set(out, XZ_LOADER_INVALID);
    if (len > XZ_LOADER_IDENTITY_MAX_BYTES) return set(out, XZ_LOADER_OVERSIZED);
    for (size_t i = 0; i < len; i++) if (!json[i]) return set(out, XZ_LOADER_INVALID);
    jsmn_parser parser;
    jsmntok_t t[14];
    jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, len, t, 14);
    if (count != 13 || t[0].type != JSMN_OBJECT || t[0].size != 6) return set(out, XZ_LOADER_INVALID);
    for (size_t i = (size_t)t[0].end; i < len; i++)
        if (json[i] != ' ' && json[i] != '\t' && json[i] != '\r' && json[i] != '\n') return set(out, XZ_LOADER_INVALID);
    unsigned seen = 0;
    char id[12] = {0}, installation[12] = {0};
    unsigned char key[XZ_LOADER_KEY_BYTES];
    for (int i = 1; i < count; i += 2) {
        const jsmntok_t *k = &t[i], *v = &t[i + 1];
        if (k->type != JSMN_STRING || v->size != 0) return set(out, XZ_LOADER_INVALID);
        const char *value = json + v->start;
        size_t n = (size_t)(v->end - v->start);
        unsigned bit;
        if (is_key(json, k, "version")) {
            bit = 1;
            if (v->type != JSMN_PRIMITIVE || n != 1 || value[0] != '1') return set(out, XZ_LOADER_INVALID);
        } else {
            /* Every other field is a plain string: no escapes, so its bytes are its text. */
            if (v->type != JSMN_STRING || memchr(value, '\\', n)) return set(out, XZ_LOADER_INVALID);
            if (is_key(json, k, "loader_id")) {
                bit = 2;
                if (canonical_id(value, n, 'L', id)) return set(out, XZ_LOADER_INVALID);
            } else if (is_key(json, k, "loader_key")) {
                bit = 4;
                if (n != 2 * XZ_LOADER_KEY_BYTES) return set(out, XZ_LOADER_INVALID);
                for (size_t b = 0; b < XZ_LOADER_KEY_BYTES; b++) {
                    int hi = hex_value(value[2 * b]), lo = hex_value(value[2 * b + 1]);
                    if (hi < 0 || lo < 0) return set(out, XZ_LOADER_INVALID);
                    key[b] = (unsigned char)(hi << 4 | lo);
                }
            } else if (is_key(json, k, "installation_id")) {
                bit = 8;
                if (canonical_id(value, n, 'I', installation)) return set(out, XZ_LOADER_INVALID);
            } else if (is_key(json, k, "created_at")) {
                bit = 16;
                if (!timestamp(value, n)) return set(out, XZ_LOADER_INVALID);
            } else if (is_key(json, k, "app_version")) {
                bit = 32;
                if (!version_text(value, n)) return set(out, XZ_LOADER_INVALID);
            } else return set(out, XZ_LOADER_INVALID);
        }
        if (seen & bit) return set(out, XZ_LOADER_INVALID);
        seen |= bit;
    }
    if (seen != 63) return set(out, XZ_LOADER_INVALID);
    set(out, XZ_LOADER_OK);
    memcpy(out->id, id, sizeof id);
    memcpy(out->installation, installation, sizeof installation);
    memcpy(out->key, key, sizeof key);
    volatile unsigned char *wipe = key;
    for (size_t i = 0; i < sizeof key; i++) wipe[i] = 0;
    return XZ_LOADER_OK;
}

int xz_loader_identity_read(const char *usb_root, struct xz_loader_identity *out) {
    if (!out) return XZ_LOADER_INVALID;
    if (!usb_root || !usb_root[0]) return set(out, XZ_LOADER_NONE);
    char path[1100];
    if (snprintf(path, sizeof path, "%s/VJ.Tools/loader-identity.json", usb_root) >= (int)sizeof path)
        return set(out, XZ_LOADER_NONE);
#if !defined(_WIN32)
    struct stat link;
    if (lstat(path, &link)) return set(out, errno == ENOENT || errno == ENOTDIR ? XZ_LOADER_NONE : XZ_LOADER_UNREADABLE);
    if (!S_ISREG(link.st_mode)) return set(out, XZ_LOADER_UNREADABLE);
#endif
    int fd = open(path, O_IDENTITY_FLAGS);
    if (fd < 0) return set(out, errno == ENOENT || errno == ENOTDIR ? XZ_LOADER_NONE : XZ_LOADER_UNREADABLE);
    struct stat st;
    if (fstat(fd, &st) || (st.st_mode & S_IFMT) != S_IFREG) { close(fd); return set(out, XZ_LOADER_UNREADABLE); }
    if (st.st_size > XZ_LOADER_IDENTITY_MAX_BYTES) { close(fd); return set(out, XZ_LOADER_OVERSIZED); }
    char buffer[XZ_LOADER_IDENTITY_MAX_BYTES + 1];
    size_t total = 0;
    for (;;) {
        int n = (int)read(fd, buffer + total, (unsigned)(sizeof buffer - total));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return set(out, XZ_LOADER_UNREADABLE); }
        if (n == 0) break;
        total += (size_t)n;
        if (total == sizeof buffer) break;
    }
    close(fd);
    int state = total > XZ_LOADER_IDENTITY_MAX_BYTES ? set(out, XZ_LOADER_OVERSIZED) : xz_loader_identity_parse(buffer, total, out);
    volatile char *wipe = buffer;
    for (size_t i = 0; i < sizeof buffer; i++) wipe[i] = 0;
    return state;
}
