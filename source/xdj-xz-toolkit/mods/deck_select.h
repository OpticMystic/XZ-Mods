/* SPDX-License-Identifier: MIT */
#ifndef XZ_DECK_SELECT_H
#define XZ_DECK_SELECT_H
/* Decks 3 and 4 from the deck controls, the way stock switches them for DJ software: press SHIFT twice
   (XDJ-XZ operating instructions DRI1625B p.129; rekordbox dj hardware diagram, SHIFT "Press twice:
   Change decks (deck1 <-> deck3, etc)"). The left SHIFT toggles the left controls between DECK 1 and
   DECK 3, the right SHIFT the right ones between DECK 2 and DECK 4. Built from those documents and the
   key codes our own onPhysicalKey hook sees, nothing else.

   Tempo fader pickup, as DJ software does after a deck switch: the deck that becomes active keeps its
   tempo until the physical fader crosses that deck's value (or lands within XZ_DS_PICKUP_SLACK of it).
   This holds both ways. DECK 3 keeps its tempo while the left side drives DECK 1, and DECK 1 keeps its
   tempo while the side drives DECK 3; returning to either needs the fader to cross back.

   Pure: no device, threads, clock or allocation. The caller serialises access. */
#include "extra_decks.h"
#include <stdint.h>

#define XZ_DS_WINDOW_MS 400u        /* most time between the two SHIFT press-downs */
#define XZ_DS_PICKUP_SLACK 50       /* tempo positions (0.5 % of half travel) that count as reached */
#define XZ_DS_KEY_SHIFT 0x4103u
#define XZ_DS_KEY_TEMPO 0x410au
#define XZ_DS_KEY_LOAD 0x430fu
#define XZ_DS_OP_JOG 4u
#define XZ_DS_OP_FADER 5u

enum xz_ds_phase {
 XZ_DS_IDLE,
 XZ_DS_DOWN_ONCE,   /* first press held, nothing else touched on this side yet */
 XZ_DS_MODIFIER,    /* held and used as a modifier (SHIFT + PLAY, pads, SYNC): never counts */
 XZ_DS_RELEASED,    /* first press released; the second must come down within the window */
 XZ_DS_DOWN_TWICE,  /* second press held; the side toggled, or the route was down */
};
enum xz_ds_target { XZ_DS_NATIVE, XZ_DS_EXTRA };

struct xz_ds_pickup { int armed, have_prev, require_motion, arm_known; int32_t target, prev, arm_position; };
/* Where a LOAD pressed on a switched side went, decided by the caller at the press (xz_deck_select_load_route):
   swallowed, to the FLAGSHIP shell (it loads its highlighted row to the side's software deck), or on to stock
   (the classic interface's one-shot divert sends stock's own load to the software deck). */
enum xz_ds_load_route { XZ_DS_LOAD_SWALLOW, XZ_DS_LOAD_SHELL, XZ_DS_LOAD_STOCK };
/* Arms pickup toward target. Out-of-span targets stay blocked until a new mapping
   makes them reachable; held target and fresh motion survive side switches. */
void xz_ds_pickup_arm(struct xz_ds_pickup *, int32_t target, int physical_known, int32_t physical);
/* One fader position. 1 once the fader has reached the target: deliver this and every later position. */
int xz_ds_pickup_feed(struct xz_ds_pickup *, int32_t position);

struct xz_ds_side {
 enum xz_ds_phase phase;
 uint32_t first_down_ms;
 uint64_t selection_epoch;
 enum xz_ds_target target;            /* the deck these controls drive now */
 int physical_known; int32_t physical; /* where the DJ's hand left the tempo fader */
 int held_known[2]; int32_t held[2];  /* by target: the tempo position that deck keeps */
 struct xz_ds_pickup pickup;          /* toward held[target] */
 int fresh_pickup[2];                /* persists while this logical target is not selected */
 uint32_t extra_revision; int extra_revision_known, extra_following;
 uint64_t early_native_down, early_native_release; /* global native press and one fallback release pass */
 uint64_t owner[2];                   /* by target: buttons down whose press that deck received */
 int load_down;                       /* a LOAD press went to the software deck; hold and release follow load */
 int sync_held,sync_shift,load_previous;uint32_t load_at;
 int time_down, time_shift, time_held; /* TIME MODE: pressed; SHIFT at the press; the hold (AUTO CUE) fired */
 enum xz_ds_load_route load;
};
struct xz_deck_select {
 int route_up;uint32_t route_epoch;
 struct xz_ds_side side[2];           /* 0 left (channel 1), 1 right (channel 2) */
 uint16_t logged[16]; unsigned logged_count;
};

/* One event at the onPhysicalKey hook. */
struct xz_ds_key {
 int side;
 uint16_t code;
 unsigned op;                         /* 0 press, 1 hold, 2/3 release, 4 jog turn, 5 fader */
 int shift;                           /* SHIFT held, as the player reports it on the event */
 int32_t delta; float value;          /* op 4: delta i32 +12, speed f32 +16; op 5: position f32 +16 */
};
/* PlayerInnards self (channel byte +0x26 = 1/2, SHIFT flag +0x34) and the key input (code u16 +8,
   operation low nibble of +11). A specified input channel +10 of 1/2 must match self;
   an unspecified input channel retains the physical callback's channel. Returns 0 for
   another self channel or an explicit mismatch, without modifying the decoded key. */
int xz_ds_key_decode_global(const void *input, struct xz_ds_key *);
int xz_ds_key_decode(const void *innards, const void *input, struct xz_ds_key *);

enum xz_ds_verdict {
 XZ_DS_PASS,                          /* stock handles it, as without this module */
 XZ_DS_DELIVER,                       /* taken from stock; event goes to the side's software deck */
 XZ_DS_SWALLOW,                       /* taken from stock; nothing receives it */
 XZ_DS_LOAD,                          /* LOAD press of a switched side (GLOBAL path): event.deck is the side. The caller
                                         answers with xz_deck_select_load_route; until it does the press is swallowed */
};
enum xz_ds_notice { XZ_DS_NOTICE_NONE, XZ_DS_NOTICE_NO_ROUTE };
#define XZ_DS_NO_ROUTE_TEXT "DECK 3 AND 4 NEED THE CH3/CH4 ROUTE"
/* LOAD on a switched side that neither the FLAGSHIP shell nor the classic divert can take. */
#define XZ_DS_LOAD_REFUSED_TEXT "LOAD DECK 3/4 FROM THE FLAGSHIP SCREEN"
struct xz_ds_outcome {
 enum xz_ds_verdict verdict;
 struct xz_extra_deck_event event;    /* when DELIVER */
 enum xz_ds_notice notice;            /* a double press with the route down */
 int toggled;                         /* this event switched its side */
 int unknown_first;                   /* first sighting of an unknown code on a switched side: log it */
 int also_stock;                      /* DELIVER and pass the key on to stock as well */
};

/* Where a key reaches the runtime. PLAYER keys arrive at onPhysicalKey; GLOBAL keys only at the
   IKeyManager::onKey hook, ahead of the shell (LOAD, TIME MODE, QUANTIZE). */
enum xz_ds_path { XZ_DS_PATH_PLAYER, XZ_DS_PATH_GLOBAL };
enum xz_ds_class { XZ_DS_ROW_STOCK, XZ_DS_ROW_DELIVER, XZ_DS_ROW_SWALLOW };
struct xz_ds_row { uint16_t lo, hi; uint8_t cls, path; int8_t kind; };
/* The row for code, or NULL; slot is the code's own ownership bit (0..63). */
const struct xz_ds_row *xz_ds_row_for(uint16_t code, unsigned *slot);

#define XZ_DS_HAS_SELECTION_SNAPSHOT 1
struct xz_ds_selection { uint64_t epoch[2]; int deck[2],route_up; uint32_t route_epoch; };
void xz_deck_select_snapshot(const struct xz_deck_select *,struct xz_ds_selection *);
void xz_deck_select_route_instance(struct xz_deck_select *,int route_up,uint32_t route_epoch);
void xz_deck_select_init(struct xz_deck_select *);
/* Call before each event with the route as it is now. A drop returns both sides to DECK 1 / DECK 2. */
void xz_deck_select_route(struct xz_deck_select *, int route_up);
/* A new software SYNC revision rearms pickup. Ordinary fader snapshots never rearm it. */
void xz_deck_select_feedback(struct xz_deck_select *,const struct xz_extra_decks *);
/* A SHIFT edge from any source: the SHIFT key code, or a poll of the +0x34 flag. Repeats are ignored. */
void xz_deck_select_shift(struct xz_deck_select *, int side, int down, uint32_t now_ms, struct xz_ds_outcome *);
/* Early jog ownership: native contacts remain untouched for the physical fallback. */
struct xz_ds_outcome xz_deck_select_global_player(struct xz_deck_select *, const struct xz_ds_key *, uint32_t now);
struct xz_ds_outcome xz_deck_select_key(struct xz_deck_select *, const struct xz_ds_key *, uint32_t now_ms);
/* A key at the IKeyManager::onKey hook (channel +0xa, 1/2 for the deck sections). Feeds SHIFT edges and
   takes GLOBAL-path keys of a switched side; everything else passes untouched. LOAD's press on a switched
   side is XZ_DS_LOAD; its hold and release follow the route the caller gave the press (SHELL and SWALLOW:
   swallowed, STOCK: passed). A LOAD whose press reached stock keeps reaching stock to its release. */
struct xz_ds_outcome xz_deck_select_global_key(struct xz_deck_select *, unsigned channel, uint16_t code,
 unsigned op, uint32_t now_ms);
void xz_deck_select_load_route(struct xz_deck_select *, int side, enum xz_ds_load_route);
/* SHORTCUT DECK SELECT: the same switch as SHIFT pressed twice, with the same notice while the route is down. */
void xz_deck_select_toggle(struct xz_deck_select *, int side, struct xz_ds_outcome *);
#endif
