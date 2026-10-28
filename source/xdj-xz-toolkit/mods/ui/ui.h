#ifndef XZ_MOD_UI_H
#define XZ_MOD_UI_H
#include <stddef.h>
#include <stdint.h>
#include "../ram/status.h"
#include "themes.h"
#include "band_colors.h"
#include "stem_colors.h"
#include "beat_jump.h"
#include "../doom/native_bridge.h"
#include "../ota/native_update.h"
#include "../support/native_support.h"
#include "sdk_ui.h"
#include "metrics.h"
#include "../network/ui_types.h"

#define XZ_UI_WIDTH 800
#define XZ_UI_HEIGHT 480
#define XZ_UI_WIDGETS 64
#define XZ_UI_ACTIONS 4

enum xz_ui_page { XZ_UI_STEMS, XZ_UI_XPAD, XZ_UI_SETTINGS, XZ_UI_THEMES, XZ_UI_CONNECTION, XZ_UI_CONTROLS, XZ_UI_BEAT_JUMP, XZ_UI_EXTRAS, XZ_UI_DOOM, XZ_UI_DOOM_SETUP, XZ_UI_DOOM_FILES, XZ_UI_UPDATES, XZ_UI_UPDATE_SETUP, XZ_UI_GAMES, XZ_UI_MYHOUSE_SETUP, XZ_UI_KEY_SETUP, XZ_UI_BROWSE_SETUP, XZ_UI_PC_STEMS, XZ_UI_STEM_COLORS, XZ_UI_APPS, XZ_UI_APPS_SETUP, XZ_UI_RAM_SETUP, XZ_UI_LOCAL_STEM_SETUP, XZ_UI_METRICS_SETUP, XZ_UI_PROFILE_SETUP, XZ_UI_PROFILE_EDIT, XZ_UI_GENERAL, XZ_UI_FEATURE_SETUP, XZ_UI_THEME_PICK, XZ_UI_BUTTONS, XZ_UI_NETWORK, XZ_UI_NETWORK_SETUP, XZ_UI_NETWORK_EDIT, XZ_UI_ABOUT, XZ_UI_SUPPORT,
    /* The MODS sections (the rail, top to bottom), then the STEM PADS picture page. GENERAL, CONTROLS, THEMES
       (APPEARANCE) and SETTINGS (ADVANCED) are retired: opening one opens its replacement (xz_ui_page_current). */
    XZ_UI_HOME, XZ_UI_PLAY, XZ_UI_STEM_SECTION, XZ_UI_SCREEN, XZ_UI_FOUR_DECKS, XZ_UI_CONNECT, XZ_UI_STEM_PADS,
    /* SCREEN > LAMPS AND TITLES: the MODERN play screen's own parts (model.modern_look). MODERN only. */
    XZ_UI_SCREEN_LOOK,
    /* SCREEN > 3-BAND COLOURS: a colour per band (model.band_colors), both interfaces. */
    XZ_UI_BAND_COLORS,
    XZ_UI_PAGE_COUNT };
/* The section rail in order; EXTRAS is the old page of that name. */
enum { XZ_UI_SECTIONS=7 };
extern const enum xz_ui_page xz_ui_sections[XZ_UI_SECTIONS];
const char *xz_ui_section_label(int page);
/* A retired page's replacement, or page itself. */
enum xz_ui_page xz_ui_page_current(enum xz_ui_page page);
enum xz_ui_cap {
    XZ_UI_GATE=1u, XZ_UI_SMART=2u, XZ_UI_PREVIEW=4u, XZ_UI_STEM=8u,
    XZ_UI_GROOVE=16u, XZ_UI_SAMPLE=32u, XZ_UI_THEME=64u, XZ_UI_SERVER=128u,
    XZ_UI_KEY=256u, XZ_UI_HYBRID=512u, XZ_UI_HOTCUE=1024u,
    XZ_UI_VJ_CONNECTION=2048u, XZ_UI_VJ_DISCOVERY=4096u, XZ_UI_KEYSYNC=8192u
};
enum xz_ui_action_kind {
    XZ_UI_NONE, XZ_UI_PANEL, XZ_UI_DECK, XZ_UI_CLOSE, XZ_UI_UNAVAILABLE,
    XZ_UI_ENABLE, XZ_UI_LEVEL, XZ_UI_MUTE, XZ_UI_BYPASS, XZ_UI_GROOVE_PAD,
    XZ_UI_SAMPLE_PAD, XZ_UI_STRIP, XZ_UI_HOLD, XZ_UI_OVERDUB, XZ_UI_VOLUME,
    XZ_UI_SET_THEME, XZ_UI_SERVER_AUTO, XZ_UI_SERVER_ADDRESS, XZ_UI_KEY_SHIFT,
    XZ_UI_KEY_SYNC, XZ_UI_HOTCUE_PAD, XZ_UI_CONNECTION_ENABLE, XZ_UI_DISCOVERY,
    XZ_UI_STEM_PAGE, XZ_UI_SHIFT_PAGES, XZ_UI_PAD_FEEDBACK, XZ_UI_SHIFT_KEYSYNC,
    XZ_UI_TAKEOVER_TOGGLE, XZ_UI_TAKEOVER_ASSIGN, XZ_UI_STEMS_OVERLAY, XZ_UI_STEM_BANK,
    XZ_UI_JUMP_ENABLE, XZ_UI_JUMP_SHIFT, XZ_UI_JUMP_EDIT_PAGE, XZ_UI_JUMP_SIZE, XZ_UI_JUMP_PAIR, XZ_UI_JUMP_STEP, XZ_UI_CONTROL_SECTION, XZ_UI_STEM_PAD_CONFIG,
    XZ_UI_DOOM_START, XZ_UI_DOOM_CHECK, XZ_UI_WAD_SELECT, XZ_UI_WAD_PAGE, XZ_UI_WAD_SCAN, XZ_UI_WAD_CLEAR,
    XZ_UI_UPDATE_CHECK, XZ_UI_UPDATE_INSTALL, XZ_UI_UPDATE_ROLLBACK,
    XZ_UI_ARCADE_OPEN, XZ_UI_WAVE_MODE, XZ_UI_GAME_SELECT, XZ_UI_MYHOUSE_CHECK, XZ_UI_MYHOUSE_START,
    XZ_UI_WAVE_RIDER_OPEN, XZ_UI_KEY_CHECK, XZ_UI_RHYTHM_LAB_OPEN, XZ_UI_GLYPH_RUN_OPEN,
    /* index = slot*4 + column (0 TITLE, 1 ARTIST, 2 KEY, 3 BPM) */
    XZ_UI_BROWSE_COLUMN, XZ_UI_BROWSE_KEY_PREFIX, XZ_UI_BROWSE_TRAFFIC, XZ_UI_KEY_FORMAT,
    /* PC (rekordbox PERFORMANCE over USB) stems. Every action carries .deck 0-1.
     * PC_TOGGLE: index = enum xz_ui_pc_role; acts on PRESS only, RELEASE is a cancel.
     * PC_SETUP: open the PC STEMS page for .deck; index = enum xz_ui_pc_reason.
     * PC_CHECK: index = the setup_step shown when pressed (DONE asks to start over).
     * PC_TEST: send one live DRUMS toggle; index = XZ_UI_PC_DRUMS.
     * PC_CONFIRM: index/value 1 = the user saw DRUMS switch, 0 = they did not.
     * PC_BYPASS is no longer laid out inline (rekordbox has no stem bypass over
     * USB); the value is kept so action numbering stays stable. */
    XZ_UI_PC_TOGGLE, XZ_UI_PC_SETUP, XZ_UI_PC_CHECK, XZ_UI_PC_TEST, XZ_UI_PC_CONFIRM, XZ_UI_PC_BYPASS, XZ_UI_STEM_COLOR, XZ_UI_STEM_COLORS_RESET,
    /* XZ SDK APPLICATIONS browser and SET UP APPS wizard (sdk_ui.h). index is a
     * snapshot row, wizard step or page direction; sdk_generation pins rows. */
    XZ_UI_SDK_RUN, XZ_UI_SDK_SAVE, XZ_UI_SDK_STOP, XZ_UI_SDK_FIX, XZ_UI_SDK_PAGE, XZ_UI_SDK_SCAN, XZ_UI_SDK_AUTOLOAD,
    XZ_UI_SDK_SETUP, XZ_UI_SDK_STEP, XZ_UI_SDK_GOTO, XZ_UI_SDK_CHOICE, XZ_UI_SDK_DONE, XZ_UI_KEY_FEEDBACK, XZ_UI_RAM_DECK, XZ_UI_RAM_REQUEST, XZ_UI_RAM_CANCEL, XZ_UI_RAM_RELEASE, XZ_UI_LOCAL_STEM_SETUP_OPEN, XZ_UI_LOCAL_STEM_SETUP_STEP, XZ_UI_RAM_DEBUG, XZ_UI_BACK,
    /* Retired switch (REKORDBOX_ASSIGN replaced it; nothing lays it out): value 1 sets the REKORDBOX
     * key's press to MODS MENU, 0 to its stock action. */
    XZ_UI_REKORDBOX_KEY, XZ_UI_METRICS_TOGGLE, XZ_UI_METRICS_SETUP_OPEN, XZ_UI_METRICS_CHECK,
    /* DJ Profile. PROFILE is the header chip and opens or closes the list.
     * PROFILE_PICK: index = xz_ui_dj entry; the runtime makes it its stick's active profile.
     * PROFILE_PAGE: index -1/+1 pages the list. PROFILE_CHECK: rescan the USBs now. */
    XZ_UI_PROFILE, XZ_UI_PROFILE_PICK, XZ_UI_PROFILE_PAGE, XZ_UI_PROFILE_CHECK,
    /* DJ Profile page and editor (ui_profile.c).
     * PROFILE_VIEW: index -1/+1 shows the previous or next profile on the DJ PROFILE page.
     * PROFILE_EDIT_OPEN: index = entry to edit, or -1 for a new profile.
     * KEYBOARD: index = the byte typed, or an XZ_UI_KB_* key. Typing stays in the UI.
     * EDIT_STEP: index = the editor step to show. LOGO_PICK: index = image, or XZ_UI_LOGO_KEEP/NONE.
     * LOGO_PAGE: index -1/+1. LOGO_RESCAN: list the USB's images again.
     * PROFILE_SAVE: index = USB slot; the runtime reads the draft from the UI state. */
    XZ_UI_PROFILE_VIEW, XZ_UI_PROFILE_EDIT_OPEN, XZ_UI_KEYBOARD, XZ_UI_EDIT_STEP,
    XZ_UI_LOGO_PICK, XZ_UI_LOGO_PAGE, XZ_UI_LOGO_RESCAN, XZ_UI_PROFILE_SAVE,
    /* Links retain the originating page even when their destination is a rail tab. */
    XZ_UI_FEATURE_LINK, XZ_UI_FEATURE_SETUP_OPEN, XZ_UI_FEATURE_SETUP_STEP,
    /* Push cycles the saved left-to-right stem order (xz_stem_orders). */
    XZ_UI_STEM_ORDER,
    /* APPEARANCE. SET_INTERFACE: index XZ_UI_INTERFACE_*; the one switch for who draws the
     * player screen. SET_THEME: index = classic theme (0..XZ_THEME_FLAGSHIP-1).
     * SET_MODERN_THEME: index = modern theme (STANDARD only for now). */
    XZ_UI_SET_INTERFACE, XZ_UI_SET_MODERN_THEME,
    /* CONTROLS > BUTTONS. What the physical REKORDBOX key does. Widget index = slot *
     * XZ_UI_RB_CHOICES + choice; the action carries index = slot (0 press, 1 SHIFT + press) and
     * value = choice (enum xz_ui_rekordbox). */
    XZ_UI_REKORDBOX_ASSIGN,
    XZ_UI_FOUR_WAVE_ORDER, /* Session-only shared display order; value 0 numeric / 1 mixer. */
    XZ_UI_NETWORK_SETUP_OPEN, XZ_UI_NETWORK_DETECT, XZ_UI_NETWORK_ADAPTER, XZ_UI_NETWORK_PRIORITY,
    XZ_UI_NETWORK_MODE, XZ_UI_NETWORK_FIELD, XZ_UI_NETWORK_KEY, XZ_UI_NETWORK_STEP,
    XZ_UI_NETWORK_APPLY, XZ_UI_NETWORK_REVERT, XZ_UI_NETWORK_CANCEL, XZ_UI_ABOUT_LINK,
    /* REPORT AN ISSUE (ui_support.c), ten slots. STEP: index = the step to show. TOPIC: index = enum
     * xz_support_topic, or -1 NOT SURE. KEY: index = the byte typed, or an XZ_UI_KB_* key. SKIP / NEXT leave the
     * text step without or with the text. SEND and RETRY carry a send (value 1). EDIT goes back to the send step,
     * DONE ends the report. ALERT: REPORT THIS on the dock's error strip (touch only). */
    XZ_UI_SUPPORT_STEP, XZ_UI_SUPPORT_TOPIC, XZ_UI_SUPPORT_KEY, XZ_UI_SUPPORT_SKIP,
    XZ_UI_SUPPORT_NEXT, XZ_UI_SUPPORT_EDIT, XZ_UI_SUPPORT_RETRY, XZ_UI_SUPPORT_SEND,
    XZ_UI_SUPPORT_DONE, XZ_UI_SUPPORT_ALERT,
    /* DECK VIEW (settings deck_view): value 0 2 DECKS, 1 4 DECKS 1 2 3 4, 2 4 DECKS 3 1 2 4. Saved. */
    XZ_UI_DECK_VIEW,
    /* Retired (the DECKS button is always on the play screen): nothing lays it out or runs it. Kept for numbering. */
    XZ_UI_DECK_VIEW_BUTTON,
    /* A MODERN-only play-screen part the shell owns (model.modern_look). Widget index = enum xz_ui_look_item * 8 +
       choice; the action carries index = the item, value = the choice. The runtime hands it to the shell. */
    XZ_UI_SHELL_LOOK,
    /* MODERN's rail: UTILITY, the player's own settings. The runtime closes MODS and shows that screen. */
    XZ_UI_OPEN_UTILITY,
    /* Something the menu explains but cannot change (a fact, a row of the 4 DECKS grid): focusing it explains it,
       pressing it does nothing. A feature that is not built gets no row at all. index = enum xz_ui_info_topic. */
    XZ_UI_INFO,
    /* A HOME tile's SET UP button: index = the section that needs the DJ; it opens that section's fix. */
    XZ_UI_HOME_SETUP,
    /* 4 DECKS > RESCAN: wake the four-deck connector for an immediate check. */
    XZ_UI_ROUTE_RESCAN,
    XZ_UI_BROWSE_PREVIEW, /* MODERN browse tile only; value 0 hidden, 1 shown. Saved. */
    /* 3-BAND COLOURS. BAND_COLOR: widget index = band * XZ_BAND_COLOR_CHOICES + choice; the action carries
       index = enum xz_wave_band, value = the choice. BAND_COLORS_RESET puts all three back to STANDARD. Saved. */
    XZ_UI_BAND_COLOR, XZ_UI_BAND_COLORS_RESET,
    XZ_UI_SHELL_DISPLAY, /* appended: shell item id in index, stored choice in value */
    XZ_UI_SHELL_BROWSE_COLUMNS, /* appended: MODERN column mask in value; zero means automatic */
    XZ_UI_RAM_SETUP_OPEN, XZ_UI_RAM_SETUP_STEP, XZ_UI_RAM_CHOOSE, XZ_UI_RAM_CONNECT, XZ_UI_RAM_MANAGE,
};
/* The REKORDBOX key's choices, numbered as settings.h enum xz_rekordbox_action (stored values). */
enum xz_ui_rekordbox { XZ_UI_RB_MODS, XZ_UI_RB_STOCK, XZ_UI_RB_INTERFACE, XZ_UI_RB_VJ_SCREEN, XZ_UI_RB_STEMS,
    XZ_UI_RB_NOTHING, XZ_UI_RB_DECK_VIEW, XZ_UI_RB_CHOICES };
/* The choices in the order MODS lists them (DECK VIEW before NOTHING). */
extern const int xz_ui_rekordbox_order[XZ_UI_RB_CHOICES];
/* Short caps label and one line on what the button then does; NULL outside 0..XZ_UI_RB_CHOICES-1. */
const char *xz_ui_rekordbox_choice_name(int choice);
const char *xz_ui_rekordbox_choice_help(int choice);
enum { XZ_UI_INTERFACE_CLASSIC=0, XZ_UI_INTERFACE_MODERN=1, XZ_UI_MODERN_THEMES=1 };
enum xz_ui_kb_key { XZ_UI_KB_SHIFT=-1, XZ_UI_KB_SYMBOLS=-2, XZ_UI_KB_DELETE=-3 };
enum xz_ui_edit_step { XZ_UI_EDIT_NAME, XZ_UI_EDIT_LOGO, XZ_UI_EDIT_TAGLINE, XZ_UI_EDIT_REVIEW };
enum { XZ_UI_LOGO_KEEP=-1, XZ_UI_LOGO_NONE=-2 };
enum xz_ui_preview_state { XZ_UI_PREVIEW_NONE, XZ_UI_PREVIEW_LOADING, XZ_UI_PREVIEW_READY, XZ_UI_PREVIEW_FAILED };
#define XZ_UI_DJ_NAME_TYPED 64     /* characters the keyboard lets the DJ type */
#define XZ_UI_DJ_TAGLINE_TYPED 80
/* Which store the onscreen stem row drives. LOCAL is the XZ's own OverCue
 * stems (deck muted/levels/bypass). PC is rekordbox over USB. UNKNOWN shows
 * neutral locked controls; a lost PC link stays PC and never falls back. */
enum xz_ui_stem_source { XZ_UI_SOURCE_LOCAL, XZ_UI_SOURCE_PC, XZ_UI_SOURCE_UNKNOWN };
/* Same order and bit values as pc_control/stems.h. */
enum xz_ui_pc_role { XZ_UI_PC_DRUMS, XZ_UI_PC_INST, XZ_UI_PC_BASS, XZ_UI_PC_VOCAL, XZ_UI_PC_ROLES };
enum xz_ui_pc_cap { XZ_UI_PC_CAP_TOGGLE=1u, XZ_UI_PC_CAP_FEEDBACK=2u };
enum xz_ui_pc_step { XZ_UI_PC_STEP_DETECT, XZ_UI_PC_STEP_MAPPING, XZ_UI_PC_STEP_TEST, XZ_UI_PC_STEP_CONFIRM, XZ_UI_PC_STEP_DONE };
enum xz_ui_pc_reason {
    XZ_UI_PC_WHY_NONE, XZ_UI_PC_WHY_SOURCE, XZ_UI_PC_WHY_LINK, XZ_UI_PC_WHY_UNVERIFIED,
    XZ_UI_PC_WHY_NO_LEVEL, XZ_UI_PC_WHY_NO_BYPASS, XZ_UI_PC_WHY_TAG, XZ_UI_PC_WHY_ROLE
};
/* Runtime snapshot of the PC stem store for one deck. Bits are 1u<<role.
 * active is meaningful only where known has the bit and caps has FEEDBACK:
 * it mirrors rekordbox's lit/unlit stem LED, not a confirmed mute level.
 * reason, when set, replaces the built-in explanation for locked controls. */
struct xz_ui_pc_stems {
    int source, connected, verified, target;
    uint32_t epoch;
    unsigned caps, known, active, pending, available;
    const char *reason;
    int setup_step;
};
/* Why USB (local OverCue) stems on a deck cannot play, or NONE when they can.
 * The runtime fills xz_ui_deck.usb_reason every refresh. UNKNOWN (zero) means
 * the host did not say, and the UI falls back to ready/enabled/error/track. */
enum xz_ui_usb_reason {
    XZ_UI_USB_WHY_UNKNOWN=0, XZ_UI_USB_WHY_NONE, XZ_UI_USB_WHY_ENGINE, XZ_UI_USB_WHY_OFF,
    XZ_UI_USB_WHY_NO_TRACK, XZ_UI_USB_WHY_NOT_PREPARED, XZ_UI_USB_WHY_LOADING,
    XZ_UI_USB_WHY_ALIGNING, XZ_UI_USB_WHY_ERROR
};
enum xz_ui_phase { XZ_UI_PRESS, XZ_UI_MOVE, XZ_UI_RELEASE };
/* XZ_UI_STEMS_OVERLAY's index: above 0 a MODS layout picture (layout + 1), 0 one layout step, and this, the STEMS
   button itself, which shows or hides the stem buttons in a 4 DECKS view instead (wave_viewport.h xz_stems_press). */
#define XZ_UI_STEMS_BUTTON (-1)
struct xz_ui_action {
    enum xz_ui_action_kind kind;
    enum xz_ui_phase phase;
    int deck, index;
    float value, secondary;
    uint32_t pc_epoch;
    int pc_target;
    uint64_t network_revision; /* immutable draft revision at submission */
    uint64_t network_publication; /* one consumed successful review paint */
    uint32_t sdk_generation; /* catalog generation an SDK row action was made from */
    struct xz_ram_track_key ram_key;
};
/* Mirrors enum xz_ram_state in ../ram/runtime.h so the panel builds without the RAM backend.
   READY means the original compressed audio is fully copied while the USB source is still present;
   DETACHED means that source has gone and only the RAM copy remains. */
enum xz_ui_ram_state { XZ_UI_RAM_UNAVAILABLE, XZ_UI_RAM_EMPTY, XZ_UI_RAM_LOADING, XZ_UI_RAM_READY, XZ_UI_RAM_ERROR, XZ_UI_RAM_DETACHED };
struct xz_ui_ram {
    enum xz_ui_ram_state state;
    uint32_t copied, bytes;
    uint64_t retained_stems_bytes; /* live prepared-stem retention, never serialized */
    char message[96];
    enum xz_ram_reason reason;
    enum xz_ram_phase phase;
    struct xz_ram_track_key key;
    unsigned supported, route_up, source_current, can_request, can_release, retained_assets, software;
};
const char *xz_ui_ram_scope(const struct xz_ui_ram *);
struct xz_ui_deck {
    uint32_t ready;
    const char *track, *status;
    /* levels[role]: 0 drums, 1 harmonics (INST on four-stem), 2 vocals, 3 bass.
       levels[3] and muted bit 3 mean nothing unless stem_count is 4. */
    float bpm, levels[4], sample_volume;
    unsigned muted, groove_assigned, groove_loaded, sample_loaded;
    int bypass, groove_active, sample_active, hold, overdub, loop_index;
    float pitch;
    int key_semitones;
    char key_readout[48];
    unsigned key_color;
    int key_synced; /* last KEY SYNC still holds: same track, shift untouched */
    int key_master; /* this deck is the sync master: KEY SYNC has nothing to match here */
    int stem_loading;
    int stem_count;          /* loaded stem set: 4, else three (OverCue default, zero included) */
    const char *stem_format; /* short set name for setup, e.g. "OVERCUE 3"; NULL when none */
    const char *stem_error;  /* why a stem set found on the USB can't play, one actionable line; NULL when none */
    enum xz_ui_usb_reason usb_reason; /* runtime verdict for USB stems; UNKNOWN = not reported */
    unsigned char groove_stem[8];
    const unsigned char *wave_peaks;
    size_t wave_count;
    struct xz_ui_pc_stems pc_stems; /* independent of muted/levels/bypass */
    struct xz_ui_ram ram;
};
struct xz_ui_connection {
    int ready, enabled, connected, discoverable;
    int can_enable, can_discover, stats_valid;
    const char *device_ip, *protocol, *status;
    unsigned port;
    float frame_hz;
};
/* DJ Profiles found on the USBs (DJPROFILE/VJTOOLS). Runtime-owned, read under
 * the UI lock like the rest of the model. Strings and thumbs point into it. */
#define XZ_UI_DJ_ENTRIES 32
#define XZ_UI_DJ_THUMB 40
#define XZ_UI_DJ_TILE 160
struct xz_ui_dj_entry {
    const char *name, *tagline, *dj_id;
    int usb;              /* 0 or 1 */
    int stick_active;     /* the stick's index.json names this profile */
    int locked;           /* the stick has a single legacy card: nothing to switch */
    int damaged;          /* listed, but its profile file could not be read */
    uint32_t accent;      /* 0xRRGGBB, valid when has_accent */
    int has_accent;
    const uint16_t *thumb; /* XZ_UI_DJ_THUMB square RGB565 logo, or NULL for a monogram */
    int folder;           /* a hand-made DJPROFILE/<name> folder; picking or editing saves it as a profile */
};
struct xz_ui_dj {
    int count;
    int current;          /* entry shown in the header: the master deck's stick, else -1 */
    unsigned usb_present; /* bit per mounted USB slot */
    int reading;          /* a USB is being read */
    int pending;          /* entry being switched to, or -1 */
    const char *status;   /* result of the last switch or check, or NULL */
    int flash;            /* status just arrived: the footer shows it on every page */
    int saving;           /* a profile is being written */
    /* The profile the DJ PROFILE page shows, loaded on request. */
    int detail;                   /* entry these belong to, or -1 */
    const uint16_t *detail_tile;  /* 160 x 160 RGB565 logo, or NULL */
    /* Logo picker: PNG and JPEG files on one USB, and the picked one rendered. */
    int images_usb, images_reading, image_count;
    const char (*images)[256];    /* stick-relative paths */
    int preview;                  /* image index previewed, or -1 */
    int preview_state;            /* enum xz_ui_preview_state */
    const uint16_t *preview_tile; /* 160 x 160 RGB565 when READY */
    const char *preview_error;
    struct xz_ui_dj_entry entry[XZ_UI_DJ_ENTRIES];
};
struct xz_ui_model {
    struct xz_ui_deck deck[4];
    uint32_t enabled;
    int theme, server_auto; /* theme: the look drawn now, xz_settings_look(ui_interface, classic_theme) */
    int ui_interface, classic_theme, modern_theme; /* enum xz_interface and its theme lists (settings.h) */
    const char *server_address;
    unsigned blink;
    uint32_t clock_ms;      /* the panel's frame clock (monotonic ms), for its moving pictures */
    struct xz_ui_connection connection;
    int stem_page, shift_pages, pad_feedback, shift_keysync;
    int fb_takeover, takeover_assign, stems_overlay; /* stems_overlay: enum xz_stem_layout */
    int modern_stems; /* MODERN's stem layout: stems_overlay's values or XZ_MODERN_STEMS_PADS */
    int stem_bank;
    int wave_mode; /* 0 stock, 1 3-band, 2 stems */
    int browse_columns[3], browse_key_prefix, browse_traffic;
    int stem_colors[3];
    int band_colors[3]; /* 3-BAND colour choices, enum xz_wave_band order (band_colors.h); 0 STANDARD */
    int stem_order; /* xz_stem_orders index for the rows and pads A/B/C */
    int key_format; /* 0 stock browser setting, 1 classic, 2 Camelot */
    int key_feedback;
    int rekordbox_assign[2]; /* enum xz_ui_rekordbox: [0] a plain press, [1] with either deck's SHIFT held */
    struct xz_jump_settings jump;
    int jump_encoder;
    const char *settings_status;
    int settings_flash; /* a save result just arrived: the footer shows it on every page */
    const char *theme_status;
    const char *key_browser_status;
    int doom_ready;
    struct xz_doom_browser doom;
    struct xz_update_model update;
    struct xz_support_model support;
    struct xz_sdk_ui_model sdk;
    int ram_debug, ram_debug_valid;
    uint32_t ram_free_mib;
    uint64_t ram_original_bytes, ram_asset_bytes;
    int metrics_enabled;
    struct xz_metrics_sample metrics;
    const struct xz_ui_dj *dj; /* NULL: this runtime has no DJ Profile support, so no header chip */
    int four_wave_mixer; /* deck_view == MIXER, for the retired FOUR_WAVE_ORDER readers */
    struct xz_net_view network;
    /* DECK VIEW (settings.h enum xz_deck_view). */
    int deck_view;
    int browse_preview; /* MODERN browse preview column: 0 hidden (default), 1 shown. Saved. */
    /* The pad page each logical deck's BEAT JUMP pads are on now: 0 page 1, 1 page 2 (SHIFT + BEAT JUMP),
       -1 unknown (deck not running, or its player does not report it). */
    int jump_page[4];
    /* Decks 3 and 4: a copy of the connector's struct xz_extra_route_status (extra_decks.h), field for field.
       state is enum xz_ui_route_state (numbered as enum xz_extra_route_state), fail_part enum xz_ui_route_part,
       reason one sentence (<= 80 characters) from the connector. checked_at_ms is monotonic; revision and
       ready_revision use serial-number arithmetic. An observation only: it does not change playable deck_count
       or capabilities. */
    struct xz_ui_route { int state, fail_part; char reason[81]; uint32_t checked_at_ms, revision, ready_revision; } route;
    /* The MODERN play screen's own parts, as the shell has them now (shell_settings.h items). known 0: no shell
       runs (those rows and the UTILITY rail cell say NOT READY). wave_color: XZ_UI_WAVE_*; usb_colors: 0 DECK +
       WAVE, 1 DECK ONLY, 2 OFF; hidden: bit enum xz_ui_look_item set while that part is hidden. */
    struct xz_ui_modern_look { int known, wave_color, usb_colors; unsigned hidden; } modern_look;
    /* Known stock counter errors on decks 1 and 2 (stock_counter.h): the code and the firmware's English text, or
       NULL. The runtime sets them in MODERN only, where the dock keeps them in view while MODS is open (stock
       errors are never covered). CLASSIC leaves the stock deck area itself visible instead. */
    struct xz_ui_counter_error { const char *code, *text; } counter_error[2];
    /* The player's live window, in its own words ("E-8304: USB ACCESS ERROR"), or NULL. An open MODS keeps the
       screen and its controls in both interfaces and shows the window in its dock instead (stock_dialog.h). The
       loopback cable's E-8307 hub warning is no error and never appears here. */
    const char *stock_window;
};
/* MODERN's look items (XZ_UI_SHELL_LOOK). WAVE_COLOR is the same setting as UTILITY's WAVEFORM COLOR. */
enum xz_ui_look_item { XZ_UI_LOOK_WAVE_COLOR, XZ_UI_LOOK_USB_COLORS, XZ_UI_LOOK_MODE_LAMPS, XZ_UI_LOOK_QUANTIZE_LAMP,
    XZ_UI_LOOK_BYPASS, XZ_UI_LOOK_WAVE_TITLES, XZ_UI_LOOK_ITEMS };
enum xz_ui_wave_color { XZ_UI_WAVE_STOCK, XZ_UI_WAVE_RGB, XZ_UI_WAVE_3BAND, XZ_UI_WAVE_STEMS, XZ_UI_WAVE_COLORS };
/* 1 in MODERN: the UTILITY rail cell and the MODERN look rows show (they act once modern_look.known). */
int xz_ui_modern_menu(const struct xz_ui_model *);
/* Bit d set: deck d has a known stock counter error that the MODS dock shows (MODERN only, counter_error). */
unsigned xz_ui_counter_errors(const struct xz_ui_model *);
/* 1: the dock shows the player's live window (stock_window): no counter error takes the strip. */
int xz_ui_stock_window_notice(const struct xz_ui_model *);
/* XZ_UI_CLOSE's index on the dock's counter error strip; the header CLOSE is index 0. */
enum { XZ_UI_CLOSE_COUNTER=1 };
/* Decks 3 and 4's route, as the connector publishes it (enum xz_extra_route_state). NO_ENGINE: the software
   players are not running (unavailable or not started). */
enum xz_ui_route_state { XZ_UI_ROUTE_NO_ENGINE, XZ_UI_ROUTE_NOT_DETECTED, XZ_UI_ROUTE_SCANNING, XZ_UI_ROUTE_STARTING,
    XZ_UI_ROUTE_WAITING_NATIVE_STOP, XZ_UI_ROUTE_READY, XZ_UI_ROUTE_FAILED, XZ_UI_ROUTE_LOST, XZ_UI_ROUTE_STATES };
enum xz_ui_route_part { XZ_UI_ROUTE_PART_NONE, XZ_UI_ROUTE_PART_PORT, XZ_UI_ROUTE_PART_CABLE, XZ_UI_ROUTE_PART_SOCKET,
    XZ_UI_ROUTE_PART_SELECTOR };
/* What works on each logical deck (0..3), one bit per feature. The menu's WORKS ON chips and the 4 DECKS grid
   read only this, so a deck is never shown as able to do what it cannot. ui/deck_caps.c. */
enum xz_deck_feature {
    XZ_DECK_CAN_BEAT_JUMP=1u, XZ_DECK_CAN_GATE_CUE=2u, XZ_DECK_CAN_SMART_CUE=4u, XZ_DECK_CAN_CUE_PREVIEW=8u,
    XZ_DECK_CAN_PLAYING_KEY=16u, XZ_DECK_CAN_KEY_SYNC=32u, XZ_DECK_CAN_STEMS=64u, XZ_DECK_CAN_XPAD=128u,
    XZ_DECK_CAN_GROOVE=256u, XZ_DECK_CAN_RAM=512u, XZ_DECK_CAN_DISPLAY=1024u
};
unsigned xz_deck_caps(const struct xz_ui_model *, int logical_deck);
/* One sentence on why feature cannot work on logical_deck, or NULL when it can. */
const char *xz_deck_cap_reason(const struct xz_ui_model *, int logical_deck, unsigned feature);
/* 4 DECKS needs the DJ: a four-deck view is chosen and decks 3 and 4 are not connected (the rail's amber dot,
   DESIGN 1.1, in both interfaces). */
int xz_ui_four_decks_attention(const struct xz_ui_model *);
struct xz_ui {
    enum xz_ui_page page;
    int deck, down, capture, jump_edit_page, jump_edit_pair;
    int ram_deck, ram_setup, ram_step;
    enum xz_ui_action_kind held_kind;
    int held_index, held_deck, held_pc_target;
    uint32_t held_pc_epoch;
    unsigned held_pc_available; /* role mask at press; a change cancels the gesture */
    int touch_start_x, dragged,wad_offset;
    int notice_deck; /* inline row that owns notice */
    float touch_start_level;
    int stem_setup_step, metrics_step, feature_cap, feature_step;
    int sdk_offset, sdk_step, sdk_choice; /* APPLICATIONS page offset, wizard step, start choice */
    char notice[96];
    enum xz_ui_page history[16];
    unsigned history_count;
    /* Hardware focus. The browse encoder turns it, encoder push activates it and
       a touch moves it to the touched control. Stored by identity, not slot, so a
       relayout keeps it; a missing control falls back to the page's first one.
       adjusting: the encoder edits the focused control's value instead of moving. */
    enum xz_ui_action_kind focus_kind;
    int focus_index, focus_deck, adjusting;
    /* The DJ Profile list hangs from the header chip. While open it is the only
       thing touch and the encoder reach; a tap outside or BACK closes it. */
    int profile_open, profile_offset;
    /* DJ PROFILE page and editor. Text is UTF-8; the keyboard types ASCII. */
    int profile_view;          /* entry the page shows, -1 = the header's */
    int edit_entry, edit_usb;  /* entry being edited (-1 new) and the USB it saves to */
    int edit_step, edit_origin;/* enum xz_ui_edit_step; origin is where the editor opened */
    int edit_logo;             /* image index, XZ_UI_LOGO_KEEP or XZ_UI_LOGO_NONE */
    int about_link;
    /* REPORT AN ISSUE: enum xz_ui_support_step; topic 0 none yet, 1..XZ_SUPPORT_TOPICS a topic + 1, else NOT SURE;
       entry enum xz_support_entry; skipped: the DJ chose SKIP, so text stays here but is not sent. The text is
       kept until the report is sent or saved, so nothing is typed twice. */
    struct { int step, topic, entry, skipped, from; char text[XZ_SUPPORT_TEXT_CHARS + 1]; } support; /* from: page it opened from */
    int edit_logo_chosen;      /* the DJ picked; no automatic pick after that */
    int kb_shift, kb_symbols, image_offset;
    struct xz_network_ui_draft network;
    struct {
        struct xz_network_ui_draft staged;
        struct xz_net_snapshot observed;
        uint64_t request_id,trial_id,token,painted,consumed;
        int phase,result,theme,valid,contact_blocked,press_armed;
    } network_review;
    char edit_name[641], edit_tagline[321], edit_id[129]; /* edit_id "" = a new profile */
};
struct xz_ui_widget {
    int x,y,w,h;
    enum xz_ui_action_kind kind;
    int index;
    uint32_t requires;
    const char *label;
    int deck;
};
void xz_ui_navigate(struct xz_ui *,enum xz_ui_page);
size_t xz_ui_back(struct xz_ui *,struct xz_ui_action out[XZ_UI_ACTIONS]);
void xz_ui_init(struct xz_ui *ui);
/* The model is a runtime snapshot. Rendering and touch never change it. */
size_t xz_ui_layout(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget out[XZ_UI_WIDGETS]);
int xz_ui_render(struct xz_ui *, const struct xz_ui_model *, uint16_t *pixels, size_t count, size_t stride);
/* Up to XZ_UI_ACTIONS actions. Runtime applies these and supplies the next model. */
size_t xz_ui_touch(struct xz_ui *, const struct xz_ui_model *, int x, int y, int down, struct xz_ui_action out[XZ_UI_ACTIONS]);
/* Call on close, focus loss or device removal to release a held mute/strip/pad. */
size_t xz_ui_cancel(struct xz_ui *, struct xz_ui_action out[XZ_UI_ACTIONS]);
/* Browse encoder. Turning moves focus through the page (content, then deck
   pickers, then the section rail), wrapping at the ends, or edits the value
   while adjusting. Push activates the focused control exactly like a tap. */
size_t xz_ui_encoder_turn(struct xz_ui *, const struct xz_ui_model *, int delta, struct xz_ui_action out[XZ_UI_ACTIONS]);
size_t xz_ui_encoder_push(struct xz_ui *, const struct xz_ui_model *, struct xz_ui_action out[XZ_UI_ACTIONS]);
/* The focused control after resolving identity against the current layout; 0 when the page has none. */
int xz_ui_focused(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_widget *out);
/* What the info box says about the focused control. Strings are static or point into the model.
   feature: the enum xz_deck_feature the control belongs to, 0 for the whole player. works_on: bit d set when
   it works on logical deck d now (xz_deck_caps). excluded: why a deck is left out, one sentence, or NULL. */
struct xz_ui_info { const char *title, *body, *state; int available, selected; unsigned feature, works_on; const char *excluded; };
/* PREVIEW, XPAD and GROOVE keep their numbers but nothing lists them: those features are not built. */
enum xz_ui_info_topic { XZ_UI_INFO_PREVIEW, XZ_UI_INFO_XPAD, XZ_UI_INFO_GROOVE, XZ_UI_INFO_SIDES, XZ_UI_INFO_COLUMNS,
    XZ_UI_INFO_ROUTE, XZ_UI_INFO_GRID=16 /* + row of xz_ui_grid_features */ };
/* The 4 DECKS grid's rows, top to bottom then the second column: only features this runtime has. */
enum { XZ_UI_GRID_ROWS=7 };
extern const unsigned xz_ui_grid_features[XZ_UI_GRID_ROWS];
const char *xz_ui_grid_name(int row);
/* The save line of every explanation area (DESIGN 3.4), from the runtime's settings_status. */
const char *xz_ui_save_state(const struct xz_ui_model *, int *tone_alert);
int xz_ui_describe(const struct xz_ui *, const struct xz_ui_model *, struct xz_ui_info *out);
/* The same for any control of the current layout, focused or not. */
int xz_ui_describe_widget(const struct xz_ui *, const struct xz_ui_model *, const struct xz_ui_widget *, struct xz_ui_info *out);
/* A page's name ("FEATURE SETUP") and one sentence on what it holds. */
const char *xz_ui_page_title(int page);
/* 1 for the five top pages; a page's parent (a top page is its own). */
int xz_ui_page_is_top(int page);
int xz_ui_page_parent(int page);
const char *xz_ui_page_body(int page);
/* 1 when the page shows deck pickers in the header. */
int xz_ui_header_decks(enum xz_ui_page);
/* Profile list rows: first entry shown and how many (at most XZ_UI_PROFILE_ROWS). */
#define XZ_UI_PROFILE_ROWS 5
int xz_ui_profile_window(const struct xz_ui *, const struct xz_ui_model *, int *first);
/* Entry the DJ PROFILE page or the editor shows, or -1. */
int xz_ui_profile_shown(const struct xz_ui *, const struct xz_ui_model *);
/* A new profile on the LOGO step takes the logo in its DJ folder (DJPROFILE/<typed name>/) until the DJ picks.
   Returns the image index to preview, or -1. Call with the model current. */
int xz_ui_profile_autopick(struct xz_ui *, const struct xz_ui_model *);
const char *xz_ui_theme_name(int theme);
const char *xz_ui_ram_state_text(enum xz_ui_ram_state state);
uint32_t xz_ui_stem_color(int theme,int index);
uint32_t xz_ui_model_stem_color(const struct xz_ui_model *,int index);
uint32_t xz_ui_pc_role_color(int theme,int role);
uint32_t xz_ui_model_pc_role_color(const struct xz_ui_model *,int role);
/* Local stem role 0-3 (stem_pads.h). Bass shares the PC BASS colour, so LOCAL
   and PC bass look the same and follow the three user stem colours. */
uint32_t xz_ui_model_local_stem_color(const struct xz_ui_model *,int role);
/* 4 when the deck's loaded set is four-stem, else 3. */
int xz_ui_stem_count(const struct xz_ui_model *,int deck);
/* Why PC controls on this deck are locked, or XZ_UI_PC_WHY_NONE when toggles may be sent. */
enum xz_ui_pc_reason xz_ui_pc_blocked(const struct xz_ui_model *,int deck);
const char *xz_ui_pc_reason_text(enum xz_ui_pc_reason);
/* Why USB stems on this deck can't play: usb_reason when the runtime set it,
   else derived from ready/enabled/stem_error/stem_loading/track. Never UNKNOWN. */
enum xz_ui_usb_reason xz_ui_usb_blocked(const struct xz_ui_model *,int deck);
/* 1 when the USB stem engine is present, so STANDALONE STEMS can be switched on directly. */
int xz_ui_usb_engine(const struct xz_ui_model *);
/* Short status ("STEMS OFF", "PLAY TO ALIGN"), at most 16 characters. */
const char *xz_ui_usb_reason_text(enum xz_ui_usb_reason);
/* The one fix for this reason ("TURN ON", "SET UP"), or NULL when there is
   nothing to set up (ready, no track, loading, aligning). */
const char *xz_ui_usb_action_label(enum xz_ui_usb_reason);
/* One sentence naming the exact next step, for the setup page and notices. */
const char *xz_ui_usb_next_step(enum xz_ui_usb_reason);
/* Each deck row is equal slots: its stems, then one trailing control. A LOCAL
 * three-stem deck lays out the same three MUTE widgets and BYPASS as always; a
 * LOCAL four-stem deck adds a BASS MUTE at slot 3 (pad D/H) before BYPASS. While
 * USB stems can't play, the trailing slot carries the deck's one reason: ENABLE
 * (XZ_UI_STEM, "TURN ON", all decks) when switched off with the engine present,
 * else LOCAL_STEM_SETUP_OPEN labelled with the fix or the reason. A PC or
 * UNKNOWN deck lays out one PC_TOGGLE per role in pc_stems.available (three in
 * 3-stem mode, four in 4-stem mode; the three shared roles, locked, while the
 * mode is unreported), then PC_SETUP as the source tag in BYPASS's place. */
size_t xz_ui_inline_layout(const struct xz_ui *,const struct xz_ui_model *,int width,int height,struct xz_ui_widget out[XZ_UI_WIDGETS]);
int xz_ui_inline_render(const struct xz_ui *,const struct xz_ui_model *,uint16_t *,size_t count,size_t stride,int width,int height);
size_t xz_ui_inline_touch(struct xz_ui *,const struct xz_ui_model *,int width,int height,int x,int y,int down,struct xz_ui_action out[XZ_UI_ACTIONS]);
void xz_ui_render_lcars_play_header(uint16_t *pixels,size_t stride);
/* The ORIGINAL theme's MODS, STEMS (layout 0 hidden, with its layout marks) and VJ.TOOLS / EXIT VJ boxes,
   inside the top band (top_bar.h). */
void xz_ui_render_badge(uint16_t *pixels,size_t stride);
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int layout);
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active);
void xz_ui_render_ram_debug(uint16_t *pixels,size_t stride,int theme,int enabled,int valid,uint32_t free_mib);
void xz_ui_render_native_buttons(uint16_t *pixels,size_t stride,int theme,int stems,int vj_visible,int vj_active);
/* Native Shortcut screen WAVEFORM COLOR group as BLUE | RGB | 3BAND | STEMS (segments 0-3).
 * hit returns the segment under a screen point or -1; selected is the segment drawn on. */
int xz_ui_shortcut_wave_hit(int x,int y);
void xz_ui_render_shortcut_wave(uint16_t *pixels,size_t stride,int selected,const uint32_t stem_rgb[3]);
#endif
