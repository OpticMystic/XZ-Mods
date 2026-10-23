#ifndef XZ_SHELL_SETTINGS_H
#define XZ_SHELL_SETTINGS_H
/* SHORTCUT, and the MODS screen with its UTILITY tab: the model behind the
   settings_screen slot (UTILITY, the device's own menu) and the mods slot
   (the mod tabs).

   MODS screen (XZ_SHELL_UTILITY). One rail on the left: UTILITY first, the
   player's own menu (captioned STOCK, a divider under it), then one entry
   per tab of xz_shell_mods_model. The open page fills the right: its items
   in a list where each category (UTILITY's stock categories, or a tab's
   sections) is a header row, and the values pane, as the stock UTILITY
   screen draws them. On UTILITY the title reads UTILITY with the version at
   the right; on a mod page it reads MODS. A TAB_PAGE entry shows a card with
   its open button.

   Keys: short MENU on PLAY, SOURCE, INFO and SHORTCUT opens it on the first
   mod tab (UTILITY when there are none; the list screens keep their MENU
   panel); held MENU opens it on UTILITY, and held again there closes it;
   short MENU on it closes it. The MODS header button opens it on the first
   mod page. The encoder starts on the rail (turn picks a page, push enters
   it); inside a page it works the items and values, encoder hold jumps to
   the next category; BACK steps values, items, then back to the rail, then
   closes the screen.

   A TAB_ITEMS tab is an item table of its own, drawn and driven exactly like
   UTILITY; setting one of its items emits XZ_SHELL_ACT_SET_MOD {deck = tab
   index in the mods model, id = item id, value = choice index}. A TAB_PAGE
   tab is a mod page the shell does not draw: one button opens it in the
   runtime's mod panel (XZ_SHELL_ACT_OPEN_MODS, value = page); the runtime
   no longer publishes any. A TAB_MENU tab is a mod page the runtime
   describes as a menu (model.menu, below): the shell draws it in the same
   three panes and every push becomes XZ_SHELL_ACT_MOD_MENU, so the MODS
   screen is the only menu and the encoder never leaves it.


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
   MODE_LAMPS and QUANTIZE_LAMP are shell-owned too but not added: they
   belong on the runtime's APPEARANCE tab, which applies a pick with
   xz_shell_restore_setting and stores it like the others.
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
 XZ_SHELL_ITEM_USB_COLORS,         /* shell-owned: enum xz_shell_usb_colors */
 XZ_SHELL_ITEM_NEEDLE_LOCK,        /* UNLOCK, LOCK (model.settings.needle_lock) */
 XZ_SHELL_ITEM_MODE_LAMPS,         /* shell-owned: SHOW, HIDE the top bar's mode lamps (shell.h XZ_SHELL_IND_MODE_LAMPS) */
 XZ_SHELL_ITEM_QUANTIZE_LAMP,      /* shell-owned: SHOW, HIDE each deck's QUANTIZE lamp (XZ_SHELL_IND_QUANTIZE) */
 XZ_SHELL_ITEM_BYPASS_BUTTON,      /* shell-owned: SHOW, HIDE the stem rows' BYPASS button (XZ_SHELL_IND_BYPASS) */
 XZ_SHELL_ITEM_WAVE_TITLES,        /* shell-owned: SHOW, HIDE the waveform lane's track title */
 XZ_SHELL_ITEM_ID_END
};
/* Stock UTILITY items the runtime publishes under its own ids (assigned by the runtime, 2026-10-19). The
   shell names them only to describe them in the description panel. */
enum xz_shell_runtime_item {
 XZ_SHELL_ITEM_RT_NEEDLE_LOCK=0x100, XZ_SHELL_ITEM_RT_JOG_DISPLAY_MODE, XZ_SHELL_ITEM_RT_JOG_RING_INDICATOR,
 XZ_SHELL_ITEM_RT_AUTO_PLAY_MODE, XZ_SHELL_ITEM_RT_MASTER_EQUALIZER, XZ_SHELL_ITEM_RT_MASTER_ATTENUATOR,
 XZ_SHELL_ITEM_RT_BOOTH_ATTENUATOR, XZ_SHELL_ITEM_RT_PEAK_LIMITER, XZ_SHELL_ITEM_RT_MIC_OUT_TO_BOOTH,
 XZ_SHELL_ITEM_RT_TALK_OVER_MODE, XZ_SHELL_ITEM_RT_TALK_OVER_LEVEL, XZ_SHELL_ITEM_RT_USB_OUTPUT_LEVEL,
 XZ_SHELL_ITEM_RT_CH3_CONTROL_TONE, XZ_SHELL_ITEM_RT_CH4_CONTROL_TONE, XZ_SHELL_ITEM_RT_MIXER_MIDI_MESSAGE,
 XZ_SHELL_ITEM_RT_PAD_BRIGHTNESS, XZ_SHELL_ITEM_RT_TOUCH_CALIBRATION
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

/* TAB_MENU: a mod page the runtime describes as a menu (struct xz_shell_menu below), drawn and driven by
   the shell; page is its root page id. */
enum xz_shell_mods_tab_kind { XZ_SHELL_TAB_ITEMS, XZ_SHELL_TAB_PAGE, XZ_SHELL_TAB_MENU };
struct xz_shell_mods_tab {
 const char *label;                       /* caps, short: "STEMS", "APPEARANCE" */
 enum xz_shell_mods_tab_kind kind;
 const struct xz_shell_settings_model *items; /* TAB_ITEMS: the tab's items; their categories are its sections */
 const char *const *sections;             /* TAB_ITEMS: section names by category number; NULL = the UTILITY names */
 int section_count;
 int page;                                /* TAB_PAGE: the page OPEN_MODS opens */
 const char *summary;                     /* TAB_PAGE: one line on what the page holds */
};
/* The mod tabs, in order. Built from the mod settings (no hard-coded list in the shell). A TAB_ITEMS
   tab's sections become its rail; a long section name wraps to two lines. */
struct xz_shell_mods_model { uint32_t count; const struct xz_shell_mods_tab *tabs; };

/* MENU tabs. model.menu is the one mod page the runtime has open: its rows, their choices and the
   page's live lines, all copied into the struct (no pointers). It belongs to the MENU tab whose page
   equals menu.root; any other MENU tab shows an empty page until the runtime opens it.

   Rows, in the list with section header rows between them:
     CHOICE  push opens the values pane; push there picks a choice (ACTIVATE row, choice)
     TOGGLE  two choices; push picks the one that is not lit, so one push flips it
     ACTION  push runs it (ACTIVATE row, -1): a setup step, a check, a game, a key shift
     LINK    push opens a deeper page (ACTIVATE); the runtime publishes that page with depth + 1
     PAD     a performance pad: push taps it; on its pad area a touch holds it (PRESS, MOVE, RELEASE)
     KEY     a keyboard key (layout KEYBOARD): drawn as a key grid, push or a tap types it
   An unavailable row still takes the cursor and shows its reason; push asks the runtime anyway (it
   answers with its setup page or a notice), so a missing capability is never a dead control.
   The values pane shows a CHOICE or TOGGLE row's choices, the row's help, then the page's status and
   lines; for the other rows the status, the lines, then the help. A setup flow names its steps
   (steps, step) and the shell draws them as a strip above the list.

   Encoder: on the rail turn picks a tab (a MENU tab sends OPEN), push enters the page on its focus row;
   in the list turn moves, push acts as above, hold jumps to the next section; in the values pane turn
   moves, push picks and returns to the list. BACK: values pane, then one page up (BACK while depth > 0),
   then the rail, then MODS closes. Nothing in a MENU tab hands the screen to another menu. */
enum xz_shell_menu_op {
 XZ_SHELL_MENU_OPEN,      /* value = the tab's root page: open it, history cleared */
 XZ_SHELL_MENU_ACTIVATE,  /* value = row, deck = choice index or -1 */
 XZ_SHELL_MENU_BACK,      /* one page up */
 XZ_SHELL_MENU_PRESS, XZ_SHELL_MENU_MOVE, XZ_SHELL_MENU_RELEASE /* PAD touch: value = row, deck = x * 1024 + y,
                                                                 each 0..1000 across the pad area */
};
enum xz_shell_menu_kind { XZ_SHELL_MENU_CHOICE, XZ_SHELL_MENU_TOGGLE, XZ_SHELL_MENU_ACTION, XZ_SHELL_MENU_LINK,
 XZ_SHELL_MENU_PAD, XZ_SHELL_MENU_KEY };
enum xz_shell_menu_tone { XZ_SHELL_TONE_PLAIN, XZ_SHELL_TONE_DIM, XZ_SHELL_TONE_GOOD, XZ_SHELL_TONE_ALERT, XZ_SHELL_TONE_HEAD };
enum xz_shell_menu_layout { XZ_SHELL_MENU_LIST, XZ_SHELL_MENU_KEYBOARD, XZ_SHELL_MENU_ABOUT };
enum { XZ_SHELL_MENU_ROWS=48, XZ_SHELL_MENU_CHOICES=96, XZ_SHELL_MENU_SECTIONS=8, XZ_SHELL_MENU_LINES=10,
 XZ_SHELL_MENU_STEPS=6 };
/* What a row or choice runs, in the runtime's own terms; the shell never reads it. */
struct xz_shell_menu_target { int16_t kind, index; int8_t deck, how; int16_t value; };
struct xz_shell_menu_choice {
 char label[28];
 uint8_t available, has_rgb;
 uint32_t rgb;                            /* a colour swatch beside the label (0xRRGGBB) when has_rgb */
 struct xz_shell_menu_target target;
};
struct xz_shell_menu_row {
 uint8_t kind, section, available, choice_count;  /* section indexes sections[] */
 int16_t first_choice, current;                   /* choices first_choice..+count; current: the lit one, -1 none */
 int32_t key;                                     /* the same control keeps its key across refreshes */
 char label[40];                                  /* caps */
 char value[28];                                  /* right of the label: the lit choice, ON / OFF, a state */
 char help[176];                                  /* what it does, 1-3 short sentences */
 char reason[88];                                 /* unavailable: why, one line */
 struct xz_shell_menu_target target;
};
struct xz_shell_menu_line { char text[160]; uint8_t tone; };
struct xz_shell_menu {
 int32_t page, root;                      /* the open page and the top page (a MENU tab's page) it lives under */
 uint8_t depth, layout, row_count, choice_count, section_count, line_count, step_count, step;
 int16_t focus_row;                       /* where the cursor starts on this page; -1: the first row */
 uint8_t status_tone;
 char title[32];                          /* the page's name */
 char path[64];                           /* the pages above it, "GENERAL" or "EXTRAS / GAMES" */
 char status[100];                        /* the last result or what the page waits for; empty: none */
 char field[100];                         /* KEYBOARD: the text typed so far */
 char field_label[24];
 char sections[XZ_SHELL_MENU_SECTIONS][28];
 char steps[XZ_SHELL_MENU_STEPS][20];
 struct xz_shell_menu_line line[XZ_SHELL_MENU_LINES];
 struct xz_shell_menu_row row[XZ_SHELL_MENU_ROWS];
 struct xz_shell_menu_choice choice[XZ_SHELL_MENU_CHOICES];
};
/* Opens MODS on tab (an index into the rail: 0 UTILITY, 1.. the mod tabs) with the encoder inside its
   page, as the runtime asks when one of its own controls led to a mod page. Sends nothing. */
void xz_shell_mods_show(struct xz_shell *, const struct xz_shell_model *, int tab);
/* Closes MODS when it is open (the screen under it shows again). */
void xz_shell_mods_close(struct xz_shell *);
/* MENU tab geometry: list row slot i (0 = the top slot under the steps strip), a KEYBOARD row's key or
   button, and the PAD area in the values pane. Zero size when not shown. */
struct xz_shell_rect xz_shell_menu_row_rect(const struct xz_shell *, const struct xz_shell_model *, int row);
struct xz_shell_rect xz_shell_menu_pad_rect(void);
/* The MENU tab row the cursor is on (an index into model.menu rows), -1 when no MENU page shows. */
int xz_shell_menu_cursor(const struct xz_shell *, const struct xz_shell_model *);

/* Descriptions: what a setting does, shown under its values on MODS, as SHORTCUT's title line and at
   the foot of the MY SETTINGS panel. The shell carries its own text for the UTILITY items (by item id,
   then by label); m->help adds or replaces text per scope. scope: XZ_SHELL_HELP_UTILITY for the UTILITY
   page and SHORTCUT, a mod tab's index in m->mods + 1 for that tab, XZ_SHELL_HELP_MY_SETTINGS for the
   stick's stored values. help is 1-3 short plain sentences; choice_help[i] (NULL entries allowed) is one
   line on what choice i does. An item with no text shows no panel. */
enum { XZ_SHELL_HELP_MY_SETTINGS=-1, XZ_SHELL_HELP_UTILITY=0 };
struct xz_shell_item_help {
 int scope, id;
 const char *help;
 int choice_count;
 const char *const *choice_help;
};
struct xz_shell_help_model { uint32_t count; const struct xz_shell_item_help *items; };

/* MY SETTINGS: MENU on BROWSE while the category rail has the encoder (a USB stick picked, no category
   entered) opens a slide-over panel for that stick instead of the list MENU. Rows, top to bottom:
     LOAD TO DECK n     XZ_SHELL_ACT_MY_SETTINGS {id MY_LOAD, value stick, deck = the active deck}; only
                        when the stick has a MY SETTINGS file (on_stick)
     SAVE TO <STICK>    push twice: XZ_SHELL_ACT_MY_SETTINGS {id MY_SAVE, value stick}; only when writable
     DJ PROFILE         one row per profile; push on another profile: DJ_PROFILE {id PROFILE_LOAD, value i};
                        then SAVE TO <ACTIVE NAME>, push twice: DJ_PROFILE {id PROFILE_SAVE, value active}
     WAVEFORM           the shell's own WAVEFORM COLOR, CURRENT POSITION, ZOOM and USB COLORS (the same
                        values SHORTCUT and UTILITY show; push steps to the next choice, SET_SETTING / ZOOM),
                        then the runtime's waveform items (push: SET_MOD {deck waveform_tab, id, value})
     ON THIS STICK      the stick's stored MY SETTINGS with their real choices; push steps a writable one:
                        MY_SETTING_SET {deck stick, id, value}; an unwritable one shows its value only
   Results come back as m->notice: the runtime raises one caps sentence per request, e.g. "LOADED USB 1
   SETTINGS TO DECK 1", "SAVED TO USB 1", "USB 1 IS LOCKED: NOT SAVED", "DJ PROFILE: ANNA". */
enum xz_shell_my_op { XZ_SHELL_MY_LOAD, XZ_SHELL_MY_SAVE };
enum xz_shell_profile_op { XZ_SHELL_PROFILE_LOAD, XZ_SHELL_PROFILE_SAVE };
struct xz_shell_dj_profile {
 const char *name;                        /* caps or mixed, UTF-8; NULL draws "---" */
 int has_accent; uint32_t accent_rgb;     /* the profile's colour, 0xRRGGBB */
 int damaged;                             /* listed but unreadable: shown, cannot load */
};
/* Where a LOAD stands (newer players load MY SETTINGS only onto a paused deck, XDJ-AZ manual p.150):
   READY, NEEDS_PAUSE (LOAD shows "PAUSE DECK n TO LOAD" and asks nothing), APPLYING, DONE or FAILED
   (result says what happened, e.g. "31 APPLIED, 7 NOT ON THIS DECK"). */
enum xz_shell_my_state { XZ_SHELL_MY_READY, XZ_SHELL_MY_NEEDS_PAUSE, XZ_SHELL_MY_APPLYING, XZ_SHELL_MY_DONE,
 XZ_SHELL_MY_FAILED };
struct xz_shell_my_settings_model {
 enum xz_shell_source source;             /* the medium: USB1, USB2, or LINK (link_player set; others: panel shut) */
 const char *source_name;                 /* "USB 1"; NULL draws the shell's name for source */
 int on_stick;                            /* the stick holds a MY SETTINGS file */
 int writable;                            /* the stick can be written (MY_SAVE, MY_SETTING_SET, PROFILE_SAVE) */
 const struct xz_shell_settings_model *stored; /* the stick's MY SETTINGS values; NULL or empty: none read */
 int profile_count;                       /* DJ profiles on the stick (0: the DJ PROFILE group is left off) */
 const struct xz_shell_dj_profile *profiles;
 int profile_active;                      /* index, -1 none */
 int profiles_switchable;                 /* 0: a single legacy profile; it shows, it does not switch */
 const struct xz_shell_settings_model *waveform; /* the runtime's waveform items (stems row layout, ...) */
 int waveform_tab;                        /* their tab index in m->mods, for SET_MOD */
 int link_player;                         /* LINK: the player whose medium this is (1..6) */
 enum xz_shell_my_state state;            /* of the last LOAD */
 const char *result;                      /* DONE / FAILED: one caps line */
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
enum { XZ_SHELL_SETTINGS_ROW_H=38, XZ_SHELL_SETTINGS_ROWS=10, XZ_SHELL_SETTINGS_RAIL_H=48 };
/* The MODS screen's page rail: page i's entry (0 = UTILITY), 48 px tall; zero size past the last page
   or while the rail is scrolled so page i is out of view. */
struct xz_shell_rect xz_shell_mods_tab_rect(const struct xz_shell *, const struct xz_shell_model *, int tab);
/* A TAB_PAGE tab's open button. */
struct xz_shell_rect xz_shell_mods_page_button(void);
/* SHORTCUT: quadrant 0..3 in reading order, and the chip row of slot 0..3 inside it. */
struct xz_shell_rect xz_shell_shortcut_quadrant(int quadrant);
struct xz_shell_rect xz_shell_shortcut_slot(int quadrant, int slot);
/* SHORTCUT's DECK STATUS lamps (top right): deck 0..XZ_SHELL_SHORTCUT_DECKS-1, lamp per enum
   xz_shell_shortcut_lamp. QUANTIZE and TIME toggle that deck's setting when the runtime can set it; the
   others only show. The quadrant holds two decks' rows above the jump tiles. A four-deck model with
   XZ_SHELL_CAP_DECK_SELECT ends each row in a DECK SELECT tile, and the lamps narrow to make room;
   a NULL model is the two-deck view. */
#define XZ_SHELL_SHORTCUT_DECKS 2
enum xz_shell_shortcut_lamp { XZ_SHELL_LAMP_QUANTIZE, XZ_SHELL_LAMP_SYNC, XZ_SHELL_LAMP_SLIP, XZ_SHELL_LAMP_MASTER, XZ_SHELL_LAMP_TIME, XZ_SHELL_LAMP_COUNT };
struct xz_shell_rect xz_shell_shortcut_lamp(const struct xz_shell_model *, int deck, int lamp);
/* DECK SELECT, as stock SHORTCUT offers it while a unit plays four decks: one tile per side (0: DECK 1 / 3,
   1: DECK 2 / 4) showing the deck that side's controls drive. A touch emits XZ_SHELL_ACT_DECK_SELECT
   {deck = side}. Zero size unless the model plays four decks with XZ_SHELL_CAP_DECK_SELECT. */
struct xz_shell_rect xz_shell_shortcut_deck_select(const struct xz_shell_model *, int side);
/* SHORTCUT jump tiles under the lamps, as the stock SHORTCUT offers them. */
enum xz_shell_shortcut_jump { XZ_SHELL_JUMP_TRACK, XZ_SHELL_JUMP_PLAYLIST, XZ_SHELL_JUMP_SEARCH, XZ_SHELL_JUMP_MATCHING, XZ_SHELL_JUMP_COUNT };
struct xz_shell_rect xz_shell_shortcut_jump(int jump);
#endif
