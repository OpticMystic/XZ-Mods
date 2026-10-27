/* SPDX-License-Identifier: MIT */
#ifndef XZ_SETTINGS_H
#define XZ_SETTINGS_H
#include <stddef.h>
#include "ui/beat_jump.h"
#include "ui/stem_colors.h"
#include "ui/stem_pads.h"
enum xz_browse_column { XZ_BROWSE_TITLE, XZ_BROWSE_ARTIST, XZ_BROWSE_KEY, XZ_BROWSE_BPM, XZ_BROWSE_COLUMN_COUNT };
enum xz_key_format_preference { XZ_KEY_FORMAT_NATIVE, XZ_KEY_FORMAT_CLASSIC, XZ_KEY_FORMAT_CAMELOT };

/* The interface decides who draws the player screen: MODERN is the FLAGSHIP
   shell (its own look, modern_theme), CLASSIC is the stock layout restyled by
   classic_theme. It is the one switch for screen ownership. */
enum xz_interface { XZ_INTERFACE_CLASSIC = 0, XZ_INTERFACE_MODERN = 1 };
#define XZ_CLASSIC_THEME_COUNT 24 /* ORIGINAL..ANALOG; theme 24 FLAGSHIP is the modern interface's look */
#define XZ_MODERN_THEME_COUNT 1   /* STANDARD */

enum xz_takeover_assign {
    XZ_TAKEOVER_LINK = 0,
    XZ_TAKEOVER_REKORDBOX = 1, /* retired: the REKORDBOX key's VJ.TOOLS SCREEN choice replaced it; loads as LINK */
    XZ_TAKEOVER_ONSCREEN = 2,
    XZ_TAKEOVER_COUNT = 3
};
/* What the physical REKORDBOX key does: rekordbox_press for a plain press, rekordbox_shift with either
   deck's SHIFT held. Stored numbers: append, never renumber. STOCK is the key's own action (export mode
   playback over the network). */
enum xz_rekordbox_action {
    XZ_REKORDBOX_MODS = 0,      /* opens and closes MODS (the shell's own MODS screen in MODERN) */
    XZ_REKORDBOX_STOCK = 1,
    XZ_REKORDBOX_INTERFACE = 2, /* switches MODERN <-> CLASSIC */
    XZ_REKORDBOX_VJ_SCREEN = 3, /* switches the VJ.Tools screen */
    XZ_REKORDBOX_STEMS = 4,     /* shows or hides the stem rows */
    XZ_REKORDBOX_NOTHING = 5,
    XZ_REKORDBOX_DECK_VIEW = 6, /* steps DECK VIEW 2 -> 4 (1 2 3 4) -> 4 (3 1 2 4) -> 2 */
    XZ_REKORDBOX_ACTIONS = 7
};
/* DECK VIEW: what the play screen shows. Display only: sound, mixer channels and which deck each side
   controls do not change. Values are ui/layout_cycle.h enum xz_deck_layout. */
enum xz_deck_view { XZ_DECK_VIEW_TWO = 0, XZ_DECK_VIEW_FOUR = 1, XZ_DECK_VIEW_MIXER = 2, XZ_DECK_VIEW_COUNT = 3 };
struct xz_settings {
    /* theme is derived, never chosen: xz_settings_look(ui_interface, classic_theme).
       It keeps its wire slot so older records migrate (24 = MODERN). */
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
    int rekordbox_menu; /* derived on every write: rekordbox_press == MODS, for older runtimes */
    int stem_order; /* xz_stem_orders index: left-to-right stems of the rows and pads A/B/C */
    int ui_interface; /* enum xz_interface */
    int classic_theme; /* 0..XZ_CLASSIC_THEME_COUNT-1, used while CLASSIC */
    int modern_theme; /* 0..XZ_MODERN_THEME_COUNT-1, used while MODERN */
    int rekordbox_press, rekordbox_shift; /* enum xz_rekordbox_action; a record without them migrates (SETTINGS.md) */
    int modern_stems; /* MODERN's stem layout: enum xz_stem_layout or XZ_MODERN_STEMS_PADS (ui/wave_viewport.h) */
    int deck_view; /* enum xz_deck_view; a record without it keeps the current view (2 DECKS by default) */
    int deck_view_button; /* retired: the DECKS button always shows; reads as 1, written as 1 */
};
/* The theme every renderer draws: FLAGSHIP (24) while MODERN, else the classic theme. */
int xz_settings_look(int ui_interface, int classic_theme);
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
