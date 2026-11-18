/* SPDX-License-Identifier: MIT */
/* Mixed audio from the stem store, written out block by block, so verify_stem_pool.py can build this same driver
 * against the stream of the reference commit (whole-track store) and against the current one (segmented store)
 * and compare the two files byte for byte.
 *
 * Usage: test_flat_identity <source> <lag> <output>. Alignment is made deterministic: the stream captures only at
 * two places, so whichever comes first the averaged offset is the same. Then a fixed sequence of blocks (play,
 * jumps, reverse runs, blocks across chunk edges, level changes with their ramps, unity, past the end) is mixed
 * with the whole track in the store. Every block must take the store path. */
#define _POSIX_C_SOURCE 200809L
#include "../overcue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef NDEBUG
#error The identity driver needs active assertions
#endif

static double lag;
static void nap(void) { struct timespec t = {0, 10000000}; nanosleep(&t, NULL); }
/* The fixture's native audio: the prepared full mix at 1.5 times its level, `lag` deck frames late. */
static void native(struct xz_oc_assets *a, int64_t at, size_t frames, float *pcm) {
    double first = (at + lag) * a->sample_rate / 44100;
    int64_t base = (int64_t)floor(first);
    size_t count = (size_t)ceil((double)frames * a->sample_rate / 44100) + 4;
    int16_t *raw = malloc(count * 4);
    assert(raw && !xz_oc_read(&a->reference, base, count, raw));
    for (size_t i = 0; i < frames; i++) for (unsigned c = 0; c < 2; c++) {
        double t = first + (double)i * a->sample_rate / 44100 - (double)base;
        size_t k = (size_t)t; double f = t - (double)k;
        pcm[i * 2 + c] = (float)((raw[k * 2 + c] * (1 - f) + raw[(k + 1) * 2 + c] * f) / 32768 * 1.5);
    }
    free(raw);
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    lag = atof(argv[2]);
    unsetenv("XZ_MODS_STEMS_FLAT");
    struct xz_oc_assets assets; char error[256];
    assert(!xz_oc_open(argv[1], &assets, error, sizeof(error)));
    struct xz_oc_stream *s = xz_oc_stream_open(argv[1], 44100, error, sizeof(error));
    assert(s);
    static float pcm[65536 * 2];
    const float unity[4] = {1, 1, 1, 1};
    struct xz_oc_status status;
    for (unsigned attempt = 0;; attempt++) {
        assert(attempt < 3000);
        int64_t at = attempt & 1 ? 60000 : 20000;
        native(&assets, at, 2048, pcm);
        xz_oc_stream_mix4_trace(s, pcm, 2048, at, unity, NULL);
        nap();
        xz_oc_stream_status(s, &status);
        if (status.state == XZ_OC_READY && status.correlation > 0 && status.flat_chunks && status.flat_built == status.flat_chunks) break;
    }
    uint32_t trace[10];
    xz_oc_stream_trace_state(s, trace);
    printf("aligned: scale %08x%08x offset %08x%08x, store %u chunks\n", trace[5], trace[4], trace[7], trace[6], status.flat_chunks);
    FILE *out = fopen(argv[3], "wb");
    assert(out);
    fwrite(trace + 4, 4, 4, out);
    const float choices[][4] = {{1, 1, 1, 1}, {0, 1, 1, 1}, {1, 0, 0.5f, 1}, {0.3f, 1.7f, 0, 1}, {0, 0, 0, 1},
                                {2, 2, 2, 1}, {1, 1, 0, 1}, {0.25f, 0.5f, 0.75f, 1}, {1, 1, 1, 1}, {0, 2, 1, 1}};
    const size_t sizes[] = {64, 333, 512, 1024, 2048, 4096, 8192, 16383, 16385, 40000, 65536};
    uint32_t seed = 4242, store_blocks = 0, mixed_blocks = 0;
    int64_t at = 1000;
    int back = 0;
    unsigned level = 0;
    const int64_t end = (int64_t)status.flat_chunks * 16384;
    for (unsigned block = 0; block < 600; block++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        size_t frames = sizes[seed % (sizeof(sizes) / sizeof(sizes[0]))];
        if (!(block % 9)) level = (seed >> 8) % (sizeof(choices) / sizeof(choices[0]));
        uint32_t move = (seed >> 12) % 40;
        if (move == 0) at = (int64_t)((seed >> 4) % (uint32_t)end);       /* jump */
        else if (move == 1) back = !back;                                 /* reverse run */
        else if (move == 2) at = end - 3000;                              /* across the end of the track */
        else at += back ? -(int64_t)frames : (int64_t)frames;
        if (at < 0) { at = 0; back = 0; }
        if (at > end + 70000) { at = end - 5000; back = 1; }
        native(&assets, at, frames, pcm);
        uint32_t flags[16];
        size_t changed = xz_oc_stream_mix4_trace(s, pcm, frames, at, choices[level], flags);
        store_blocks += (flags[3] & 16u) != 0;
        mixed_blocks += changed != 0;
        uint64_t header[2] = {(uint64_t)at, changed};
        fwrite(header, sizeof(header), 1, out);
        fwrite(pcm, sizeof(float), frames * 2, out);
    }
    fclose(out);
    xz_oc_stream_status(s, &status);
    printf("600 blocks: %u through the store, %u changed, misses %u, starved runs %u\n", store_blocks, mixed_blocks, status.misses,
           status.starve_runs);
    assert(store_blocks == 600 && mixed_blocks > 300 && !status.starve_runs);
    xz_oc_stream_close(s); xz_oc_close(&assets);
    return 0;
}
