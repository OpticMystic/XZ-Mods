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
struct xz_shell_runtime_key { int valid; char text[8]; int matches_master, shift; };
struct xz_shell_runtime_frame_input {
 int eligible, theme;
 struct xz_shell_runtime_key key[XZ_SHELL_DECKS];
};
int xz_shell_runtime_start(void);
void xz_shell_runtime_stop(void);
/* Lock-free: the shell owns the screen now. */
int xz_shell_runtime_owns(void);
/* Lock-free: owns the screen and no frame is due, so the caller can skip all work. */
int xz_shell_runtime_idle(void);
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

/* ---- Pure core (shell_runtime_core.c) ---- */

/* R1 key table. A key is delivered on its press, or (MENU, BACK) on its
   release with a separate event for the hold. STOCK entries are listed so the
   table documents every browse-section key the shell leaves alone. */
enum xz_shrt_rule { XZ_SHRT_STOCK, XZ_SHRT_ON_PRESS, XZ_SHRT_ON_RELEASE };
struct xz_shrt_key_map { uint16_t code; uint8_t channel; uint8_t rule; int8_t key, hold, deck; };
const struct xz_shrt_key_map *xz_shrt_key_lookup(uint16_t code, unsigned channel);
enum xz_shrt_phase { XZ_SHRT_PRESS, XZ_SHRT_HOLD, XZ_SHRT_RELEASE };
enum xz_shrt_phase xz_shrt_phase_of(unsigned op);

/* Decided once per key contact, at the press, so all three ops of a key are
   swallowed or none. PASS_HAND_OFF: the shell would hand this screen to stock,
   so the key goes to stock untouched and the shell steps aside. */
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
int xz_shrt_handoff_yields(const struct xz_shrt_handoff *);

/* Commands executed on the native UI thread. */
enum xz_shrt_command_kind { XZ_SHRT_CMD_NONE, XZ_SHRT_CMD_TIME_MODE, XZ_SHRT_CMD_QUANTIZE_BEAT, XZ_SHRT_CMD_LOAD, XZ_SHRT_CMD_STOCK_MENU };
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
/* SET_SETTING -> the C3 command it needs. 0 when no proven setter exists for that id or value. */
int xz_shrt_setting_writable(int id, int time_mode, int quantize_beat);
int xz_shrt_setting_command(const struct xz_shell_action *, uint32_t now, struct xz_shrt_command *);
/* QUANTIZE BEAT VALUE choices in shell order, and the stored R4 value (0x30..0x33) -> choice index. */
extern const char *const xz_shrt_quantize_choices[4];
int xz_shrt_quantize_index(int quantize_den);

/* Frame period: 30 Hz on PLAY while a deck plays, 10 Hz otherwise (input redraws at once). */
uint32_t xz_shrt_frame_period_ms(enum xz_shell_screen, int any_playing);
#endif
