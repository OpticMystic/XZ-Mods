/* SPDX-License-Identifier: MIT */
/* Loader identity against spec/generated/loader-identity.schema.json: strict parse, bounded reads, and the
   contract's report reference vectors. argv[1] is an empty scratch directory; argv[2], if given, the contract's
   spec/fixtures/loader-identity.json, which must parse. */
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
#define REST "\"installation_id\":\"I-4F7K-2M9Q\",\"created_at\":\"2026-10-27T00:00:00.000Z\",\"app_version\":\"0.5.0-preview.22.6\""
static const char valid[] = "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "}\n";

static int parse(const char *text) {
    struct xz_loader_identity id;
    memset(&id, 0x5a, sizeof id);
    int state = xz_loader_identity_parse(text, strlen(text), &id);
    assert(id.state == state);
    if (state != XZ_LOADER_OK) {
        static const unsigned char zero[XZ_LOADER_KEY_BYTES];
        assert(!id.id[0] && !id.installation[0] && !memcmp(id.key, zero, sizeof zero));
    }
    return state;
}

static void write_file(const char *path, const char *data, size_t n) {
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(data, 1, n, f) == n && !fclose(f));
}

int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    char ref[12];
    /* spec/support-contract.json identifiers.report_reference.vectors */
    static const struct { const char *input, *canonical; } vectors[] = {
        {"R-4F7K-2M9Q", "R-4F7K-2M9Q"}, {" r 4f7k - 2m9q ", "R-4F7K-2M9Q"}, {"R-OIl0-0001", "R-0110-0001"},
        {"r00000000", "R-0000-0000"}, {"R-ZZZZ-ZZZZ", "R-ZZZZ-ZZZZ"}, {"", NULL}, {"4F7K2M9Q", NULL}, {"R-4F7K-2M9U", NULL},
        {"R-1234-567", NULL}, {"R-1234-56789", NULL}, {"R-1234/5678", NULL}, {"R-1234_5678", NULL}, {"R-1234\t5678", NULL},
    };
    for (size_t i = 0; i < sizeof vectors / sizeof *vectors; i++) {
        int ok = !xz_crockford_ref(vectors[i].input, strlen(vectors[i].input), 'R', ref);
        if (ok != (vectors[i].canonical != NULL) || (ok && strcmp(ref, vectors[i].canonical))) {
            fprintf(stderr, "vector %zu: %s\n", i, vectors[i].input);
            assert(0);
        }
    }
    assert(xz_crockford_ref("L-4F7K-2M9Q", 11, 'R', ref));

    /* Valid identity: both IDs and the key. */
    struct xz_loader_identity id;
    assert(xz_loader_identity_parse(valid, strlen(valid), &id) == XZ_LOADER_OK);
    assert(!strcmp(id.id, "L-4F7K-2M9Q") && !strcmp(id.installation, "I-4F7K-2M9Q") && id.key[0] == 0x00 && id.key[1] == 0x11 && id.key[31] == 0xff);
    assert(parse("{" REST ",\"loader_key\":\"" KEY "\",\"loader_id\":\"L-4F7K-2M9Q\",\"version\":1}") == XZ_LOADER_OK);
    assert(parse("{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\",\"installation_id\":\"I-4F7K-2M9Q\","
                 "\"created_at\":\"2026-10-27T00:00:00.000Z\",\"app_version\":\"unknown\"}") == XZ_LOADER_OK);

    /* Everything the schema refuses, refused, and refusal leaves nothing behind. */
#define ID(l, k, i, c, a) "{\"version\":1,\"loader_id\":\"" l "\",\"loader_key\":\"" k "\",\"installation_id\":\"" i "\",\"created_at\":\"" c "\",\"app_version\":\"" a "\"}"
    static const char *const bad[] = {
        "", "{}", "[]", "null",
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"}",
        "{\"version\":2,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "}",
        "{\"version\":\"1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "}",
        "{\"schema\":\"xz-loader-identity/1\",\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "}",
        ID("l-4f7k-2m9q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),   /* not canonical */
        ID("L4F7K2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),
        ID("L-4F7K-2MIQ", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),
        ID("R-4F7K-2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),
        ID("L-4F7K-2M9Q", KEY, "L-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),
        ID("L-4F7K-2M9Q", KEY "0", "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),
        ID("L-4F7K-2M9Q", "00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff", "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0"),
        ID("L-4F7K-2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00Z", "1.0.0"),
        ID("L-4F7K-2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27 00:00:00.000Z", "1.0.0"),
        ID("L-4F7K-2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0"),
        ID("L-4F7K-2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0-Beta"),
        ID("L-4F7K-2M9Q", KEY, "I-4F7K-2M9Q", "2026-10-27T00:00:00.000Z", "1.0.0-"),
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST ",\"extra\":\"x\"}",
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_id\":\"L-4F7K-2M9Q\",\"installation_id\":\"I-4F7K-2M9Q\",\"created_at\":\"2026-10-27T00:00:00.000Z\",\"app_version\":\"1.0.0\"}",
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "} x",
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "}{}",
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9\\u0051\",\"loader_key\":\"" KEY "\"," REST "}",
        "{\"version\":1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST,
        "{version:1,\"loader_id\":\"L-4F7K-2M9Q\",\"loader_key\":\"" KEY "\"," REST "}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (parse(bad[i]) != XZ_LOADER_INVALID) { fprintf(stderr, "accepted %zu: %s\n", i, bad[i]); assert(0); }
    }
    char nul[sizeof valid];
    memcpy(nul, valid, sizeof valid); nul[40] = 0;
    assert(xz_loader_identity_parse(nul, sizeof valid - 1, &id) == XZ_LOADER_INVALID);

    /* 2048 bytes is the limit: trailing spaces up to it are fine, one more is refused before parsing. */
    static char big[XZ_LOADER_IDENTITY_MAX_BYTES + 64];
    memset(big, ' ', sizeof big); memcpy(big, valid, strlen(valid));
    assert(xz_loader_identity_parse(big, sizeof big, &id) == XZ_LOADER_OVERSIZED);
    assert(xz_loader_identity_parse(big, XZ_LOADER_IDENTITY_MAX_BYTES, &id) == XZ_LOADER_OK);
    assert(xz_loader_identity_parse(big, XZ_LOADER_IDENTITY_MAX_BYTES + 1, &id) == XZ_LOADER_OVERSIZED);

    /* The contract's own fixture. */
    if (argc == 3) {
        char fixture[XZ_LOADER_IDENTITY_MAX_BYTES + 1];
        FILE *f = fopen(argv[2], "rb");
        assert(f);
        size_t n = fread(fixture, 1, sizeof fixture, f);
        fclose(f);
        assert(n <= XZ_LOADER_IDENTITY_MAX_BYTES && xz_loader_identity_parse(fixture, n, &id) == XZ_LOADER_OK);
        assert(!strcmp(id.id, "L-4F7K-2M9Q") && !strcmp(id.installation, "I-4F7K-2M9Q") && id.key[0] == 0xbb);
    }

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
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_OK && !strcmp(id.id, "L-4F7K-2M9Q") && !strcmp(id.installation, "I-4F7K-2M9Q") && id.key[31] == 0xff);
    xz_loader_identity_clear(&id);
    assert(!id.id[0] && !id.installation[0] && !id.key[31] && id.state == XZ_LOADER_NONE);
    write_file(path, big, sizeof big);
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_OVERSIZED && !id.id[0]);
    write_file(path, "{\"version\":", 11);
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_INVALID && !id.id[0]);
    assert(!remove(path) && !make_dir(path));
    assert(xz_loader_identity_read(root, &id) == XZ_LOADER_UNREADABLE && !id.id[0]);

    puts("PASS loader identity: schema-strict fields and IDs, contract reference vectors, fixture, missing/malformed/oversized/unreadable refused, key wiped");
    return 0;
}
