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

/* ---- Device entry points (shell_runtime.c). Weak in ui_runtime.c. ---- */
struct xz_shell_runtime_key { int valid; char text[8]; char original[8]; int cents, synced; int matches_master, shift; };
#define XZ_SHELL_RUNTIME_DJ 32
struct xz_shell_runtime_frame_input {
 int eligible, theme;
 struct xz_shell_runtime_key key[XZ_SHELL_DECKS];
 int stems_known, stems_on;   /* the STEMS overlay layout (lights the shell's STEMS header button) */
 int stem_muted_known[XZ_SHELL_DECKS];  /* the deck plays 3-stem local stems: stem_muted is meaningful */
 unsigned stem_muted[XZ_SHELL_DECKS];   /* bit 0 drums, 1 harmonics, 2 vocal */
 int stem_rgb_known; uint32_t stem_rgb[3]; /* stem pad colours 0xRRGGBB: drums, harmonics, vocal */
 int cached;                  /* ui_runtime.c's UI lock was busy: this is the previous frame's input */
 /* DJ profiles on the sticks: dj_runtime's view, copied (entry i = view entry i). */
 int dj_known, dj_count;
 /* The appearance settings the MODS > APPEARANCE tab shows (ui_runtime.c's model). */
 int look_known, ui_interface, modern_theme, wave_mode, key_format, key_feedback, browse_traffic, stem_colors[3];
 int stems_overlay;           /* enum xz_stem_layout: the stem row layout (MY SETTINGS > WAVEFORM) */
 int stem_level_known[XZ_SHELL_DECKS]; float stem_level[XZ_SHELL_DECKS][3]; /* the stem levels 0..1 (drums, harmonics, vocal) */
 struct xz_shell_runtime_dj { char name[48]; int usb, stick_active, locked, damaged, has_accent; uint32_t accent; } dj[XZ_SHELL_RUNTIME_DJ];
};
int xz_shell_runtime_start(void);
void xz_shell_runtime_stop(void);
/* The shell runs only in the MODERN interface: starts it (thread, USB scan) on MODERN, stops it on
   CLASSIC. The stop happens off the caller's thread; drawing ends at once. Safe under the UI lock. */
int xz_shell_runtime_follow_interface(int modern);
/* Lock-free: the shell owns the screen now. */
int xz_shell_runtime_owns(void);
/* Lock-free: owns the screen and no frame is due, so the caller can skip all work. */
int xz_shell_runtime_idle(void);
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
int xz_shell_runtime_encoder_turn(int eligible, int theme, int delta);
/* UiKey_EncoderPush entry words: flags (+0) and state (+8). */
int xz_shell_runtime_encoder_push(int eligible, int theme, uint32_t flags, uint32_t state);
/* The native event-loop hook (repaint owner thread): runs queued commands and the starvation pump. */
void xz_shell_runtime_owner_tick(void);
/* Header-button requests the shell cannot execute itself (they belong to ui_runtime.c's MODS panel and
   STEMS overlay). Set by shell input; ui_runtime.c takes them under its UI mutex on the same input thread
   right after the shell call and runs them exactly as the stock-mode badge and STEMS button do. */
enum { XZ_SHELL_UI_OPEN_MODS=1u, XZ_SHELL_UI_TOGGLE_STEMS=2u, XZ_SHELL_UI_STEM_MUTE=4u, XZ_SHELL_UI_DJ_PICK=8u, XZ_SHELL_UI_ACTION=16u };
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

/* ---- Pure core (shell_runtime_core.c) ---- */

/* R1 key table. A key is delivered on its press, or (MENU, BACK) on its
   release with a separate event for the hold. STOCK entries are listed so the
   table documents every browse-section key the shell leaves alone. */
/* HAND_OFF: a browse key the shell has no screen for: while the shell owns the screen it goes to stock
   with a visible hand-off, never to a hidden stock screen. (No key uses it today.) */
enum xz_shrt_rule { XZ_SHRT_STOCK, XZ_SHRT_ON_PRESS, XZ_SHRT_ON_RELEASE, XZ_SHRT_HAND_OFF };
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

#define XZ_SHRT_CONTACTS 8
struct xz_shrt_contacts { struct { uint16_t code; uint8_t channel, used, swallow, held; } c[XZ_SHRT_CONTACTS]; };
void xz_shrt_contact_press(struct xz_shrt_contacts *, uint16_t code, unsigned channel, int swallow);
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
};
#define XZ_SHRT_QUEUE 16
struct xz_shrt_queue { struct xz_shrt_command c[XZ_SHRT_QUEUE]; unsigned head, count; uint32_t dropped, coalesced; };
/* A queued command of the same kind and deck takes the new value instead of queueing twice. */
int xz_shrt_queue_push(struct xz_shrt_queue *, const struct xz_shrt_command *);
int xz_shrt_queue_pop(struct xz_shrt_queue *, struct xz_shrt_command *);
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
#define XZ_SHRT_REFRESH_MS 33u
/* model.clock_ms leads the render time by one presenter period: the receiver shows a frame on the vblank
   after it was drawn (present-then-prepare, 61.6 Hz measured on the deck). */
#define XZ_SHRT_PRESENT_LEAD_MS 16u
uint32_t xz_shrt_frame_period_ms(enum xz_shell_screen, int any_playing);

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

/* Presented-frame statistics in microseconds: frames, the largest gap between two frames and the gaps
   longer than XZ_SHRT_LATE_US (a missed 60 Hz slot). Gaps of a second or more (idle screens) are ignored. */
#define XZ_SHRT_LATE_US 20000u
/* hist: frame-to-frame gaps under 14 ms, 14-18 (one 61.6 Hz vblank), 18-26, 26-34 (one missed), 34+ ms. */
struct xz_shrt_frames { uint64_t last_us; uint32_t frames, gap_max_us, late, hist[5]; };
void xz_shrt_frame_mark(struct xz_shrt_frames *, uint64_t now_us);
#endif
