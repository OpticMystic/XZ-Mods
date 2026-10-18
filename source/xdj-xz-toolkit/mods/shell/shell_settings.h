#ifndef XZ_SHELL_SETTINGS_H
#define XZ_SHELL_SETTINGS_H
/* SHORTCUT and UTILITY: the model behind the settings_screen slot.

   One table of setting items. The runtime lists the items it can read (or
   at least name) in any order; the screens group them by category for
   UTILITY and by a fixed per-id placement for the SHORTCUT quadrants. An
   item the table does not list is simply not shown.

   Item ids extend enum xz_shell_setting, so SET_SETTING carries one id space
   for the whole shell: the ids the play screen already sends keep their
   values. SET_SETTING from these screens is {deck -1, id, value = index into
   the item's choices}. For the shared ids the runtime orders choices like
   the shell.h enum (TIME MODE: REMAIN, TIME; WAVEFORM COLOR: BLUE, RGB,
   3BAND; WAVEFORM CURRENT POSITION: CENTER, LEFT; PLAY MODE: SINGLE,
   CONTINUE).

   A read-only item (writable clear, or no XZ_SHELL_CAP_SET_SETTING) still
   shows and moves like the others, with one sentence saying why it is read only;
   applying a value on it emits HAND_OFF for the screen, so the stock menu can
   make the change.

   Shell-owned display items (WAVE_COLOR, WAVE_POSITION, KEY_DISPLAY,
   WAVE_ZOOM) are always shown and always settable: the shell adds them when
   the table lacks them and overrides their current value with its own state
   when the table lists them. Setting one changes the shell at once and emits
   SET_SETTING for the runtime to store (WAVE_ZOOM emits XZ_SHELL_ACT_ZOOM).
   The shell also adds PLAY MODE, HOT CUE AUTO LOAD (from model.settings)
   and BEAT JUMP and QUANTIZE BEAT VALUE (from the active deck) when the table
   lacks them, read only unless model.settings.writable says otherwise. */
#include "shell.h"

enum xz_shell_item_id {
 XZ_SHELL_ITEM_TIME_MODE=XZ_SHELL_SET_TIME_MODE,
 XZ_SHELL_ITEM_WAVE_COLOR=XZ_SHELL_SET_WAVE_COLOR,
 XZ_SHELL_ITEM_WAVE_POSITION=XZ_SHELL_SET_WAVE_POSITION,
 XZ_SHELL_ITEM_PLAY_MODE=XZ_SHELL_SET_PLAY_MODE,
 XZ_SHELL_ITEM_AUTO_CUE=XZ_SHELL_SET_AUTO_CUE,
 XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD=XZ_SHELL_SET_HOT_CUE_AUTO_LOAD,
 XZ_SHELL_ITEM_QUANTIZE_BEAT=XZ_SHELL_SETTING_COUNT,
 XZ_SHELL_ITEM_BEAT_JUMP_BEAT,
 XZ_SHELL_ITEM_WAVE_PHASE_METER,
 XZ_SHELL_ITEM_HOT_CUE_COLOR,
 XZ_SHELL_ITEM_AUTO_CUE_LEVEL,
 XZ_SHELL_ITEM_VINYL_SPEED_ADJUST,
 XZ_SHELL_ITEM_LOAD_LOCK,
 XZ_SHELL_ITEM_LCD_BRIGHTNESS,
 XZ_SHELL_ITEM_JOG_LCD_BRIGHTNESS,
 XZ_SHELL_ITEM_LANGUAGE,
 XZ_SHELL_ITEM_SCREEN_SAVER,
 XZ_SHELL_ITEM_SLIP_FLASHING,
 XZ_SHELL_ITEM_ON_AIR_DISPLAY,
 XZ_SHELL_ITEM_JOG_RING_BRIGHTNESS,
 XZ_SHELL_ITEM_PLAYER_NUMBER,
 XZ_SHELL_ITEM_MIDI_CHANNEL,
 XZ_SHELL_ITEM_CH_FADER_CURVE,
 XZ_SHELL_ITEM_CROSSFADER_CURVE,
 XZ_SHELL_ITEM_EQ_MODE,            /* EQ / ISOLATOR */
 XZ_SHELL_ITEM_MIC_EQ,
 XZ_SHELL_ITEM_HEADPHONE_CUE_MODE,
 XZ_SHELL_ITEM_AUTO_STANDBY,
 XZ_SHELL_ITEM_VERSION,            /* one choice: the version text */
 XZ_SHELL_ITEM_KEY_DISPLAY,        /* shell-owned: enum xz_shell_key_display */
 XZ_SHELL_ITEM_WAVE_ZOOM,          /* shell-owned: zoom level 0..XZ_SHELL_ZOOM_MAX */
 XZ_SHELL_ITEM_ID_END
};

/* The UTILITY rail, top to bottom. Empty categories are left off the rail. */
enum xz_shell_setting_category {
 XZ_SHELL_SETTING_CAT_DJ_SETTING, XZ_SHELL_SETTING_CAT_DISPLAY_LCD, XZ_SHELL_SETTING_CAT_DISPLAY_INDICATOR, XZ_SHELL_SETTING_CAT_PRO_DJ_LINK,
 XZ_SHELL_SETTING_CAT_MIXER, XZ_SHELL_SETTING_CAT_SYSTEM, XZ_SHELL_SETTING_CAT_INFO, XZ_SHELL_SETTING_CAT_COUNT
};

struct xz_shell_setting_item {
 int id;                              /* enum xz_shell_item_id */
 enum xz_shell_setting_category category;
 const char *label;                   /* caps; NULL draws "---" */
 const char *const *choices;          /* choice_count labels; NULL entries draw "---" */
 int choice_count;
 int current;                         /* index into choices; read only when known */
 int known;                           /* current was really read */
 int writable;                        /* the runtime can set this item */
};

/* count comes first: a zeroed model reads nothing else. */
struct xz_shell_settings_model {
 uint32_t count;
 const struct xz_shell_setting_item *items;
};

/* BACK on UTILITY moves focus one pane left. Returns 1 when it did, 0 when
   the navigation table should handle BACK (focus already on the rail, or any
   other screen). shell.c calls this before its table; see the hand-off note
   in shell_settings.c. */
int xz_shell_settings_back(struct xz_shell *, const struct xz_shell_model *);

/* Geometry shared by both screens, for the runtime's hit tests and the tests. */
enum xz_shell_settings_region {
 XZ_SHELL_SR_BACK_BUTTON, XZ_SHELL_SR_RAIL, XZ_SHELL_SR_ITEMS, XZ_SHELL_SR_VALUES, XZ_SHELL_SR_STATUS
};
struct xz_shell_rect xz_shell_settings_rect(enum xz_shell_settings_region);
enum { XZ_SHELL_SETTINGS_ROW_H=38, XZ_SHELL_SETTINGS_ROWS=10, XZ_SHELL_SETTINGS_RAIL_H=54 };
/* SHORTCUT: quadrant 0..3 in reading order, and the chip row of slot 0..3 inside it. */
struct xz_shell_rect xz_shell_shortcut_quadrant(int quadrant);
struct xz_shell_rect xz_shell_shortcut_slot(int quadrant, int slot);
#endif
