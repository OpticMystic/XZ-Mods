/* SPDX-License-Identifier: MIT */
/* Pro DJ Link wire core for the XDJ-XZ software decks.
   Pure: no sockets, clocks, allocation, globals or floating point. The caller supplies time and
   every byte received, and sends every datagram this code asks for.
   Layouts and their sources: research/protocol-reference.md. */
#ifndef XZ_DJL_H
#define XZ_DJL_H
#include <stddef.h>
#include <stdint.h>

#define DJL_PORT_ANNOUNCE 50000
#define DJL_PORT_BEAT 50001
#define DJL_PORT_STATUS 50002
#define DJL_MAX_DATAGRAM 0x200
#define DJL_NAME 20
#define DJL_PITCH_UNITY 0x100000u /* Q20: 0 % pitch */
#define DJL_NO_TIME 0xffffffffu
#define DJL_NO_DEVICE 0xffu
#define DJL_MIXER_FIRST 33

/* ---- packets ---------------------------------------------------------------------------- */

enum djl_kind {
 DJL_UNKNOWN,
 DJL_HELLO,           /* 50000 0x0a */
 DJL_CLAIM1,          /* 50000 0x00 */
 DJL_CLAIM2,          /* 50000 0x02 */
 DJL_CLAIM3,          /* 50000 0x04 */
 DJL_ASSIGN_FINISHED, /* 50000 0x05 */
 DJL_KEEPALIVE,       /* 50000 0x06 */
 DJL_DEFENSE,         /* 50000 0x08 */
 DJL_FADER_START,     /* 50001 0x02 */
 DJL_ON_AIR,          /* 50001 0x03 */
 DJL_POSITION,        /* 50001 0x0b */
 DJL_MASTER_REQUEST,  /* 50001 0x26 */
 DJL_MASTER_RESPONSE, /* 50001 or 50002 0x27 */
 DJL_BEAT,            /* 50001 0x28 */
 DJL_SYNC_CONTROL,    /* 50001 0x2a */
 DJL_MEDIA_QUERY,     /* 50002 0x05 */
 DJL_STATUS,          /* 50002 0x0a */
 DJL_LOAD_TRACK,      /* 50002 0x19 */
 DJL_MIXER_STATUS     /* 50002 0x29 */
};

enum djl_sync_command { DJL_SYNC_ON = 0x10, DJL_SYNC_OFF = 0x20, DJL_BECOME_MASTER = 0x01 };
enum djl_slot { DJL_SLOT_NONE = 0, DJL_SLOT_CD = 1, DJL_SLOT_SD = 2, DJL_SLOT_USB = 3, DJL_SLOT_COLLECTION = 4 };
enum djl_track_type { DJL_TRACK_NONE = 0, DJL_TRACK_REKORDBOX = 1, DJL_TRACK_UNANALYZED = 2 };
/* Values of status byte 0x7b. */
enum djl_play {
 DJL_PLAY_EMPTY = 0x00, DJL_PLAY_LOADING = 0x02, DJL_PLAY_PLAYING = 0x03, DJL_PLAY_LOOPING = 0x04,
 DJL_PLAY_PAUSED = 0x05, DJL_PLAY_CUED = 0x06, DJL_PLAY_CUE_PLAY = 0x07, DJL_PLAY_CUE_SCRATCH = 0x08,
 DJL_PLAY_SEARCHING = 0x09, DJL_PLAY_ENDED = 0x11
};
#define DJL_FLAG_PLAYING 0x40
#define DJL_FLAG_MASTER 0x20
#define DJL_FLAG_SYNC 0x10
#define DJL_FLAG_ON_AIR 0x08
#define DJL_FLAG_BPM_SYNC 0x02

struct djl_track_ref { uint8_t player, slot, type; uint32_t rekordbox_id; };

struct djl_packet {
 enum djl_kind kind;
 uint8_t device;            /* sender's device number; 0 when the kind carries none */
 char name[DJL_NAME + 1];
 union {
  struct { uint8_t n, device_class, mac[6]; } claim1;
  struct { uint8_t ip[4], mac[6], device, n, device_class, auto_assign; } claim2;
  struct { uint8_t device, n; } claim3;
  struct { uint8_t device, mac[6], ip[4], peers, device_class, model; } keepalive;
  struct { uint8_t device, ip[4]; } defense;
  struct { uint8_t command[4]; } fader_start;               /* 0 start, 1 stop, 2 unchanged */
  struct { uint8_t channels, on[6]; } on_air;               /* channels is 4 or 6 */
  struct { uint32_t track_seconds, playhead_ms, bpm_x10; int32_t pitch_x100; } position;
  struct { uint8_t requester; } master_request;
  struct { uint8_t accepted; } master_response;
  struct { uint32_t next_beat_ms, second_beat_ms, next_bar_ms, fourth_beat_ms, second_bar_ms, eighth_beat_ms,
           pitch_q20; uint16_t bpm_x100; uint8_t beat_in_bar; } beat;
  struct { uint8_t command; } sync_control;                 /* enum djl_sync_command */
  struct { uint8_t reply_ip[4], player, slot; } media_query;
  struct {
   uint16_t length;                                         /* whole datagram */
   uint8_t active, play, flags, master_mode, handoff_to, beat_in_bar, firmware[4];
   struct djl_track_ref track;
   uint32_t sync_n, pitch_q20, fader_pitch_q20, beat_number, packet_counter;
   uint16_t bpm_x100, tempo_valid;                          /* tempo_valid is byte 0x90: 0x8000 when usable */
  } status;
  struct { uint8_t source, target_deck; struct djl_track_ref track; } load_track; /* target_deck = byte 0x40 + 1 */
  struct { uint8_t flags, handoff_to, beat_in_bar; uint16_t bpm_x100; uint32_t pitch_q20; } mixer_status;
 } u;
};

/* Decodes one datagram received on `port`. Returns 1 and fills `out` for a well-formed packet of a
   known kind, else 0. Never reads past `len`; never trusts a length field over `len`. */
int djl_parse(uint16_t port, const uint8_t *data, size_t len, struct djl_packet *out);

/* ---- identity --------------------------------------------------------------------------- */

/* The constant bytes a device shows. xz_deck() gives the values captured from the stock XZ decks. */
struct djl_identity {
 char name[DJL_NAME + 1];
 uint8_t device, mac[6], ip[4];
 uint8_t structure;      /* announce byte 0x21: stock XZ sends 0x03 */
 uint8_t device_class;   /* 1 player, 2 mixer */
 uint8_t model;          /* keep-alive byte 0x35 */
 uint8_t status_subtype; /* status byte 0x20 */
 uint8_t play2_playing, play2_stopped; /* status byte 0x8b: stock XZ 0x9a / 0x9e */
 uint8_t nx;             /* status byte 0xcc: stock XZ 0x1f */
 uint8_t firmware[4];
 uint16_t status_length; /* whole datagram; the stock XZ decks send 0x124 */
};
/* Device `number` (3 or 4) on a unit whose real MAC and address are given. The MAC continues the
   stock pattern: locally administered bit set, last octet = real + (number - 1), plus DJL_XZ_MAC_SKIP
   for numbers 3 and up because the stock mixer (device 33) already shows real + 2. */
#define DJL_XZ_MAC_SKIP 1
void djl_identity_xz_deck(struct djl_identity *id, uint8_t number, const uint8_t unit_mac[6], const uint8_t ip[4]);

/* ---- what a deck says about itself ------------------------------------------------------- */

#define DJL_AHEAD 12
/* Where the heard playhead is, and the beats ahead of it in track time (0 % pitch). */
struct djl_timeline {
 uint64_t captured_us;          /* monotonic time at which playhead_us was heard */
 int64_t playhead_us;           /* track time */
 int32_t speed_q20;             /* heard speed, DJL_PITCH_UNITY = 1.0; 0 when not advancing */
 uint32_t track_ms;             /* total length */
 uint32_t next_beat;            /* 1-based number of ahead_ms[0]; 0 when there is no grid */
 uint32_t ahead_ms[DJL_AHEAD];  /* track time of each upcoming beat, DJL_NO_TIME past the end */
 uint8_t ahead_bar[DJL_AHEAD];  /* beat within bar, 1..4 */
};
struct djl_deck_facts {
 uint32_t content_epoch;        /* changes on every load or unload */
 struct djl_track_ref track;    /* type DJL_TRACK_NONE when empty */
 uint8_t play;                  /* enum djl_play */
 uint8_t master, sync, on_air, bpm_only_sync;
 uint8_t wants_master;          /* the DJ asked for MASTER and it is not granted yet */
 uint16_t track_bpm_x100;       /* at the heard position; 0xffff unknown */
 uint32_t pitch_q20;            /* effective */
 uint32_t fader_pitch_q20;
 struct djl_timeline timeline;
};

/* ---- network view shared by the local players ------------------------------------------- */

#define DJL_PEERS 16
struct djl_peer {
 uint8_t device, ip[4], mac[6], device_class, in_use;
 char name[DJL_NAME + 1];
 uint64_t seen_us, status_us;
 uint8_t flags, master_mode, handoff_to;   /* from its latest status */
 uint32_t sync_n;
};
struct djl_net {
 struct djl_peer peers[DJL_PEERS];
 uint8_t local_ip[4];
 uint8_t on_air[6], on_air_known;
 uint64_t on_air_us;
};
void djl_net_init(struct djl_net *, const uint8_t local_ip[4]);
/* Records a packet from another host. Packets whose source is local_ip are ignored. */
void djl_net_receive(struct djl_net *, uint64_t now_us, const uint8_t src_ip[4], const uint8_t src_mac[6],
                     const struct djl_packet *);
/* Drops peers silent for 10 s. */
void djl_net_expire(struct djl_net *, uint64_t now_us);
/* The peer currently flagged tempo master, or NULL. */
const struct djl_peer *djl_net_master(const struct djl_net *);
uint32_t djl_net_highest_sync_n(const struct djl_net *);

/* ---- one local player ------------------------------------------------------------------- */

enum djl_phase { DJL_OFF, DJL_HELLOING, DJL_CLAIMING1, DJL_CLAIMING2, DJL_CLAIMING3, DJL_RUNNING, DJL_EVICTED };
enum djl_command_kind {
 DJL_CMD_SYNC_ON, DJL_CMD_SYNC_OFF,
 DJL_CMD_TAKE_MASTER,   /* a remote asked this deck to become tempo master */
 DJL_CMD_MASTER_GRANTED,/* the previous master yielded to this deck */
 DJL_CMD_YIELD_MASTER,  /* a remote device took over; `device` is the new master */
 DJL_CMD_LOAD,
 DJL_CMD_FADER_START, DJL_CMD_FADER_STOP,
 DJL_CMD_ON_AIR         /* `value` 0 or 1 */
};
struct djl_command { enum djl_command_kind kind; uint8_t device, value; struct djl_track_ref track; };

struct djl_sink {
 void *context;
 /* dst_ip NULL means the subnet broadcast address. */
 void (*send)(void *context, const struct djl_identity *from, const uint8_t *dst_ip, uint16_t port,
              const uint8_t *data, size_t len);
 void (*command)(void *context, uint8_t local_device, const struct djl_command *);
};

struct djl_player {
 struct djl_identity id;
 enum djl_phase phase;
 uint8_t step;                   /* 0..2 within a claim stage */
 uint64_t due_us, keepalive_us, status_us, position_us;
 uint32_t sync_n, packet_counter;
 uint32_t beat_epoch, beat_sent; /* last beat number announced for content beat_epoch */
 uint8_t master_on_wire;         /* our status currently carries the master flag */
 uint8_t yielding_to;            /* DJL_NO_DEVICE, or the requester named in our status byte 0x9f */
 uint8_t asking;                 /* DJL_NO_DEVICE, or the master we sent a request to */
 uint64_t asked_us;
 uint8_t granted;                /* 1: that master answered yes; 2: MASTER_GRANTED sent at asked_us */
 /* What our status says about master during a handoff is decided here, not by when the player
    catches up. yield_us: when we decided to yield (0 none); for 3 s from then the deck is not master
    whatever facts.master says, until facts.master clears. grant_us: when MASTER_GRANTED started the
    current grant (0 none); for 3 s from then the deck is master until facts.master is set. */
 uint64_t yield_us, yield_cmd_us; /* yield_cmd_us: last YIELD_MASTER sent to the player */
 uint64_t grant_us;
 uint8_t on_air;                 /* last on-air value seen for our channel; 0xff none yet */
 /* The next beat as the last step saw it, so a beat the caller's refreshed timeline has already
    moved past is still announced once. */
 uint32_t pending_beat, pending_ms;
 uint8_t pending_bar;
 uint64_t pending_us;
 struct djl_track_ref load_track; /* last LOAD delivered, to drop duplicate deliveries */
 uint64_t load_us;
};

void djl_player_init(struct djl_player *, const struct djl_identity *);
/* Starts the claim for id.device. Calling it while not OFF or EVICTED does nothing. */
void djl_player_start(struct djl_player *, uint64_t now_us);
/* Stops announcing at once. */
void djl_player_stop(struct djl_player *);
/* Advances timers: claim packets every 300 ms, keep-alive every 2 s, status every 200 ms (kept
   10 ms clear after and 5 ms clear before a beat), one beat packet at each heard beat while playing
   with a grid, and the tempo-master handoff. Returns the monotonic time of the next call it needs;
   calling earlier is harmless.
   Handoff and the master flag on the wire: once this player decides to yield (a remote asserts
   master while we are master), YIELD_MASTER goes to the player once and every status says not
   master until facts.master clears, for at most 3 s; if facts.master is still set then, the wire
   follows the facts again and YIELD_MASTER is repeated at most once a second while a remote still
   asserts master. Once MASTER_GRANTED goes to the player, every status says master until
   facts.master is set, for at most 3 s per want of master (a refused grant is not claimed again
   until facts.master is set or wants_master clears). During either window the deck counts as
   what the wire says for every other handoff rule. */
uint64_t djl_player_step(struct djl_player *, uint64_t now_us, const struct djl_deck_facts *,
                         const struct djl_net *, const struct djl_sink *);
/* Handles one packet from another host: defends the device number, answers handoff requests, and
   turns sync, master, load, fader and on-air packets aimed at this player into commands. Call
   djl_player_step afterwards: a receive can change what is due. */
void djl_player_receive(struct djl_player *, uint64_t now_us, const uint8_t src_ip[4], const struct djl_packet *,
                        const struct djl_deck_facts *, const struct djl_net *, const struct djl_sink *);

/* ---- builders (exposed for tests and for the judge fixtures) ----------------------------- */

size_t djl_build_hello(const struct djl_identity *, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_claim1(const struct djl_identity *, uint8_t n, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_claim2(const struct djl_identity *, uint8_t n, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_claim3(const struct djl_identity *, uint8_t n, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_keepalive(const struct djl_identity *, uint8_t peers, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_defense(const struct djl_identity *, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_assign_finished(const struct djl_identity *, uint8_t out[DJL_MAX_DATAGRAM]);
/* The beat at timeline.ahead_ms[index]. Returns 0 when that entry is DJL_NO_TIME. */
size_t djl_build_beat(const struct djl_identity *, const struct djl_deck_facts *, unsigned index,
                      uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_position(const struct djl_identity *, const struct djl_deck_facts *, uint64_t now_us,
                          uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_status(const struct djl_player *, const struct djl_deck_facts *, const struct djl_net *,
                        uint64_t now_us, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_master_request(const struct djl_identity *, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_master_response(const struct djl_identity *, uint8_t out[DJL_MAX_DATAGRAM]);
size_t djl_build_load_ack(const struct djl_identity *, uint8_t out[DJL_MAX_DATAGRAM]);

/* ---- timeline helpers -------------------------------------------------------------------- */

/* Monotonic time at which ahead_ms[index] is heard at the current speed, or 0 when it never is
   (stopped, reverse, or no such beat). */
uint64_t djl_beat_time_us(const struct djl_timeline *, unsigned index);
/* Track position in ms heard at now_us, clamped to 0..track_ms. */
uint32_t djl_playhead_ms(const struct djl_timeline *, uint64_t now_us);

#endif
