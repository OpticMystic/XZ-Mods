#ifndef XZ_SHELL_SETTINGS_H
#define XZ_SHELL_SETTINGS_H
/* SHORTCUT, and the MODS screen with its UTILITY tab: the model behind the
   settings_screen slot (UTILITY, the device's own menu) and the mods slot
   (the mod tabs).

   MODS screen (XZ_SHELL_UTILITY). A row of tabs under the title: UTILITY
   first, then each tab of xz_shell_mods_model. Held MENU opens it on
   UTILITY; the MODS header button opens it on the first mod tab. The encoder
   starts on the tab row (turn picks a tab, push enters it); inside a tab it
   works the panes (category rail, items, values) as before, BACK steps one
   pane left, then back to the tab row, then closes the screen.

   A TAB_ITEMS tab is an item table of its own, drawn and driven exactly like
   UTILITY; setting one of its items emits XZ_SHELL_ACT_SET_MOD {deck = tab
   index in the mods model, id = item id, value = choice index}. A TAB_PAGE
   tab is a mod page the shell does not draw: one button opens it in the
   runtime's mod panel (XZ_SHELL_ACT_OPEN_MODS, value = page).


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

   An item the runtime cannot write (writable clear, or no
   XZ_SHELL_CAP_SET_SETTING) still shows its value and one line on how to
   change it (notes, or the shell's line): pushing it hands the screen to the
   stock deck (HAND_OFF, value = 1 + item id), where holding MENU opens the
   stock UTILITY.

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

/* count comes first: a zeroed model reads nothing else. notes, when set, is parallel to items: one caps
   line saying how to change an item the runtime cannot write (NULL entries take the shell's line). */
struct xz_shell_settings_model {
 uint32_t count;
 const struct xz_shell_setting_item *items;
 const char *const *notes;
};

enum xz_shell_mods_tab_kind { XZ_SHELL_TAB_ITEMS, XZ_SHELL_TAB_PAGE };
struct xz_shell_mods_tab {
 const char *label;                       /* caps, short: "STEMS", "APPEARANCE" */
 enum xz_shell_mods_tab_kind kind;
 const struct xz_shell_settings_model *items; /* TAB_ITEMS: the tab's items; their categories are its sections */
 const char *const *sections;             /* TAB_ITEMS: section names by category number; NULL = the UTILITY names */
 int section_count;
 int page;                                /* TAB_PAGE: the page OPEN_MODS opens */
 const char *summary;                     /* TAB_PAGE: one line on what the page holds */
};
/* The mod tabs, in order. Built from the mod settings (no hard-coded list in the shell). */
struct xz_shell_mods_model { uint32_t count; const struct xz_shell_mods_tab *tabs; };

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
enum { XZ_SHELL_SETTINGS_ROW_H=38, XZ_SHELL_SETTINGS_ROWS=9, XZ_SHELL_SETTINGS_RAIL_H=48 };
/* The MODS screen's tab row: tab i's rect (44 px touch target), -1 past the last tab. */
struct xz_shell_rect xz_shell_mods_tab_rect(const struct xz_shell_model *, int tab);
/* A TAB_PAGE tab's open button. */
struct xz_shell_rect xz_shell_mods_page_button(void);
/* SHORTCUT: quadrant 0..3 in reading order, and the chip row of slot 0..3 inside it. */
struct xz_shell_rect xz_shell_shortcut_quadrant(int quadrant);
struct xz_shell_rect xz_shell_shortcut_slot(int quadrant, int slot);
/* SHORTCUT's DECK STATUS lamps (top right): deck 0..1, lamp per enum xz_shell_shortcut_lamp. QUANTIZE
   and TIME toggle that deck's setting when the runtime can set it; the others only show. */
enum xz_shell_shortcut_lamp { XZ_SHELL_LAMP_QUANTIZE, XZ_SHELL_LAMP_SYNC, XZ_SHELL_LAMP_SLIP, XZ_SHELL_LAMP_MASTER, XZ_SHELL_LAMP_TIME, XZ_SHELL_LAMP_COUNT };
struct xz_shell_rect xz_shell_shortcut_lamp(int deck, int lamp);
/* SHORTCUT jump tiles under the lamps, as the stock SHORTCUT offers them. */
enum xz_shell_shortcut_jump { XZ_SHELL_JUMP_TRACK, XZ_SHELL_JUMP_PLAYLIST, XZ_SHELL_JUMP_SEARCH, XZ_SHELL_JUMP_MATCHING, XZ_SHELL_JUMP_COUNT };
struct xz_shell_rect xz_shell_shortcut_jump(int jump);
#endif
