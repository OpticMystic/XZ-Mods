/* SPDX-License-Identifier: MIT */
/* Stem store: the resampler and build order on their own ("kernel"), and through a real stream
 * ("stream <source> [lag]"): once the store is built, a selection or a position never used before is mixed
 * on the very next block, with no miss and no starvation. "yield <source>": a store whose grant falls to nothing
 * hands the playhead to the windows without a starved block, and a closed stream leaves the pool empty.
 * "gone-whole|gone-bubble|gone-none <source> <stick/>": the stick under <stick/> goes (USB STOP); a whole store plays
 * on with its files closed, a bubble or windows end the stems through the ramp, and no file stays open on the stick. */
#define _POSIX_C_SOURCE 200809L
#include "../overcue.h"
#include "../overcue_flat.h"
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef NDEBUG
#error Overcue acceptance requires active assertions
#endif
#define PI 3.14159265358979323846
struct tone { double hz, amplitude; uint32_t rate; uint64_t frames; };
static int tone_read(void *context, unsigned single, int64_t first, size_t frames, int16_t *stereo) {
    const struct tone *t = context;
    for (size_t i = 0; i < frames; i++) {
        int64_t n = first + (int64_t)i;
        int16_t value = n < 0 || (uint64_t)n >= t->frames ? 0 :
            (int16_t)lrint(t->amplitude * sin(2 * PI * t->hz * (double)n / t->rate + single));
        stereo[i * 2] = value; stereo[i * 2 + 1] = (int16_t)-value;
    }
    return 0;
}
/* Sample of a built store at deck frame i, through the chunk base a reader would use. */
static int16_t stored(const struct xz_oc_flat *f, unsigned single, uint64_t i, unsigned channel) {
    const int16_t *base = xz_oc_flat_chunk(f, i / XZ_OC_FLAT_CHUNK);
    assert(base);
    return base[single * XZ_OC_FLAT_SINGLE + (i % XZ_OC_FLAT_CHUNK) * 2 + channel];
}
/* The store alone in the pool asks for the whole track; these tracks are one segment. */
static struct xz_oc_flat *whole(struct xz_oc_flat *f) {
    assert(f && f->segments == 1);
    xz_oc_flat_want(f, XZ_STEM_PRELOAD, 1);
    return f;
}
/* RMS error of single 0 against the ideal resampled tone, as a share of the amplitude. */
static double tone_error(uint32_t deck_rate, uint32_t stem_rate, double offset, double hz, int expect_direct) {
    struct tone t = {hz, 20000, stem_rate, (uint64_t)stem_rate * 3};
    struct xz_oc_flat *f = whole(xz_oc_flat_create(deck_rate, stem_rate, t.frames, offset, 3, tone_read, &t));
    assert(f->direct == expect_direct);
    while (xz_oc_flat_pass(f, 0, 0, NULL, 0) == 1) {}
    assert(xz_oc_flat_complete(f) && xz_oc_flat_covers(f, 0, (size_t)f->frames));
    double sum = 0; uint64_t count = 0;
    for (uint64_t i = 2000; i + 2000 < f->frames; i++) {
        double at = ((double)i + offset) * stem_rate / deck_rate, ideal = t.amplitude * sin(2 * PI * hz * at / stem_rate);
        double left = stored(f, 0, i, 0) - ideal, right = stored(f, 0, i, 1) + ideal;
        sum += left * left + right * right; count += 2;
    }
    xz_oc_flat_destroy(f);
    return sqrt(sum / (double)count) / t.amplitude;
}
static void kernel(void) {
    const double audible[] = {1000, 10000, 15000};
    for (unsigned i = 0; i < 3; i++) {
        double down = tone_error(44100, 96000, 29.4, audible[i], 0), same = tone_error(44100, 44100, 29.4, audible[i], 0);
        printf("tone %5.0f Hz: 96k->44.1k error %.5f, 44.1k shifted 0.4 frame error %.5f\n", audible[i], down, same);
        assert(down < 0.003 && same < 0.003);
    }
    /* A 30 kHz tone cannot exist at 44.1 kHz: it must be removed, not folded back into the audible band. */
    struct tone high = {30000, 20000, 96000, 96000u * 3};
    struct xz_oc_flat *f = whole(xz_oc_flat_create(44100, 96000, high.frames, 0, 3, tone_read, &high));
    while (xz_oc_flat_pass(f, 0, 0, NULL, 0) == 1) {}
    double energy = 0;
    for (uint64_t i = 2000; i + 2000 < f->frames; i++) energy += (double)stored(f, 0, i, 0) * stored(f, 0, i, 0);
    double leak = sqrt(energy / (double)(f->frames - 4000)) / high.amplitude;
    printf("30 kHz tone left after 96k->44.1k: %.6f of its level\n", leak);
    assert(leak < 0.001);
    xz_oc_flat_destroy(f);
    /* Same rate and a whole-frame shift: the stored samples are the decoded ones. */
    assert(tone_error(44100, 44100, 30.0, 16000, 1) < 0.00005);
    /* The chunk at the playhead is built first, then the ones ahead, then the ones behind. */
    struct tone low = {440, 9000, 44100, 44100u * 8};
    f = whole(xz_oc_flat_create(44100, 44100, low.frames, 0, 3, tone_read, &low));
    assert(f->chunks > 8);
    int64_t playhead = (int64_t)5 * XZ_OC_FLAT_CHUNK + 10;
    assert(!xz_oc_flat_covers(f, playhead, 512) && xz_oc_flat_pass(f, playhead, 0, NULL, 0) == 1);
    assert(xz_oc_flat_covers(f, playhead, 512) && f->ready[5] && !f->ready[4] && !f->ready[6] && f->built == 1);
    assert(!xz_oc_flat_covers(f, playhead, XZ_OC_FLAT_CHUNK));
    assert(xz_oc_flat_pass(f, playhead, 0, NULL, 0) == 1 && f->ready[6] && !f->ready[4]);
    unsigned steps = 2;
    while (xz_oc_flat_pass(f, playhead, 0, NULL, 0) == 1) steps++;
    assert(steps == f->chunks && xz_oc_flat_complete(f) && !xz_oc_flat_pass(f, playhead, 0, NULL, 0));
    assert(xz_oc_flat_covers(f, (int64_t)f->frames + 50, 64) && !xz_oc_flat_covers(f, -1, 64));
    xz_oc_flat_destroy(f);
    /* No length limit any more: a track longer than the pool holds gets a bubble. Twenty minutes need 101 segments. */
    assert(xz_oc_flat_need(44100, 96000, (uint64_t)96000 * 60 * 20, 0) == 101 && xz_oc_flat_need(44100, 96000, (uint64_t)96000 * 60 * 4, 0) == 21);
    puts("PASS stem store: resampling accuracy, alias rejection, exact copy, playhead-first order, any length");
}
static double lag = 29.4;
/* Stands in for the RAM ledger's count of refused track loads (ram/budget.h): the pool watches it (tracks first). */
static uint32_t track_refusals;
uint32_t xz_ram_track_refusals(void) { return __atomic_load_n(&track_refusals, __ATOMIC_ACQUIRE); }
static void pool_empty(void) {
    struct xz_stem_pool_status pool;
    xz_stem_pool_status(&pool);
    assert(!pool.mapped && !pool.lost && !pool.spare);
}
static void nap(void) { struct timespec t = {0, 20000000}; nanosleep(&t, NULL); }
/* The fixture's native audio: the prepared full mix at 1.5 times its level, `lag` deck frames late. */
static void native(struct xz_oc_assets *a, int64_t at, float pcm[4096]) {
    double first = (at + lag) * a->sample_rate / 44100; int64_t base = (int64_t)floor(first); int16_t raw[10000];
    assert(!xz_oc_read(&a->reference, base, 5000, raw));
    for (unsigned i = 0; i < 2048; i++) for (unsigned c = 0; c < 2; c++) {
        double t = first + i * (double)a->sample_rate / 44100 - base; size_t k = (size_t)t; double f = t - k;
        pcm[i * 2 + c] = (float)((raw[k * 2 + c] * (1 - f) + raw[(k + 1) * 2 + c] * f) / 32768 * 1.5);
    }
}
/* RMS distance between a mixed block and the given singles at 1.5 times their level, ramp excluded. */
static double distance(struct xz_oc_assets *a, int64_t at, const float pcm[4096], unsigned mask) {
    double first = (at + lag) * a->sample_rate / 44100; int64_t base = (int64_t)floor(first); int16_t raw[10000], sum[10000] = {0};
    for (unsigned r = 0; r < 3; r++) if (mask & (1u << r)) {
        assert(!xz_oc_read(&a->role[r], base, 5000, raw));
        for (unsigned i = 0; i < 10000; i++) sum[i] = (int16_t)(sum[i] + raw[i]);
    }
    double total = 0;
    for (unsigned i = 160; i < 2048; i++) for (unsigned c = 0; c < 2; c++) {
        double t = first + i * (double)a->sample_rate / 44100 - base; size_t k = (size_t)t; double f = t - k;
        double delta = pcm[i * 2 + c] - (sum[k * 2 + c] * (1 - f) + sum[(k + 1) * 2 + c] * f) / 32768 * 1.5;
        total += delta * delta;
    }
    return sqrt(total / (1888 * 2));
}
static void stream(const char *source) {
    unsetenv("XZ_MODS_STEMS_FLAT");
    struct xz_oc_assets assets; char error[256];
    assert(!xz_oc_open(source, &assets, error, sizeof(error)));
    struct xz_oc_stream *s = xz_oc_stream_open(source, 44100, error, sizeof(error)); assert(s);
    float pcm[4096], before[4096]; const float unity[3] = {1, 1, 1}; struct xz_oc_status status;
    for (unsigned attempt = 0; attempt < 400; attempt++) {
        int64_t position = 10000 + (attempt % 8) * 3000; native(&assets, position, pcm);
        for (unsigned chunk = 0; chunk < 2048; chunk += 64) xz_oc_stream_mix(s, pcm + chunk * 2, 64, position + chunk, unity);
        nap(); xz_oc_stream_status(s, &status);
        if (status.state == XZ_OC_READY && status.flat_chunks && status.flat_built == status.flat_chunks) break;
    }
    assert(status.flat_chunks && status.flat_built == status.flat_chunks && status.state == XZ_OC_READY);
    /* The store's segments count as RAM held as soon as they are taken. */
    size_t held = 0;
    for (unsigned attempt = 0; attempt < 200 && !held; attempt++) { held = xz_oc_stream_ram_bytes(s); if (!held) nap(); }
    assert(held >= (size_t)status.flat_chunks * 3 * 4 * (XZ_OC_FLAT_CHUNK - 1) / 2);
    uint32_t misses = status.misses;
    /* Jumps to places never played, with selections never used: every block is mixed at once and matches. */
    const int64_t places[] = {15000, 250000, 40000, 330000, 120000, 15000, 200123};
    const float choice[][3] = {{1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 0}, {0, 1, 1}, {1, 0, 0}, {1, 1, 0}};
    const unsigned masks[] = {3, 4, 5, 2, 6, 1, 3};
    for (unsigned i = 0; i < 7; i++) {
        native(&assets, places[i], pcm);
        assert(xz_oc_stream_mix(s, pcm, 2048, places[i], choice[i]) == 2048);
        double apart = distance(&assets, places[i], pcm, masks[i]);
        printf("jump to %7lld, stems %u: mixed at once, rms distance %.5f\n", (long long)places[i], masks[i], apart);
        assert(apart < 0.006);
    }
    /* Back to all stems: the stock samples pass through untouched once the short ramp is over. */
    native(&assets, 60000, pcm); xz_oc_stream_mix(s, pcm, 2048, 60000, unity);
    native(&assets, 62048, pcm); memcpy(before, pcm, sizeof(pcm));
    assert(!xz_oc_stream_mix(s, pcm, 2048, 62048, unity) && !memcmp(pcm, before, sizeof(pcm)));
    /* Past the end of the prepared audio only the native share remains: muted stems are silent. */
    for (unsigned i = 0; i < 4096; i++) pcm[i] = 0.25f;
    const float mute[3] = {0, 0, 0};
    xz_oc_stream_mix(s, pcm, 2048, 5000000, mute); for (unsigned i = 0; i < 4096; i++) pcm[i] = 0.25f;
    assert(xz_oc_stream_mix(s, pcm, 2048, 5002048, mute) == 2048);
    for (unsigned i = 0; i < 4096; i++) assert(pcm[i] == 0);
    xz_oc_stream_status(s, &status);
    assert(status.misses == misses && !status.starve_runs && status.state == XZ_OC_READY);
    printf("store %u of %u chunks, built in %u ms; misses %u, starved runs %u\n", status.flat_built, status.flat_chunks, status.flat_ms,
           status.misses - misses, status.starve_runs);
    xz_oc_stream_close(s); xz_oc_close(&assets);
    pool_empty();
    puts("PASS whole-track store in a stream: jumps and new selections are mixed on the next block, unity stays untouched");
}
/* Tracks first while stems are in use: a refused track load takes the store's whole grant. The store gives up the
 * playhead's segments only after the windows hold the playhead, so the deck plays on without a starved block. The
 * playhead is somewhere the windows never held: until the store goes, the store alone covers it. */
static void yield(const char *source) {
    unsetenv("XZ_MODS_STEMS_FLAT");
    struct xz_oc_assets assets; char error[256];
    assert(!xz_oc_open(source, &assets, error, sizeof(error)));
    struct xz_oc_stream *s = xz_oc_stream_open(source, 44100, error, sizeof(error)); assert(s);
    float pcm[4096]; const float unity[3] = {1, 1, 1}, no_vocals[3] = {1, 1, 0}; struct xz_oc_status status;
    for (unsigned attempt = 0; attempt < 400; attempt++) {
        int64_t position = 10000 + (attempt % 8) * 3000; native(&assets, position, pcm);
        for (unsigned chunk = 0; chunk < 2048; chunk += 64) xz_oc_stream_mix(s, pcm + chunk * 2, 64, position + chunk, unity);
        nap(); xz_oc_stream_status(s, &status);
        if (status.state == XZ_OC_READY && status.flat_chunks && status.flat_built == status.flat_chunks) break;
    }
    assert(status.flat_chunks && status.flat_built == status.flat_chunks && status.store_held);
    const int64_t at = 250000;
    for (unsigned block = 0; block < 10; block++) {
        native(&assets, at, pcm); assert(xz_oc_stream_mix(s, pcm, 2048, at, no_vocals) == 2048); nap();
    }
    xz_oc_stream_status(s, &status);
    const uint32_t misses = status.misses, starved = status.starve_runs, held = status.store_held;
    assert(!starved && status.store_grant >= held);
    __atomic_add_fetch(&track_refusals, 1, __ATOMIC_RELEASE);
    unsigned blocks = 0, after = 0;
    for (; blocks < 20000 && after < 50; blocks++) {
        native(&assets, at, pcm);
        assert(xz_oc_stream_mix(s, pcm, 2048, at, no_vocals) == 2048);
        xz_oc_stream_status(s, &status);
        if (status.misses != misses || status.starve_runs != starved) {
            fprintf(stderr, "block %u after the refusal: misses %u -> %u, starved runs %u -> %u, store held %u grant %u\n", blocks,
                    misses, status.misses, starved, status.starve_runs, status.store_held, status.store_grant);
            assert(0);
        }
        after += !status.store_held;
        struct timespec t = {0, 1000000}; nanosleep(&t, NULL);
    }
    assert(!status.store_held && !status.store_grant);
    double apart = distance(&assets, at, pcm, 3);
    printf("store of %u segments given up for a refused track load after %u blocks; windows play on, rms distance %.5f\n", held,
           blocks - after, apart);
    assert(apart < 0.006);
    xz_oc_stream_close(s); xz_oc_close(&assets);
    pool_empty();
    puts("PASS a store that loses its grant hands the playhead to the windows first: no miss, no starved block");
}
/* ---- USB STOP: the stream lets go of the stick its stem files are on (xz_oc_stream_source_gone) ---- */
static const char *stick;
/* Open descriptors of this process under the stick's folder. */
static int stick_fds(void) {
    DIR *dir = opendir("/proc/self/fd");
    assert(dir);
    int n = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        char link[300], target[1024];
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        snprintf(link, sizeof link, "/proc/self/fd/%s", entry->d_name);
        ssize_t got = readlink(link, target, sizeof target - 1);
        if (got <= 0) continue;
        target[got] = 0;
        n += !strncmp(target, stick, strlen(stick));
    }
    closedir(dir);
    return n;
}
static int gone_outcome = -1;
static uint32_t gone_token;
static char gone_why[128];
static void gone_done(uint32_t token, int outcome, const char *why) {
    gone_token = token; snprintf(gone_why, sizeof gone_why, "%s", why ? why : "");
    __atomic_store_n(&gone_outcome, outcome, __ATOMIC_RELEASE);
}
static const float no_vocals[3] = {1, 1, 0};
static struct xz_oc_stream *s_under_test;
/* The stick goes; the audio thread keeps mixing at `at` while the worker answers. */
static int stick_goes(struct xz_oc_assets *a, struct xz_oc_stream *s, const char *prefix, uint32_t token, int64_t at) {
    __atomic_store_n(&gone_outcome, -1, __ATOMIC_RELEASE);
    xz_oc_stream_source_gone(s, prefix, gone_done, token);
    float pcm[4096];
    for (unsigned i = 0; i < 1000 && __atomic_load_n(&gone_outcome, __ATOMIC_ACQUIRE) < 0; i++) {
        native(a, at, pcm); xz_oc_stream_mix(s, pcm, 2048, at, no_vocals);
        struct timespec t = {0, 2000000}; nanosleep(&t, NULL);
    }
    int outcome = __atomic_load_n(&gone_outcome, __ATOMIC_ACQUIRE);
    assert(outcome >= 0 && gone_token == token);
    printf("  stick %s: outcome %d (%s)\n", prefix, outcome, gone_why);
    return outcome;
}
/* What ends the stems: the stick goes (prefix and token), or tracks first takes the pool (prefix NULL). */
struct ending { const char *prefix; uint32_t token; };
static void trigger(const struct ending *e) {
    if (!e->prefix) { __atomic_add_fetch(&track_refusals, 1, __ATOMIC_RELEASE); return; }
    __atomic_store_n(&gone_outcome, -1, __ATOMIC_RELEASE);
    xz_oc_stream_source_gone(s_under_test, e->prefix, gone_done, e->token);
}
/* Mixes no-vocals blocks at `at`; after the first (which carries the stems) the ending is triggered. Blocks before
 * the end carry the stems (drums and harmonics), exactly one block ramps from the last sample given out to the
 * native audio over 128 frames (the stream's own ramp: no click), and every block after it is the native audio,
 * untouched. */
static void ends_through_the_ramp(struct xz_oc_assets *a, struct xz_oc_stream *s, int64_t at, struct ending e) {
    float pcm[4096], plain[4096], last[2] = {0, 0};
    unsigned stems = 0, ramps = 0, natives = 0;
    s_under_test = s;
    for (unsigned block = 0; block < 3000 && natives < 25; block++) {
        if (block == 1) { assert(stems == 1); trigger(&e); }
        native(a, at, plain); memcpy(pcm, plain, sizeof pcm);
        size_t mixed = xz_oc_stream_mix(s, pcm, 2048, at, no_vocals);
        if (!mixed && !memcmp(pcm, plain, sizeof pcm)) { assert(ramps == 1); natives++; }
        else if (!ramps && distance(a, at, pcm, 3) < 0.006) stems++;
        else {
            assert(!ramps && stems && mixed == 2048);
            for (unsigned i = 0; i < 2048; i++) for (unsigned c = 0; c < 2; c++) {
                float want = plain[i * 2 + c];
                if (i < 128) { float f = (float)(i + 1) / 128.f; want = last[c] * (1 - f) + want * f; }
                want = want > 1 ? 1 : want < -1 ? -1 : want;
                if (fabsf(pcm[i * 2 + c] - want) > 1e-6f) {
                    fprintf(stderr, "ramp block frame %u channel %u: %.7f, expected %.7f\n", i, c, pcm[i * 2 + c], want);
                    assert(0);
                }
            }
            ramps++;
        }
        memcpy(last, pcm + 4094, sizeof last);
        struct timespec t = {0, 1000000}; nanosleep(&t, NULL);
    }
    printf("  %u block(s) with stems, then one ramp block, then %u native block(s) untouched\n", stems, natives);
    assert(stems && ramps == 1 && natives >= 25);
}
/* Aligns the stream and plays no vocals at `at` until the stems mix there. built: 0 for a stream without a store,
 * UINT32_MAX for the whole track built, otherwise at least that many chunks built. */
static void until_mixed(struct xz_oc_assets *a, struct xz_oc_stream *s, int64_t at, uint32_t built) {
    float pcm[4096]; const float unity[3] = {1, 1, 1}; struct xz_oc_status status;
    for (unsigned attempt = 0; attempt < 1500; attempt++) {
        int64_t position = at + (attempt % 8) * 3000; native(a, position, pcm);
        for (unsigned chunk = 0; chunk < 2048; chunk += 64) xz_oc_stream_mix(s, pcm + chunk * 2, 64, position + chunk, unity);
        native(a, at, pcm);
        int stems = xz_oc_stream_mix(s, pcm, 2048, at, no_vocals) == 2048 && distance(a, at, pcm, 3) < 0.006;
        xz_oc_stream_status(s, &status);
        if (stems && status.state == XZ_OC_READY &&
            (!built ? !status.flat_chunks : status.flat_chunks && (built == UINT32_MAX ? status.flat_built == status.flat_chunks : status.flat_built >= built)))
            return;
        nap();
    }
    fprintf(stderr, "stems never ready: state %d built %u of %u held %u grant %u need %u\n", status.state, status.flat_built,
            status.flat_chunks, status.store_held, status.store_grant, status.store_need);
    assert(0);
}
/* A store with the whole track built: it plays on with no file open; a later lack of pool memory ends it cleanly. */
static void gone_whole(const char *source) {
    unsetenv("XZ_MODS_STEMS_FLAT");
    struct xz_oc_assets assets; char error[256];
    assert(!xz_oc_open(source, &assets, error, sizeof(error)));
    const int base = stick_fds();
    struct xz_oc_stream *s = xz_oc_stream_open(source, 44100, error, sizeof(error)); assert(s);
    const int64_t at = 250000;
    until_mixed(&assets, s, at, UINT32_MAX);
    struct xz_oc_status status; xz_oc_stream_status(s, &status);
    const uint32_t misses = status.misses;
    assert(stick_fds() == base + 4);
    assert(stick_goes(&assets, s, "/media/usb9/", 5, at) == XZ_OC_GONE_NOTHING && stick_fds() == base + 4);
    assert(stick_goes(&assets, s, stick, 9, at) == XZ_OC_GONE_KEPT && stick_fds() == base);
    /* Jumps and new selections still mix from the store at once; nothing reopens the stick. */
    const int64_t places[] = {15000, 250000, 40000, 330000, 120000};
    const float choice[][3] = {{1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 0}, {0, 1, 1}};
    const unsigned masks[] = {3, 4, 5, 2, 6};
    float pcm[4096];
    for (unsigned round = 0; round < 3; round++) for (unsigned i = 0; i < 5; i++) {
        native(&assets, places[i], pcm);
        assert(xz_oc_stream_mix(s, pcm, 2048, places[i], choice[i]) == 2048);
        assert(distance(&assets, places[i], pcm, masks[i]) < 0.006);
        nap();
    }
    xz_oc_stream_status(s, &status);
    assert(status.state == XZ_OC_READY && status.misses == misses && !status.starve_runs && stick_fds() == base);
    assert(status.store_held == status.store_need && status.store_grant >= status.store_held && status.stems_whole);
    printf("  kept: %u of %u chunks in %u segments, granted %u; jumps and selections mixed with no miss\n", status.flat_built,
           status.flat_chunks, status.store_held, status.store_grant);
    assert(stick_goes(&assets, s, stick, 13, at) == XZ_OC_GONE_NOTHING); /* already let go */
    /* Tracks first takes the pool below what the store holds: the stems end, through the ramp. */
    ends_through_the_ramp(&assets, s, at, (struct ending){NULL, 0});
    xz_oc_stream_status(s, &status);
    assert(status.state == XZ_OC_ERROR && !status.store_held && stick_fds() == base);
    xz_oc_stream_close(s); xz_oc_close(&assets);
    pool_empty();
    assert(!stick_fds());
    puts("PASS USB STOP, whole store: the stems play on from RAM with every stem file closed; a pool that must take the memory ends them through the ramp");
}
/* A bubble (the pool holds 2 of the track's 3 segments): the stems end through the ramp and the files close. */
static void gone_bubble(const char *source) {
    unsetenv("XZ_MODS_STEMS_FLAT");
    setenv("XZ_MODS_STEM_POOL_EXTENTS", "2", 1);
    struct xz_oc_assets assets; char error[256];
    assert(!xz_oc_open(source, &assets, error, sizeof(error)));
    const int base = stick_fds();
    struct xz_oc_stream *s = xz_oc_stream_open(source, 44100, error, sizeof(error)); assert(s);
    const int64_t at = 600000;
    until_mixed(&assets, s, at, 36); /* segments 1 and 2 of this track: 32 + 4 chunks */
    struct xz_oc_status status; xz_oc_stream_status(s, &status);
    printf("  bubble: %u of %u segments held (granted %u), %u of %u chunks built\n", status.store_held, status.store_need,
           status.store_grant, status.flat_built, status.flat_chunks);
    assert(status.store_need == 3 && status.store_held == 2 && status.flat_built == 36 && status.flat_built < status.flat_chunks);
    assert(stick_fds() == base + 4);
    ends_through_the_ramp(&assets, s, at, (struct ending){stick, 21});
    assert(__atomic_load_n(&gone_outcome, __ATOMIC_ACQUIRE) == XZ_OC_GONE_ENDED && gone_token == 21);
    printf("  stick %s: outcome %d (%s)\n", stick, gone_outcome, gone_why);
    xz_oc_stream_status(s, &status);
    assert(status.state == XZ_OC_ERROR && !status.store_held && stick_fds() == base);
    xz_oc_stream_close(s); xz_oc_close(&assets);
    pool_empty();
    assert(!stick_fds());
    puts("PASS USB STOP, bubble: the stems end through the ramp, the store goes back to the pool, no stem file stays open");
}
/* No store (windows only): the stems end through the ramp and the files close. */
static void gone_none(const char *source) {
    setenv("XZ_MODS_STEMS_FLAT", "0", 1);
    struct xz_oc_assets assets; char error[256];
    assert(!xz_oc_open(source, &assets, error, sizeof(error)));
    const int base = stick_fds();
    struct xz_oc_stream *s = xz_oc_stream_open(source, 44100, error, sizeof(error)); assert(s);
    const int64_t at = 250000;
    until_mixed(&assets, s, at, 0);
    struct xz_oc_status status; xz_oc_stream_status(s, &status);
    assert(!status.flat_chunks && stick_fds() == base + 4);
    ends_through_the_ramp(&assets, s, at, (struct ending){stick, 33});
    assert(__atomic_load_n(&gone_outcome, __ATOMIC_ACQUIRE) == XZ_OC_GONE_ENDED && gone_token == 33);
    printf("  stick %s: outcome %d (%s)\n", stick, gone_outcome, gone_why);
    xz_oc_stream_status(s, &status);
    assert(status.state == XZ_OC_ERROR && stick_fds() == base);
    xz_oc_stream_close(s); xz_oc_close(&assets);
    assert(!stick_fds());
    puts("PASS USB STOP, windows only: the stems end through the ramp and no stem file stays open");
}
int main(int argc, char **argv) {
    if (argc == 4 && !strncmp(argv[1], "gone-", 5)) {
        stick = argv[3];
        if (!strcmp(argv[1], "gone-whole")) gone_whole(argv[2]);
        else if (!strcmp(argv[1], "gone-bubble")) gone_bubble(argv[2]);
        else if (!strcmp(argv[1], "gone-none")) gone_none(argv[2]);
        else return 2;
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "kernel")) { kernel(); return 0; }
    if (argc >= 3 && !strcmp(argv[1], "stream")) { if (argc == 4) lag = atof(argv[3]); stream(argv[2]); return 0; }
    if (argc == 3 && !strcmp(argv[1], "yield")) { yield(argv[2]); return 0; }
    return 2;
}
