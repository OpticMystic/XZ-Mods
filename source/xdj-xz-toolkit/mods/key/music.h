/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_MUSIC_H
#define XZ_KEY_MUSIC_H
#include <stddef.h>
#include <stdint.h>

enum xz_key_format { XZ_KEY_CLASSIC, XZ_KEY_CAMELOT };
enum xz_key_match { XZ_KEY_UNKNOWN, XZ_KEY_OTHER, XZ_KEY_DETUNED, XZ_KEY_COMPATIBLE };
struct xz_music_key { int root, minor; };
struct xz_key_snapshot {
    struct xz_music_key original;
    uint32_t player, reader, epoch;
    double tempo_ratio;
    int loaded, master, master_tempo, format;
};
struct xz_playing_key { struct xz_music_key key; int cents, shift_cents; };
int xz_music_key_parse(const char *, struct xz_music_key *);
void xz_music_key_format(struct xz_music_key, int format, char out[8]);
int xz_key_playing(const struct xz_key_snapshot *, int shift_cents, struct xz_playing_key *);
int xz_key_sync_cents(const struct xz_key_snapshot *source,
    const struct xz_playing_key *master, int *cents);
enum xz_key_match xz_key_match(struct xz_playing_key candidate, struct xz_playing_key master);
void xz_key_readout(const struct xz_key_snapshot *, int shift_cents, char *out, size_t size);
/* Prefix a display copy. Neither tags nor the USB database are changed. */
size_t xz_key_prefix_utf16(uint16_t *out, size_t capacity,
    const uint16_t *title, size_t length, struct xz_music_key key, int format);
#endif
