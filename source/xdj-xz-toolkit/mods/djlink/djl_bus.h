/* SPDX-License-Identifier: MIT */
/* Shared-memory bus between the player (rbp mod, or the lab stand-in) and the xz-djlink helper.

   One file, normally /dev/shm/xz-djlink/bus, created and sized by the player. The player writes
   the header last, so a helper that sees the right magic, version and size sees a complete layout.

   Sections and their single writers:
     deck[2]  player -> helper   sequence lock
     net      helper -> player   sequence lock
     ring     helper -> player   head written by the helper, tail by the player

   Fixed-width fields only and no pointers: the helper is a 32-bit ARM musl binary on the unit and
   x86_64 glibc in the lab, the player is 32-bit ARM glibc. The asserts below pin every section
   offset, so two builds that disagree on the layout fail to compile. */
#ifndef XZ_DJL_BUS_H
#define XZ_DJL_BUS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "djl.h"

#define DJL_BUS_MAGIC 0x4c4a445au /* "ZDJL" little-endian */
#define DJL_BUS_VERSION 1u
#define DJL_BUS_DECKS 2
#define DJL_BUS_PEERS 16
#define DJL_BUS_RING 32u /* power of two */
#define DJL_BUS_PATH "/dev/shm/xz-djlink/bus"

struct djl_bus_header {
 uint32_t magic, version, size, reserved;
};

/* Player -> helper, one per software deck. */
struct djl_bus_deck {
 uint32_t seq;                  /* odd while the player is writing */
 uint8_t announce;              /* 0: keep this deck off the network */
 uint8_t device;                /* 3 or 4 */
 uint8_t reserved[2];
 struct djl_deck_facts facts;
};

struct djl_bus_peer {
 uint8_t in_use, device, device_class, flags; /* flags: status byte 0x89 or mixer byte 0x27 */
 uint8_t ip[4];
 uint16_t bpm_x100;             /* 0xffff unknown */
 uint8_t master_mode, handoff_to;
 char name[DJL_NAME];           /* NUL padded, not terminated */
};

/* The external tempo master as heard on the wire. */
struct djl_bus_clock {
 uint8_t valid, device, beat_in_bar, reserved0;
 uint16_t bpm_x100;
 uint16_t reserved1;
 uint32_t pitch_q20;
 uint32_t next_beat_ms;         /* from its latest beat packet */
 uint32_t beats;                /* beat packets received from it; changes on every new beat */
 uint32_t reserved2;
 uint64_t beat_rx_us;           /* CLOCK_MONOTONIC receive time of that packet */
};

/* Helper -> player. */
struct djl_bus_net {
 uint32_t seq;
 int32_t helper_pid;
 uint64_t heartbeat_us;         /* CLOCK_MONOTONIC, refreshed every 100 ms */
 uint8_t unit_ip[4], broadcast_ip[4];
 uint8_t unit_mac[6];
 uint8_t identity_ok;           /* a stock deck 1 keep-alive was seen leaving in the last 6 s */
 uint8_t reserved0;
 uint8_t phase[DJL_BUS_DECKS];  /* enum djl_phase */
 uint8_t reserved1[2];
 uint32_t sync_dropped;         /* sync-control packets not delivered: zero or two decks loaded */
 uint32_t ring_dropped;         /* commands lost to a full ring */
 uint32_t rx_frames, rx_rejected;
 uint32_t rx_drops;             /* frames the kernel dropped at the helper's packet socket */
 struct djl_bus_peer peers[DJL_BUS_PEERS];
 struct djl_bus_clock master;
};

/* struct djl_command with a fixed-width kind (an enum's size is the compiler's choice). */
struct djl_bus_command {
 uint8_t local_device;          /* 3 or 4 */
 uint8_t kind;                  /* enum djl_command_kind */
 uint8_t device, value;
 struct djl_track_ref track;
};

struct djl_bus_ring {
 uint32_t head;                 /* next slot the helper writes; free-running */
 uint32_t tail;                 /* next slot the player reads; free-running */
 struct djl_bus_command slot[DJL_BUS_RING];
};

struct djl_bus {
 struct djl_bus_header header;
 struct djl_bus_deck deck[DJL_BUS_DECKS];
 struct djl_bus_net net;
 struct djl_bus_ring ring;
};

_Static_assert(sizeof(struct djl_track_ref) == 8, "djl_track_ref layout");
_Static_assert(sizeof(struct djl_timeline) == 88, "djl_timeline layout");
_Static_assert(offsetof(struct djl_deck_facts, timeline) == 32, "djl_deck_facts layout");
_Static_assert(sizeof(struct djl_deck_facts) == 120, "djl_deck_facts layout");
_Static_assert(offsetof(struct djl_bus_deck, facts) == 8 && sizeof(struct djl_bus_deck) == 128, "deck section");
_Static_assert(sizeof(struct djl_bus_peer) == 32, "peer layout");
_Static_assert(sizeof(struct djl_bus_clock) == 32, "clock layout");
_Static_assert(offsetof(struct djl_bus_net, peers) == 56 && sizeof(struct djl_bus_net) == 600, "net section");
_Static_assert(sizeof(struct djl_bus_command) == 12, "command layout");
_Static_assert(sizeof(struct djl_bus_ring) == 8 + 12 * DJL_BUS_RING, "ring section");
_Static_assert(offsetof(struct djl_bus, deck) == 16, "deck offset");
_Static_assert(offsetof(struct djl_bus, net) == 272, "net offset");
_Static_assert(offsetof(struct djl_bus, ring) == 872, "ring offset");
_Static_assert(sizeof(struct djl_bus) == 1264, "bus size");

/* ---- sequence lock ---------------------------------------------------------------------- */

/* Writer side; one writer per section. */
static inline void djl_seq_write_begin(uint32_t *seq)
{
 __atomic_store_n(seq, __atomic_load_n(seq, __ATOMIC_RELAXED) + 1u, __ATOMIC_RELAXED);
 __atomic_thread_fence(__ATOMIC_RELEASE);
}
static inline void djl_seq_write_end(uint32_t *seq)
{
 __atomic_store_n(seq, __atomic_load_n(seq, __ATOMIC_RELAXED) + 1u, __ATOMIC_RELEASE);
}

/* Copies `len` bytes at `src`, guarded by `seq`, into `dst`. Returns the even sequence value of the
   copy, or 1 (never a valid value) when no consistent copy was made within `tries` attempts. */
static inline uint32_t djl_seq_read(const uint32_t *seq, void *dst, const void *src, size_t len, unsigned tries)
{
 while (tries--) {
  uint32_t before = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
  if (before & 1u) continue;
  /* Byte-wise atomic loads: the writer may change these bytes during the copy, and a plain
     memcpy of memory another process writes is a data race the compiler may exploit. */
  for (size_t i = 0; i < len; i++)
   ((uint8_t *)dst)[i] = __atomic_load_n((const uint8_t *)src + i, __ATOMIC_RELAXED);
  __atomic_thread_fence(__ATOMIC_ACQUIRE);
  if (__atomic_load_n(seq, __ATOMIC_RELAXED) == before) return before;
 }
 return 1u;
}

static inline uint32_t djl_bus_read_deck(const struct djl_bus_deck *d, struct djl_bus_deck *out)
{
 return djl_seq_read(&d->seq, out, d, sizeof *out, 64);
}
static inline uint32_t djl_bus_read_net(const struct djl_bus_net *n, struct djl_bus_net *out)
{
 return djl_seq_read(&n->seq, out, n, sizeof *n, 64);
}
/* Writes everything after `seq` from `src` (whose own seq field is ignored). */
static inline void djl_bus_write_deck(struct djl_bus_deck *d, const struct djl_bus_deck *src)
{
 djl_seq_write_begin(&d->seq);
 for (size_t i = sizeof d->seq; i < sizeof *d; i++)
  __atomic_store_n((uint8_t *)d + i, ((const uint8_t *)src)[i], __ATOMIC_RELAXED);
 djl_seq_write_end(&d->seq);
}
static inline void djl_bus_write_net(struct djl_bus_net *n, const struct djl_bus_net *src)
{
 djl_seq_write_begin(&n->seq);
 for (size_t i = sizeof n->seq; i < sizeof *n; i++)
  __atomic_store_n((uint8_t *)n + i, ((const uint8_t *)src)[i], __ATOMIC_RELAXED);
 djl_seq_write_end(&n->seq);
}

/* ---- command ring ----------------------------------------------------------------------- */

/* Helper side. Returns 0 when the ring is full. */
static inline int djl_bus_ring_push(struct djl_bus_ring *r, const struct djl_bus_command *c)
{
 uint32_t head = __atomic_load_n(&r->head, __ATOMIC_RELAXED);
 if (head - __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE) >= DJL_BUS_RING) return 0;
 struct djl_bus_command *s = &r->slot[head & (DJL_BUS_RING - 1)];
 for (size_t i = 0; i < sizeof *s; i++) __atomic_store_n((uint8_t *)s + i, ((const uint8_t *)c)[i], __ATOMIC_RELAXED);
 __atomic_store_n(&r->head, head + 1u, __ATOMIC_RELEASE);
 return 1;
}
/* Player side. Returns 0 when the ring is empty. A head more than a ring ahead of the tail means
   the helper restarted or the counters are corrupt; the reader then skips to the head. */
static inline int djl_bus_ring_pop(struct djl_bus_ring *r, struct djl_bus_command *out)
{
 uint32_t tail = __atomic_load_n(&r->tail, __ATOMIC_RELAXED);
 uint32_t head = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE);
 if (head == tail) return 0;
 if (head - tail > DJL_BUS_RING) {
  __atomic_store_n(&r->tail, head, __ATOMIC_RELEASE);
  return 0;
 }
 const struct djl_bus_command *s = &r->slot[tail & (DJL_BUS_RING - 1)];
 for (size_t i = 0; i < sizeof *out; i++) ((uint8_t *)out)[i] = __atomic_load_n((const uint8_t *)s + i, __ATOMIC_RELAXED);
 __atomic_store_n(&r->tail, tail + 1u, __ATOMIC_RELEASE);
 return 1;
}

static inline void djl_bus_command_from(struct djl_bus_command *out, uint8_t local_device, const struct djl_command *c)
{
 memset(out, 0, sizeof *out);
 out->local_device = local_device;
 out->kind = (uint8_t)c->kind;
 out->device = c->device;
 out->value = c->value;
 out->track = c->track;
}

/* 1 when the header names this layout. */
static inline int djl_bus_header_ok(const struct djl_bus *b)
{
 return __atomic_load_n(&b->header.magic, __ATOMIC_ACQUIRE) == DJL_BUS_MAGIC && b->header.version == DJL_BUS_VERSION &&
        b->header.size == sizeof(struct djl_bus);
}

#endif
