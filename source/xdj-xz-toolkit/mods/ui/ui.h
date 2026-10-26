#ifndef XZ_MOD_UI_H
#define XZ_MOD_UI_H
#include <stddef.h>
#include <stdint.h>
#include "themes.h"
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

enum xz_ui_page { XZ_UI_STEMS, XZ_UI_XPAD, XZ_UI_SETTINGS, XZ_UI_THEMES, XZ_UI_CONNECTION, XZ_UI_CONTROLS, XZ_UI_BEAT_JUMP, XZ_UI_EXTRAS, XZ_UI_DOOM, XZ_UI_DOOM_SETUP, XZ_UI_DOOM_FILES, XZ_UI_UPDATES, XZ_UI_UPDATE_SETUP, XZ_UI_GAMES, XZ_UI_MYHOUSE_SETUP, XZ_UI_KEY_SETUP, XZ_UI_BROWSE_SETUP, XZ_UI_PC_STEMS, XZ_UI_STEM_COLORS, XZ_UI_APPS, XZ_UI_APPS_SETUP, XZ_UI_RAM_SETUP, XZ_UI_LOCAL_STEM_SETUP, XZ_UI_METRICS_SETUP, XZ_UI_PROFILE_SETUP, XZ_UI_PROFILE_EDIT, XZ_UI_GENERAL, XZ_UI_FEATURE_SETUP, XZ_UI_THEME_PICK, XZ_UI_BUTTONS, XZ_UI_NETWORK, XZ_UI_NETWORK_SETUP, XZ_UI_NETWORK_EDIT, XZ_UI_ABOUT, XZ_UI_SUPPORT, XZ_UI_PAGE_COUNT };
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
    XZ_UI_SUPPORT_STEP, XZ_UI_SUPPORT_CATEGORY, XZ_UI_SUPPORT_SUMMARY, XZ_UI_SUPPORT_PREPARE,
    XZ_UI_SUPPORT_PAGE, XZ_UI_SUPPORT_CONSENT, XZ_UI_SUPPORT_CLOCK, XZ_UI_SUPPORT_SEND,
    XZ_UI_SUPPORT_EXPORT, XZ_UI_SUPPORT_CHECK,
    XZ_UI_ROUTE_RESCAN /* connector wake only */
};
/* The REKORDBOX key's choices, numbered as settings.h enum xz_rekordbox_action (stored values). */
enum xz_ui_rekordbox { XZ_UI_RB_MODS, XZ_UI_RB_STOCK, XZ_UI_RB_INTERFACE, XZ_UI_RB_VJ_SCREEN, XZ_UI_RB_STEMS,
    XZ_UI_RB_NOTHING, XZ_UI_RB_CHOICES };
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
    struct xz_ui_connection connection;
    int stem_page, shift_pages, pad_feedback, shift_keysync;
    int fb_takeover, takeover_assign, stems_overlay; /* stems_overlay: enum xz_stem_layout */
    int modern_stems; /* MODERN's stem layout: stems_overlay's values or XZ_MODERN_STEMS_PADS */
    int stem_bank;
    int wave_mode; /* 0 stock, 1 3-band, 2 stems */
    int browse_columns[3], browse_key_prefix, browse_traffic;
    int stem_colors[3];
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
    int metrics_enabled;
    struct xz_metrics_sample metrics;
    const struct xz_ui_dj *dj; /* NULL: this runtime has no DJ Profile support, so no header chip */
    int four_wave_mixer; /* Session preference; never persisted in settings. */
    struct xz_net_view network;
    /* Connector observation only; does not change playable deck_count or capabilities. */
    int route_state, route_fail_part;
    char route_reason[81];
    uint32_t route_checked_at_ms, route_revision, route_ready_revision;
};
struct xz_ui {
    enum xz_ui_page page;
    int deck, down, capture, jump_edit_page, jump_edit_pair;
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
    struct { int step, category, summary, preview_page, consented, clock_edit, clock_field[5]; uint32_t consented_revision; char consented_sha256[65]; } support;
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
/* What the info box says about the focused control. Strings are static or point into the model. */
struct xz_ui_info { const char *title, *body, *state; int available, selected; };
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
void xz_ui_render_badge(uint16_t *pixels,size_t stride);
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int enabled);
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active);
void xz_ui_render_ram_debug(uint16_t *pixels,size_t stride,int theme,int enabled,int valid,uint32_t free_mib);
void xz_ui_render_native_buttons(uint16_t *pixels,size_t stride,int theme,int stems,int vj_visible,int vj_active);
/* Native Shortcut screen WAVEFORM COLOR group as BLUE | RGB | 3BAND | STEMS (segments 0-3).
 * hit returns the segment under a screen point or -1; selected is the segment drawn on. */
int xz_ui_shortcut_wave_hit(int x,int y);
void xz_ui_render_shortcut_wave(uint16_t *pixels,size_t stride,int selected,const uint32_t stem_rgb[3]);
#endif
