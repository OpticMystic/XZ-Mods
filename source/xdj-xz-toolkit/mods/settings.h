/* SPDX-License-Identifier: MIT */
#ifndef XZ_SETTINGS_H
#define XZ_SETTINGS_H
#include <stddef.h>
#include "ui/beat_jump.h"
enum xz_takeover_assign {
    XZ_TAKEOVER_LINK = 0,
    XZ_TAKEOVER_REKORDBOX = 1,
    XZ_TAKEOVER_ONSCREEN = 2,
    XZ_TAKEOVER_COUNT = 3
};
struct xz_settings {
    int stems, gate, smart, theme, stem_page, shift_pages, pad_feedback, shift_keysync;
    int fb_takeover, takeover_assign, spare_eq, stem_bank;
    struct xz_jump_settings jump;
    int stems_overlay;
    int wave_mode; /* 0 stock, 1 3-band, 2 stems */
};
void xz_settings_default(struct xz_settings *);
int xz_settings_parse(const char *, struct xz_settings *);
int xz_settings_format(const struct xz_settings *, char *, size_t);
/* These run on the settings worker, never the audio or input callback. */
int xz_settings_load(const char *usb, struct xz_settings *);
int xz_settings_save(const char *usb, const struct xz_settings *);
#endif
