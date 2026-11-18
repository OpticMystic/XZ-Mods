#ifndef XZ_BENCH_KEYS_H
#define XZ_BENCH_KEYS_H
/* Private bench operations 14..20: loops, beat jumps and hot cues on all four decks, and the stock recorder's
   MASTER REC / TRACK MARK, for unattended tests on the unit. Reachable only through the opt-in bench socket
   (xz_bench_control.h, XZ_MODS_TEST_CONTROL=1); never from the interface.

   Stock decks 1/2 and the recorder: the bench injects the hardware key exactly as the panel readers do, through
   uif::IKeyManager::sendKey (firmware 1.26, 0x2ea3f8). Called from the bench's thread (gui_task) it posts the key
   to the UiMain message thread, which replays it through the same synchronous sendKey -> onKey path a finger
   takes, so the mods' key hooks, the stock state machine, its guards and LEDs all follow. A beat jump of N beats
   has no single key; it runs DjEngineIF::playBeatJump on UiMain through a stock IMsgHandler post, the same call
   the mods' BEAT JUMP pads make there. Software decks 3/4: the player's own input queue (extra_decks.h).

   Wire (struct xz_bench_request / xz_bench_result), deck 0..3 = DECK 1..4, deck 4 = global:
   14 LOOP        a0 action (enum xz_bk_loop_action), a1 identity (decks 1/2: rekordbox id of the loaded track;
                  3/4: timeline.load_revision), a2 BEAT length in milli-beats.
                  Reply a0 keys or events sent, a1 loop flags before (as op 15 a0).
   15 LOOP STATE  a0 identity, or -1 for any. Reply a0 flags (XZ_BK_LS_*), a1 IN ms, a2 OUT ms,
                  a3 length in milli-beats (stock: engine auto-beat num/den; 3/4: the running pad's size), -1 unknown.
   16 BEAT JUMP   a0 signed beats (1..256 either way), a1 identity. Reply a0 job number (1/2) or 1 (3/4).
   17 HOT CUE     a0 pad 0..7, a1 identity. Press and release in HOT CUE pad mode only: sets an empty cue, jumps
                  to a set one.
   18 REC PRESS   deck 4. a0 1 MASTER REC / 2 TRACK MARK, a1 arm token from op 19, a2 expected recording state
                  (0 or 1), a3 XZ_BK_REC_MAGIC. Reply a0 generation before, a1 recording before.
   19 REC STATE   deck 4. a0 0 read, 1 read and arm. Reply a0 known | recording<<1 | arm_valid<<2,
                  a1 elapsed ms, a2 generation, a3 arm token (only when a0 was 1 and the state is known).
   20 STATUS      deck 4. Reply a0 availability (XZ_BK_AV_*), a1 jobs posted, a2 jobs finished, a3 last job result
                  (1 accepted, 0 refused by stock, negative refused before the call).
   21 BROWSE KEY  deck 4. One browse-section key from a short list, through the same sendKey path:
                  a0 1 BROWSE (0x209), 2 BACK (0x4208), 3 the rotary selector's push (0x4207, channel 0),
                     4 the selector's turn (0x4207, operation 4, channel 1, value = steps), 0 read only.
                  a1 keys 1..3: 0 a tap (press and release back to back) or 1..XZ_BK_HOLD_MAX_MS: the press now, the
                     release from the bench's tick that many ms later; key 4: signed steps, 1..XZ_BK_TURN_MAX either way.
                  One held key at a time (-EBUSY while one is down); stopping the bench releases it at once.
                  Reply a0 keys sent, a1 ms until the release (0 when none is due), a2 1 while a key is held,
                  a3 the held key's code. LOAD 1/2 and every deck key stay out.
   26 USB STOP    deck 4. The USB STOP button of a USB slot, pressed and held through the same sendKey path as the
                  panel reader (EupRxDataCheck::checkDeckData sends key 0x8002, operation 0/2, channel = the slot).
                  Stock times the hold itself: at 1500 ms it stops the stick (UsbStorageManager::request_usb_stop).
                  a0 1 or 2 the slot, 0 read only; a1 the hold in ms, 1..XZ_BK_HOLD_MAX_MS (the release comes from the
                  bench's tick, or at once when the bench stops); a3 XZ_BK_USB_STOP_MAGIC. Shares op 21's one held key.
                  Reply as op 21 (a0 keys sent, a1 ms until the release, a2 held, a3 code).
                  Read (a0 0): a0 stops begun, a1 partitions unmounted, a2 busy answers, a3 loop devices released
                  (usb_release.h counters). It unloads that stick's decks and stops a recording on it, as the button. */
#include <stdint.h>
#include "xz_bench_control.h"

enum { XZ_BK_OP_LOOP = 14, XZ_BK_OP_LOOP_STATE, XZ_BK_OP_JUMP, XZ_BK_OP_HOT_CUE, XZ_BK_OP_REC_PRESS,
       XZ_BK_OP_REC_STATE, XZ_BK_OP_STATUS, XZ_BK_OP_BROWSE_KEY };
#define XZ_BK_OP_FIRST 14u
#define XZ_BK_OP_LAST 21u
/* One bit per operation, as xz_bench_control_start's op_mask counts them. */
#define XZ_BK_OP_MASK ((((1u << (XZ_BK_OP_LAST + 1u)) - 1u)) & ~((1u << XZ_BK_OP_FIRST) - 1u))
#define XZ_BK_OP_USB_STOP 26u /* outside 14..21: 24..25 are bench_lights.h */
#define XZ_BK_OP_USB_STOP_MASK (1u << XZ_BK_OP_USB_STOP)

enum xz_bk_loop_action { XZ_BK_LOOP_BEAT = 1, XZ_BK_LOOP_IN, XZ_BK_LOOP_OUT, XZ_BK_LOOP_EXIT, XZ_BK_LOOP_RELOOP,
                         XZ_BK_LOOP_HALVE, XZ_BK_LOOP_DOUBLE };
enum xz_bk_rec_button { XZ_BK_REC_MASTER = 1, XZ_BK_REC_TRACK_MARK = 2 };
#define XZ_BK_REC_MAGIC 0x52454321 /* "REC!" */
#define XZ_BK_ARM_MS 5000u        /* an arm token lives this long and is used at most once */
#define XZ_BK_REC_MIN_MS 2500u    /* no stop or TRACK MARK sooner after a start/mark (stock refuses within 2 s) */

/* op 15 flags */
#define XZ_BK_LS_KNOWN 1u
#define XZ_BK_LS_ACTIVE 2u
#define XZ_BK_LS_STORED 4u    /* 3/4: a loop RELOOP can return to */
#define XZ_BK_LS_EDIT_SHIFT 4 /* 3/4: bits 4..5 adjust mode (1 IN, 2 OUT) */
#define XZ_BK_LS_PAD_SHIFT 8  /* 3/4: bits 8..15 running BEAT LOOP pad + 1 */
/* op 20 availability */
#define XZ_BK_AV_KEYS 1u      /* stock key injection guards passed */
#define XZ_BK_AV_DECK_KEYS 2u /* deck key codes match */
#define XZ_BK_AV_JUMP 4u      /* UiMain beat-jump job guards passed */
#define XZ_BK_AV_LOOP_READ 8u /* stock loop read-back guards passed */
#define XZ_BK_AV_REC_KEYS 16u /* MASTER REC / TRACK MARK key path matches */
#define XZ_BK_AV_REC_STATE 32u /* xz_recorder_state() knows the recorder now */
#define XZ_BK_AV_BROWSE_KEYS 64u /* browse key path (BROWSE, BACK, selector) matches */
#define XZ_BK_AV_USB_STOP 128u   /* USB STOP key path (panel key, hold time, storage manager) matches */

/* Stock key codes, firmware 1.26 (evidence in bench-keys/REPORT.md). */
#define XZ_BK_KEY_LOOP_IN 0x410du
#define XZ_BK_KEY_LOOP_OUT 0x410eu
#define XZ_BK_KEY_RELOOP_EXIT 0x410fu
#define XZ_BK_KEY_4BEAT 0x4118u
#define XZ_BK_KEY_PAD1 0x4119u
#define XZ_BK_KEY_CALL_NEXT 0x4324u /* in a loop: double */
#define XZ_BK_KEY_CALL_PREV 0x4325u /* in a loop: halve */
#define XZ_BK_KEY_MASTER_REC 0x401u
#define XZ_BK_KEY_TRACK_MARK 0x402u
#define XZ_BK_OP_PRESS 0u
#define XZ_BK_OP_RELEASE 2u
/* Op 21's keys (evidence in browse-encoder/REPORT.md): MixerCpuCom::disassemble sends them. */
enum xz_bk_browse_key { XZ_BK_BROWSE_READ = 0, XZ_BK_BROWSE_BROWSE, XZ_BK_BROWSE_BACK, XZ_BK_BROWSE_PUSH, XZ_BK_BROWSE_TURN };
#define XZ_BK_KEY_BROWSE 0x209u
#define XZ_BK_KEY_BACK 0x4208u
#define XZ_BK_KEY_SELECTOR 0x4207u
#define XZ_BK_OP_ROTATE 4u          /* the selector's turn: channel 1, value = signed steps */
#define XZ_BK_HOLD_MAX_MS 3000
/* Op 26 (evidence in stick-removal/REPORT.md). */
#define XZ_BK_KEY_USB_STOP 0x8002u
#define XZ_BK_USB_STOP_HOLD_MS 1500    /* KeyInput::getLongPressTime for 0x8002 */
#define XZ_BK_USB_STOP_MAGIC 0x55534221 /* "USB!": a stray request never stops a stick */
#define XZ_BK_TURN_MAX 32

/* What the shell knows about a stock deck, copied under its own lock before the call. */
struct xz_bk_native_deck { int settled; uint32_t id; int contact; };

struct xz_bk_loop_view { int known, active, stored, edit, pad; int32_t in_ms, out_ms, length_milli; };
/* A software deck as the bench needs it. pad_mode: enum xz_extra_pad_mode. */
struct xz_bk_extra_view { int ready, contact, pad_mode, emergency; uint32_t revision; struct xz_bk_loop_view loop; };
struct xz_bk_rec_view { int known, recording; uint32_t elapsed_ms, generation; };

/* Software deck events the adapter turns into struct xz_extra_deck_event. */
enum xz_bk_soft_kind { XZ_BK_SOFT_LOOP_IN, XZ_BK_SOFT_LOOP_OUT, XZ_BK_SOFT_RELOOP, XZ_BK_SOFT_LOOP_HALF,
                       XZ_BK_SOFT_LOOP_DOUBLE, XZ_BK_SOFT_BEAT_LOOP, XZ_BK_SOFT_BEAT_JUMP, XZ_BK_SOFT_HOT_CUE };

/* Everything that touches the player, injected so the decisions can run on a PC. Every callback is bounded and
   non-blocking. 0 or a positive count on success, a negative errno otherwise. */
struct xz_bk_env {
 void *ctx;
 uint32_t (*now_ms)(void *ctx);
 unsigned (*available)(void *ctx);                                   /* XZ_BK_AV_* guards, cached */
 int (*send_key)(void *ctx, unsigned code, unsigned operation, unsigned channel);
 int (*post_jump)(void *ctx, int deck, int beats, uint32_t job);    /* UiMain job */
 int (*stock_loop)(void *ctx, int deck, struct xz_bk_loop_view *out);
 int (*stock_pad_mode)(void *ctx, int deck);                         /* PlayerInnards +0x80; 0 HOT CUE */
 int (*side_deck)(void *ctx, int side);                              /* 1..4: the deck this side drives */
 int (*extra_view)(void *ctx, int deck, struct xz_bk_extra_view *out);
 int (*extra_input)(void *ctx, int deck, enum xz_bk_soft_kind kind, int pressed, int value);
 int (*recorder)(void *ctx, struct xz_bk_rec_view *out);
 uint32_t (*entropy)(void *ctx);
 /* sendKey with its value argument (the selector's turn); NULL refuses op 21's turn. */
 int (*send_key_value)(void *ctx, unsigned code, unsigned operation, unsigned channel, int32_t value);
 /* usb_release.h counters for op 26's read: stops begun, unmounted, busy, loops. NULL refuses the read. */
 int (*usb_release)(void *ctx, int32_t out[4]);
};

struct xz_bk {
 struct xz_bk_env env;
 uint32_t jobs_posted, jobs_done; int32_t last_job; /* atomics: jobs_done/last_job are written on UiMain */
 struct { uint32_t token, at; int valid; } arm;
 struct { uint32_t since, mark_at, generation; int seen; } rec; /* when this bench saw the current recording begin */
 uint32_t token_counter;
 struct { uint32_t due; uint16_t code; uint8_t channel; uint8_t active; } held; /* op 21's key held down */
};

void xz_bk_init(struct xz_bk *, const struct xz_bk_env *);
/* Validates and runs one request. Runs only on the bench's tick thread. */
void xz_bk_apply(struct xz_bk *, const struct xz_bench_request *, struct xz_bench_result *,
                 const struct xz_bk_native_deck *native);
/* UiMain finished job number job with result (1 taken, 0 refused by stock, negative refused before the call). */
void xz_bk_job_done(struct xz_bk *, uint32_t job, int32_t result);
/* Milli-beats to the software decks' BEAT LOOP pad (0..7), -1 when no pad has that length. */
int xz_bk_beat_pad(int32_t milli);
/* Op 21's held key: the release once due (the bench's tick), or at once (the bench stopping). */
void xz_bk_tick(struct xz_bk *);
void xz_bk_flush(struct xz_bk *);

#ifndef XZ_BENCH_KEYS_NO_RUNTIME
/* Runtime entry, called by bench_adapter.h with the shell's lock released. */
void xz_bench_keys_apply(const struct xz_bench_request *, struct xz_bench_result *, const struct xz_bk_native_deck *);
/* Releases op 21's held key at once, then waits up to timeout_ms for posted UiMain jobs to finish. 1 when none is
   outstanding. */
int xz_bench_keys_quiesce(unsigned timeout_ms);
/* The bench's tick (shell_runtime.c owner tick, with the bench gate on): op 21's release when due. */
void xz_bench_keys_tick(void);
#endif
#endif
