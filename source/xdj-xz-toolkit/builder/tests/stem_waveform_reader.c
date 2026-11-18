/* SPDX-License-Identifier: MIT */
/* Reads a published stem bundle through the XZ runtime's own readers and prints what they saw as JSON.
 * Built by test_stem_waveforms_device.py against mods/ui/layered_wave_source.c and mods/audio/overcue_file.c. */
#include "layered_wave_source.h"
#include "overcue.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); data = NULL; }
    fclose(f);
    *n = data ? (size_t)size : 0;
    return data;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <USB>/Contents/<track> <bundle folder>\n", argv[0]); return 2; }
    static const char *stems[3] = {"drums", "harmonics", "vocal"};
    printf("{\"sections\":[");
    for (unsigned s = 0; s < 3; s++) {
        char path[4096]; size_t n = 0, at = 0; uint32_t count = 0;
        snprintf(path, sizeof(path), "%s/stems-%s-waveform.2EX", argv[2], stems[s]);
        uint8_t *file = slurp(path, &n);
        int rc = file ? xz_anlz_section(file, n, "PWV7", 3, &at, &count) : -2;
        unsigned long long sum[3] = {0, 0, 0};
        unsigned peak[3] = {0, 0, 0};
        for (uint32_t i = 0; !rc && i < count; i++)
            for (unsigned b = 0; b < 3; b++) {
                uint8_t v = file[at + (size_t)i * 3 + b];
                sum[b] += v;
                if (v > peak[b]) peak[b] = v;
            }
        printf("%s{\"stem\":\"%s\",\"rc\":%d,\"count\":%u,\"sum\":[%llu,%llu,%llu],\"peak\":[%u,%u,%u]}", s ? "," : "",
               stems[s], rc, count, sum[0], sum[1], sum[2], peak[0], peak[1], peak[2]);
        free(file);
    }
    struct xz_wave_load w;
    int rc = xz_wave_load_stems(argv[1], &w);
    printf("],\"stems\":{\"rc\":%d,\"count\":%u,\"kind\":%u,\"normalization\":%u,\"columns\":[", rc, rc ? 0 : w.count,
           rc ? 0 : w.kind, rc ? 0 : w.normalization);
    for (uint32_t i = 0; !rc && i < w.count; i++)
        printf("%s[%u,%u,%u]", i ? "," : "", w.bands[i * 3], w.bands[i * 3 + 1], w.bands[i * 3 + 2]);
    printf("]}");
    if (!rc) xz_wave_load_free(&w);
    struct xz_oc_assets assets;
    char error[256] = "";
    rc = xz_oc_open(argv[1], &assets, error, sizeof(error));
    printf(",\"open\":{\"rc\":%d,\"error\":\"%s\",\"frames\":%llu,\"overview\":[", rc, error,
           rc ? 0ULL : (unsigned long long)assets.frames);
    for (unsigned r = 0; !rc && r < 3; r++) {
        unsigned peak = 0;
        for (uint32_t i = 0; i < assets.wave_count[r]; i++) if (assets.wave[r][i] > peak) peak = assets.wave[r][i];
        printf("%s{\"count\":%u,\"peak\":%u}", r ? "," : "", assets.wave_count[r], peak);
    }
    printf("]}}\n");
    if (!rc) xz_oc_close(&assets);
    return 0;
}
