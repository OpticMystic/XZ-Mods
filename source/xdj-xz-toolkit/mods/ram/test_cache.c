/* SPDX-License-Identifier: MIT */
/* xz_ram_copy_all: the order of the pieces, and what it leaves alone while a track loads elsewhere. */
#define _GNU_SOURCE
#ifdef NDEBUG
#error RAM acceptance requires active assertions
#endif
#include "cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { PIECE = XZ_RAM_COPY_PIECE, PIECES = 6, BYTES = 5 * PIECE + 1000 };
static unsigned char source[BYTES], copied[BYTES];
static uint32_t order[PIECES], window_at[PIECES], pieces_read, reads, most, progress_last, progress_calls;
static uint32_t iterations, read_iteration, window, cancel_at;
static uint32_t (*place)(void);
static uint32_t lead_bytes;

/* The rule of ram/budget.h as the copy sees it. This test owns it: the window closes once the copy has stood
   back five times in a row. */
int xz_ram_track_load_window(void) { return (int)window; }
static int cancelled(void *context) {
    (void)context;
    ++iterations;
    if (window && iterations - read_iteration > 5) window = 0;
    return cancel_at && iterations >= cancel_at;
}
static int read_at(void *context, uint32_t offset, void *out, uint32_t count) {
    (void)context;
    assert(offset < BYTES && count && count <= BYTES - offset && count <= PIECE);
    if (!(offset % PIECE)) { assert(pieces_read < PIECES); order[pieces_read] = offset / PIECE; window_at[pieces_read++] = window; }
    if (most && count > most) count = most;
    memcpy(out, source + offset, count);
    ++reads; read_iteration = iterations;
    return (int)count;
}
static void progress(void *context, uint32_t done) {
    (void)context;
    assert(done > progress_last && done <= BYTES);
    progress_last = done; ++progress_calls;
}
static uint32_t wanted(void *context) { (void)context; return place(); }
static uint32_t lead(void *context) { (void)context; return lead_bytes; }
static uint32_t nowhere(void) { return UINT32_MAX; }
static uint32_t past_the_end(void) { return BYTES; }
static uint32_t in_piece_three(void) { return 3 * PIECE + 77; }
static uint32_t four_then_one(void) { return pieces_read < 2 ? 4 * PIECE : PIECE + 1; }
static uint32_t in_piece_two(void) { return 2 * PIECE + 5; }

static int run(uint32_t (*where)(void), uint32_t lead_now, uint32_t window_now, uint32_t most_now, uint32_t cancel_now) {
    memset(copied, 0, sizeof(copied)); memset(order, 0xff, sizeof(order)); memset(window_at, 0, sizeof(window_at));
    pieces_read = reads = progress_last = progress_calls = iterations = read_iteration = 0;
    place = where; lead_bytes = lead_now; window = window_now; most = most_now; cancel_at = cancel_now;
    struct xz_ram_copy copy = {NULL, read_at, cancelled, progress, where ? wanted : NULL, where ? lead : NULL};
    return xz_ram_copy_all(copied, BYTES, &copy);
}
static void whole(const uint32_t *expected) {
    assert(pieces_read == PIECES && !memcmp(copied, source, BYTES) && progress_last == BYTES);
    for (unsigned i = 0; i < PIECES; ++i) assert(order[i] == expected[i]);
}
int main(void) {
    for (uint32_t i = 0; i < BYTES; ++i) source[i] = (unsigned char)(i * 131u + (i >> 9));
    static const uint32_t plain[PIECES] = {0, 1, 2, 3, 4, 5}, from_three[PIECES] = {3, 4, 5, 0, 1, 2},
        moved[PIECES] = {4, 5, 1, 2, 3, 0}, from_two[PIECES] = {2, 3, 4, 5, 0, 1};
    /* No reader named: start to end, one read and one progress call a piece, as before. */
    assert(!run(NULL, 0, 0, 0, 0)); whole(plain); assert(reads == PIECES && progress_calls == PIECES);
    /* A source that returns less than asked: every piece is still completed before the next. */
    assert(!run(NULL, 0, 0, 10000, 0)); whole(plain); assert(reads > PIECES && progress_calls == reads);
    /* A reader that reads nothing, or past the end, changes nothing. */
    assert(!run(nowhere, PIECE, 0, 0, 0)); whole(plain);
    assert(!run(past_the_end, PIECE, 0, 0, 0)); whole(plain);
    /* The piece the reader is in comes first, then on to the end, then round to the start. */
    assert(!run(in_piece_three, 0, 0, 0, 0)); whole(from_three);
    /* The reader jumps while the copy runs: the copy follows it and still misses nothing. */
    assert(!run(four_then_one, 0, 0, 0, 0)); whole(moved);
    /* A track loads elsewhere and the reader is not playing: nothing is read until the load is over. */
    assert(!run(in_piece_two, 0, 1, 0, 0)); whole(from_two);
    for (unsigned i = 0; i < PIECES; ++i) assert(!window_at[i]);
    assert(iterations > PIECES + 5);
    /* The same with a playing reader and a lead of two pieces: the pieces that start inside the lead (2, 3 and
       4: the reader stands five bytes into piece 2) are read during the load, the rest after it. */
    assert(!run(in_piece_two, 2 * PIECE, 1, 0, 0)); whole(from_two);
    assert(window_at[0] && window_at[1] && window_at[2] && !window_at[3] && !window_at[4] && !window_at[5]);
    /* Cancelled while it stands back, and cancelled between two parts of one piece. */
    assert(run(in_piece_two, 0, 1, 0, 3) == -2 && !pieces_read);
    assert(run(NULL, 0, 0, 10000, 2) == -2 && reads == 1);
    /* The limits are still checked first. */
    struct xz_ram_copy copy = {NULL, read_at, cancelled, progress, NULL, NULL};
    assert(xz_ram_copy_all(copied, 0, &copy) == -1 && xz_ram_copy_all(NULL, BYTES, &copy) == -1);
    assert(xz_ram_copy_all(copied, XZ_RAM_TRACK_LIMIT + 1u, &copy) == -1);
    puts("PASS RAM copy order: start to end without a reader; the reader's piece first, wrapping; follows a jump; stands back for a load except inside a playing reader's lead; cancel; limits");
    return 0;
}
