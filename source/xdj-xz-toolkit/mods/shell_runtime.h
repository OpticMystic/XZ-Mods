/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_RUNTIME_H
#define XZ_SHELL_RUNTIME_H
/* FLAGSHIP shell on the deck: frame ownership, input routing, the command
   queue onto the native UI thread and the stock hand-off.

   Active while the FLAGSHIP theme (24) is selected and XZ_MODS_SHELL=1 was in
   the environment at start. The MODS panel, the games, Doom and the VJ
   takeover keep priority: ui_runtime.c passes `eligible` = none of them owns
   the screen. While the shell owns the screen xz_mods_visible_v1 reports
   true, so the receiver treats it exactly like the open MODS panel.

   Two layers. shell_runtime_core.c is pure (key table, press-time key verdict,
   touch and encoder ownership, hand-off state machine, command queue, setting
   mapping) and host-tested by test_shell_runtime.c. shell_runtime.c is the
   device glue: native reads, the worker, the C2/C3/UiKey_Menu calls. */
#include <stddef.h>
#include <stdint.h>
#include "shell/shell.h"
#include "shell_model.h"
#include "extra_decks.h"
#include "shell/shell_settings.h"

/* ---- Device entry points (shell_runtime.c). Weak in ui_runtime.c. ---- */
struct xz_shell_runtime_key { int valid; char text[8]; char original[8]; int cents, synced; int matches_master, shift; };
#define XZ_SHELL_RUNTIME_DJ 32
struct xz_shell_runtime_frame_input {
 int eligible, theme;
 int key_controls_open; /* transient PLAY overlay, forces a clean frame beneath it */
 struct xz_shell_runtime_key key[XZ_SHELL_NATIVE_DECKS];
 int browse_preview; /* applied saved preference from ui_runtime, default collapsed */
 int stems_known, stems_on;   /* the STEMS overlay layout (lights the shell's STEMS header button) */
 int stem_muted_known[XZ_SHELL_NATIVE_DECKS];  /* the deck plays 3-stem local stems: stem_muted is meaningful */
 unsigned stem_muted[XZ_SHELL_NATIVE_DECKS];   /* bit 0 drums, 1 harmonics, 2 vocal */
 int stem_bypass[XZ_SHELL_NATIVE_DECKS]; /* Native stem BYPASS state. */
 int stem_rgb_known; uint32_t stem_rgb[3]; /* stem pad colours 0xRRGGBB: drums, harmonics, vocal */
 int cached;                  /* ui_runtime.c's UI lock was busy: this is the previous frame's input */
 /* DJ profiles on the sticks: dj_runtime's view, copied (entry i = view entry i). */
 int dj_known, dj_count;
 int four_decks_attention;   /* a four-deck view is chosen and decks 3 and 4 are not connected: 4 DECKS gets a dot */
 /* The look settings MY SETTINGS and the shell follow (ui_runtime.c's model). */
 int look_known, ui_interface, modern_theme, wave_mode, key_format, key_feedback, browse_traffic, stem_colors[3];
 int stem_order;
 int stems_overlay;           /* MODERN's stem layout (settings modern_stems): enum xz_shell_stem_layout */
 int rekordbox_known, rekordbox_assign[2]; /* the REKORDBOX button's press and SHIFT + press (enum xz_rekordbox_action) */
 int stem_level_known[XZ_SHELL_NATIVE_DECKS]; float stem_level[XZ_SHELL_NATIVE_DECKS][3]; /* the stem levels 0..1 (drums, harmonics, vocal) */
 struct xz_shell_runtime_dj { char name[48]; int usb, stick_active, locked, damaged, has_accent; uint32_t accent; } dj[XZ_SHELL_RUNTIME_DJ];
};
int xz_shell_runtime_start(void);
void xz_shell_runtime_stop(void);
/* The shell runs only in the MODERN interface: starts it (thread, USB scan) on MODERN, stops it on
   CLASSIC. The stop happens off the caller's thread; drawing ends at once. Safe under the UI lock. */
int xz_shell_runtime_follow_interface(int modern);
/* Lock-free: the shell owns the screen now. */
int xz_shell_runtime_owns(void);
/* Logical deck under a PLAY key readout, -1 on other screens or hidden decks. */
int xz_shell_runtime_key_hit(int x,int y,uint32_t *projection);
uint32_t xz_shell_runtime_key_frame(void);
/* Lock-free: owns the screen and no frame is due, so the caller can skip all work. */
int xz_shell_runtime_idle(void);
/* Scheduling hint only; position and motion still come from native observations. */
void xz_shell_runtime_native_input(int side,uint32_t monotonic_ms);
/* Lock-free: the next frame refreshes the model (native reads). ui_runtime.c moves its own 10 Hz refresh
   off such frames so the two never land in the same 16 ms. */
int xz_shell_runtime_will_refresh(void);
/* Called for every xz_mods_render_v1 under the UI mutex. 1 = drew the whole
   frame, -1 = owns the screen but drew nothing (keep the last frame), 0 = not
   the owner (draw the usual badges). */
int xz_shell_runtime_frame(uint16_t *pixels, size_t stride, const struct xz_shell_runtime_frame_input *in);
enum xz_shell_touch_result { XZ_SHELL_TOUCH_TO_STOCK, XZ_SHELL_TOUCH_TAKEN, XZ_SHELL_TOUCH_TAKEN_RELEASE_STOCK };
/* stock_down: the native solver's previous status had a contact down. */
int xz_shell_runtime_touch(int eligible, int theme, int x, int y, int down, int stock_down);
/* IKeyManager::onKey input: code at +8, channel at +0xa, op = low nibble of +0xb. 1 = swallow. */
int xz_shell_runtime_key(int eligible, int theme, uint16_t code, unsigned channel, unsigned op);
/* LOAD of a side whose controls drive DECK 3 / 4 (side 0 left, 1 right), decided by runtime.c's deck select:
   the press loads the highlighted row to that player. 1 when the shell owns the screen and took it. */
int xz_shell_runtime_extra_load_key(int eligible, int theme, int side);
int xz_shell_runtime_encoder_turn(int eligible, int theme, int delta);
/* UiKey_EncoderPush entry words: flags (+0) and state (+8). */
int xz_shell_runtime_encoder_push(int eligible, int theme, uint32_t flags, uint32_t state);
/* The native event-loop hook (repaint owner thread): runs queued commands and the starvation pump. */
void xz_shell_runtime_owner_tick(void);
/* Header-button requests the shell cannot execute itself (they belong to ui_runtime.c's MODS panel and
   STEMS overlay). Set by shell input; ui_runtime.c takes them under its UI mutex on the same input thread
   right after the shell call and runs them exactly as the stock-mode badge and STEMS button do. */
enum { XZ_SHELL_UI_OPEN_MODS=1u, XZ_SHELL_UI_TOGGLE_STEMS=2u, XZ_SHELL_UI_STEM_MUTE=4u, XZ_SHELL_UI_DJ_PICK=8u, XZ_SHELL_UI_ACTION=16u,
 XZ_SHELL_UI_MENU=32u, XZ_SHELL_UI_DECK_VIEW=64u };
/* XZ_SHELL_UI_DECK_VIEW: the DECK VIEW the PLAY picker or the decks 3 and 4 card chose (enum xz_deck_layout),
   or -1. Taken once; ui_runtime.c applies it through its one DECK VIEW entry point. */
int xz_shell_runtime_take_deck_view(void);
/* The MODS screen's MENU tabs (GENERAL, CONTROLS, ADVANCED, EXTRAS) are the MODS panel's own pages, drawn by
   the shell (ui/ui_menu.h describes them). ui_runtime.c publishes the panel's open page whenever menu_wanted
   says the shell shows a MENU tab (every frame, and right after it runs a command, so the next input already
   sees the page a push opened). XZ_SHELL_UI_MENU: the commands a push asked for, oldest first; ui_runtime.c
   runs them on the panel state under its UI lock. */
struct xz_shell_menu;
int xz_shell_runtime_menu_wanted(void);
void xz_shell_runtime_menu(const struct xz_shell_menu *);
/* NETWORK's review token is acknowledged only by an actual interactive menu
 * frame and a later fresh input sequence. A build/publication is not a paint. */
void xz_shell_runtime_menu_network(const struct xz_shell_menu *,uint64_t review_token);
/* op: enum xz_shell_menu_op. OPEN: root; ACTIVATE: target; BACK; PRESS/MOVE/RELEASE: target, x, y (0..1000). */
struct xz_shell_runtime_menu_command { int op, root, x, y; struct xz_shell_menu_target target; uint64_t network_publication; };
int xz_shell_runtime_take_menu_command(struct xz_shell_runtime_menu_command *);
/* A panel control led to a mod page (a setup the runtime opens itself): MODS opens on the MENU tab whose
   root holds it, inside the page. 0 when the shell cannot show it (no such tab, not running). close: MODS
   leaves the screen (a setup finished with "back to play"). */
int xz_shell_runtime_show_mods(int root);
void xz_shell_runtime_close_mods(void);
/* MODS is the shell's top screen (it then draws over the VJ takeover, as the panel does in CLASSIC). */
int xz_shell_runtime_mods_open(void);
/* XZ_SHELL_UI_ACTION: MODS-panel actions the shell's item tabs asked for, oldest first (kind = enum
   xz_ui_action_kind, index, value as the panel sends them); 0 when none is left. ui_runtime.c runs them
   through apply(), so side effects and the settings save are the panel's own. */
int xz_shell_runtime_take_ui_action(int *kind, int *deck, int *index, float *value);
/* XZ_SHELL_UI_DJ_PICK: the dj_runtime view entry the shell's DJ PROFILE row asked to make active, or -1. */
int xz_shell_runtime_take_dj_pick(void);
unsigned xz_shell_runtime_take_ui_requests(void);
/* XZ_SHELL_UI_STEM_MUTE: the stem mutes the shell asked for, oldest first; 0 when none is left.
   value 1 = muted, 0 = playing. ui_runtime.c runs them through its own stem mute path. */
int xz_shell_runtime_take_stem_mute(int *deck, int *stem, int *value);
/* XZ_SHELL_UI_OPEN_MODS: the MODS panel page the shell's MODS screen asked for (enum xz_ui_page), or -1 for
   the panel's own start page. Taken once. */
int xz_shell_runtime_take_mods_page(void);
/* A header action ui_runtime.c took but could not carry out: shown as the shell's notice toast. */
void xz_shell_runtime_notice(const char *text);
/* REKORDBOX button (ui_runtime.c decides; see xz_shrt_rekordbox_verdict).
   rekordbox_action: the action of the press about to reach xz_shell_runtime_key, set under the UI lock on the
   same input thread just before it. mods_key: the MODS action outside the key path (the shell yielded to
   stock, or the press reached the native handler): ends a hand-off and opens or closes the shell's MODS
   screen as XZ_SHELL_KEY_MODS does. 0 when the shell cannot show it (not running, not eligible). */
void xz_shell_runtime_rekordbox_action(int action);
int xz_shell_runtime_mods_key(int eligible, int theme);
/* SOURCE screen LINK and EXPORT MODE tiles. A select queues the slot's stock key (0 LINK, 1 REKORDBOX); it
   runs only on the native key task, where stock's own key handlers run: ui_runtime.c takes it right after
   the encoder push that selected it (UiKey_EncoderPush runs on Ui_EventTask) and calls the stock handler.
   A touch select cannot run it (the touch thread is not that task): the shell says so. Returns -1 when
   nothing is queued. ran: what the stock call returned (0 ran, -1 the handler hook is not armed); the shell
   hands the screen to stock when it ran and says why when it did not. */
int xz_shell_runtime_take_stock_key(void);
void xz_shell_runtime_stock_key_ran(int which, int ran);

/* ---- Pure core (shell_runtime_core.c) ---- */

/* R1 key table. A key is delivered on its press, or (MENU, BACK) on its
   release with a separate event for the hold. STOCK entries are listed so the
   table documents every browse-section key the shell leaves alone. */
/* HAND_OFF: a browse key the shell has no screen for: while the shell owns the screen it goes to stock
   with a visible hand-off, never to a hidden stock screen (LINK).
   ASSIGNED: the key does what the DJ assigned (REKORDBOX, settings.h enum xz_rekordbox_action); the runtime
   decides its press with xz_shrt_rekordbox_verdict. */
enum xz_shrt_rule { XZ_SHRT_STOCK, XZ_SHRT_ON_PRESS, XZ_SHRT_ON_RELEASE, XZ_SHRT_HAND_OFF, XZ_SHRT_ASSIGNED };
struct xz_shrt_key_map { uint16_t code; uint8_t channel; uint8_t rule; int8_t key, hold, deck; };
const struct xz_shrt_key_map *xz_shrt_key_lookup(uint16_t code, unsigned channel);
enum xz_shrt_phase { XZ_SHRT_PRESS, XZ_SHRT_HOLD, XZ_SHRT_RELEASE };
enum xz_shrt_phase xz_shrt_phase_of(unsigned op);

/* Decided once per key contact, at the press, so all three ops of a key are
   swallowed or none. PASS_HAND_OFF: the shell would hand this screen to stock,
   so the key goes to stock untouched and the shell steps aside. PASS is only for
   STOCK-rule deck keys: a browse-section key is never passed to a hidden stock
   screen (an inert key, LOAD on anything but a loadable track, and BACK on PLAY
   are swallowed). */
enum xz_shrt_verdict { XZ_SHRT_PASS, XZ_SHRT_PASS_HAND_OFF, XZ_SHRT_SWALLOW };
enum xz_shrt_verdict xz_shrt_key_verdict(const struct xz_shell *, const struct xz_shell_model *, const struct xz_shrt_key_map *);
/* The shell key for this phase of a swallowed contact, or -1. held: a hold was already delivered. */
int xz_shrt_key_for_phase(const struct xz_shrt_key_map *, enum xz_shrt_phase, int held);
/* REKORDBOX while the shell owns the screen. action: what ui_runtime.c decided for this press (enum
   xz_rekordbox_action, the SHIFT slot already picked). Only the shell's own actions are swallowed: MODS
   (effect SHELL_MODS: the shell key XZ_SHELL_KEY_MODS) and STEMS (effect STEMS: the header STEMS request).
   STOCK passes with a visible hand-off; INTERFACE, VJ.TOOLS SCREEN and NOTHING pass untouched, and
   ui_runtime.c's REKORDBOX handler hook runs them once without stock. One decider: the same action reaches
   that hook, so nothing runs twice. */
enum xz_shrt_rb_effect { XZ_SHRT_RB_NONE, XZ_SHRT_RB_SHELL_MODS, XZ_SHRT_RB_STEMS };
enum xz_shrt_verdict xz_shrt_rekordbox_verdict(int action, enum xz_shrt_rb_effect *effect);

#define XZ_SHRT_CONTACTS 8
struct xz_shrt_contacts { struct { uint16_t code; uint8_t channel, used, swallow, held; } c[XZ_SHRT_CONTACTS]; };
void xz_shrt_contact_press(struct xz_shrt_contacts *, uint16_t code, unsigned channel, int swallow);
/* LOAD-only duplicate PRESS: -1 new/non-LOAD, 0 repeats a passed contact,
 * 1 repeats a swallowed contact. Never changes contact or hold state. */
int xz_shrt_load_duplicate(const struct xz_shrt_contacts *,const struct xz_shrt_key_map *,enum xz_shrt_phase);
/* Hold/release follow the press. Returns the press decision (1 swallow, 0 pass)
   or -1 when no press was seen; *held says whether a hold came before. A
   release forgets the contact; a hold marks it. */
int xz_shrt_contact_follow(struct xz_shrt_contacts *, uint16_t code, unsigned channel, enum xz_shrt_phase, int *held);

/* Touch: a contact belongs to whoever had it at touch-down, until release. */
enum xz_shrt_touch_owner { XZ_SHRT_T_NONE, XZ_SHRT_T_SHELL, XZ_SHRT_T_STOCK, XZ_SHRT_T_SWALLOW };
struct xz_shrt_touch { enum xz_shrt_touch_owner owner; int x, y; };
struct xz_shrt_touch_step { int to_stock, release_stock, event; /* event: -1 or enum xz_shell_event_kind */ int x, y; };
struct xz_shrt_touch_step xz_shrt_touch_route(struct xz_shrt_touch *, int owns, int x, int y, int down, int stock_down);

/* MENU follows the general probe: its hold opens the shell's own UTILITY, so the key is swallowed on
   every screen that has it. A short MENU that would hand off to stock's menu popup (list screens) is
   inert: there is no proven way to open that popup (stock's short MENU needs a native browse list and
   Ui_EventTask), so the shell neither synthesises it nor steps aside. Should a probe ever give
   PASS_HAND_OFF for MENU, the whole key goes to stock and the runtime steps aside only on the hold. */

/* Encoder push from UiKey_EncoderPush entries: press phase = flags 1; hold = state 3. */
struct xz_shrt_push { int down, held; };
int xz_shrt_push_event(struct xz_shrt_push *, uint32_t flags, uint32_t state);

/* Hand-off. Resume once stock has been back on its play screen for
   XZ_SHRT_RESUME_STABLE_MS, after it either left the play screen or
   XZ_SHRT_RESUME_GRACE_MS passed; BACK_HOLD resumes at once. A caution
   (non-zero, including the UINT32_MAX "unknown" sentinel) yields until it clears. */
#define XZ_SHRT_RESUME_STABLE_MS 400u
#define XZ_SHRT_RESUME_GRACE_MS 2500u
struct xz_shrt_handoff { int handed, left_play, caution; uint32_t since, play_since; uint32_t count, resumes; };
void xz_shrt_handoff_begin(struct xz_shrt_handoff *, uint32_t now);
void xz_shrt_handoff_end(struct xz_shrt_handoff *);
/* play: 1 stock shows its play screen, 0 it does not, -1 unknown (never resumes). Returns 1 on resume. */
int xz_shrt_handoff_step(struct xz_shrt_handoff *, uint32_t now, int play);
void xz_shrt_handoff_caution(struct xz_shrt_handoff *, uint32_t caution);
/* Come back `remaining` ms from now instead of after the full grace if stock never leaves its play
   screen (a short MENU on PLAY opens nothing in stock). */
void xz_shrt_handoff_shorten(struct xz_shrt_handoff *, uint32_t now, uint32_t remaining);
/* The caution read fails closed with UINT32_MAX. One failed read must not hand the screen to stock for a
   frame (a full-screen flash plus a full stock repaint): UNKNOWN passes only after XZ_SHRT_CAUTION_RUNS
   refreshes in a row; until then the last known value stands. A real caution id passes at once. */
#define XZ_SHRT_CAUTION_RUNS 3
struct xz_shrt_caution { uint32_t last, shown, ack; int unknown_run; };
uint32_t xz_shrt_caution_filter(struct xz_shrt_caution *, uint32_t caution);
/* BACK held while the shell yields to a caution: the user has seen it. That caution id (or the unknown
   sentinel) no longer yields until the read value changes or clears, so a caution id that stays latched
   in memory after the popup is gone cannot keep the shell away. */
void xz_shrt_caution_ack(struct xz_shrt_caution *);
int xz_shrt_handoff_yields(const struct xz_shrt_handoff *);

/* Commands executed on the native UI thread. */
/* UTILITY: one stock UTILITY item (shell_utility.h); deck = its row in the generated table, so sets of one
   item coalesce and different items do not; value = the setter argument. */
enum xz_shrt_command_kind { XZ_SHRT_CMD_NONE, XZ_SHRT_CMD_TIME_MODE, XZ_SHRT_CMD_QUANTIZE_BEAT, XZ_SHRT_CMD_LOAD, XZ_SHRT_CMD_STOCK_MENU,
 XZ_SHRT_CMD_UTILITY };
struct xz_shrt_command {
 enum xz_shrt_command_kind kind;
 int deck;            /* 0/1, or -1 for both decks (TIME_MODE) */
 int32_t value;       /* TIME_MODE: 1 = remain; QUANTIZE_BEAT: C3 value 0..3 (1, 1/2, 1/4, 1/8 beat) */
 uint32_t track_id;   /* LOAD */
 uint8_t device_kind; /* LOAD: 2 USB1, 3 USB2 (inferred) */
 uint32_t at_ms;
 uint32_t media_generation; /* LOAD: nonzero source incarnation captured at admission. */
};
#define XZ_SHRT_QUEUE 16
struct xz_shrt_queue { struct xz_shrt_command c[XZ_SHRT_QUEUE]; unsigned head, count; uint32_t dropped, coalesced; };
/* A queued command of the same kind and deck takes the new value instead of queueing twice. */
int xz_shrt_queue_push(struct xz_shrt_queue *, const struct xz_shrt_command *);
int xz_shrt_queue_pop(struct xz_shrt_queue *, struct xz_shrt_command *);
unsigned xz_shrt_queue_drop_source(struct xz_shrt_queue *,uint8_t device_kind);
int xz_shrt_load_generation_current(const struct xz_shrt_command *,uint32_t observed_generation);
/* Shell-owned display settings (wave colour, wave position, key display, wave zoom): the shell already
   applied them; the runtime only stores the value and restores it with xz_shell_restore_setting after
   xz_shell_init. They are never stock commands. */
int xz_shrt_shell_owned_setting(int id);
/* SET_SETTING -> the C3 command it needs. 0 when no proven setter exists for that id or value. */
int xz_shrt_setting_writable(int id, int time_mode, int quantize_beat);
int xz_shrt_setting_command(const struct xz_shell_action *, uint32_t now, struct xz_shrt_command *);
/* QUANTIZE BEAT VALUE choices in shell order, and the stored R4 value (0x30..0x33) -> choice index. */
extern const char *const xz_shrt_quantize_choices[4];
int xz_shrt_quantize_index(int quantize_den);

/* Frame period: every presenter tick (60 Hz) on PLAY while a deck moves, 10 Hz otherwise (input
   redraws at once). 12 ms, not 16: a vsync-paced caller with a millisecond of jitter must still be due
   on every tick, while a faster caller stays near 60 Hz. The model refreshes at most every
   XZ_SHRT_REFRESH_MS; the renderer extrapolates the playhead in between. */
#define XZ_SHRT_FRAME_LIVE_MS 12u
#define XZ_SHRT_FRAME_IDLE_MS 100u
/* A render longer than this (half a 60 Hz vsync) marks the next frame late: moving pictures step down. */
#define XZ_SHRT_LATE_RENDER_US 8000u
#define XZ_SHRT_REFRESH_MS 33u
/* model.clock_ms leads the render time by one presenter period: the receiver shows a frame on the vblank
   after it was drawn (present-then-prepare, 61.6 Hz measured on the deck). */
#define XZ_SHRT_PRESENT_LEAD_MS 16u
uint32_t xz_shrt_frame_period_ms(enum xz_shell_screen, int any_moving);
unsigned xz_shrt_moving_decks(const struct xz_shell_model *);

/* Deck motion from the stat frame's elapsed time: moving while it changed within XZ_SHRT_MOTION_MS.
   Used for PLAYING only when the native clock read gives no answer. */
#define XZ_SHRT_MOTION_MS 300u
struct xz_shrt_motion { int valid; int32_t last_ms; uint32_t changed_at; };
int xz_shrt_motion_step(struct xz_shrt_motion *, int32_t elapsed_ms, uint32_t read_at);

/* Playhead smoother. The stat frame's elapsed time is coarse and read at the refresh rate; extrapolating
   from each raw reading makes the scrolling lane step back and forth by the reading's jitter. This keeps
   one continuous estimate: it predicts with the tempo, moves toward each new reading by 1/8 of the error
   (at most XZ_SHRT_POSITION_NUDGE_MS per reading) and snaps on a jump larger than
   XZ_SHRT_POSITION_SNAP_MS (seek, cue, loop, scratch). Paused decks publish the raw reading.
   rate_ppm: playback speed in parts per million (1000000 = normal). Outputs xz_shell_deck elapsed_ms
   and elapsed_at_ms. */
#define XZ_SHRT_POSITION_SNAP_MS 60
#define XZ_SHRT_POSITION_NUDGE_MS 2
/* measure (a precise source: the audio engine position): consecutive readings give the deck's observed
   speed. When it is far from rate_ppm (a jog hold, a scratch, reverse, a brake) the deck is not moving
   as its pitch says, so the smoother follows the readings exactly and publishes elapsed_at_ms = 0 (no
   extrapolation, shell.h): the playhead never runs ahead of the last reading. It returns to smoothing
   after XZ_SHRT_REGULAR_RUNS regular readings in a row. Returns 1 while following. */
#define XZ_SHRT_RATE_TOLERANCE_PPM 250000 /* of the nominal rate, per million */
#define XZ_SHRT_REGULAR_RUNS 2
struct xz_shrt_position { int valid; int64_t base_us; uint32_t base_at, read_at; int32_t read_ms;
 int have_last, following, regular_run; int32_t last_ms; uint32_t last_at; int32_t observed_ppm; };
void xz_shrt_position_reset(struct xz_shrt_position *);
int xz_shrt_position_step(struct xz_shrt_position *, int playing, int32_t elapsed_ms, uint32_t read_at,
                          int32_t rate_ppm, int measure, int32_t *out_ms, uint32_t *out_at);
/* Speed in ppm from the stat frame's tempo (0.01 %), the fallback when the engine speed is not read. */
int32_t xz_shrt_rate_from_tempo(int32_t tempo_x100);

/* ---- DECK 3 / DECK 4 (extra_decks.h): the software players in the shell model ----
   The model's decks 2 and 3 while the CH3/CH4 route is up. Pure: the runtime takes the snapshot, converts
   its arrays and executes what these decide. */
/* Every per-deck action of enum xz_shell_cap, and the ones the players take. The native decks have no
   seek input. */
#define XZ_SHRT_DECK_CAPS (XZ_SHELL_CAP_SEEK|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_SET_SETTING|XZ_SHELL_CAP_GRID|\
 XZ_SHELL_CAP_GRID_EDIT|XZ_SHELL_CAP_TAG_TRACK|XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN|XZ_SHELL_CAP_STEM_BYPASS|XZ_SHELL_CAP_CUE_LIST)
#define XZ_SHRT_EXTRA_CAPS (XZ_SHELL_CAP_SEEK|XZ_SHELL_CAP_LOAD)
/* A player's wave (lane and overview columns) and beat grid, converted by the runtime into memory it keeps
   (the snapshot's arrays are the players'). */
struct xz_shrt_extra_parts {
 const struct xz_shell_column *scroll, *overview; uint32_t scroll_n, overview_n, span_ms;
 const struct xz_shell_beat *grid; uint32_t grid_n;
};
/* After the native refresh: deck_count, decks 2 and 3 and sides_extra from one snapshot. Route down leaves
   exactly the two-deck model. Title, artist and key point into s, so s must outlive the frame. model_now is
   this refresh's model clock; mono_now the monotonic ms that s->deck[].elapsed_at_ms counts in. */
void xz_shrt_extra_map(struct xz_shell_model *, const struct xz_extra_decks *s, const struct xz_shrt_extra_parts parts[XZ_EXTRA_DECKS],
                       uint32_t model_now, uint32_t mono_now);
/* The model's capabilities and every deck's caps_off for its deck count. caps: the runtime's capabilities
   without LOAD; native_load / extra_load: LOAD works for the native decks / the players; deck_select: the
   runtime can switch the sides (XZ_SHELL_ACT_DECK_SELECT). Two decks: caps (with LOAD when native_load) and
   no caps_off. Returns the capabilities, also stored in the model. */
uint32_t xz_shrt_extra_caps(struct xz_shell_model *, uint32_t caps, int native_load, int extra_load, int deck_select);
/* A shell action for DECK 3 or 4. INPUT: *event is the player input (LOAD from the browse stick
   browse_slot 0/1, SEEK); REFUSE: a per-deck action no player takes, or one the deck's capabilities do not
   allow; NONE: not a player's action, the native path runs it. */
enum xz_shrt_extra_route { XZ_SHRT_EXTRA_NONE, XZ_SHRT_EXTRA_INPUT, XZ_SHRT_EXTRA_REFUSE };
enum xz_shrt_extra_route xz_shrt_extra_action(const struct xz_shell_model *, const struct xz_shell_action *, int browse_slot,
                                              struct xz_extra_deck_event *event);
/* 1 when s carries a notice not shown yet and still fresh (raised less than XZ_SHELL_NOTICE_MS before
   mono_now). *seen keeps the last notice_seq looked at. */
int xz_shrt_extra_notice(uint32_t *seen, const struct xz_extra_decks *s, uint32_t mono_now);
/* 2 DECKS shows the decks the sides control; when a side switches deck the shell says where it went, the same
   words as the CLASSIC banner (deck_view.h xz_deck_view_pair_notice): the notice for this refresh, or NULL.
   seen keeps the last pair and when it was looked at. The route down, a refresh gap of a second or more (the
   shell was not drawing) and any view but 2 DECKS adopt the new pair silently. */
struct xz_shrt_pair_seen { int valid; unsigned pair; uint32_t at_ms; };
const char *xz_shrt_pair_notice(struct xz_shrt_pair_seen *, const struct xz_shell_model *, int deck_view, uint32_t now_ms);
/* The resolver's answer from a library row: path = root + rel (rel starts "/Contents/"), the text fields
   truncated to their arrays on a character boundary. anlz_dir is left empty. 0, or -1 when rel is empty or
   the path does not fit. */
int xz_shrt_extra_track(struct xz_extra_track *out, int source, uint32_t track_id, const char *root, const char *rel,
                        const char *title, const char *artist, const char *key, uint32_t bpm_x100);
/* Three-band columns (low, mid, high bytes per column) for the lanes: band_peak is the 99.5th percentile of
   each column's loudest band; bin_columns writes up to bins columns, each band's peak over its bin scaled so
   that peak is full height, and returns how many. */
uint32_t xz_shrt_band_peak(const uint8_t *bands, uint32_t count);
uint32_t xz_shrt_bin_columns(const uint8_t *bands, uint32_t count, uint32_t peak, struct xz_shell_column *out, uint32_t bins);

/* Presented-frame statistics in microseconds: frames, the largest gap between two frames and the gaps
   longer than XZ_SHRT_LATE_US (a missed 60 Hz slot). Gaps of a second or more (idle screens) are ignored. */
#define XZ_SHRT_LATE_US 20000u
/* hist: frame-to-frame gaps under 14 ms, 14-18 (one 61.6 Hz vblank), 18-26, 26-34 (one missed), 34+ ms. */
struct xz_shrt_frames { uint64_t last_us; uint32_t frames, gap_max_us, late, hist[5]; };
void xz_shrt_frame_mark(struct xz_shrt_frames *, uint64_t now_us);
#endif
