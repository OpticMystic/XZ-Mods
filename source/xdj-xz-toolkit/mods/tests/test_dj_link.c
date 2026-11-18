/* SPDX-License-Identifier: MIT */
/* dj_link CLI and self-test. Build with the mods dir on the include path plus vendor/qrcodegen/qrcodegen.c:
     test_dj_link self
     test_dj_link link <profile.json>        prints the link (or -1), then "dropped <n>" on stderr
     test_dj_link qr <text> <max_version>    prints the side, then one row of 0/1 per line */
#define _POSIX_C_SOURCE 200809L
#include "dj_link.c" /* the static helpers are tested directly */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 20;
    char *data = malloc(cap + 1);
    *n = data ? fread(data, 1, cap + 1, f) : 0;
    fclose(f);
    if (!data || *n > cap) { free(data); return NULL; }
    data[*n] = 0;
    return data;
}

/* Inverse of the encoder for the self-test: base64url, then stored blocks only. Returns JSON length or -1. */
static int unb64(char c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
         : c == '-' ? 62 : c == '_' ? 63 : -1;
}
static long decode_fragment(const char *link, char *out, size_t cap) {
    size_t prefix = strlen(PAGE_PREFIX);
    if (strncmp(link, PAGE_PREFIX, prefix)) return -1;
    const char *s = link + prefix;
    size_t n = strlen(s), raw_n = 0;
    uint8_t *raw = malloc(n);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = unb64(s[i]);
        if (v < 0) { free(raw); return -1; }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; raw[raw_n++] = (uint8_t)(acc >> bits); }
    }
    size_t at = 0, o = 0;
    int final = 0;
    while (!final) {
        if (at + 5 > raw_n || (raw[at] & 0xfe)) { free(raw); return -1; }
        final = raw[at] & 1;
        size_t len = raw[at + 1] | (size_t)raw[at + 2] << 8, nlen = raw[at + 3] | (size_t)raw[at + 4] << 8;
        at += 5;
        if ((len ^ 0xffff) != nlen || at + len > raw_n || o + len >= cap) { free(raw); return -1; }
        memcpy(out + o, raw + at, len);
        at += len;
        o += len;
    }
    free(raw);
    if (at != raw_n) return -1;
    out[o] = 0;
    return (long)o;
}
static char *json_of(const char *s) {
    static char text[4096];
    struct buf b = { 0 };
    json_string(&b, s);
    snprintf(text, sizeof(text), "%s", b.err ? "" : b.p);
    free(b.p);
    return text;
}
static int link_of(const char *doc, char *out, size_t cap, int *dropped) {
    return xz_dj_link_from_profile(doc, strlen(doc), out, cap, dropped);
}

static void test_escaping(void) {
    CHECK(!strcmp(json_of("plain"), "\"plain\""));
    CHECK(!strcmp(json_of("a\"b\\c/d"), "\"a\\\"b\\\\c/d\""));
    CHECK(!strcmp(json_of("\b\f\n\r\t"), "\"\\b\\f\\n\\r\\t\""));
    CHECK(!strcmp(json_of("\x01\x1f\x0b"), "\"\\u0001\\u001f\\u000b\""));
    CHECK(!strcmp(json_of("\x7f" "caf\xc3\xa9 \xf0\x9f\x8e\xa7"), "\"\x7f" "caf\xc3\xa9 \xf0\x9f\x8e\xa7\""));
}
static void test_stored(void) {
    uint8_t out[16];
    CHECK(stored_size(0) == 5 && stored_deflate(NULL, 0, out) == 5);
    CHECK(!memcmp(out, "\x01\x00\x00\xff\xff", 5));
    CHECK(stored_deflate((const uint8_t *)"abc", 3, out) == 8);
    CHECK(!memcmp(out, "\x01\x03\x00\xfc\xff" "abc", 8));
    size_t n = 70000;
    uint8_t *in = malloc(n), *big = malloc(stored_size(n));
    for (size_t i = 0; i < n; i++) in[i] = (uint8_t)(i * 7);
    CHECK(stored_size(n) == n + 10);
    CHECK(stored_deflate(in, n, big) == n + 10);
    CHECK(!memcmp(big, "\x00\xff\xff\x00\x00", 5) && !memcmp(big + 5, in, 65535));
    CHECK(!memcmp(big + 5 + 65535, "\x01\x71\x11\x8e\xee", 5) && !memcmp(big + 10 + 65535, in + 65535, n - 65535));
    CHECK(stored_size(65535) == 65540 && stored_size(65536) == 65546);
    free(in);
    free(big);
}
static void test_base64url(void) {
    char out[16];
    CHECK(base64url((const uint8_t *)"", 0, out) == 0 && !strcmp(out, ""));
    CHECK(base64url((const uint8_t *)"f", 1, out) == 2 && !strcmp(out, "Zg"));
    CHECK(base64url((const uint8_t *)"fo", 2, out) == 3 && !strcmp(out, "Zm8"));
    CHECK(base64url((const uint8_t *)"foo", 3, out) == 4 && !strcmp(out, "Zm9v"));
    CHECK(base64url((const uint8_t *)"\xfb\xff", 2, out) == 3 && !strcmp(out, "-_8"));
    CHECK(base64url((const uint8_t *)"\xfb\xef\xbe", 3, out) == 4 && !strcmp(out, "----"));
}
static void test_links(void) {
    char link[XZ_DJ_LINK_MAX + 1], json[8192];
    int dropped = -1;
    const char *basic = "{\"dj_id\":\"djp_a\",\"dj_name\":\"A \\\"B\\\"\",\"accent\":\"#00ff00\",\"tagline\":\"\"}";
    int n = link_of(basic, link, sizeof(link), &dropped);
    CHECK(n > 0 && (size_t)n == strlen(link) && dropped == 0);
    CHECK(decode_fragment(link, json, sizeof(json)) > 0);
    CHECK(!strcmp(json, "{\"i\":\"djp_a\",\"n\":\"A \\\"B\\\"\",\"t\":\"\",\"a\":\"#00ff00\"}"));

    /* page_url precedence; http and malformed page_urls are ignored. */
    n = link_of("{\"dj_id\":\"x\",\"dj_name\":\"X\",\"page_url\":\"https://example.com/x\"}", link, sizeof(link), NULL);
    CHECK(n == 21 && !strcmp(link, "https://example.com/x"));
    n = link_of("{\"dj_id\":\"x\",\"dj_name\":\"X\",\"page_url\":\"http://example.com/x\"}", link, sizeof(link), NULL);
    CHECK(n > 0 && !strncmp(link, PAGE_PREFIX, strlen(PAGE_PREFIX)));
    CHECK(decode_fragment(link, json, sizeof(json)) > 0 && !strcmp(json, "{\"i\":\"x\",\"n\":\"X\"}"));
    n = link_of("{\"dj_id\":\"x\",\"dj_name\":\"X\",\"page_url\":\"https:///nohost\"}", link, sizeof(link), NULL);
    CHECK(n > 0 && !strncmp(link, PAGE_PREFIX, strlen(PAGE_PREFIX)));

    /* Links: invalid ones skipped, empty list omitted, null tagline/accent omitted. */
    n = link_of("{\"dj_id\":\"x\",\"dj_name\":\"X\",\"tagline\":null,\"accent\":\"red\",\"links\":["
                "{\"kind\":\"web\",\"url\":\"http://a.example\"},{\"kind\":\" \",\"url\":\"https://a.example\"}]}",
                link, sizeof(link), &dropped);
    CHECK(n > 0 && dropped == 0 && decode_fragment(link, json, sizeof(json)) > 0 && !strcmp(json, "{\"i\":\"x\",\"n\":\"X\"}"));

    /* Twelve long links: dropped from the end until the link fits version 12. */
    char doc[16384];
    size_t at = (size_t)snprintf(doc, sizeof(doc), "{\"dj_id\":\"djp_many\",\"dj_name\":\"Many\",\"tagline\":\"tag\",\"links\":[");
    for (int i = 0; i < 12; i++)
        at += (size_t)snprintf(doc + at, sizeof(doc) - at, "%s{\"kind\":\"k%d\",\"url\":\"https://example.com/link/%02d\"}", i ? "," : "", i, i);
    snprintf(doc + at, sizeof(doc) - at, "]}");
    n = link_of(doc, link, sizeof(link), &dropped);
    CHECK(n > 0 && dropped > 0 && dropped < 12);
    CHECK(fits_qr(link, (size_t)n));
    uint8_t modules[XZ_DJ_QR_MAX_SIDE * XZ_DJ_QR_MAX_SIDE];
    CHECK(xz_dj_qr(link, XZ_DJ_QR_VERSION, modules, sizeof(modules)) > 0);
    CHECK(decode_fragment(link, json, sizeof(json)) > 0 && strstr(json, "\"t\":\"tag\"") && strstr(json, "\"k0\""));
    char last[32];
    snprintf(last, sizeof(last), "\"k%d\"", 12 - dropped - 1);
    CHECK(strstr(json, last) != NULL);
    snprintf(last, sizeof(last), "\"k%d\"", 12 - dropped);
    CHECK(strstr(json, last) == NULL);
    /* One more character in the kept part would not fit: the previous link count did not fit. */

    /* A tagline too long for the budget goes after the links. 80 four-byte characters = 320 bytes. */
    at = (size_t)snprintf(doc, sizeof(doc), "{\"dj_id\":\"djp_t\",\"dj_name\":\"T\",\"tagline\":\"");
    for (int i = 0; i < 80; i++) at += (size_t)snprintf(doc + at, sizeof(doc) - at, "\xf0\x9f\x8e\xa7");
    snprintf(doc + at, sizeof(doc) - at, "\",\"links\":[{\"kind\":\"web\",\"url\":\"https://t.example\"}]}");
    n = link_of(doc, link, sizeof(link), &dropped);
    CHECK(n > 0 && dropped == 2);
    CHECK(decode_fragment(link, json, sizeof(json)) > 0 && !strcmp(json, "{\"i\":\"djp_t\",\"n\":\"T\"}"));

    /* A name too long for the budget: still returned, the QR refuses it. */
    at = (size_t)snprintf(doc, sizeof(doc), "{\"dj_id\":\"djp_n\",\"dj_name\":\"");
    for (int i = 0; i < 160; i++) at += (size_t)snprintf(doc + at, sizeof(doc) - at, "\xf0\x9f\x8e\xa7");
    snprintf(doc + at, sizeof(doc) - at, "\"}");
    n = link_of(doc, link, sizeof(link), &dropped);
    CHECK(n > 0 && dropped == 0 && xz_dj_qr(link, XZ_DJ_QR_VERSION, modules, sizeof(modules)) == 0);

    /* Unusable documents. */
    CHECK(link_of("{\"dj_name\":\"X\"}", link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x y\",\"dj_name\":\"X\"}", link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x\",\"dj_name\":\"  \"}", link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x\",\"dj_name\":\"a\\nb\"}", link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x\",\"dj_name\":\"\\ud800\"}", link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x\",\"dj_name\":7}", link, sizeof(link), NULL) == -1);
    CHECK(link_of("[1]", link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x\",", link, sizeof(link), NULL) == -1);
    CHECK(xz_dj_link_from_profile(NULL, 0, link, sizeof(link), NULL) == -1);
    CHECK(link_of("{\"dj_id\":\"x\",\"dj_name\":\"X\"}", link, 10, NULL) == -1);
    /* Repeated key: the last wins, as with JSON.parse. */
    n = link_of("{\"dj_id\":\"a\",\"dj_id\":\"b\",\"dj_name\":\"X\"}", link, sizeof(link), NULL);
    CHECK(n > 0 && decode_fragment(link, json, sizeof(json)) > 0 && !strcmp(json, "{\"i\":\"b\",\"n\":\"X\"}"));
}
static void test_qr(void) {
    uint8_t modules[XZ_DJ_QR_MAX_SIDE * XZ_DJ_QR_MAX_SIDE];
    char text[3000];
    memset(text, 'a', sizeof(text));
    text[300] = 0;
    CHECK(xz_dj_qr(text, XZ_DJ_QR_VERSION, modules, sizeof(modules)) == 0);
    int side = xz_dj_qr(text, 40, modules, sizeof(modules));
    CHECK(side > 4 * XZ_DJ_QR_VERSION + 17 && side <= XZ_DJ_QR_MAX_SIDE && (side - 17) % 4 == 0);
    memset(text, 'a', sizeof(text));
    text[2999] = 0;
    CHECK(xz_dj_qr(text, 40, modules, sizeof(modules)) == 0);
    CHECK(xz_dj_qr("hi", 1, modules, sizeof(modules)) == 21);
    CHECK(modules[0] == 1 && modules[7] == 0 && modules[20] == 1); /* finder patterns and separator */
    CHECK(xz_dj_qr("hi", 1, modules, 21 * 21 - 1) == 0);
    CHECK(xz_dj_qr("hi", 0, modules, sizeof(modules)) == 0);
    CHECK(xz_dj_qr("", 1, modules, sizeof(modules)) == 21);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "link")) {
        size_t n = 0;
        char *doc = slurp(argv[2], &n), link[XZ_DJ_LINK_MAX + 1];
        if (!doc) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        int dropped = 0, len = xz_dj_link_from_profile(doc, n, link, sizeof(link), &dropped);
        free(doc);
        if (len < 0) { puts("-1"); return 0; }
        puts(link);
        fprintf(stderr, "dropped %d\n", dropped);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "qr")) {
        static uint8_t modules[XZ_DJ_QR_MAX_SIDE * XZ_DJ_QR_MAX_SIDE];
        int side = xz_dj_qr(argv[2], atoi(argv[3]), modules, sizeof(modules));
        printf("%d\n", side);
        for (int y = 0; y < side; y++) {
            for (int x = 0; x < side; x++) putchar('0' + modules[y * side + x]);
            putchar('\n');
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "self")) {
        test_escaping();
        test_stored();
        test_base64url();
        test_links();
        test_qr();
        printf("test_dj_link self: %s (%d failures)\n", failures ? "FAIL" : "ok", failures);
        return failures ? 1 : 0;
    }
    fprintf(stderr, "usage: %s self | link <profile.json> | qr <text> <max_version>\n", argv[0]);
    return 2;
}
