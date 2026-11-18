/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Host test for recorder_state.c against a fake firmware-1.26 address space.
 * Build: cc -std=c11 -Wall -Wextra -DXZ_RECORDER_STATE_NO_RUNTIME recorder_state.c tests/test_recorder_state.c */
#include "../recorder_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

struct region { uint32_t base; uint32_t size; unsigned char *bytes; };
struct fake {
    struct region r[24]; unsigned n;
    unsigned text_reads;            /* reads that touch a string text */
    int swap_on_text;               /* >0: each text read moves writer+0x8c to the other String */
    int flip_recording;             /* flip writer+0x9c after every writer read */
    int fail_writer;
    int fail_guards;                /* guard windows unreadable */
    unsigned input_reads;           /* reads of RecInput+8 */
    unsigned move_writer_at;        /* >0: that read of RecInput+8 and later ones see another writer */
};
static struct fake F;
static const uint32_t ENGINE = 0x01200000, INPUT = 0x01200100, WRITER = 0x01201000, COMP = 0x01202000;
static const uint32_t TEXT_A = 0x01300008, TEXT_B = 0x01301008;

static unsigned char *map(uint32_t base, uint32_t size)
{
    CHECK(F.n < 24);
    F.r[F.n] = (struct region){base, size, calloc(1, size)};
    return F.r[F.n++].bytes;
}
static unsigned char *at(uint32_t address)
{
    for (unsigned i = 0; i < F.n; ++i)
        if (address >= F.r[i].base && address - F.r[i].base < F.r[i].size) return F.r[i].bytes + (address - F.r[i].base);
    return NULL;
}
static void put32(uint32_t address, uint32_t v) { unsigned char *p = at(address); CHECK(p); memcpy(p, &v, 4); }
static uint32_t get32(uint32_t address) { uint32_t v; memcpy(&v, at(address), 4); return v; }

static int fake_read(void *ctx, uint32_t address, void *out, size_t n)
{
    (void)ctx;
    for (unsigned i = 0; i < F.n; ++i) {
        struct region *r = &F.r[i];
        if (address >= r->base && (uint64_t)address + n <= (uint64_t)r->base + r->size) {
            if (F.fail_writer && r->base == WRITER) return -1;
            if (F.fail_guards && address < 0x100000u) return -1;
            if (address == INPUT + 8 && F.move_writer_at && ++F.input_reads >= F.move_writer_at) {
                uint32_t other = WRITER + 0x1000; memcpy(out, &other, 4); return 0;
            }
            memcpy(out, r->bytes + (address - r->base), n);
            if ((address >= TEXT_A && address < TEXT_A + 0x800) || (address >= TEXT_B && address < TEXT_B + 0x800)) {
                F.text_reads++;
                if (F.swap_on_text > 0) { F.swap_on_text--; put32(WRITER + 0x8c, get32(WRITER + 0x8c) == TEXT_A ? TEXT_B : TEXT_A); }
            }
            if (r->base == WRITER && F.flip_recording) r->bytes[0x9c] ^= 1;
            return 0;
        }
    }
    return -1;
}

static void put32(uint32_t address, uint32_t v);
static const struct { uint32_t a; unsigned n; unsigned char b[36]; } code[] = {
    {0x4c0d0u,16,{0x18,0x00,0x90,0xe5,0x00,0x00,0x50,0xe3,0x1e,0xff,0x2f,0x01,0x97,0x91,0x00,0xea}},
    {0x70740u,12,{0x08,0x30,0x90,0xe5,0x9c,0x00,0xd3,0xe5,0x1e,0xff,0x2f,0xe1}},
    {0x7e0e8u,36,{0xd8,0x2b,0xc4,0xe1,0x86,0x60,0xa0,0xe1,0x7c,0x10,0x94,0xe5,0x01,0x00,0xa0,0xe3,0x06,0x20,0x52,0xe0,
                  0x00,0x30,0xc3,0xe2,0xf8,0x2b,0xc4,0xe1,0x05,0x10,0x81,0xe0,0x7c,0x10,0x84,0xe5}},
    {0x7e510u,12,{0xa8,0x50,0x90,0xe5,0x01,0x50,0x85,0xe2,0xa8,0x50,0x80,0xe5}},
    {0x7e670u,12,{0x24,0x10,0x8d,0xe2,0x8c,0x00,0x84,0xe2,0xc5,0x27,0x0b,0xeb}},
    {0x7eed4u,24,{0x84,0x00,0x94,0xe5,0x1c,0x20,0x95,0xe5,0x10,0x30,0x90,0xe5,0x03,0x30,0x82,0xe0,0xaf,0x20,0x02,0xe3,
                  0x02,0x00,0x53,0xe1}},
    {0x99a0cu,36,{0xf8,0x40,0x2d,0xe9,0x02,0x50,0xa0,0xe1,0x10,0x20,0x90,0xe5,0x3d,0x39,0x65,0xe2,0x09,0x3d,0x83,0xe2,
                  0x00,0x40,0xa0,0xe1,0x02,0x00,0x53,0xe1,0x01,0x60,0xa0,0xe1,0x1d,0x00,0x00,0xda}},
    {0x99abcu,36,{0x00,0x30,0xa0,0xe3,0x90,0x21,0x04,0xe3,0x0c,0x30,0x80,0xe5,0x0f,0x20,0x40,0xe3,0x10,0x30,0x80,0xe5,
                  0x01,0x30,0xa0,0xe3,0x08,0x20,0x80,0xe5,0x58,0x30,0xc0,0xe5,0x1e,0xff,0x2f,0xe1}},
};
static void ring(int32_t read_index, int32_t write_index)
{
    put32(COMP + 0x08, (uint32_t)read_index); put32(COMP + 0x0c, (uint32_t)write_index);
}

/* A juce::String holder: {refcount, capacity units} then UTF-32 text. */
static void put_string(uint32_t text, const uint32_t *units, unsigned count, uint32_t capacity)
{
    put32(text - 8, 1); put32(text - 4, capacity);
    for (unsigned i = 0; i < count; ++i) put32(text + 4 * i, units[i]);
    put32(text + 4 * count, 0);
}
static void put_ascii(uint32_t text, const char *s)
{
    uint32_t u[256]; unsigned n = 0;
    while (s[n]) { u[n] = (unsigned char)s[n]; n++; }
    put_string(text, u, n, n + 8);
}

static void build(void)
{
    for (unsigned i = 0; i < F.n; ++i) free(F.r[i].bytes);
    memset(&F, 0, sizeof F);
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; ++i) memcpy(map(code[i].a, code[i].n), code[i].b, code[i].n);
    map(XZ126_REC_PLAY_ENGINE_CELL, 4); put32(XZ126_REC_PLAY_ENGINE_CELL, ENGINE);
    map(ENGINE, 0x3c); put32(ENGINE + 0x18, INPUT);
    map(INPUT, 12); put32(INPUT + 8, WRITER);
    map(WRITER, 0x140);
    put32(WRITER, XZ126_REC_WRITER_VTABLE); put32(WRITER + 0x3c, XZ126_REC_WRITER_VTABLE2);
    put32(WRITER + 0x84, COMP); put32(WRITER + 0x98, 2); put32(WRITER + 0xa8, 6);
    map(COMP, 0x60);
    map(TEXT_A - 8, 0x1000); map(TEXT_B - 8, 0x1000);
    put_ascii(TEXT_A, "/media/usb2/sda1/PIONEER REC/REC006.WAV");
    put32(WRITER + 0x8c, TEXT_A);
}
static void start(int32_t file_no, uint32_t text, const char *path)
{
    unsigned char *w = at(WRITER);
    w[0x9c] = 1; put32(WRITER + 0x98, 0); put32(WRITER + 0xa8, (uint32_t)file_no);
    put_ascii(text, path); put32(WRITER + 0x8c, text);
}

int main(void)
{
    struct xz126_recorder_tracker t; struct xz_recorder_state s;

    /* 1. Idle recorder: known, not recording, generation 0. */
    build(); memset(&t, 0, sizeof t);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.known == 1 && s.recording == 0 && s.generation == 0 && s.elapsed_ms == 0 && s.file[0] == 0);

    /* 2. Start: generation 1, path, elapsed = (written + fill + pending) / 44.1. */
    start(7, TEXT_A, "/media/usb2/sda1/PIONEER REC/REC007.WAV");
    put32(WRITER + 0x7c, 441000); put32(COMP + 0x10, 8192); put32(WRITER + 0x138, 128);
    ring(440824, 440824 + 8496);   /* read index = written - 176 (mod ring); backlog 8496 */
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.known && s.recording && s.generation == 1);
    CHECK(!strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC007.WAV"));
    CHECK(s.elapsed_ms == (uint32_t)((441000ull + 8496) * 1000 / 44100) && t.backlog == 8496);

    /* 2b. Writer stalled: messages queue, fill stops moving, but the ring indices show the real backlog. */
    ring(440824, (440824 + 600000) % 1000000);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(t.backlog == 600000 && t.backlog_max == 600000 && s.elapsed_ms == (uint32_t)((441000ull + 600000) * 1000 / 44100));
    ring(440824, 440824 + 9000);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && t.backlog == 9000 && t.backlog_max == 600000);

    /* 3. Same file again: generation unchanged, path served from cache (no text read). */
    unsigned before = F.text_reads;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.generation == 1 && F.text_reads == before && !strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC007.WAV"));

    /* 4. TRACK MARK split: new number and new String; generation 2; elapsed restarts. */
    start(8, TEXT_B, "/media/usb2/sda1/PIONEER REC/REC008.WAV");
    put32(WRITER + 0x7c, 0); put32(COMP + 0x10, 300); put32(WRITER + 0x138, 0);
    ring(5000, 5300);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.generation == 2 && !strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC008.WAV") && s.elapsed_ms == 6);
    CHECK(t.backlog_max == 300);   /* reset at the new file */

    /* 5. Number already advanced, String not yet reassigned (fixNextFileName order): file "" but state known. */
    put32(WRITER + 0xa8, 9);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.known && s.recording && s.generation == 3 && s.file[0] == 0);

    /* 6. Stop: recording 0, generation kept, file "". */
    at(WRITER)[0x9c] = 0; put32(WRITER + 0x98, 2);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.known && !s.recording && s.generation == 3 && s.file[0] == 0 && s.elapsed_ms == 0);

    /* 7. Restart with a number reused after a stick swap (the idle poll still saw 9): it looks like the START
     *    window at first, so it reads as not recording; once it lasts XZ126_REC_SETTLE_MS it is a real start. */
    start(9, TEXT_A, "/media/usb2/sda1/PIONEER REC/REC009.WAV");
    t.now_ms = 1000;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.known && !s.recording && s.generation == 3);
    t.now_ms = 1000 + XZ126_REC_SETTLE_MS - 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && !s.recording && s.generation == 3);
    t.now_ms = 1000 + XZ126_REC_SETTLE_MS;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 4);
    CHECK(!strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC009.WAV"));

    /* 8. Pointer swapped while the text is read once: the retry reads the new String. */
    build(); memset(&t, 0, sizeof t);
    start(12, TEXT_A, "/media/usb2/sda1/PIONEER REC/REC012.WAV");
    put_ascii(TEXT_B, "/media/usb2/sda1/PIONEER REC/REC012.WAV");
    F.swap_on_text = 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(!strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC012.WAV") && t.path_ptr == TEXT_B);

    /* 9. Pointer keeps changing: state known, file "". */
    build(); memset(&t, 0, sizeof t);
    start(12, TEXT_A, "/media/usb2/sda1/PIONEER REC/REC012.WAV");
    put_ascii(TEXT_B, "/media/usb2/sda1/PIONEER REC/REC012.WAV");
    F.swap_on_text = 1000;
    /* every text read moves the pointer to the other String, so no copy is ever confirmed */
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
    CHECK(s.known && s.recording && s.generation == 1 && s.file[0] == 0);
    F.swap_on_text = 0;

    /* 10. Non-ASCII mount name is converted to UTF-8. */
    build(); memset(&t, 0, sizeof t);
    {
        const char *tail = "/PIONEER REC/REC003.WAV";
        uint32_t u[64]; unsigned n = 0;
        const uint32_t head[] = {'/', 'm', 'e', 'd', 'i', 'a', '/', 0xe9u, 0x266bu, 0x1f3b5u};
        for (unsigned i = 0; i < 10; ++i) u[n++] = head[i];
        for (const char *c = tail; *c; ++c) u[n++] = (unsigned char)*c;
        at(WRITER)[0x9c] = 1; put32(WRITER + 0xa8, 3);
        put_string(TEXT_A, u, n, n + 4); put32(WRITER + 0x8c, TEXT_A);
        CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK);
        CHECK(!strcmp(s.file, "/media/\xc3\xa9\xe2\x99\xab\xf0\x9f\x8e\xb5/PIONEER REC/REC003.WAV"));
    }

    /* 11. A surrogate or a missing terminator is refused. */
    build(); memset(&t, 0, sizeof t);
    { uint32_t bad[] = {'/', 0xd800u, '/'}; at(WRITER)[0x9c] = 1; put_string(TEXT_A, bad, 3, 8); }
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.known && s.file[0] == 0);
    build(); memset(&t, 0, sizeof t); at(WRITER)[0x9c] = 1;
    put32(TEXT_A - 4, 4); for (unsigned i = 0; i < 4; ++i) put32(TEXT_A + 4 * i, 'x');
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.known && s.file[0] == 0);

    /* 12. Other firmware: one guard byte differs. That is final: the guards are not read again. An unreadable
     *     guard window is not final: the next poll tries again. */
    build(); memset(&t, 0, sizeof t); at(0x7e510u)[0] ^= 0xff;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_BAD_ABI && s.known == 0 && t.abi_refused);
    at(0x7e510u)[0] ^= 0xff;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_BAD_ABI && s.known == 0);
    build(); memset(&t, 0, sizeof t); F.fail_guards = 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_BAD_ABI && !s.known && !t.abi_refused);
    F.fail_guards = 0;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.known);

    /* 13. Engine not created, RecInput missing, WavWriter vtables wrong, writer unreadable. */
    build(); memset(&t, 0, sizeof t); put32(XZ126_REC_PLAY_ENGINE_CELL, 0);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_NO_ENGINE && !s.known);
    build(); memset(&t, 0, sizeof t); put32(ENGINE + 0x18, 0);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_NO_INPUT && !s.known);
    build(); memset(&t, 0, sizeof t); put32(WRITER, 0x3d2fa4);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_BAD_WRITER && !s.known);
    build(); memset(&t, 0, sizeof t); put32(INPUT + 8, 0x01201001);
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_BAD_WRITER && !s.known);
    build(); memset(&t, 0, sizeof t); F.fail_writer = 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_BAD_WRITER && !s.known);

    /* 14. The flag flips between every pair of reads: unstable, not known, generation kept. */
    build(); memset(&t, 0, sizeof t); t.generation = 5; F.flip_recording = 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_UNSTABLE && !s.known && s.generation == 5);

    /* 15. Absurd counters are clamped out of elapsed instead of producing hours. */
    build(); memset(&t, 0, sizeof t);
    start(1, TEXT_A, "/media/usb2/sda1/PIONEER REC/REC001.WAV");
    put32(WRITER + 0x7c, 0x7fffffff); put32(COMP + 0x10, (uint32_t)-5); put32(WRITER + 0x138, 44100);
    ring(-500, 1000000);           /* both indices impossible: fall back to fill + unannounced frames */
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.elapsed_ms == 1000);
    ring(-176, 44100 - 176);       /* first block: read index still at -176 */
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.elapsed_ms == 1000 && t.backlog == 44100);

    /* 16. No reader / no tracker. */
    CHECK(xz126_recorder_poll(NULL, NULL, &t, &s) == XZ126_REC_NO_READER && !s.known);
    CHECK(xz126_recorder_poll(fake_read, NULL, NULL, &s) == XZ126_REC_NO_READER && !s.known);

    /* 17. START window (WavWriter::run 0x7edc8): flag set, number and path still the previous file's. Not a
     *     recording yet, no generation, and never the previous file's name; then the new number counts once. */
    build(); memset(&t, 0, sizeof t); t.now_ms = 5000;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && !s.recording && t.file_no == 6);
    at(WRITER)[0x9c] = 1; put32(WRITER + 0x98, 0); t.now_ms += 10;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.known && !s.recording);
    CHECK(s.generation == 0 && s.file[0] == 0 && s.elapsed_ms == 0);
    start(7, TEXT_B, "/media/usb2/sda1/PIONEER REC/REC007.WAV"); ring(999824, 999824); t.now_ms += 10;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 1);
    CHECK(!strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC007.WAV"));

    /* 18. TRACK MARK window (fileRenew 0x7e9f8..0x7ea0c): frames written fall to 0 before the number advances.
     *     The closing file keeps its last elapsed time and name; the next number then starts generation 2. */
    put32(WRITER + 0x7c, 441000); ring(441000 - 176, 441000 - 176 + 4410); t.now_ms += 500;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 1);
    uint32_t before_mark = s.elapsed_ms;
    CHECK(before_mark == (uint32_t)((441000ull + 4410) * 1000 / 44100));
    put32(WRITER + 0x7c, 0); ring(441000 - 176 + 4410, 441000 - 176 + 4500); t.now_ms += 5;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 1);
    CHECK(s.elapsed_ms == before_mark && !strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC007.WAV"));
    start(8, TEXT_A, "/media/usb2/sda1/PIONEER REC/REC008.WAV"); t.now_ms += 5;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 2);
    CHECK(s.elapsed_ms == 2 && !strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC008.WAV"));

    /* 19. Stop window (eventRecExit 0x7df98..0x7dfa4): frames written fall to 0 before the flag. Held, then the
     *     flag falls: not recording, generation kept. */
    put32(WRITER + 0x7c, 88200); t.now_ms += 500;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 2);
    before_mark = s.elapsed_ms;
    put32(WRITER + 0x7c, 0); t.now_ms += 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.elapsed_ms == before_mark);
    at(WRITER)[0x9c] = 0; put32(WRITER + 0x98, 2); t.now_ms += 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && !s.recording && s.generation == 2);

    /* 20. A closing picture that lasts XZ126_REC_SETTLE_MS (a renewal that failed) is accepted as it is. */
    start(9, TEXT_B, "/media/usb2/sda1/PIONEER REC/REC009.WAV"); put32(WRITER + 0x7c, 44100); ring(0, 0); t.now_ms += 500;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 3 && s.elapsed_ms == 1000);
    put32(WRITER + 0x7c, 0); t.now_ms += 1;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.elapsed_ms == 1000);
    t.now_ms += XZ126_REC_SETTLE_MS;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && s.generation == 3 && s.elapsed_ms == 0);

    /* 21. Path text that crosses a 4 KiB page, and text that ends right before an unmapped page although its
     *     holder claims a much larger capacity. */
    build(); memset(&t, 0, sizeof t);
    {
        const uint32_t cross = 0x01400ff0u;   /* 4 units before the page end */
        map(cross - 8, 0x1010); put_ascii(cross, "/media/usb2/sda1/PIONEER REC/REC004.WAV");
        start(4, cross, "/media/usb2/sda1/PIONEER REC/REC004.WAV");
        CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && !strcmp(s.file, "/media/usb2/sda1/PIONEER REC/REC004.WAV"));
        const char *path = "/media/usb2/sda1/PIONEER REC/REC005.WAV";
        uint32_t len = (uint32_t)strlen(path), end = 0x01600000u, text = end - 4 * (len + 1);
        map(text - 8, 4 * (len + 1) + 8);
        start(5, text, path); put32(text - 4, 1024);
        CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && !strcmp(s.file, path));
    }

    /* 22. The chain moves to another WavWriter while it is read: unstable, not known. */
    build(); memset(&t, 0, sizeof t); F.move_writer_at = 2;
    CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_UNSTABLE && !s.known);

    /* 23. A path that fills its buffer exactly: the holder says 39 units for 39 characters and the terminator is the
     *     unit after them. Seen on the unit (r61, 2026-11-16: refcount 0, capacity 39); the list of that recording
     *     never opened because the path read stopped one unit short. */
    build(); memset(&t, 0, sizeof t);
    {
        const char *path = "/media/usb2/sda2/PIONEER REC/REC044.WAV";
        start(44, TEXT_A, path); put32(TEXT_A - 8, 0); put32(TEXT_A - 4, (uint32_t)strlen(path));
        CHECK(xz126_recorder_poll(fake_read, NULL, &t, &s) == XZ126_REC_OK && s.recording && !strcmp(s.file, path));
    }
    puts("recorder_state: 23 groups passed");
    return 0;
}
