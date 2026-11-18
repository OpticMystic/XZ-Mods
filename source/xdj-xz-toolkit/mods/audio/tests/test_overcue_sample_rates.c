/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "../overcue.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define FRAMES 2048u
static unsigned native_rate;
static double lag, maximum_error;
static unsigned callbacks;

static void pause_worker(void) {
    struct timespec delay = {0, 10000000};
    nanosleep(&delay, NULL);
}

/* Independent analytic oracle for verify_overcue.fixture(signal=True).
 * Its PCM was generated on a 96000-Hz sample grid, including for a fixture
 * relabeled as the slower OVPGZ004 profile. No production interpolation helper
 * is used to construct native samples or expected isolated stems. */
static double part(unsigned role, double frame) {
    const double tau = 6.28318530717958647692;
    const unsigned frequencies[3][2] = {{193, 0}, {557, 1297}, {3719, 0}};
    double result = 0;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned frequency = frequencies[role][i];
        if (frequency) result += 3500 * sin(tau * frequency * frame / 96000 + frequency / 100.0);
    }
    return result / 32768.0;
}

static float expected(const struct xz_oc_assets *assets, int64_t position,
                      unsigned frame, unsigned channel, const float levels[3]) {
    double at = ((double)position + frame + lag) * assets->sample_rate / native_rate + channel * 17;
    double value = 0;
    for (unsigned role = 0; role < 3; ++role) value += levels[role] * part(role, at) * 1.5;
    return (float)(value < -1 ? -1 : value > 1 ? 1 : value);
}

static void native_pcm(const struct xz_oc_assets *assets, int64_t at, float pcm[FRAMES * 2]) {
    const float unity[3] = {1, 1, 1};
    for (unsigned i = 0; i < FRAMES; ++i)
        for (unsigned c = 0; c < 2; ++c) pcm[i * 2 + c] = expected(assets, at, i, c, unity);
}

static double error_at(const struct xz_oc_assets *assets, int64_t at,
                       const float *pcm, const float levels[3], unsigned skip) {
    double sum = 0;
    for (unsigned i = skip; i < FRAMES; ++i) for (unsigned c = 0; c < 2; ++c) {
        float value = pcm[i * 2 + c];
        CHECK(isfinite(value) && value >= -1 && value <= 1);
        double delta = value - expected(assets, at, i, c, levels);
        sum += delta * delta;
    }
    return sqrt(sum / ((FRAMES - skip) * 2));
}

static void mix(struct xz_oc_stream *stream, const struct xz_oc_assets *assets,
                int64_t at, const float levels[3], float pcm[FRAMES * 2]) {
    native_pcm(assets, at, pcm);
    xz_oc_stream_mix(stream, pcm, FRAMES, at, levels);
    ++callbacks;
}

static void wait_mix(struct xz_oc_stream *stream, const struct xz_oc_assets *assets,
                     int64_t at, const float levels[3]) {
    float pcm[FRAMES * 2];
    unsigned stable = 0;
    struct xz_oc_status status = {0};
    double error = 1;
    for (unsigned attempt = 0; attempt < 300; ++attempt) {
        mix(stream, assets, at, levels, pcm);
        error = error_at(assets, at, pcm, levels, 128);
        pause_worker();
        xz_oc_stream_status(stream, &status);
        CHECK(status.state != XZ_OC_ERROR);
        stable = status.state == XZ_OC_READY && error < 0.009 ? stable + 1 : 0;
        if (stable == 2) {
            if (levels[0] == 0 && levels[1] == 0 && levels[2] == 0)
                for (unsigned i = 0; i < FRAMES * 2; ++i) CHECK(pcm[i] == 0);
            if (error > maximum_error) maximum_error = error;
            return;
        }
    }
    fprintf(stderr, "native=%u prepared=%u position=%lld levels=%g/%g/%g state=%d rms=%g\n",
            native_rate, assets->sample_rate, (long long)at,
            levels[0], levels[1], levels[2], status.state, error);
    CHECK(0 && "selection never reached the expected native-rate audio");
}

int main(int argc, char **argv) {
    CHECK(argc == 4);
    native_rate = (unsigned)strtoul(argv[2], NULL, 10);
    lag = strtod(argv[3], NULL);
    CHECK(native_rate == 32000 || native_rate == 44100 || native_rate == 48000 ||
          native_rate == 88200 || native_rate == 96000);
    struct xz_oc_assets assets;
    char error[256] = {0};
    CHECK(!xz_oc_open(argv[1], &assets, error, sizeof(error)));
    struct xz_oc_stream *stream = xz_oc_stream_open(argv[1], native_rate, error, sizeof(error));
    CHECK(stream);
    struct xz_oc_status status = {0};
    const float unity[3] = {1, 1, 1};
    float pcm[FRAMES * 2], before[FRAMES * 2];
    for (unsigned attempt = 0; attempt < 400; ++attempt) {
        int64_t position = (int64_t)(native_rate * (0.4 + (attempt % 8) * 0.06));
        native_pcm(&assets, position, pcm); memcpy(before, pcm, sizeof(pcm));
        for (unsigned i = 0; i < FRAMES; i += 64)
            CHECK(!xz_oc_stream_mix(stream, pcm + i * 2, 64, position + i, unity));
        CHECK(!memcmp(before, pcm, sizeof(pcm)));
        pause_worker(); xz_oc_stream_status(stream, &status);
        CHECK(status.state != XZ_OC_ERROR);
        if (status.state == XZ_OC_READY) break;
    }
    fprintf(stderr, "alignment native=%u prepared=%u lag=%d score=%g\n",
            native_rate, assets.sample_rate, status.lag, status.correlation);
    CHECK(status.state == XZ_OC_READY && fabs(status.lag - lag) <= 1.25);
    CHECK(status.correlation >= 0.995);
    int64_t position = (int64_t)(native_rate * 1.25) + 17;
    const float selections[][3] = {
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
        {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {0.25f, 0.6f, 1}, {0, 0, 0}
    };
    for (unsigned i = 0; i < sizeof(selections) / sizeof(selections[0]); ++i)
        wait_mix(stream, &assets, position, selections[i]);
    const float vocal[3] = {0, 0, 1};
    int64_t loop = (int64_t)(native_rate * 0.75) + 3;
    /* After reverse playback, parking at a forward-pinned loop still needs a
     * reverse-ready current window. A forward cache hit must not block refill. */
    wait_mix(stream,&assets,(int64_t)(native_rate*3.5),vocal);
    for(unsigned step=0;step<8;step++){
        mix(stream,&assets,(int64_t)(native_rate*3.5)-(int64_t)step*FRAMES,vocal,pcm);
        pause_worker();
    }
    xz_oc_stream_loop_start(stream, (int32_t)loop);
    wait_mix(stream, &assets, loop, vocal);
    wait_mix(stream, &assets, (int64_t)(native_rate * 5.5) + 117, vocal);
    mix(stream, &assets, loop, vocal, pcm);
    CHECK(error_at(&assets, loop, pcm, vocal, 0) < 0.009);
    wait_mix(stream, &assets, (int64_t)(native_rate * 2.1) + 229, vocal);
    wait_mix(stream, &assets, position, unity);
    native_pcm(&assets, position, pcm); memcpy(before, pcm, sizeof(pcm));
    CHECK(!xz_oc_stream_mix(stream, pcm, FRAMES, position, unity));
    CHECK(!memcmp(before, pcm, sizeof(pcm)));
    printf("{\"prepared_hz\":%u,\"reader_hz\":%u,\"codec\":%u,\"lag\":%g,"
           "\"correlation\":%.9g,\"maximum_selected_rms_error\":%.9g,"
           "\"callbacks\":%u,\"unity_bit_exact\":true,\"loop_return\":true,"
           "\"hardware_tested\":false}\n",
           assets.sample_rate, native_rate, assets.reference.codec, lag, status.correlation,
           maximum_error, callbacks);
    xz_oc_stream_close(stream);
    xz_oc_close(&assets);
    return 0;
}
