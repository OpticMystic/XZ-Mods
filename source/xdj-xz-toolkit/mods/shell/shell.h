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
#define XZ_SHELL_DECKS 2
#define XZ_SHELL_ACTIONS 4
#define XZ_SHELL_STACK 4
#define XZ_SHELL_HOT_CUES 8
#define XZ_SHELL_MEMORY_CUES 16
#define XZ_SHELL_ZOOM_MAX 6
#define XZ_SHELL_ZOOM_DEFAULT 3

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
 XZ_SHELL_FIELD_COUNT
};
#define XZ_SHELL_KNOWN(field) ((uint64_t)1<<(field))

enum xz_shell_source { XZ_SHELL_SRC_NONE, XZ_SHELL_SRC_USB1, XZ_SHELL_SRC_USB2, XZ_SHELL_SRC_LINK, XZ_SHELL_SRC_COMPUTER };
enum xz_shell_time_mode { XZ_SHELL_TIME_REMAIN, XZ_SHELL_TIME_ELAPSED };
enum xz_shell_wave_color { XZ_SHELL_WAVE_BLUE, XZ_SHELL_WAVE_RGB, XZ_SHELL_WAVE_3BAND };
enum xz_shell_wave_position { XZ_SHELL_POSITION_CENTER, XZ_SHELL_POSITION_LEFT };
enum xz_shell_play_mode { XZ_SHELL_PLAY_SINGLE, XZ_SHELL_PLAY_CONTINUE };

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
 enum xz_shell_time_mode time_mode;  /* per-deck override of settings.time_mode */
 const char *key;
 int key_matches_master;
 int key_shift;                      /* semitones */
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
 const uint16_t *artwork;            /* RGB565 square, or NULL */
 int artwork_side;
};

/* Actions the runtime can execute. A missing bit hides or disables the control. */
enum xz_shell_cap {
 XZ_SHELL_CAP_SEEK=1u, XZ_SHELL_CAP_LOAD=2u, XZ_SHELL_CAP_SET_SETTING=4u, XZ_SHELL_CAP_BROWSE=8u,
 XZ_SHELL_CAP_TAG_LIST=16u, XZ_SHELL_CAP_SEARCH=32u, XZ_SHELL_CAP_SOURCE=64u, XZ_SHELL_CAP_GRID=128u,
 XZ_SHELL_CAP_TAG_TRACK=256u
};
enum xz_shell_setting {
 XZ_SHELL_SET_TIME_MODE, XZ_SHELL_SET_WAVE_COLOR, XZ_SHELL_SET_WAVE_POSITION, XZ_SHELL_SET_PLAY_MODE,
 XZ_SHELL_SET_AUTO_CUE, XZ_SHELL_SET_HOT_CUE_AUTO_LOAD, XZ_SHELL_SET_QUANTIZE, XZ_SHELL_SETTING_COUNT
};
/* Device-wide settings the play screen reads. known and writable are bits per enum xz_shell_setting. */
struct xz_shell_settings {
 uint32_t known;
 uint64_t writable;
 enum xz_shell_time_mode time_mode;
 enum xz_shell_wave_color wave_color;
 enum xz_shell_wave_position wave_position;
 enum xz_shell_play_mode play_mode;
 int auto_cue, hot_cue_auto_load;
};
/* Filled by later units; opaque here. NULL hands the screen to the stock UI. */
struct xz_shell_browse_model; struct xz_shell_settings_model; struct xz_shell_source_model; struct xz_shell_info_model;
struct xz_shell_model {
 struct xz_shell_deck deck[XZ_SHELL_DECKS];
 uint32_t capabilities;
 struct xz_shell_settings settings;
 int link_known, link_connected;
 uint32_t clock_ms;                  /* animation only */
 const struct xz_shell_browse_model *browse;
 const struct xz_shell_settings_model *settings_screen;
 const struct xz_shell_source_model *source;
 const struct xz_shell_info_model *info;
};

enum xz_shell_event_kind {
 XZ_SHELL_TOUCH_DOWN, XZ_SHELL_TOUCH_MOVE, XZ_SHELL_TOUCH_UP,
 XZ_SHELL_ENCODER_TURN, XZ_SHELL_ENCODER_PUSH, XZ_SHELL_ENCODER_HOLD, XZ_SHELL_KEY
};
enum xz_shell_key {
 XZ_SHELL_KEY_BACK, XZ_SHELL_KEY_BACK_HOLD, XZ_SHELL_KEY_BROWSE, XZ_SHELL_KEY_TAG_LIST, XZ_SHELL_KEY_PLAYLIST,
 XZ_SHELL_KEY_SEARCH, XZ_SHELL_KEY_MENU, XZ_SHELL_KEY_MENU_HOLD, XZ_SHELL_KEY_SHORTCUT, XZ_SHELL_KEY_INFO,
 XZ_SHELL_KEY_SOURCE, XZ_SHELL_KEY_TAG_TRACK, XZ_SHELL_KEY_TIME_MODE, XZ_SHELL_KEY_QUANTIZE, XZ_SHELL_KEY_LOAD, XZ_SHELL_KEY_COUNT
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
 XZ_SHELL_ACT_HAND_OFF,    /* id = enum xz_shell_screen, or XZ_SHELL_HAND_OFF_MENU for the stock menu popup */
 XZ_SHELL_ACT_CLOSE_SHELL,
 XZ_SHELL_ACT_TAG_TRACK,   /* deck */
 XZ_SHELL_ACT_ACTIVE_DECK, /* deck = the new active deck */
 XZ_SHELL_ACT_BROWSE,      /* id = enum xz_shell_browse_op (shell_browse.h), value per op */
 XZ_SHELL_ACT_INFO_JUMP,   /* deck (-1 = list track), id = info field, value = catalog id or BPM x100 (shell_info.h) */
 XZ_SHELL_ACT_SET_RATING,  /* deck, id = track id, value = 0..5 */
 XZ_SHELL_ACT_SET_SOURCE   /* id = enum xz_shell_source; the runtime makes it the browse source */
};
#define XZ_SHELL_HAND_OFF_MENU (-1)
struct xz_shell_action { enum xz_shell_action_kind kind; int deck; int32_t value; int id; };

struct xz_shell_view { enum xz_shell_screen screen; int cursor, scroll, focus; };
enum xz_shell_popup_kind { XZ_SHELL_POPUP_NONE, XZ_SHELL_POPUP_RECENT };
struct xz_shell_popup { enum xz_shell_popup_kind kind; int value; };
enum xz_shell_touch_region { XZ_SHELL_TOUCH_NONE, XZ_SHELL_TOUCH_OVERVIEW, XZ_SHELL_TOUCH_OTHER };
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
};

void xz_shell_init(struct xz_shell *);
enum xz_shell_screen xz_shell_screen(const struct xz_shell *);
/* Up to XZ_SHELL_ACTIONS actions; returns how many. */
size_t xz_shell_input(struct xz_shell *, const struct xz_shell_model *, const struct xz_shell_event *, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
/* 0 when pixels is NULL or stride < XZ_SHELL_WIDTH. */
int xz_shell_render(const struct xz_shell *, const struct xz_shell_model *, uint16_t *pixels, size_t stride);
const char *xz_shell_screen_name(enum xz_shell_screen);

/* Play-screen geometry, for the runtime's hit tests and the host tests. */
struct xz_shell_rect { int x, y, w, h; };
enum xz_shell_play_region {
 XZ_SHELL_R_TOP_BAR, XZ_SHELL_R_INFO_COLUMN, XZ_SHELL_R_DECK_TAG, XZ_SHELL_R_LANE,
 XZ_SHELL_R_STRIP, XZ_SHELL_R_TIME, XZ_SHELL_R_OVERVIEW, XZ_SHELL_R_ZOOM, XZ_SHELL_R_INFO_BUTTON
};
struct xz_shell_rect xz_shell_play_rect(enum xz_shell_play_region, int deck);
/* Track position under x on the deck's overview, clamped to 0..total. */
int32_t xz_shell_overview_ms(int32_t total_ms, int x, int deck);
#endif
