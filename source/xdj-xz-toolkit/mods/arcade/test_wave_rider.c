/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Wave Rider acceptance requires active assertions
#endif
#include "wave_rider.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum { TRACK_SAMPLES = 7200, WIDTH = 800, HEIGHT = 480, GUARD = 16, FRAMES = 180 };
static uint8_t track[TRACK_SAMPLES * 3];
static uint16_t pixels[WIDTH * HEIGHT];
static uint32_t now = 1000;

static struct xz_wave_rider_snapshot sample(unsigned first, double position)
{
    struct xz_wave_rider_snapshot out = {0};
    assert(first < TRACK_SAMPLES);
    out.first = first;
    out.count = TRACK_SAMPLES - first;
    if (out.count > XZ_WAVE_RIDER_SAMPLES) out.count = XZ_WAVE_RIDER_SAMPLES;
    out.total = TRACK_SAMPLES;
    out.normalization = 255;
    out.generation = 1;
    out.observed_ms = now;
    out.track_hash = UINT64_C(0x739a12e4);
    out.position = position;
    out.kind = 1;
    out.deck = 0;
    out.playing = out.valid = 1;
    memcpy(out.bands, track + first * 3, out.count * 3);
    snprintf(out.title, sizeof(out.title), "TEST STEM TRACK");
    return out;
}

static const struct xz_rider_object *object_at(const struct xz_rider_course *course,
                                              unsigned kind, unsigned at)
{
    for (unsigned i = 0; i < course->object_count; ++i)
        if (course->objects[i].kind == kind && course->objects[i].at == at)
            return &course->objects[i];
    return NULL;
}

static void silence_and_roles(void)
{
    struct xz_rider_course quiet, drums, vocals, harmonics, again;
    memset(track, 0, sizeof(track));
    struct xz_wave_rider_snapshot wave = sample(0, 300);
    xz_rider_analyze(&wave, &quiet);
    assert(!quiet.object_count && !quiet.spikes && !quiet.gems);
    for (unsigned i = 0; i < wave.count; ++i) assert(!quiet.swell[i] && !quiet.slip[i]);

    track[700 * 3] = 255;
    wave = sample(0, 300);
    xz_rider_analyze(&wave, &drums);
    assert(object_at(&drums, XZ_RIDER_SPIKE, 700));
    assert(drums.spikes == 1 && drums.gems == 0);
    assert(drums.swell[700] > quiet.swell[700]);
    xz_rider_analyze(&wave, &again);
    assert(drums.object_count == again.object_count && drums.spikes == again.spikes && drums.gems == again.gems);
    assert(!memcmp(drums.swell, again.swell, sizeof(drums.swell)));
    assert(!memcmp(drums.slip, again.slip, sizeof(drums.slip)));
    for (unsigned i = 0; i < drums.object_count; ++i)
        assert(drums.objects[i].at == again.objects[i].at &&
               drums.objects[i].kind == again.objects[i].kind &&
               drums.objects[i].altitude == again.objects[i].altitude);
    track[700 * 3] = 0;
    track[900 * 3 + 2] = 255;
    wave = sample(0, 300);
    xz_rider_analyze(&wave, &vocals);
    assert(object_at(&vocals, XZ_RIDER_GEM, 900));
    assert(vocals.gems == 1 && vocals.spikes == 0);
    assert(!object_at(&vocals, XZ_RIDER_SPIKE, 700));
    memset(track, 0, sizeof(track));
    for (unsigned i = 400; i < 1100; ++i) track[i * 3 + 1] = 180;
    wave = sample(0, 300);
    xz_rider_analyze(&wave, &harmonics);
    assert(harmonics.slip[700]);
    assert(!harmonics.spikes && !harmonics.gems);
    assert(harmonics.swell[700] > quiet.swell[700]);
    puts("PASS silence is flat; actual drum/vocal onsets place different objects; harmonic sustain creates slipstream");
}

static void window_identity(void)
{
    struct xz_rider_course left, right;
    memset(track, 0, sizeof(track));
    track[1100 * 3] = 255;
    track[1250 * 3 + 2] = 255;
    struct xz_wave_rider_snapshot a = sample(100, 450), b = sample(700, 1050);
    xz_rider_analyze(&a, &left);
    xz_rider_analyze(&b, &right);
    const struct xz_rider_object *spike_a = object_at(&left, XZ_RIDER_SPIKE, 1100);
    const struct xz_rider_object *spike_b = object_at(&right, XZ_RIDER_SPIKE, 1100);
    const struct xz_rider_object *gem_a = object_at(&left, XZ_RIDER_GEM, 1250);
    const struct xz_rider_object *gem_b = object_at(&right, XZ_RIDER_GEM, 1250);
    assert(spike_a && spike_b && gem_a && gem_b);
    assert(spike_a->altitude == spike_b->altitude && gem_a->altitude == gem_b->altitude);
    assert(left.swell[1100 - a.first] == right.swell[1100 - b.first]);
    for (unsigned i = 1; i < right.object_count; ++i)
        assert(right.objects[i - 1].at <= right.objects[i].at);
    puts("PASS object positions and altitude follow the same absolute samples across moving windows");
}

static void dense_track(void)
{
    for (unsigned i = 0; i < TRACK_SAMPLES; ++i) {
        unsigned phrase = (i / 480) % 4;
        track[i * 3] = (uint8_t)(i % 75 < 7 ? 210 - (i % 75) * 23 : 8);
        track[i * 3 + 1] = (uint8_t)(phrase == 2 ? 8 : 48 + (i % 240) / 3);
        track[i * 3 + 2] = (uint8_t)((i + 38) % 150 < 9 ? 190 - ((i + 38) % 150) * 18 : 3);
    }
}

static void start(struct xz_rider *game, struct xz_wave_rider_snapshot *wave)
{
    now += 20;
    wave->observed_ms = now;
    xz_rider_init(game, 0, now);
    xz_rider_feed(game, wave, now);
    xz_rider_begin(game, now);
    assert(game->phase == XZ_RIDER_RIDE);
}

static void feed(struct xz_rider *game, struct xz_wave_rider_snapshot *wave, double position)
{
    now += 20;
    wave->position = position;
    wave->observed_ms = now;
    xz_rider_feed(game, wave, now);
    xz_rider_step(game, now);
}

static void lifecycle(void)
{
    memset(track, 0, sizeof(track));
    track[500 * 3 + 2] = 255;
    struct xz_wave_rider_snapshot wave = sample(0, 470);
    struct xz_rider game;
    start(&game, &wave);
    const struct xz_rider_object *gem = object_at(&game.course, XZ_RIDER_GEM, 500);
    assert(gem);
    game.cruise = game.target = game.altitude = gem->altitude;
    for (unsigned i = 471; i <= 501; ++i) feed(&game, &wave, i);
    assert(game.catches == 1 && game.score > 0);
    unsigned score = game.score, judged = game.judged, catches = game.catches;
    for (unsigned i = 0; i < 50; ++i) {
        xz_rider_key(&game, XZ_RIDER_CUE, now);
        feed(&game, &wave, 501);
    }
    assert(game.score == score && game.judged == judged && game.catches == catches);
    wave.playing = 0;
    feed(&game, &wave, 501);
    assert(game.phase == XZ_RIDER_HOLD);
    double stopped = game.position;
    for (unsigned i = 0; i < 50; ++i) feed(&game, &wave, 501);
    assert(game.valid && game.phase == XZ_RIDER_HOLD && game.position == stopped && game.score == score);
    wave.playing = 1;
    feed(&game, &wave, 470);
    assert(game.phase == XZ_RIDER_RIDE);
    for (unsigned i = 471; i <= 501; ++i) feed(&game, &wave, i);
    assert(game.score == score && game.catches == catches);
    feed(&game, &wave, 1000);
    assert(game.phase == XZ_RIDER_RIDE && game.score == score && game.catches == catches);

    for (unsigned i = 0; i < 100; ++i) xz_rider_jog(&game, 100000, now);
    float highest = game.target;
    assert(isfinite(highest) && highest <= 290);
    for (unsigned i = 0; i < 100; ++i) xz_rider_jog(&game, -100000, now);
    assert(isfinite(game.target) && game.target >= 0 && game.target < highest);
    float before_invalid_jog = game.target;
    xz_rider_jog(&game, NAN, now);
    assert(game.target == before_invalid_jog);
    wave.track_hash++;
    wave.generation++;
    feed(&game, &wave, 30);
    assert(game.score == 0 && game.catches == 0 && game.judged == 0);
    wave.valid = 0;
    wave.error = XZ_WAVE_RIDER_NO_ANALYSIS;
    feed(&game, &wave, 30);
    assert(!game.valid && game.phase != XZ_RIDER_RIDE);
    unsigned before = game.score;
    xz_rider_key(&game, XZ_RIDER_CUE, now);
    now += 1000;
    xz_rider_step(&game, now);
    assert(game.score == before);
    puts("PASS real-position scoring; repeated/paused/revisited positions cannot farm; jog bounded; track/no-data reset");
}

static void hop_scoring(void)
{
    memset(track, 0, sizeof(track));
    track[500 * 3] = track[800 * 3] = 255;
    struct xz_wave_rider_snapshot wave = sample(0, 470);
    struct xz_rider game;
    start(&game, &wave);
    for (unsigned i = 471; i <= 511; ++i) {
        feed(&game, &wave, i);
        if (i == 495) xz_rider_key(&game, XZ_RIDER_CUE, now);
    }
    assert(game.hops == 1 && game.perfects == 1 && game.score > 0);
    unsigned earned = game.score;
    for (unsigned i = 0; i < 30; ++i) {
        xz_rider_key(&game, XZ_RIDER_CUE, now);
        feed(&game, &wave, 511);
    }
    assert(game.score == earned && game.hops == 1);
    for (unsigned i = 512; i <= 805; ++i) {
        feed(&game, &wave, i);
        if (i == 804) xz_rider_key(&game, XZ_RIDER_CUE, now);
    }
    assert(game.hops == 2 && game.perfects == 2 && game.score > earned);
    assert(game.spike_total == 2 && game.judged == 2);
    puts("PASS CUE on either side of an observed drum crossing earns one rhythmic hop; static tapping earns nothing");
}

static void track_change_after_gap(void)
{
    memset(track, 0, sizeof(track));
    track[500 * 3 + 2] = 255;
    struct xz_wave_rider_snapshot wave = sample(0, 470);
    struct xz_rider game;
    start(&game, &wave);
    const struct xz_rider_object *gem = object_at(&game.course, XZ_RIDER_GEM, 500);
    assert(gem);
    game.cruise = game.target = game.altitude = gem->altitude;
    for (unsigned i = 471; i <= 501; ++i) feed(&game, &wave, i);
    assert(game.score && game.catches == 1);
    struct xz_wave_rider_snapshot unavailable = {0};
    unavailable.error = XZ_WAVE_RIDER_NO_TRACK;
    feed(&game, &unavailable, 0);
    assert(!game.valid);
    wave.track_hash++;
    wave.generation++;
    feed(&game, &wave, 30);
    assert(game.valid && !game.score && !game.catches && !game.judged);
    puts("PASS a track unload/loading gap cannot carry the previous track's score into the new course");
}

static void track_end(void)
{
    memset(track, 0, sizeof(track));
    struct xz_wave_rider_snapshot wave = sample(TRACK_SAMPLES - XZ_WAVE_RIDER_SAMPLES,
                                                TRACK_SAMPLES - 3);
    struct xz_rider game;
    start(&game, &wave);
    feed(&game, &wave, TRACK_SAMPLES - 2);
    wave.playing = 0;
    feed(&game, &wave, TRACK_SAMPLES - 1);
    assert(game.phase == XZ_RIDER_RESULT);
    xz_rider_key(&game, XZ_RIDER_CUE, now);
    assert(game.leave && game.phase != XZ_RIDER_RIDE);
    game.leave = 0;
    wave.valid = 0;
    wave.error = XZ_WAVE_RIDER_NO_TRACK;
    feed(&game, &wave, TRACK_SAMPLES - 1);
    assert(game.phase == XZ_RIDER_SETUP && !game.valid);
    puts("PASS a stopped track at EOF shows results; its action returns to the deck; missing source exits results");
}

static uint32_t pixel_hash(const uint16_t *image, size_t count)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < count; ++i) { hash ^= image[i]; hash *= UINT32_C(16777619); }
    return hash;
}

static void screenshot(const struct xz_rider *game, const char *directory, const char *name)
{
    assert(xz_rider_render(game, pixels, WIDTH * HEIGHT, WIDTH));
    if (!directory) return;
    char filename[2048];
    int count = snprintf(filename, sizeof(filename), "%s/wave-rider-%s.ppm", directory, name);
    assert(count > 0 && (size_t)count < sizeof(filename));
    FILE *file = fopen(filename, "wb");
    assert(file);
    assert(fprintf(file, "P6\n800 480\n255\n") > 0);
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
        uint16_t pixel = pixels[i];
        unsigned char rgb[3] = {(unsigned char)(((pixel >> 11) & 31) * 255 / 31),
            (unsigned char)(((pixel >> 5) & 63) * 255 / 63), (unsigned char)((pixel & 31) * 255 / 31)};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    assert(!fclose(file));
}

static void rendering(const char *directory)
{
    static uint16_t guarded[GUARD + (WIDTH + 8) * HEIGHT + GUARD];
    dense_track();
    struct xz_wave_rider_snapshot wave = sample(400, 850);
    struct xz_rider game;
    start(&game, &wave);
    for (size_t i = 0; i < sizeof(guarded) / sizeof(*guarded); ++i) guarded[i] = 0xa56d;
    assert(xz_rider_render(&game, guarded + GUARD, (WIDTH + 8) * HEIGHT, WIDTH + 8));
    for (unsigned i = 0; i < GUARD; ++i)
        assert(guarded[i] == 0xa56d && guarded[GUARD + (WIDTH + 8) * HEIGHT + i] == 0xa56d);
    for (unsigned y = 0; y < HEIGHT; ++y)
        for (unsigned x = WIDTH; x < WIDTH + 8; ++x)
            assert(guarded[GUARD + y * (WIDTH + 8) + x] == 0xa56d);
    assert(!xz_rider_render(&game, pixels, WIDTH * HEIGHT - 1, WIDTH));
    assert(!xz_rider_render(&game, pixels, WIDTH * HEIGHT, WIDTH - 1));
    assert(!xz_rider_render(&game, NULL, WIDTH * HEIGHT, WIDTH));
    screenshot(&game, directory, "stems");
    uint32_t first = pixel_hash(pixels, WIDTH * HEIGHT);
    feed(&game, &wave, 870);
    screenshot(&game, directory, "scroll");
    assert(first != pixel_hash(pixels, WIDTH * HEIGHT));
    uint32_t dense = pixel_hash(pixels + WIDTH * 140, WIDTH * 200);
    struct xz_wave_rider_snapshot silent = wave;
    memset(silent.bands, 0, sizeof(silent.bands));
    feed(&game, &silent, 870);
    screenshot(&game, directory, "silence");
    assert(dense != pixel_hash(pixels + WIDTH * 140, WIDTH * 200));
    assert(!game.course.object_count);
    feed(&game, &wave, 870);
    wave.kind = 0;
    feed(&game, &wave, 870);
    xz_rider_begin(&game, now);
    screenshot(&game, directory, "three-band");
    wave.playing = 0;
    feed(&game, &wave, 870);
    screenshot(&game, directory, "paused");
    game.phase = XZ_RIDER_SETUP;
    for (unsigned step = 0; step < 4; ++step) {
        char name[32];
        game.setup_step = step;
        snprintf(name, sizeof(name), "setup-%u", step);
        screenshot(&game, directory, name);
    }
    game.phase = XZ_RIDER_RESULT;
    screenshot(&game, directory, "result");
    game.phase = XZ_RIDER_RIDE;
    wave.valid = 0;
    wave.error = XZ_WAVE_RIDER_NO_ANALYSIS;
    feed(&game, &wave, 870);
    assert(game.phase == XZ_RIDER_SETUP);
    screenshot(&game, directory, "no-data");
    puts("PASS RGB565 guard regions, padded stride, rejected short buffers and changing real-data frames");
}

static double milliseconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    assert(QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency));
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
#endif
}

static int compare_double(const void *a, const void *b)
{
    double left = *(const double *)a, right = *(const double *)b;
    return (left > right) - (left < right);
}

static void benchmark(void)
{
    double render[FRAMES], total[FRAMES], sum = 0, total_sum = 0;
    dense_track();
    struct xz_wave_rider_snapshot wave = sample(1000, 1300);
    struct xz_rider game;
    start(&game, &wave);
    uint32_t first = 0, last = 0;
    for (unsigned i = 0; i < FRAMES; ++i) {
        double begin = milliseconds();
        wave = sample(1000 + i * 3, 1300 + i * 3);
        feed(&game, &wave, wave.position);
        xz_rider_jog(&game, i % 40 < 20 ? 2.0f : -2.0f, now);
        if (!(i % 25)) xz_rider_key(&game, XZ_RIDER_CUE, now);
        double drawing = milliseconds();
        assert(xz_rider_render(&game, pixels, WIDTH * HEIGHT, WIDTH));
        double done = milliseconds();
        render[i] = done - drawing;
        total[i] = done - begin;
        sum += render[i];
        total_sum += total[i];
        if (!i) first = pixel_hash(pixels, WIDTH * HEIGHT);
        if (i == FRAMES - 1) last = pixel_hash(pixels, WIDTH * HEIGHT);
    }
    assert(first != last);
    qsort(render, FRAMES, sizeof(*render), compare_double);
    qsort(total, FRAMES, sizeof(*total), compare_double);
#ifdef _WIN32
    const char *timer = "windows_query_performance_counter_wall_ms";
#else
    const char *timer = "process_cpu_ms";
#endif
    printf("BENCH_JSON {\"frames\":%u,\"timer\":\"%s\",\"fixture\":\"advancing_dense_stems\","
           "\"render_mean_ms\":%.4f,\"render_p95_ms\":%.4f,\"render_worst_ms\":%.4f,"
           "\"feed_step_render_mean_ms\":%.4f,\"feed_step_render_p95_ms\":%.4f,\"feed_step_render_worst_ms\":%.4f}\n",
           (unsigned)FRAMES, timer, sum / FRAMES, render[(FRAMES * 95 - 1) / 100], render[FRAMES - 1],
           total_sum / FRAMES, total[(FRAMES * 95 - 1) / 100], total[FRAMES - 1]);
}

int main(int argc, char **argv)
{
    silence_and_roles();
    window_identity();
    lifecycle();
    hop_scoring();
    track_change_after_gap();
    track_end();
    rendering(argc > 1 ? argv[1] : NULL);
    benchmark();
    return 0;
}
