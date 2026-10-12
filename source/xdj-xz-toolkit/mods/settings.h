/* SPDX-License-Identifier: MIT */
#ifndef XZ_SETTINGS_H
#define XZ_SETTINGS_H
#include <stddef.h>
#include "ui/beat_jump.h"
#include "ui/stem_colors.h"
#include "ui/stem_pads.h"
enum xz_browse_column { XZ_BROWSE_TITLE, XZ_BROWSE_ARTIST, XZ_BROWSE_KEY, XZ_BROWSE_BPM, XZ_BROWSE_COLUMN_COUNT };
enum xz_key_format_preference { XZ_KEY_FORMAT_NATIVE, XZ_KEY_FORMAT_CLASSIC, XZ_KEY_FORMAT_CAMELOT };

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
    int stems_overlay; /* enum xz_stem_layout, persisted 0 hidden / 1 original below */
    int wave_mode; /* 0 stock, 1 3-band, 2 stems */
    int browse_columns[3], browse_key_prefix, browse_traffic, key_format;
    int stem_colors[3]; /* palette choices in drums, harmonics, vocals order */
    int key_feedback; /* 0 stock countdown, 1 effective-key display */
    int metrics_enabled; /* optional on-device CPU/RAM/SoC top bar */
    int ram_debug; /* optional free-RAM box beside STEMS */
    int rekordbox_menu; /* 1: the REKORDBOX key opens and closes MODS, 0: stock action */
    int stem_order; /* xz_stem_orders index: left-to-right stems of the rows and pads A/B/C */
};
void xz_settings_default(struct xz_settings *);
int xz_settings_parse(const char *, struct xz_settings *);
/* Like parse; *present receives how many leading fields the record wrote.
   Older records stop early, and those absent fields are unset, not Off. */
int xz_settings_parse_present(const char *, struct xz_settings *, unsigned *present);
/* Copies only the record's present fields over the applied settings, so an
   explicit Off is kept and an unset field inherits. -1 leaves current as is. */
int xz_settings_overlay(struct xz_settings *current, const struct xz_settings *record, unsigned present);
int xz_settings_format(const struct xz_settings *, char *, size_t);
/* These run on the settings worker, never the audio or input callback. */
int xz_settings_load(const char *usb, struct xz_settings *);
int xz_settings_load_present(const char *usb, struct xz_settings *, unsigned *present);
int xz_settings_save(const char *usb, const struct xz_settings *);
int xz_settings_save_mount(const char *usb, unsigned long mount_id, const struct xz_settings *);
/* Mounted local USB partition discovery. Worker only; never reads firmware. */
int xz_settings_find_usb(unsigned slot, char out[1024]);
unsigned long xz_settings_mount_id(const char *usb);
#endif
