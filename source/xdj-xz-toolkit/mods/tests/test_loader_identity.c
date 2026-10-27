/* SPDX-License-Identifier: MIT */
/* Loader identity: strict parse, tolerant references, bounded reads. argv[1] is an empty scratch directory. */
#include "../support/loader_identity.h"
#include "../support/crockford.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0700)
#endif
#ifdef NDEBUG
#error Assertions required
#endif

#define KEY "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
static const char valid[] = "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"}\n";

static int parse(const char *text) {
    struct xz_loader_identity id;
    memset(&id, 0x5a, sizeof id);
    int state = xz_loader_identity_parse(text, strlen(text), &id);
    assert(id.state == state);
    if (state != XZ_LOADER_OK) {
        static const unsigned char zero[XZ_LOADER_KEY_BYTES];
        assert(!id.id[0] && !memcmp(id.key, zero, sizeof zero));
    }
    return state;
}

static void write_file(const char *path, const char *data, size_t n) {
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(data, 1, n, f) == n && !fclose(f));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char ref[12];
    /* References: canonical, any case, I/L as 1, O as 0, hyphens and spaces anywhere. */
    assert(!xz_crockford_ref("R-4F7K-2M9Q", 11, 'R', ref) && !strcmp(ref, "R-4F7K-2M9Q"));
    assert(!xz_crockford_ref("r4f7k2m9q", 9, 'R', ref) && !strcmp(ref, "R-4F7K-2M9Q"));
    assert(!xz_crockford_ref(" r-4f7k 2m9q", 12, 'R', ref) && !strcmp(ref, "R-4F7K-2M9Q"));
    assert(!xz_crockford_ref("R-IOLO-oi1l", 11, 'R', ref) && !strcmp(ref, "R-1010-0111"));
    assert(xz_crockford_ref("R-4F7K-2M9U", 11, 'R', ref));   /* U is not Crockford */
    assert(xz_crockford_ref("R-4F7K-2M9", 10, 'R', ref));    /* short */
    assert(xz_crockford_ref("R-4F7K-2M9QQ", 12, 'R', ref));  /* long */
    assert(xz_crockford_ref("L-4F7K-2M9Q", 11, 'R', ref));   /* other prefix */
    assert(xz_crockford_ref("R-4F7K-2M9!", 11, 'R', ref));
    assert(xz_crockford_ref("", 0, 'R', ref));

    /* Valid identity, with the ID read tolerantly and the key decoded. */
    struct xz_loader_identity id;
    assert(xz_loader_identity_parse(valid, strlen(valid), &id) == XZ_LOADER_OK);
    assert(!strcmp(id.id, "L-4F7K-2M9Q") && id.key[0] == 0x00 && id.key[1] == 0x11 && id.key[31] == 0xff);
    assert(parse("{\"loader_key\":\"" KEY "\",\"loader_id\":\"l4f7k2m9q\",\"schema\":\"xz-loader-identity/1\"}") == XZ_LOADER_OK);

    /* Malformed: every way the file can differ from the format is refused, and refusal leaves nothing behind. */
    static const char *const bad[] = {
        "",
        "{}",
        "[]",
        "null",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\"}",
        "{\"schema\":\"xz-loader-identity/2\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"R-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9\",\"loader_key\":\"" KEY "\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "0\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"g0112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\",\"extra\":\"x\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_id\":\"L-4F7K-2M9Q\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"} x",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"}{}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":[\"" KEY "\"]}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":1,\"loader_key\":\"" KEY "\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9\\u0051\",\"loader_key\":\"" KEY "\"}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"",
        "{schema:\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (parse(bad[i]) != XZ_LOADER_INVALID) { fprintf(stderr, "accepted: %s\n", bad[i]); assert(0); }
    }
    char nul[sizeof valid];
    memcpy(nul, valid, sizeof valid);nul[40] = 0;
    assert(xz_loader_identity_parse(nul, sizeof valid - 1, &id) == XZ_LOADER_INVALID);

    /* Oversized text is refused before parsing. */
    static char big[XZ_LOADER_IDENTITY_MAX_BYTES + 64];
    memset(big, ' ', sizeof big);memcpy(big, valid, strlen(valid));
    assert(xz_loader_identity_parse(big, sizeof big, &id) == XZ_LOADER_OVERSIZED);
    assert(xz_loader_identity_parse(big, XZ_LOADER_IDENTITY_MAX_BYTES, &id) == XZ_LOADER_OK);

    /* Files: missing (older loader), valid, oversized, malformed, and a directory where the file should be. */
    char root[600], dir[700], path[800];
    snprintf(root, sizeof root, "%s/usb", argv[1]);
    assert(!make_dir(root));
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_NONE && !id.id[0]);
    assert(xz_loader_identity_read("", &id) == XZ_LOADER_NONE);
    assert(xz_loader_identity_read(NULL, &id) == XZ_LOADER_NONE);
    snprintf(dir, sizeof dir, "%s/VJ.Tools", root);
    assert(!make_dir(dir));
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_NONE);
    snprintf(path, sizeof path, "%s/loader-identity.json", dir);
    write_file(path, valid, strlen(valid));
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_OK && !strcmp(id.id, "L-4F7K-2M9Q") && id.key[31] == 0xff);
    xz_loader_identity_clear(&id);
    assert(!id.id[0] && !id.key[31] && id.state == XZ_LOADER_NONE);
    write_file(path, big, sizeof big);
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_OVERSIZED && !id.id[0]);
    write_file(path, "{\"schema\":", 10);
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_INVALID && !id.id[0]);
    assert(!remove(path) && !make_dir(path));
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_UNREADABLE && !id.id[0]);

    puts("PASS loader identity: strict format, tolerant L-/R- references, missing/malformed/oversized/unreadable refused, key wiped");
    return 0;
}
