/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
/* Read-only view of the stock MASTER REC recorder (firmware 1.26). Evidence and offsets: recorder/SEAM.md. */
#include "recorder_state.h"
#include <string.h>

/* Code windows that encode every offset used below. A mismatch means another firmware. */
static const struct { uint32_t address; uint8_t n; uint8_t bytes[36]; } guards[] = {
    /* PlayEngine::getRecordingStatus: ldr r0,[r0,#0x18] ... */
    {0x4c0d0u,16,{0x18,0x00,0x90,0xe5,0x00,0x00,0x50,0xe3,0x1e,0xff,0x2f,0x01,0x97,0x91,0x00,0xea}},
    /* RecInput::getRecordingStatus: ldr r3,[r0,#8]; ldrb r0,[r3,#0x9c]; bx lr */
    {0x70740u,12,{0x08,0x30,0x90,0xe5,0x9c,0x00,0xd3,0xe5,0x1e,0xff,0x2f,0xe1}},
    /* WavWriter::executeSending: budget +0xb8, frames written +0x7c */
    {0x7e0e8u,36,{0xd8,0x2b,0xc4,0xe1,0x86,0x60,0xa0,0xe1,0x7c,0x10,0x94,0xe5,0x01,0x00,0xa0,0xe3,0x06,0x20,0x52,0xe0,
                  0x00,0x30,0xc3,0xe2,0xf8,0x2b,0xc4,0xe1,0x05,0x10,0x81,0xe0,0x7c,0x10,0x84,0xe5}},
    /* WavWriter::fixNextFileName: file number +0xa8 */
    {0x7e510u,12,{0xa8,0x50,0x90,0xe5,0x01,0x50,0x85,0xe2,0xa8,0x50,0x80,0xe5}},
    /* WavWriter::fixNextFileName: final path String +0x8c */
    {0x7e670u,12,{0x24,0x10,0x8d,0xe2,0x8c,0x00,0x84,0xe2,0xc5,0x27,0x0b,0xeb}},
    /* WavWriter::run msg 3: Compressor* +0x84, fill +0x10 */
    {0x7eed4u,24,{0x84,0x00,0x94,0xe5,0x1c,0x20,0x95,0xe5,0x10,0x30,0x90,0xe5,0x03,0x30,0x82,0xe0,0xaf,0x20,0x02,0xe3,
                  0x02,0x00,0x53,0xe1}},
    /* Compressor::stockData (audio thread): fill +0x10 against the 1,000,000-frame ring, write index +0xc */
    {0x99a0cu,36,{0xf8,0x40,0x2d,0xe9,0x02,0x50,0xa0,0xe1,0x10,0x20,0x90,0xe5,0x3d,0x39,0x65,0xe2,0x09,0x3d,0x83,0xe2,
                  0x00,0x40,0xa0,0xe1,0x02,0x00,0x53,0xe1,0x01,0x60,0xa0,0xe1,0x1d,0x00,0x00,0xda}},
    /* Compressor::resetParams: write index +0xc = 0, fill +0x10 = 0, read index +8 = 999824 (-176) */
    {0x99abcu,36,{0x00,0x30,0xa0,0xe3,0x90,0x21,0x04,0xe3,0x0c,0x30,0x80,0xe5,0x0f,0x20,0x40,0xe3,0x10,0x30,0x80,0xe5,
                  0x01,0x30,0xa0,0xe3,0x08,0x20,0x80,0xe5,0x58,0x30,0xc0,0xe5,0x1e,0xff,0x2f,0xe1}},
};

#define W_VTABLE     0x00u
#define W_VTABLE2    0x3cu
#define W_WRITTEN    0x7cu
#define W_COMP       0x84u
#define W_PATH       0x8cu
#define W_STATE      0x98u
#define W_RECORDING  0x9cu
#define W_FILE_NO    0xa8u
#define W_PENDING    0x138u
#define COMP_READ    0x08u   /* writer thread's read index into the ring */
#define COMP_WRITE   0x0cu   /* audio thread's write index */
#define COMP_FILL    0x10u   /* frames announced to the writer and not yet consumed */

static uint32_t u32(const unsigned char *b, unsigned off) { uint32_t v; memcpy(&v, b + off, 4); return v; }
static int32_t i32(const unsigned char *b, unsigned off) { int32_t v; memcpy(&v, b + off, 4); return v; }
static int plausible(uint32_t p, uint32_t span)
{
    return p >= 0x10000u && !(p & 3u) && (uint64_t)p + span <= UINT32_MAX;
}

/* 1 every window matches; 0 a window could not be read (try again later); -1 read and different: another firmware. */
static int check_abi(xz126_recorder_read_fn read, void *ctx)
{
    unsigned char got[36];
    for (unsigned i = 0; i < sizeof guards / sizeof guards[0]; ++i) {
        if (read(ctx, guards[i].address, got, guards[i].n)) return 0;
        if (memcmp(got, guards[i].bytes, guards[i].n)) return -1;
    }
    return 1;
}

/* Append code point c as UTF-8; 0 when it does not fit or is not a valid scalar. */
static int put_utf8(char *out, size_t cap, size_t *len, uint32_t c)
{
    unsigned char b[4]; size_t n;
    if (c == 0 || (c >= 0xd800u && c <= 0xdfffu) || c > 0x10ffffu) return 0;
    if (c < 0x80u) { b[0] = (unsigned char)c; n = 1; }
    else if (c < 0x800u) { b[0] = (unsigned char)(0xc0u | c >> 6); b[1] = (unsigned char)(0x80u | (c & 0x3fu)); n = 2; }
    else if (c < 0x10000u) { b[0] = (unsigned char)(0xe0u | c >> 12); b[1] = (unsigned char)(0x80u | (c >> 6 & 0x3fu));
                             b[2] = (unsigned char)(0x80u | (c & 0x3fu)); n = 3; }
    else { b[0] = (unsigned char)(0xf0u | c >> 18); b[1] = (unsigned char)(0x80u | (c >> 12 & 0x3fu));
           b[2] = (unsigned char)(0x80u | (c >> 6 & 0x3fu)); b[3] = (unsigned char)(0x80u | (c & 0x3fu)); n = 4; }
    if (*len + n + 1 > cap) return 0;
    memcpy(out + *len, b, n); *len += n; out[*len] = 0;
    return 1;
}

/* "<...>/PIONEER REC/RECnnn.WAV" with nnn == file_no. */
static int path_matches(const char *p, int32_t file_no)
{
    static const char dir[] = "/PIONEER REC/REC";
    size_t len = strlen(p), d = sizeof dir - 1;
    if (file_no < 1 || file_no > 999 || len < d + 7 || p[0] != '/') return 0;
    const char *tail = p + len - (d + 7);
    if (memcmp(tail, dir, d) || memcmp(tail + d + 3, ".WAV", 4)) return 0;
    int n = 0;
    for (unsigned i = 0; i < 3; ++i) {
        char c = tail[d + i];
        if (c < '0' || c > '9') return 0;
        n = n * 10 + (c - '0');
    }
    return n == file_no;
}

/* Copy the juce::String (UTF-32 text, {refcount, capacity} at text-8) at writer+0x8c. The writer thread may swap
 * the pointer (String::toUTF8 reallocates), so the text counts only if the pointer is unchanged afterwards. The
 * capacity is only a bound: whether this JUCE counts it in units or bytes is not proven, so reads never cross a
 * 4 KiB page boundary in one call and stop at the terminator (a freed or short buffer then fails cleanly).
 * The terminator is not counted in the capacity: a path that fills its buffer has it in the unit after
 * (the unit showed refcount 0, capacity 39 for a 39-character path), so one unit more than the capacity is read. */
static int read_path(xz126_recorder_read_fn read, void *ctx, uint32_t writer, uint32_t text,
                     char *out, size_t cap)
{
    uint32_t holder[2], units[64], again;
    size_t len = 0;
    out[0] = 0;
    if (!plausible(text, 1024) || text < 0x10008u || read(ctx, text - 8, holder, sizeof holder)) return 0;
    uint32_t capacity = holder[1];
    if (capacity < 2 || capacity > 4096) return 0;
    uint32_t limit = (capacity < 255 ? capacity : 255) + 1u;
    for (uint32_t at = 0; at < limit;) {
        uint32_t address = text + at * 4u, page_left = (0x1000u - (address & 0xfffu)) / 4u;
        uint32_t chunk = limit - at < 64 ? limit - at : 64;
        if (chunk > page_left) chunk = page_left;
        if (read(ctx, address, units, chunk * 4u)) return 0;
        for (uint32_t i = 0; i < chunk; ++i) {
            if (!units[i]) goto done;
            if (!put_utf8(out, cap, &len, units[i])) { out[0] = 0; return 0; }
        }
        at += chunk;
    }
    out[0] = 0;
    return 0;  /* no terminator right after the capacity either: not a string we understand */
done:
    if (read(ctx, writer + W_PATH, &again, 4) || again != text) { out[0] = 0; return 0; }
    return 1;
}

static void unknown(struct xz126_recorder_tracker *t, struct xz_recorder_state *out, int reason)
{
    memset(out, 0, sizeof *out);
    out->generation = t ? t->generation : 0;
    if (t) { t->last_reason = reason; t->backlog_known = 0; }
}

enum { PENDING_NONE, PENDING_START, PENDING_CLOSE };

int xz126_recorder_poll(xz126_recorder_read_fn read, void *ctx,
                        struct xz126_recorder_tracker *t, struct xz_recorder_state *out)
{
    if (!out) return XZ126_REC_NO_READER;
    if (!read || !t) { unknown(t, out, XZ126_REC_NO_READER); return XZ126_REC_NO_READER; }
    if (t->abi_refused) { unknown(t, out, XZ126_REC_BAD_ABI); return XZ126_REC_BAD_ABI; }
    if (!t->abi_checked) {
        int abi = check_abi(read, ctx);
        if (abi <= 0) { if (abi < 0) t->abi_refused = 1; unknown(t, out, XZ126_REC_BAD_ABI); return XZ126_REC_BAD_ABI; }
        t->abi_checked = 1;
    }
    uint32_t engine = 0, input = 0, writer = 0, again = 0;
    if (read(ctx, XZ126_REC_PLAY_ENGINE_CELL, &engine, 4) || !plausible(engine, 0x3c)) {
        unknown(t, out, XZ126_REC_NO_ENGINE); return XZ126_REC_NO_ENGINE;
    }
    if (read(ctx, engine + XZ126_REC_ENGINE_INPUT, &input, 4) || !plausible(input, 12) ||
        read(ctx, input + XZ126_REC_INPUT_WRITER, &writer, 4)) {
        unknown(t, out, XZ126_REC_NO_INPUT); return XZ126_REC_NO_INPUT;
    }
    if (!plausible(writer, XZ126_REC_WRITER_SIZE)) { unknown(t, out, XZ126_REC_BAD_WRITER); return XZ126_REC_BAD_WRITER; }

    unsigned char a[XZ126_REC_WRITER_SIZE], b[XZ126_REC_WRITER_SIZE];
    int stable = 0;
    for (unsigned attempt = 0; attempt < 3 && !stable; ++attempt) {
        if (read(ctx, writer, a, sizeof a) || read(ctx, writer, b, sizeof b)) {
            unknown(t, out, XZ126_REC_BAD_WRITER); return XZ126_REC_BAD_WRITER;
        }
        if (u32(a, W_VTABLE) != XZ126_REC_WRITER_VTABLE || u32(a, W_VTABLE2) != XZ126_REC_WRITER_VTABLE2 ||
            u32(b, W_VTABLE) != XZ126_REC_WRITER_VTABLE || u32(b, W_VTABLE2) != XZ126_REC_WRITER_VTABLE2) {
            unknown(t, out, XZ126_REC_BAD_WRITER); return XZ126_REC_BAD_WRITER;
        }
        stable = a[W_RECORDING] == b[W_RECORDING] && u32(a, W_STATE) == u32(b, W_STATE) &&
                 i32(a, W_FILE_NO) == i32(b, W_FILE_NO) && u32(a, W_PATH) == u32(b, W_PATH);
    }
    if (!stable) { unknown(t, out, XZ126_REC_UNSTABLE); return XZ126_REC_UNSTABLE; }
    /* The chain must still lead to the same objects after the snapshot (an object replaced mid-read). */
    if (read(ctx, engine + XZ126_REC_ENGINE_INPUT, &again, 4) || again != input ||
        read(ctx, input + XZ126_REC_INPUT_WRITER, &again, 4) || again != writer) {
        unknown(t, out, XZ126_REC_UNSTABLE); return XZ126_REC_UNSTABLE;
    }

    int recording = b[W_RECORDING] ? 1 : 0;
    int32_t file_no = i32(b, W_FILE_NO);
    int32_t written = i32(b, W_WRITTEN), pending = i32(b, W_PENDING);
    if (written < 0 || written > XZ126_REC_MAX_FILE_FRAMES) written = 0;
    if (pending < 0 || pending > XZ126_REC_RING_FRAMES) pending = 0;

    /* Windows inside the writer thread (static, stock/rbp):
     * - START, WavWriter::run 0x7edc8: the recording flag is set before fixNextFileName advances the file number
     *   and reassigns the path. Until then the number and the path still name the PREVIOUS file, which exists.
     * - TRACK MARK / 3-hour split, fileRenew 0x7e9f8..0x7ea0c, and stop, eventRecExit 0x7df98..0x7dfa4: the frames
     *   written to the file fall to 0 before the number advances (or the flag falls).
     * A poll inside such a window is held: START reads as not recording yet, a closing file keeps its last elapsed
     * time. If the same picture lasts XZ126_REC_SETTLE_MS it is real and accepted (a stick swapped between two polls
     * can reuse a file number; a file renewal that failed keeps the old number). */
    int kind = PENDING_NONE;
    if (recording && t->seen && !t->was_recording && file_no == t->file_no) kind = PENDING_START;
    else if (recording && t->seen && t->was_recording && file_no == t->file_no && written < t->written) kind = PENDING_CLOSE;
    if (kind != PENDING_NONE) {
        if (t->pending != kind) { t->pending = kind; t->pending_ms = t->now_ms; }
        if ((uint32_t)(t->now_ms - t->pending_ms) < XZ126_REC_SETTLE_MS) {
            memset(out, 0, sizeof *out);
            out->known = 1;
            out->generation = t->generation;
            t->last_reason = XZ126_REC_OK;
            if (kind == PENDING_CLOSE) {
                out->recording = 1;
                out->elapsed_ms = t->elapsed_ms;
                if (t->path[0] && t->path_no == file_no) memcpy(out->file, t->path, sizeof out->file);
            }
            return XZ126_REC_OK;
        }
    }
    t->pending = PENDING_NONE;

    memset(out, 0, sizeof *out);
    out->known = 1;
    out->recording = recording;
    int new_file = recording && (!t->seen || !t->was_recording || file_no != t->file_no);
    /* Generation: a rising recording edge, or a new file number while recording (TRACK MARK, 3-hour split,
     * or a stop and new start between two polls). The first sample taken while already recording counts too. */
    if (new_file) { t->generation++; t->backlog_max = 0; }
    t->seen = 1;
    t->was_recording = recording;
    t->file_no = file_no;
    t->written = recording ? written : 0;
    t->elapsed_ms = 0;
    t->backlog_known = 0;
    out->generation = t->generation;
    t->last_reason = XZ126_REC_OK;
    if (!recording) return XZ126_REC_OK;

    /* Backlog: frames the audio thread has put into the ring that the writer has not taken yet. The stock
     * recorder loses audio silently once this reaches the ring size (1,000,000 frames, 22.7 s). Elapsed: the
     * file position of the audio being captured now = frames written to this file + backlog. If the ring
     * indices cannot be read, fall back to written + announced fill + unannounced frames (lags during a stall). */
    uint32_t comp = u32(b, W_COMP);
    int32_t ring[3] = {0, 0, 0};    /* read, write, fill */
    int64_t backlog = -1;
    if (plausible(comp, 0x60) && !read(ctx, comp + COMP_READ, ring, sizeof ring)) {
        int32_t r = ring[0], w = ring[1];
        if (w >= 0 && w < XZ126_REC_RING_FRAMES && r >= -176 && r < XZ126_REC_RING_FRAMES)
            backlog = (((int64_t)w - r) % XZ126_REC_RING_FRAMES + XZ126_REC_RING_FRAMES) % XZ126_REC_RING_FRAMES;
    }
    uint64_t frames;
    if (backlog >= 0) {
        frames = (uint64_t)written + (uint64_t)backlog;
        t->backlog = (uint32_t)backlog;
        t->backlog_known = 1;
        if (t->backlog > t->backlog_max) t->backlog_max = t->backlog;
    } else {
        int32_t fill = ring[2];
        if (fill < 0 || fill > XZ126_REC_RING_FRAMES) fill = 0;
        frames = (uint64_t)written + (uint64_t)fill + (uint64_t)pending;
        t->backlog = 0;
    }
    out->elapsed_ms = (uint32_t)(frames * 1000u / XZ126_REC_SAMPLE_RATE);
    t->elapsed_ms = out->elapsed_ms;

    /* Path, cached by (pointer, file number). */
    uint32_t text = u32(b, W_PATH);
    if (t->path[0] && t->path_ptr == text && t->path_no == file_no) {
        memcpy(out->file, t->path, sizeof out->file);
        return XZ126_REC_OK;
    }
    char path[256];
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        uint32_t now = text;
        if (attempt && read(ctx, writer + W_PATH, &now, 4)) break;
        if (read_path(read, ctx, writer, now, path, sizeof path) && path_matches(path, file_no)) {
            memcpy(t->path, path, sizeof t->path); t->path_ptr = now; t->path_no = file_no;
            memcpy(out->file, path, sizeof out->file);
            return XZ126_REC_OK;
        }
    }
    t->path[0] = 0; t->path_ptr = 0; t->path_no = 0;
    return XZ126_REC_OK;   /* state known, path not (yet): file stays "" */
}

#ifndef XZ_RECORDER_STATE_NO_RUNTIME
#include "runtime.h"
#include "xz_mutex.h"
#include "clock_compat.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t recorder_mutex = XZ_MUTEX_INITIALIZER;
static struct xz126_recorder_tracker recorder_tracker;
static int recorder_last_result;
static unsigned recorder_results;

static int runtime_read(void *ctx, uint32_t address, void *out, size_t n)
{
    (void)ctx;
    return xz_read_memory(address, out, n);
}
static uint32_t now_ms(void)
{
    struct timespec t;
    if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 0;
    return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}

int xz_recorder_state(struct xz_recorder_state *out)
{
    if (!out) return -1;
    pthread_mutex_lock(&recorder_mutex);
    recorder_tracker.now_ms = now_ms();
    xz126_recorder_poll(runtime_read, NULL, &recorder_tracker, out);
    pthread_mutex_unlock(&recorder_mutex);
    return out->known ? 0 : -1;
}

int xz_recorder_backlog(uint32_t *now_frames, uint32_t *max_frames)
{
    pthread_mutex_lock(&recorder_mutex);
    int ok = recorder_tracker.was_recording && recorder_tracker.last_reason == XZ126_REC_OK && recorder_tracker.backlog_known;
    if (now_frames) *now_frames = ok ? recorder_tracker.backlog : 0;
    if (max_frames) *max_frames = ok ? recorder_tracker.backlog_max : 0;
    pthread_mutex_unlock(&recorder_mutex);
    return ok ? 0 : -1;
}

size_t xz_recorder_status_json(char *out, size_t capacity)
{
    struct xz_recorder_state s;
    uint32_t now = 0, max = 0;
    if (!out || !capacity) return 0;
    xz_recorder_state(&s);
    int backlog = xz_recorder_backlog(&now, &max) == 0;
    pthread_mutex_lock(&recorder_mutex);
    int reason = recorder_tracker.last_reason, file_no = recorder_tracker.file_no;
    pthread_mutex_unlock(&recorder_mutex);
    /* The base name only: path_matches() proved it is RECnnn.WAV, so it needs no escaping. */
    const char *slash = strrchr(s.file, '/'), *name = slash ? slash + 1 : "";
    char backlog_now[16] = "null", backlog_max[16] = "null";
    if (backlog) { snprintf(backlog_now, sizeof backlog_now, "%u", now); snprintf(backlog_max, sizeof backlog_max, "%u", max); }
    int n = snprintf(out, capacity,
                     "{\"known\":%s,\"reason\":%d,\"recording\":%s,\"elapsed_ms\":%u,\"generation\":%u,\"file_no\":%d,"
                     "\"file\":\"%s\",\"backlog_frames\":%s,\"backlog_max_frames\":%s,\"last_result\":%d,\"results\":%u}",
                     s.known ? "true" : "false", reason, s.recording ? "true" : "false", s.elapsed_ms, s.generation,
                     s.known ? file_no : 0, name, backlog_now, backlog_max, xz_recorder_last_result(),
                     __atomic_load_n(&recorder_results, __ATOMIC_RELAXED));
    if (n < 0 || (size_t)n >= capacity) { out[0] = 0; return 0; }
    return (size_t)n;
}

void xz_recorder_note_result(int code)
{
    __atomic_store_n(&recorder_last_result, code, __ATOMIC_RELEASE);
    __atomic_fetch_add(&recorder_results, 1u, __ATOMIC_RELAXED);
}
int xz_recorder_last_result(void) { return __atomic_load_n(&recorder_last_result, __ATOMIC_ACQUIRE); }
#endif
