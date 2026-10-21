#ifndef XZ_SHELL_H
#define XZ_SHELL_H
/* FLAGSHIP shell: the whole 800x480 RGB565 frame while the FLAGSHIP theme is
   active, drawn in the style of the current flagship club players.

   Pure C11. No device, audio, network or allocation. The runtime fills a
   model snapshot and calls xz_shell_render; it feeds touch, the browse
   encoder and the hooked keys through xz_shell_input and executes the typed
   actions that come back. The shell never assumes a feature works: every deck
   readout carries a known bit and renders the flagship's blank convention
   when it is clear; a screen whose model slot is NULL or whose capability is
   missing is handed to the stock UI (XZ_SHELL_ACT_HAND_OFF) instead of
   drawing an empty page. Units after this one fill the browse, settings,
   source and info slots and replace one entry of the screen table each.

   Runtime contract, as mods/ui:
   1. Zero-initialise the model, set known bits only for values that were
      really read, keep strings and arrays alive for the frame. Call
      xz_shell_init once.
   2. Execute every action, then publish confirmed values in the next model.
      The shell shows the model, never its own guess (zoom and the active deck
      are the exceptions: they are shell state, mirrored through ZOOM and
      ACTIVE_DECK so the runtime can follow).
   3. Stride is in pixels. Rendering and input never write the model.
   4. Serialise input, model snapshots and render on one thread. */
#include <stddef.h>
#include <stdint.h>

#define XZ_SHELL_WIDTH 800
#define XZ_SHELL_HEIGHT 480
/* The model's capacity. model.deck_count says how many decks the unit plays now. */
#define XZ_SHELL_DECKS 4
#define XZ_SHELL_ACTIONS 4
#define XZ_SHELL_STACK 6
#define XZ_SHELL_HOT_CUES 8
#define XZ_SHELL_MEMORY_CUES 16
#define XZ_SHELL_ZOOM_MAX 6
#define XZ_SHELL_ZOOM_DEFAULT 3
/* Mod header buttons, drawn last on every shell screen in the place and size
   of the stock-mode header (mods/ui render_buttons): STEMS x 674..741 and MODS
   x 744..799, y 0..23. The touch target reaches down to XZ_SHELL_HEADER_HIT_H
   so each is 44 px tall; the gap at x 742..743 splits between them. Screens
   keep x >= XZ_SHELL_HEADER_X clear above y XZ_SHELL_HEADER_BUTTON_H. A touch
   emits XZ_SHELL_ACT_TOGGLE_STEMS or XZ_SHELL_ACT_OPEN_MODS and nothing else. */
#define XZ_SHELL_HEADER_X 674
#define XZ_SHELL_STEMS_X 674
#define XZ_SHELL_STEMS_W 68
#define XZ_SHELL_MODS_X 744
#define XZ_SHELL_MODS_W 56
#define XZ_SHELL_HEADER_BUTTON_H 24
#define XZ_SHELL_HEADER_HIT_H 44
/* Playhead extrapolation: while a deck plays, the renderer advances elapsed_ms
   by (clock_ms - elapsed_at_ms) x (1 + tempo), for at most this long after a
   read, so a stale reading stops instead of running on. */
#define XZ_SHELL_EXTRAPOLATE_MAX_MS 500
/* A runtime notice (model.notice) shows for this long after notice_at_ms, on every screen. */
#define XZ_SHELL_NOTICE_MS 2500

enum xz_shell_screen {
 XZ_SHELL_PLAY, XZ_SHELL_BROWSE, XZ_SHELL_PLAYLIST, XZ_SHELL_TAG_LIST, XZ_SHELL_SEARCH,
 XZ_SHELL_INFO, XZ_SHELL_SHORTCUT, XZ_SHELL_UTILITY, XZ_SHELL_SOURCE, XZ_SHELL_SCREEN_COUNT
};

/* One bit per deck readout. A clear bit means the field's data is not read. */
enum xz_shell_field {
 XZ_SHELL_F_LOADED, XZ_SHELL_F_TITLE, XZ_SHELL_F_ARTIST, XZ_SHELL_F_SOURCE, XZ_SHELL_F_TRACK_NO,
 XZ_SHELL_F_BPM, XZ_SHELL_F_TRACK_BPM, XZ_SHELL_F_TEMPO, XZ_SHELL_F_TEMPO_RANGE, XZ_SHELL_F_MASTER_TEMPO,
 XZ_SHELL_F_MASTER, XZ_SHELL_F_SYNC, XZ_SHELL_F_PLAYING, XZ_SHELL_F_CUE_STANDBY, XZ_SHELL_F_ELAPSED,
 XZ_SHELL_F_TOTAL, XZ_SHELL_F_TIME_MODE, XZ_SHELL_F_KEY, XZ_SHELL_F_KEY_SHIFT, XZ_SHELL_F_QUANTIZE,
 XZ_SHELL_F_BEAT_JUMP, XZ_SHELL_F_LOOP, XZ_SHELL_F_HOT_CUES, XZ_SHELL_F_MEMORY_CUES, XZ_SHELL_F_CUE_POINT,
 XZ_SHELL_F_GRID, XZ_SHELL_F_BAR_PHASE, XZ_SHELL_F_WAVE, XZ_SHELL_F_OVERVIEW, XZ_SHELL_F_ARTWORK,
 XZ_SHELL_F_STEM_WAVE, XZ_SHELL_F_STEM_MUTE, XZ_SHELL_F_SLIP, XZ_SHELL_F_SLIP_POSITION, XZ_SHELL_F_VINYL,
 XZ_SHELL_F_SOURCE_LOOK,
 XZ_SHELL_F_ON_AIR, XZ_SHELL_F_LOOP_EDIT, XZ_SHELL_F_HOT_CUE_CALL, XZ_SHELL_F_ALERT,
 XZ_SHELL_F_STEM_GAIN,
 XZ_SHELL_F_KEY_ORIGINAL, XZ_SHELL_F_KEY_SYNC,
 XZ_SHELL_F_STEM_BYPASS,
 XZ_SHELL_FIELD_COUNT
};
/* The deck carries the 3-stem waveform layer (struct xz_shell_deck stems, XZ_SHELL_F_STEM_WAVE). */
#define XZ_SHELL_HAS_STEM_WAVE 1
#define XZ_SHELL_KNOWN(field) ((uint64_t)1<<(field))

enum xz_shell_source { XZ_SHELL_SRC_NONE, XZ_SHELL_SRC_USB1, XZ_SHELL_SRC_USB2, XZ_SHELL_SRC_LINK, XZ_SHELL_SRC_COMPUTER };
/* A deck condition that stays on screen until the runtime clears it (XZ manual p.50 emergency loop,
   p.131 E-8302 / E-8304 / E-8305): the deck shows a banner with the code, one line, and one button into
   BROWSE to choose another track. */
enum xz_shell_alert_kind { XZ_SHELL_ALERT_NONE, XZ_SHELL_ALERT_EMERGENCY_LOOP, XZ_SHELL_ALERT_READ_ERROR,
 XZ_SHELL_ALERT_UNSUPPORTED_FILE };
/* HOT CUE AUTO LOAD (XZ manual pp.55, 123): ON loads every track's hot cues, LIBRARY loads them only for
   tracks the library marks for it. The A. HOT CUE lamp reads red for ON, white for LIBRARY. */
enum xz_shell_hot_cue_auto_load { XZ_SHELL_HCAL_OFF, XZ_SHELL_HCAL_ON, XZ_SHELL_HCAL_LIBRARY };
/* The stem buttons on PLAY, the MODERN interface's own setting (modern_stems; CLASSIC keeps stems_overlay):
   HIDDEN, rows
   below each deck's waveform (full 48 px, half 24 px), or rows at the outside edges (above deck 1,
   below deck 2). Full rows are faders: slide a stem button sideways to set its level. */
enum xz_shell_stem_layout { XZ_SHELL_STEMS_HIDDEN, XZ_SHELL_STEMS_BELOW_FULL, XZ_SHELL_STEMS_BELOW_HALF,
 XZ_SHELL_STEMS_OUTSIDE_FULL, XZ_SHELL_STEMS_OUTSIDE_HALF, XZ_SHELL_STEMS_PADS, XZ_SHELL_STEM_LAYOUTS };
/* PADS: small stem buttons in each deck's pad row beside its hot-cue lamps, no rows. The STEMS button steps
   PADS, BELOW_FULL, BELOW_HALF, OUTSIDE_FULL, OUTSIDE_HALF, HIDDEN; its marks follow that order. */
extern const enum xz_shell_stem_layout xz_shell_stem_cycle[XZ_SHELL_STEM_LAYOUTS];
#define XZ_SHELL_STEM_GAIN_MAX 100
enum xz_shell_time_mode { XZ_SHELL_TIME_REMAIN, XZ_SHELL_TIME_ELAPSED };
/* STEMS draws the deck's stem layer; a deck without one draws 3BAND until it arrives. */
enum xz_shell_wave_color { XZ_SHELL_WAVE_BLUE, XZ_SHELL_WAVE_RGB, XZ_SHELL_WAVE_3BAND, XZ_SHELL_WAVE_STEMS };
enum xz_shell_wave_position { XZ_SHELL_POSITION_CENTER, XZ_SHELL_POSITION_LEFT };
enum xz_shell_play_mode { XZ_SHELL_PLAY_SINGLE, XZ_SHELL_PLAY_CONTINUE };
/* AS_DECK shows the key text as read; the others convert it (Fm <-> 4A). */
enum xz_shell_key_display { XZ_SHELL_KEY_AS_DECK, XZ_SHELL_KEY_CLASSIC, XZ_SHELL_KEY_ALPHANUMERIC };

struct xz_shell_cue { int set; int32_t ms; uint32_t rgb; int is_loop; };
struct xz_shell_beat { int32_t ms; uint8_t beat_in_bar; }; /* 1..4 */
struct xz_shell_column { uint8_t low, mid, high; };
/* count columns cover span_ms of audio from the track start, evenly. */
struct xz_shell_wave { const struct xz_shell_column *columns; uint32_t count; uint32_t span_ms; };
struct xz_shell_loop { int active; int beats_num, beats_den; int32_t in_ms, out_ms; };

struct xz_shell_deck {
 uint64_t known;
 int loaded;
 const char *title, *artist;         /* owned by the runtime */
 enum xz_shell_source source;
 int track_no;
 int32_t bpm_x100, track_bpm_x100;
 int32_t tempo_x100;                 /* 0.01 % */
 int tempo_range;                    /* percent; 100 = WIDE */
 int master_tempo, master, sync, playing, cue_standby;
 int32_t elapsed_ms, total_ms;
 uint32_t elapsed_at_ms;             /* model clock_ms when elapsed_ms was read; 0 = not stamped, no
                                        extrapolation. See XZ_SHELL_EXTRAPOLATE_MAX_MS */
 enum xz_shell_time_mode time_mode;  /* per-deck override of settings.time_mode */
 const char *key;                    /* the key as it plays now (key shift and tempo included), e.g. "Fm" or "4A" */
 int key_matches_master;             /* fallback only: the shell colours keys itself from the master deck's key */
 int key_shift;                      /* semitones */
 const char *key_original;           /* XZ_SHELL_F_KEY_ORIGINAL: the track's analysed key, same format as key */
 int key_cents;                      /* with XZ_SHELL_F_KEY_ORIGINAL: key's tuning offset in cents, -50..+50 */
 int key_synced;                     /* XZ_SHELL_F_KEY_SYNC: KEY SYNC holds this deck's key; MASTER TEMPO blinks */
 int quantize_on, quantize_num, quantize_den;
 int beat_jump_num, beat_jump_den;   /* beats */
 struct xz_shell_loop loop;
 struct xz_shell_cue hot_cue[XZ_SHELL_HOT_CUES];
 struct xz_shell_cue memory_cue[XZ_SHELL_MEMORY_CUES];
 int memory_count;
 int32_t cue_ms;
 const struct xz_shell_beat *grid;
 uint32_t grid_count;
 int bar_phase;                      /* 1..4 */
 struct xz_shell_wave wave, overview;
 /* 3-stem layer, covering the track from 0 like wave: low = drums, mid = harmonics, high = vocal, each
    0..255 with the loudest stem's peaks at full height. It may arrive seconds after wave, or never. */
 struct xz_shell_wave stems;
 uint8_t stem_muted;                 /* XZ_SHELL_F_STEM_MUTE: bit 0 drums, 1 harmonics (INST), 2 vocal. Known
                                        only while the deck plays 3 local stems: then the PLAY screen shows
                                        its DRUMS / VOCAL / INST mute buttons */
 uint8_t stem_bypass;                /* XZ_SHELL_F_STEM_BYPASS: 1 while the deck plays the original track over
                                        its stems (the BYPASS button lit; the mutes are kept for its release) */
 int slip;                           /* XZ_SHELL_F_SLIP: SLIP is on */
 int32_t slip_ms;                    /* XZ_SHELL_F_SLIP_POSITION: where the track would be without the slip
                                        move (the ghost playhead), read at slip_at_ms like elapsed_ms */
 uint32_t slip_at_ms;
 int vinyl;                          /* XZ_SHELL_F_VINYL: the jog is in VINYL mode */
 /* XZ_SHELL_F_SOURCE_LOOK: the MY SETTINGS colours of the stick the loaded track came from. While
    display.usb_colors allows, the deck's tile takes background_rgb and its waveforms take wave_color. */
 struct xz_shell_source_look {
  int has_background; uint32_t background_rgb;   /* the stick's personal / background colour, 0xRRGGBB */
  int wave_color;                               /* enum xz_shell_wave_color; -1: the stick sets none */
 } look;
 int on_air;                         /* XZ_SHELL_F_ON_AIR: the channel is live on the master output (the ON AIR
                                        lamp, XZ manual p.70); the runtime leaves the bit clear while UTILITY
                                        ON AIR DISPLAY is OFF */
 /* XZ_SHELL_F_LOOP_EDIT: a loop IN (endpoint 0) or OUT (1) point being moved with the jog (XZ manual
    pp.47-48), at ms. Clear the bit when the adjustment ends or times out. */
 struct xz_shell_loop_edit { int endpoint; int32_t ms; } loop_edit;
 uint8_t hot_cue_called;             /* XZ_SHELL_F_HOT_CUE_CALL: bit i: hot cue i is on its pad; a stored cue
                                        whose bit is clear waits for CALL (XZ manual pp.54-55) */
 struct xz_shell_alert { enum xz_shell_alert_kind kind; const char *code; const char *text; } alert; /* F_ALERT:
                                        code e.g. "E-8302"; text one caps line; NULLs draw the kind's words */
 uint8_t stem_gain[3];               /* XZ_SHELL_F_STEM_GAIN: each stem's level, 0..XZ_SHELL_STEM_GAIN_MAX
                                        (drums, harmonics, vocal); a muted stem keeps its level here */
 const uint16_t *artwork;            /* RGB565 square, or NULL */
 int artwork_side;
 /* Actions of enum xz_shell_cap this deck cannot take although the model offers them, so its controls stay
    dark: a deck the runtime drives through a narrower input (the four-deck view's DECK 3 and 4 take LOAD
    and SEEK only). 0: every capability of the model. */
 uint32_t caps_off;
};

/* Actions the runtime can execute. A missing bit hides or disables the control. */
enum xz_shell_cap {
 XZ_SHELL_CAP_SEEK=1u, XZ_SHELL_CAP_LOAD=2u, XZ_SHELL_CAP_SET_SETTING=4u, XZ_SHELL_CAP_BROWSE=8u,
 XZ_SHELL_CAP_TAG_LIST=16u, XZ_SHELL_CAP_SEARCH=32u, XZ_SHELL_CAP_SOURCE=64u, XZ_SHELL_CAP_GRID=128u,
 XZ_SHELL_CAP_TAG_TRACK=256u,
 XZ_SHELL_CAP_PLAYLIST_EDIT=512u, /* XZ_SHELL_ACT_PLAYLIST: the runtime keeps playlist edits in its sidecar */
 XZ_SHELL_CAP_STEM_MUTE=1024u,    /* XZ_SHELL_ACT_STEM_MUTE */
 XZ_SHELL_CAP_FILTER=2048u,       /* XZ_SHELL_BROWSE_FILTER (shell_browse.h) */
 XZ_SHELL_CAP_TAG_EDIT=4096u,     /* XZ_SHELL_ACT_TAG_LIST and PL_FROM_TAG_LIST */
 XZ_SHELL_CAP_HISTORY_EDIT=8192u, /* XZ_SHELL_ACT_HISTORY */
 XZ_SHELL_CAP_CUE_LIST=16384u,    /* XZ_SHELL_ACT_LOAD_CUE */
 XZ_SHELL_CAP_GRID_EDIT=32768u,   /* XZ_SHELL_ACT_GRID_EDIT */
 XZ_SHELL_CAP_STEM_GAIN=65536u,   /* XZ_SHELL_ACT_STEM_GAIN */
 XZ_SHELL_CAP_DECK_SELECT=131072u, /* XZ_SHELL_ACT_DECK_SELECT and model.sides_extra (four-deck view only) */
 XZ_SHELL_CAP_STEM_BYPASS=262144u /* XZ_SHELL_ACT_STEM_BYPASS */
};
enum xz_shell_setting {
 XZ_SHELL_SET_TIME_MODE, XZ_SHELL_SET_WAVE_COLOR, XZ_SHELL_SET_WAVE_POSITION, XZ_SHELL_SET_PLAY_MODE,
 XZ_SHELL_SET_AUTO_CUE, XZ_SHELL_SET_HOT_CUE_AUTO_LOAD, XZ_SHELL_SET_QUANTIZE, XZ_SHELL_SETTING_COUNT
};
/* Device-wide settings the play screen reads. known and writable are bits per enum xz_shell_setting.
   WAVE_COLOR and WAVE_POSITION are shell-owned display settings (struct xz_shell_display): their bits
   here are not read. */
struct xz_shell_settings {
 uint32_t known;
 uint64_t writable;
 enum xz_shell_time_mode time_mode;
 enum xz_shell_play_mode play_mode;
 int auto_cue;
 int hot_cue_auto_load;              /* enum xz_shell_hot_cue_auto_load */
 /* NEEDLE LOCK (XZ manual p.123; item XZ_SHELL_ITEM_NEEDLE_LOCK): 1 = LOCK, the overview searches only
    on a paused deck. Its own known flag: the setting ids above are fixed by the item id space. */
 int needle_lock_known, needle_lock;
};
/* BEAT FX section of the mixer. Bits of known are enum xz_shell_fx_field; a clear bit draws the
   flagship blank convention. Strings are runtime-owned. */
enum xz_shell_fx_field { XZ_SHELL_FX_F_ON, XZ_SHELL_FX_F_NAME, XZ_SHELL_FX_F_BEAT, XZ_SHELL_FX_F_CHANNEL,
 XZ_SHELL_FX_F_LEVEL, XZ_SHELL_FX_F_TIME, XZ_SHELL_FX_F_BPM, XZ_SHELL_FX_F_TAP, XZ_SHELL_FX_F_QUANTIZE,
 XZ_SHELL_FX_FIELD_COUNT };
/* Every BEAT FX target the XZ mixer selects (XZ manual pp.73, 100): channels 1-4, MIC, crossfader A and B,
   MASTER. A value past these draws no target, never MASTER. */
enum xz_shell_fx_channel { XZ_SHELL_FX_CH_MASTER, XZ_SHELL_FX_CH_1, XZ_SHELL_FX_CH_2, XZ_SHELL_FX_CH_MIC,
 XZ_SHELL_FX_CH_3, XZ_SHELL_FX_CH_4, XZ_SHELL_FX_CH_XF_A, XZ_SHELL_FX_CH_XF_B };
struct xz_shell_fx {
 uint32_t known;
 int on;
 const char *name;                   /* effect name as the mixer shows it */
 int beat_num, beat_den;             /* beat fraction, e.g. 1/2, 3/4, 4/1 */
 enum xz_shell_fx_channel channel;
 int level_pct;                      /* LEVEL/DEPTH knob 0..100 */
 int32_t time_ms;                    /* effect time the beat value resolves to */
 int32_t bpm_x10;                    /* BEAT FX BPM, 0.1 BPM */
 int tap;                            /* BPM source: 1 = TAP, 0 = AUTO */
 int quantize;                       /* XZ_SHELL_FX_F_QUANTIZE: BEAT FX QUANTIZE is on (its own lamp) */
};
/* Filled by later units; opaque here. NULL hands the screen to the stock UI. */
struct xz_shell_browse_model; struct xz_shell_settings_model; struct xz_shell_source_model; struct xz_shell_info_model;
struct xz_shell_mods_model; struct xz_shell_my_settings_model; struct xz_shell_help_model; struct xz_shell_menu;
struct xz_shell_model {
 struct xz_shell_deck deck[XZ_SHELL_DECKS];
 int deck_count;                     /* decks the unit plays now: 4 while the runtime's extra deck route is up;
                                        any other value means 2, so a zero-initialised model has two decks. The
                                        screens draw and accept decks 0..count-1 only */
 uint32_t capabilities;
 struct xz_shell_settings settings;
 int link_known, link_connected;
 struct xz_shell_fx fx;
 int stems_known, stems_on;          /* lights the STEMS header button */
 int stem_rgb_known;
 uint32_t stem_rgb[3];               /* stem pad colours: drums, harmonics, vocal (0xRRGGBB) */
 uint8_t stem_rgb_custom;            /* bit per stem: stem_rgb holds a colour the DJ picked; clear bits draw the
                                        shell's default stem colours (sh_stem_bright) */
 int stem_layout_known;
 enum xz_shell_stem_layout stem_layout; /* modern_stems; the header STEMS button asks the runtime to step it
                                        (XZ_SHELL_ACT_TOGGLE_STEMS: 1, 2, 3, 4, 0, then 1, as the classic
                                        STEMS button does) */
 const char *notice;                 /* one short caps sentence, e.g. "DECK 1 DID NOT LOAD THE TRACK"; NULL = none */
 uint32_t notice_at_ms;              /* clock_ms when it was raised; shown while clock_ms - notice_at_ms < XZ_SHELL_NOTICE_MS */
 uint32_t clock_ms;                  /* animation and playhead extrapolation */
 const struct xz_shell_browse_model *browse;
 const struct xz_shell_settings_model *settings_screen;
 const struct xz_shell_source_model *source;
 const struct xz_shell_info_model *info;
 const struct xz_shell_mods_model *mods;  /* the MODS screen's mod tabs (shell_settings.h); NULL: the MODS header
                                             button opens the runtime's own mod panel (XZ_SHELL_ACT_OPEN_MODS) */
 const struct xz_shell_my_settings_model *my_settings; /* the USB stick BROWSE shows (shell_settings.h); NULL
                                             while BROWSE is not on a USB stick: MENU keeps the list MENU */
 const struct xz_shell_help_model *help;  /* descriptions for the runtime's items (shell_settings.h); NULL: the
                                             shell's own UTILITY descriptions only */
 /* With XZ_SHELL_CAP_DECK_SELECT: which deck each side's deck controls drive. Bit 0: the left side drives
    DECK 3 instead of DECK 1; bit 1: the right side DECK 4 instead of DECK 2. */
 uint8_t sides_extra;
 int wave_order; /* 0: numeric 1/2/3/4, 1: mixer 3/1/2/4; logical deck IDs never move */
 const struct xz_shell_menu *menu;        /* the mod page a MENU tab shows (shell_settings.h); NULL: none read yet */
};

enum xz_shell_event_kind {
 XZ_SHELL_TOUCH_DOWN, XZ_SHELL_TOUCH_MOVE, XZ_SHELL_TOUCH_UP,
 XZ_SHELL_ENCODER_TURN, XZ_SHELL_ENCODER_PUSH, XZ_SHELL_ENCODER_HOLD, XZ_SHELL_KEY
};
enum xz_shell_key {
 XZ_SHELL_KEY_BACK, XZ_SHELL_KEY_BACK_HOLD, XZ_SHELL_KEY_BROWSE, XZ_SHELL_KEY_TAG_LIST, XZ_SHELL_KEY_PLAYLIST,
 XZ_SHELL_KEY_SEARCH, XZ_SHELL_KEY_MENU, XZ_SHELL_KEY_MENU_HOLD, XZ_SHELL_KEY_SHORTCUT, XZ_SHELL_KEY_INFO,
 XZ_SHELL_KEY_SOURCE, XZ_SHELL_KEY_TAG_TRACK, XZ_SHELL_KEY_TIME_MODE, XZ_SHELL_KEY_QUANTIZE, XZ_SHELL_KEY_LOAD,
 XZ_SHELL_KEY_SOURCE_USB1, XZ_SHELL_KEY_SOURCE_USB2, /* the two USB buttons: browse that stick, or the picker */
 XZ_SHELL_KEY_TRACK_FILTER,          /* opens the FILTER panel (over BROWSE when another screen is up) */
 XZ_SHELL_KEY_SOURCE_LINK, XZ_SHELL_KEY_SOURCE_COMPUTER, /* the LINK and computer source buttons: the picker at that slot */
 XZ_SHELL_KEY_BROWSE_HOLD,           /* always open SEARCH, even when it is already visible */
 XZ_SHELL_KEY_TAG_TRACK_HOLD,        /* held TAG TRACK/REMOVE: on TAG LIST, removes the highlighted track */
 XZ_SHELL_KEY_MODS,                  /* a key assigned to MODS (the runtime's REKORDBOX button): on the MODS
                                        screen it closes it as a short MENU does there; anywhere else it opens
                                        MODS on its first mod tab, as a short MENU on PLAY does, list screens
                                        included */
 XZ_SHELL_KEY_COUNT
};
/* deck: the deck a per-deck key (TIME MODE, QUANTIZE, LOAD) belongs to, or -1 for the active deck. */
struct xz_shell_event { enum xz_shell_event_kind kind; int x, y, delta; enum xz_shell_key key; int deck; };

enum xz_shell_action_kind {
 XZ_SHELL_ACT_NONE,
 XZ_SHELL_ACT_SEEK,        /* deck, value = ms */
 XZ_SHELL_ACT_ZOOM,        /* value = shell zoom level 0..XZ_SHELL_ZOOM_MAX */
 XZ_SHELL_ACT_GRID_ADJUST, /* deck, value = encoder delta */
 XZ_SHELL_ACT_SET_SETTING, /* id = enum xz_shell_setting, value, deck (-1 = device) */
 XZ_SHELL_ACT_LOAD,        /* deck, id = track id from the browse model */
 XZ_SHELL_ACT_HAND_OFF,    /* id = enum xz_shell_screen, or XZ_SHELL_HAND_OFF_MENU for the stock menu popup;
                              value = 1 + the setting item (shell_settings.h enum xz_shell_item_id) the stock
                              screen should open at, from SHORTCUT and UTILITY; 0 = no item */
 XZ_SHELL_ACT_CLOSE_SHELL,
 XZ_SHELL_ACT_TAG_TRACK,   /* deck */
 XZ_SHELL_ACT_ACTIVE_DECK, /* deck = the new active deck */
 XZ_SHELL_ACT_BROWSE,      /* id = enum xz_shell_browse_op (shell_browse.h), value per op */
 XZ_SHELL_ACT_INFO_JUMP,   /* deck (-1 = list track), id = info field, value = catalog id or BPM x100 (shell_info.h) */
 XZ_SHELL_ACT_SET_RATING,  /* deck, id = track id, value = 0..5 */
 XZ_SHELL_ACT_SET_SOURCE,  /* id = enum xz_shell_source; the runtime makes it the browse source */
 XZ_SHELL_ACT_OPEN_MODS,   /* the runtime opens the MODS panel (header button) */
 XZ_SHELL_ACT_TOGGLE_STEMS,/* the runtime toggles the STEMS overlay (header button) */
 XZ_SHELL_ACT_STEM_MUTE,   /* deck, id = stem (0 drums, 1 harmonics/INST, 2 vocal), value = 1 mute, 0 play */
 XZ_SHELL_ACT_SET_MOD,     /* a MODS-tab item: deck = tab index in xz_shell_mods_model, id = the item's id, value =
                              choice index (shell_settings.h). Native UTILITY items keep XZ_SHELL_ACT_SET_SETTING */
 XZ_SHELL_ACT_PLAYLIST,    /* id = enum xz_shell_playlist_op; the rest is in xz_shell.edit (below) */
 XZ_SHELL_ACT_MY_SETTINGS, /* MY SETTINGS panel: id = enum xz_shell_my_op, value = the stick (enum xz_shell_source),
                              deck = the deck LOAD applies to (shell_settings.h) */
 XZ_SHELL_ACT_MY_SETTING_SET, /* a value stored in the stick's MY SETTINGS: deck = the stick (enum xz_shell_source),
                              id = the item's id, value = choice index */
 XZ_SHELL_ACT_DJ_PROFILE,  /* deck = the stick (enum xz_shell_source), id = enum xz_shell_profile_op, value = profile index */
 XZ_SHELL_ACT_TAG_LIST,    /* id = enum xz_shell_tag_op, value = the track id (ADD, REMOVE) or the folder or
                              playlist row id (ADD_LIST); deck -1. Needs XZ_SHELL_CAP_TAG_EDIT */
 XZ_SHELL_ACT_HISTORY,     /* id = enum xz_shell_history_op, value = the history list id (DELETE) or track id
                              (PLAYED, UNPLAYED); deck -1. Needs XZ_SHELL_CAP_HISTORY_EDIT */
 XZ_SHELL_ACT_LOAD_CUE,    /* deck, id = track id, value = index into the browse model's cues: load the track
                              to deck at that memory point (CUE/LOOP LIST). Needs XZ_SHELL_CAP_CUE_LIST */
 XZ_SHELL_ACT_GRID_EDIT,   /* deck, id = enum xz_shell_grid_op. Needs XZ_SHELL_CAP_GRID_EDIT */
 XZ_SHELL_ACT_STEM_GAIN,   /* deck, id = stem (0 drums, 1 harmonics, 2 vocal), value = 0..XZ_SHELL_STEM_GAIN_MAX.
                              Sent while a full-row stem button is slid; needs XZ_SHELL_CAP_STEM_GAIN */
 XZ_SHELL_ACT_DECK_SELECT,  /* deck = the side (0: DECK 1 / 3, 1: DECK 2 / 4): that side's deck controls switch to
                              its other deck, as SHIFT pressed twice does (SHORTCUT DECK SELECT). Needs
                              XZ_SHELL_CAP_DECK_SELECT */
 XZ_SHELL_ACT_STEM_BYPASS, /* deck, value = 1 bypass (the original track plays), 0 the stems play. Needs
                              XZ_SHELL_CAP_STEM_BYPASS */
 XZ_SHELL_ACT_MOD_MENU     /* a MODS menu page (shell_settings.h XZ_SHELL_TAB_MENU): id = enum xz_shell_menu_op,
                              value = the root page (OPEN) or a row of model.menu, deck = the choice or the touch */,
 XZ_SHELL_ACT_WAVE_ORDER /* value 0 numeric or 1 mixer, session display preference */
};
/* TAG LIST edits (XZ manual pp.85-89). Results come back as m->notice ("ADDED 12, 3 ALREADY ON THE LIST",
   "TAG LIST IS FULL: 100 TRACKS"). Tag list to playlist is XZ_SHELL_ACT_PLAYLIST op PL_FROM_TAG_LIST. */
enum xz_shell_tag_op { XZ_SHELL_TAG_ADD, XZ_SHELL_TAG_REMOVE, XZ_SHELL_TAG_ADD_LIST, XZ_SHELL_TAG_CLEAR };
/* HISTORY edits (XZ manual pp.83-84). */
enum xz_shell_history_op { XZ_SHELL_HISTORY_DELETE, XZ_SHELL_HISTORY_DELETE_ALL, XZ_SHELL_HISTORY_PLAYED,
 XZ_SHELL_HISTORY_UNPLAYED };
/* Beat grid commands (XZ manual p.72): RESET drops the edits, SNAP moves the grid's first beat to the cue,
   SHIFT keeps the alignment the encoder made. */
enum xz_shell_grid_op { XZ_SHELL_GRID_RESET, XZ_SHELL_GRID_SNAP_TO_CUE, XZ_SHELL_GRID_SHIFT };
#define XZ_SHELL_HAND_OFF_MENU (-1)
struct xz_shell_action { enum xz_shell_action_kind kind; int deck; int32_t value; int id; };

struct xz_shell_view { enum xz_shell_screen screen; int cursor, scroll, focus; };
enum xz_shell_popup_kind { XZ_SHELL_POPUP_NONE, XZ_SHELL_POPUP_RECENT };
struct xz_shell_popup { enum xz_shell_popup_kind kind; int value; };
enum xz_shell_touch_region { XZ_SHELL_TOUCH_NONE, XZ_SHELL_TOUCH_OVERVIEW, XZ_SHELL_TOUCH_OTHER, XZ_SHELL_TOUCH_HEADER,
 XZ_SHELL_TOUCH_STEM };
/* Shell-owned display settings. The shell draws its own waveforms and key text, so these are shell
   state like zoom: a change applies at once and is mirrored as SET_SETTING {deck -1, id, value} (ids
   XZ_SHELL_SET_WAVE_COLOR, XZ_SHELL_SET_WAVE_POSITION and the shell_settings.h display items) for the
   runtime to persist. The runtime restores saved values with xz_shell_restore_setting after init. */
/* Whether a deck follows the colours of its track's stick (deck.look): tile and waveform, tile only, or
   neither. Setting WAVEFORM COLOR on this unit while it is ALL is a local override: it moves to TILE. */
enum xz_shell_usb_colors { XZ_SHELL_USB_COLORS_ALL, XZ_SHELL_USB_COLORS_TILE, XZ_SHELL_USB_COLORS_OFF };
/* Indicators of the stock play screen the DJ may hide (APPEARANCE; shown by default): MODE_LAMPS is the
   top bar's PLAY MODE, A. HOT CUE and A. CUE lamps (XZ_SHELL_ITEM_MODE_LAMPS), QUANTIZE each deck's
   QUANTIZE lamp and beat value (XZ_SHELL_ITEM_QUANTIZE_LAMP), BYPASS the stem rows' BYPASS button
   (XZ_SHELL_ITEM_BYPASS_BUTTON). Hidden lamps and buttons also stop answering touch. */
enum xz_shell_indicator { XZ_SHELL_IND_MODE_LAMPS, XZ_SHELL_IND_QUANTIZE, XZ_SHELL_IND_BYPASS, XZ_SHELL_INDICATORS };
struct xz_shell_display {
 enum xz_shell_wave_color wave_color;
 enum xz_shell_wave_position wave_position;
 enum xz_shell_key_display key_display;
 enum xz_shell_usb_colors usb_colors;  /* XZ_SHELL_ITEM_USB_COLORS */
 uint32_t hidden;                      /* bit per enum xz_shell_indicator */
};
/* Slide-over panels on the list screens: INFO (the highlighted track), MENU (sort, columns, playlist
   edits) and FILTER. Shell state, kept in the struct so the runtime's key probe, which runs input on
   a copy of the struct, sees what a key would change. */
enum xz_shell_panel_kind { XZ_SHELL_PANEL_NONE, XZ_SHELL_PANEL_INFO, XZ_SHELL_PANEL_MENU, XZ_SHELL_PANEL_FILTER,
 XZ_SHELL_PANEL_MY_SETTINGS };
struct xz_shell_panel { enum xz_shell_panel_kind kind; int page, cursor, scroll, armed; };
/* A track picked up to move inside an edited playlist: it travels from row from to row to. */
struct xz_shell_grab { int active; uint32_t playlist, track; int32_t from, to; };
#define XZ_SHELL_NAME_MAX 48
/* The playlist edit an XZ_SHELL_ACT_PLAYLIST asks for. The runtime stores edits in a shell-owned
   sidecar file on the same USB stick, never in the stick's export database, and publishes the merged
   playlists. Valid while the runtime executes the action. */
enum xz_shell_playlist_op {
 XZ_SHELL_PL_CREATE=1,     /* name */
 XZ_SHELL_PL_RENAME,       /* playlist, name */
 XZ_SHELL_PL_DELETE,       /* playlist */
 XZ_SHELL_PL_ADD_TRACK,    /* playlist, track: appended */
 XZ_SHELL_PL_REMOVE_TRACK, /* playlist, track, from = its row */
 XZ_SHELL_PL_MOVE_TRACK,   /* playlist, track, from row, to row (the row it lands on) */
 XZ_SHELL_PL_FROM_TAG_LIST /* name: a new playlist holding the tag list in its order (XZ manual p.89) */
};
struct xz_shell_playlist_edit { enum xz_shell_playlist_op op; uint32_t playlist, track; int32_t from, to; char name[XZ_SHELL_NAME_MAX]; };
/* The track filter the lists ask for (XZ_SHELL_BROWSE_FILTER): the runtime filters, the list comes back
   filtered. Zero filters nothing. */
enum xz_shell_filter_bpm { XZ_SHELL_FILTER_BPM_OFF, XZ_SHELL_FILTER_BPM_MASTER, XZ_SHELL_FILTER_BPM_RANGE };
struct xz_shell_filter {
 enum xz_shell_filter_bpm bpm;
 int bpm_spread;                     /* BPM_MASTER: within this many BPM of the master deck's BPM */
 int32_t bpm_min_x100, bpm_max_x100; /* BPM_RANGE, inclusive */
 int key_compatible;                 /* keys the traffic light marks green or amber against the master key */
 int rating_min;                     /* 0 = any, else at least this many stars */
 uint32_t colors;                    /* bit i = colour tag i (shell_browse.h XZ_SHELL_TAG_COLOR_*); 0 = any */
 uint32_t my_tags;                   /* bit i = browse model my_tags[i]; 0 = any */
};
/* Pending browse choice is value-owned so key probes can safely copy the shell. */
struct xz_shell_load_choice {
 int mode, first, cursor, deck_count, row, has_cues;
 enum xz_shell_screen screen;
 uint64_t media_generation;
 uint32_t track, list;
 char source[64], title[96];
};
struct xz_shell {
 struct xz_shell_view stack[XZ_SHELL_STACK];
 int depth;
 int zoom;        /* shared by both lanes */
 int grid_mode;   /* encoder adjusts the active deck's grid instead of zoom */
 int deck;        /* active deck */
 struct xz_shell_popup popup;
 int down;        /* a touch is in progress */
 enum xz_shell_touch_region down_region;
 int down_deck;
 struct xz_shell_display display;
 struct xz_shell_panel panel;
 struct xz_shell_load_choice load_choice;
 struct xz_shell_grab grab;
 struct xz_shell_playlist_edit edit;
 struct xz_shell_filter filter;
 char name[XZ_SHELL_NAME_MAX];       /* a playlist name being typed */
 /* The MODS screen (XZ_SHELL_UTILITY): the open page on its rail (0 = UTILITY, the device's own menu;
    1.. = the mod tabs), and whether the encoder is on the rail (on_tabs) or inside the page. */
 struct { int tab, on_tabs; } mods_view;
 /* On a MENU tab: the model.menu page the content cursor belongs to; a different page in the model starts
    the cursor on that page's focus row, or, for a page this view left by a push, on the row it left from
    (trail[depth]), so BACK lands on the control that led away. */
 struct { int page, trail_count; struct { int page, key; } trail[XZ_SHELL_STACK]; } menu_view;
 /* A stem button under the finger (XZ_SHELL_TOUCH_STEM, down_deck): which stem, where the touch began,
    the level then, and whether it has slid (a slide sets the level, a tap toggles the mute on release). */
 struct { int stem, x0, gain0, slid; } stem_touch;
 /* One line the shell raises itself when a key or control cannot act here (static text), shown like
    model.notice; the runtime may also read it as the reason for a hand-off. */
 const char *notice;
 uint32_t notice_at_ms;
};

void xz_shell_init(struct xz_shell *);
enum xz_shell_screen xz_shell_screen(const struct xz_shell *);
/* Up to XZ_SHELL_ACTIONS actions; returns how many. */
size_t xz_shell_input(struct xz_shell *, const struct xz_shell_model *, const struct xz_shell_event *, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
/* 0 when pixels is NULL or stride < XZ_SHELL_WIDTH. */
int xz_shell_render(const struct xz_shell *, const struct xz_shell_model *, uint16_t *pixels, size_t stride);
const char *xz_shell_screen_name(enum xz_shell_screen);
/* Restores a persisted shell-owned setting (id as in its SET_SETTING; value as sent, for the zoom item
   the zoom level). Returns 0 and changes nothing for any other id or an out-of-range value. */
int xz_shell_restore_setting(struct xz_shell *, int id, int value);

/* Play-screen geometry for the model's deck count, for the runtime's hit tests and the host tests. A
   NULL model is the two-deck view. Touch targets are at least 44 px tall: the overview answers a touch
   padded upwards to that height when it is drawn shorter. */
struct xz_shell_rect { int x, y, w, h; };
enum xz_shell_play_region {
 XZ_SHELL_R_TOP_BAR, XZ_SHELL_R_INFO_COLUMN, XZ_SHELL_R_DECK_TAG, XZ_SHELL_R_LANE,
 XZ_SHELL_R_STRIP, XZ_SHELL_R_TIME, XZ_SHELL_R_OVERVIEW, XZ_SHELL_R_ZOOM, XZ_SHELL_R_INFO_BUTTON, XZ_SHELL_R_FX,
 XZ_SHELL_R_PAD_ROW,  /* per deck, between the lanes and the strips: stem mute buttons and hot-cue lamps.
                         The four-deck view has none (zero size) */
 XZ_SHELL_R_ALERT_BUTTON, /* per deck, while F_ALERT holds: CHOOSE TRACK, into BROWSE */
 XZ_SHELL_R_GRID_RESET, XZ_SHELL_R_GRID_SNAP, XZ_SHELL_R_GRID_SHIFT /* the active deck's lane in GRID mode */
};
struct xz_shell_rect xz_shell_play_rect(const struct xz_shell_model *, enum xz_shell_play_region, int deck);
/* A deck's stem row on PLAY for the model's stem layout (zero size when hidden or not read); the
   waveform takes the rest of the deck's lane. ZOOM/GRID and the GRID buttons move clear of it. In the
   four-deck view every layout draws as a half row below the lane, and only for a deck whose stem mute
   buttons show; the row is not read there, and GRID edit takes the active deck's row while it shows. */
struct xz_shell_rect xz_shell_stem_row(const struct xz_shell_model *, int deck);
/* A deck's BYPASS button's touch target (44 px tall), at the right end of its stem buttons; zero size
   while the deck shows no stem buttons, the runtime cannot bypass, or APPEARANCE hides it. */
struct xz_shell_rect xz_shell_bypass_button(const struct xz_shell *, const struct xz_shell_model *, int deck);
/* A stem mute button's touch target (44 px tall), stem 0 drums, 1 harmonics (INST), 2 vocal. Drawn
   DRUMS, VOCAL, INST from the left as on the stock deck. Zero size while the deck shows none. */
struct xz_shell_rect xz_shell_stem_button(const struct xz_shell *, const struct xz_shell_model *, int deck, int stem);
/* Track position under x on the deck's overview, clamped to 0..total. */
int32_t xz_shell_overview_ms(const struct xz_shell_model *, int32_t total_ms, int x, int deck);
/* The deck's position this frame: elapsed_ms, advanced while playing as XZ_SHELL_EXTRAPOLATE_MAX_MS
   describes and clamped to 0..total_ms when the total is known. -1 when elapsed is not known. */
int32_t xz_shell_elapsed_now(const struct xz_shell_model *, int deck);
/* Key traffic light against the master deck's key: GREEN same key or its relative, AMBER one step on
   the key wheel either way, NONE anything else, UNKNOWN when either key is unread or unparsed. The
   master is the deck with XZ_SHELL_F_MASTER and master set; its key is deck.key with XZ_SHELL_F_KEY. */
enum xz_shell_key_match { XZ_SHELL_KEY_MATCH_UNKNOWN, XZ_SHELL_KEY_MATCH_NONE, XZ_SHELL_KEY_MATCH_AMBER, XZ_SHELL_KEY_MATCH_GREEN };
enum xz_shell_key_match xz_shell_key_match(const char *key, const char *master_key);
/* The master deck's key text, or NULL. */
const char *xz_shell_master_key(const struct xz_shell_model *);
#endif
