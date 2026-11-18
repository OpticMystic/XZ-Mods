/* SPDX-License-Identifier: MIT */
#include "../djl.h"
#include <stdint.h>
#include "fixtures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c)                                                                                  \
 do {                                                                                             \
  checks++;                                                                                       \
  if (!(c)) {                                                                                     \
   if (++failures <= 40) fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);   \
  }                                                                                               \
 } while (0)

static const uint8_t UNIT_MAC[6] = {0x00, 0x00, 0x5e, 0x00, 0x53, 0x3a};
static const uint8_t XZ_IP[4] = {192, 0, 2, 147};
static const uint8_t PEER_IP[4] = {192, 0, 2, 50};
static const uint8_t PEER2_IP[4] = {192, 0, 2, 51};
static const uint8_t PEER_MAC[6] = {0x00, 0xe0, 0x36, 0x01, 0x02, 0x03};

/* ---- recording sink ------------------------------------------------------------------------ */

struct sent { uint64_t t; int broadcast; uint8_t ip[4], from; uint16_t port; size_t len; uint8_t data[DJL_MAX_DATAGRAM]; };
#define MAX_SENT 20000
static struct sent sent[MAX_SENT];
static unsigned nsent, overflow;
struct cmdrec { uint64_t t; uint8_t local; struct djl_command c; };
static struct cmdrec cmds[512];
static unsigned ncmds;
static uint64_t g_now;

static void rec_send(void *ctx, const struct djl_identity *from, const uint8_t *dst, uint16_t port, const uint8_t *data,
                     size_t len)
{
 (void)ctx;
 CHECK(len >= 0x24 && len <= DJL_MAX_DATAGRAM);
 if (nsent >= MAX_SENT || len > DJL_MAX_DATAGRAM) { overflow++; return; }
 struct sent *s = &sent[nsent++];
 s->t = g_now;
 s->broadcast = dst == NULL;
 if (dst) memcpy(s->ip, dst, 4);
 s->from = from->device;
 s->port = port;
 s->len = len;
 memcpy(s->data, data, len);
}
static void rec_command(void *ctx, uint8_t local, const struct djl_command *c)
{
 (void)ctx;
 if (ncmds < sizeof cmds / sizeof cmds[0]) cmds[ncmds++] = (struct cmdrec){g_now, local, *c};
}
static const struct djl_sink sink = {NULL, rec_send, rec_command};
static void reset_rec(void) { nsent = ncmds = 0; }
static int sent_kind(const struct sent *s, struct djl_packet *p) { return djl_parse(s->port, s->data, s->len, p) ? (int)p->kind : -1; }
static unsigned count_kind(enum djl_kind k)
{
 unsigned n = 0;
 struct djl_packet p;
 for (unsigned i = 0; i < nsent; i++) n += sent_kind(&sent[i], &p) == (int)k;
 return n;
}
static int last_status(struct djl_packet *p)
{
 for (unsigned i = nsent; i-- > 0;)
  if (sent_kind(&sent[i], p) == DJL_STATUS) return 1;
 return 0;
}
static unsigned count_cmd(enum djl_command_kind k)
{
 unsigned n = 0;
 for (unsigned i = 0; i < ncmds; i++) n += cmds[i].c.kind == k;
 return n;
}

/* ---- helpers ----------------------------------------------------------------------------- */

static struct djl_identity stock_identity(uint8_t device)
{
 struct djl_identity id;
 memset(&id, 0, sizeof id);
 memcpy(id.name, "XDJ-XZ", 7);
 id.device = device;
 memcpy(id.mac, UNIT_MAC, 6);
 if (device != 1) id.mac[0] |= 0x02; /* locally administered, as the unit does */
 id.mac[5] = (uint8_t)(UNIT_MAC[5] + (device == 1 ? 0 : device == 2 ? 1 : 2));
 memcpy(id.ip, XZ_IP, 4);
 id.structure = 3;
 id.device_class = device == 33 ? 2 : 1;
 id.model = device == 33 ? 1 : 0;
 return id;
}
static struct djl_identity remote_identity(uint8_t device, const uint8_t ip[4])
{
 struct djl_identity id;
 djl_identity_xz_deck(&id, device, PEER_MAC, ip);
 memcpy(id.name, "CDJ-2000NXS2", 13);
 id.play2_playing = 0xfa;
 id.play2_stopped = 0xfe;
 return id;
}
static void parse_ok(uint16_t port, const uint8_t *b, size_t len, struct djl_packet *p)
{
 int ok = djl_parse(port, b, len, p);
 CHECK(ok);
}
static size_t raw_packet(uint8_t kind, uint8_t device, uint8_t subtype, size_t len, uint8_t *b)
{
 memset(b, 0, len);
 memcpy(b, "Qspt1WmJOL", 10);
 b[0x0a] = kind;
 memcpy(b + 0x0b, "DJM-900NXS2", 11);
 b[0x1f] = 1;
 b[0x20] = subtype;
 b[0x21] = device;
 b[0x22] = (uint8_t)((len - 0x24) >> 8);
 b[0x23] = (uint8_t)(len - 0x24);
 return len;
}
static void empty_facts(struct djl_deck_facts *f)
{
 memset(f, 0, sizeof *f);
 f->track_bpm_x100 = 0xffff;
 f->pitch_q20 = f->fader_pitch_q20 = DJL_PITCH_UNITY;
 for (unsigned i = 0; i < DJL_AHEAD; i++) f->timeline.ahead_ms[i] = DJL_NO_TIME;
}
static void remote_status(uint8_t device, const uint8_t ip[4], uint8_t flags, uint8_t handoff, uint32_t sync_n,
                          struct djl_packet *out)
{
 struct djl_identity id = remote_identity(device, ip);
 struct djl_player rp;
 struct djl_deck_facts f;
 uint8_t b[DJL_MAX_DATAGRAM];
 djl_player_init(&rp, &id);
 empty_facts(&f);
 size_t len = djl_build_status(&rp, &f, NULL, 0, b);
 b[0x89] = flags;
 b[0x9f] = handoff;
 b[0x84] = (uint8_t)(sync_n >> 24); b[0x85] = (uint8_t)(sync_n >> 16); b[0x86] = (uint8_t)(sync_n >> 8); b[0x87] = (uint8_t)sync_n;
 parse_ok(DJL_PORT_STATUS, b, len, out);
}

/* ---- 1. capture fixtures ----------------------------------------------------------------- */

static int beat_facts(const struct djl_packet *p, struct djl_deck_facts *f)
{
 empty_facts(f);
 const uint32_t base = 100000;
 int set[DJL_AHEAD] = {0}, consistent = 1;
 uint8_t b = p->u.beat.beat_in_bar;
 const struct { unsigned at; uint32_t ms; } known[] = {
  {0, 0}, {1, p->u.beat.next_beat_ms}, {2, p->u.beat.second_beat_ms}, {4, p->u.beat.fourth_beat_ms},
  {8, p->u.beat.eighth_beat_ms}, {5u - b, p->u.beat.next_bar_ms}, {9u - b, p->u.beat.second_bar_ms}};
 for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++) {
  unsigned at = known[i].at;
  if (set[at] && f->timeline.ahead_ms[at] != base + known[i].ms) consistent = 0;
  f->timeline.ahead_ms[at] = base + known[i].ms;
  set[at] = 1;
 }
 for (unsigned i = 1; i < DJL_AHEAD; i++)
  if (!set[i]) f->timeline.ahead_ms[i] = f->timeline.ahead_ms[i - 1] + 1;
 for (unsigned i = 0; i < DJL_AHEAD; i++) f->timeline.ahead_bar[i] = (uint8_t)((b - 1 + i) % 4 + 1);
 f->timeline.next_beat = 1;
 f->timeline.speed_q20 = (int32_t)p->u.beat.pitch_q20;
 f->track.type = DJL_TRACK_REKORDBOX;
 f->pitch_q20 = p->u.beat.pitch_q20;
 f->track_bpm_x100 = p->u.beat.bpm_x100;
 return consistent;
}

static void test_fixtures(void)
{
 unsigned keepalives = 0, beats = 0, devices = 0;
 uint8_t previous_bar = 0;
 for (unsigned i = 0; i < FIXTURES; i++) {
  const struct fixture *x = &fixtures[i];
  struct djl_packet p;
  uint8_t out[DJL_MAX_DATAGRAM];
  parse_ok((uint16_t)x->port, x->bytes, x->len, &p);
  CHECK(!strcmp(p.name, "XDJ-XZ"));
  if (x->port == DJL_PORT_ANNOUNCE) {
   keepalives++;
   CHECK(p.kind == DJL_KEEPALIVE);
   CHECK(p.device == 1 || p.device == 2 || p.device == 33);
   devices |= p.device == 1 ? 1u : p.device == 2 ? 2u : 4u;
   struct djl_identity id = stock_identity(p.device);
   CHECK(!memcmp(p.u.keepalive.mac, id.mac, 6));
   CHECK(!memcmp(p.u.keepalive.ip, XZ_IP, 4));
   CHECK(p.u.keepalive.peers == 1);
   CHECK(p.u.keepalive.device_class == (p.device == 33 ? 2 : 1));
   CHECK(p.u.keepalive.model == (p.device == 33 ? 1 : 0));
   size_t len = djl_build_keepalive(&id, 1, out);
   CHECK(len == x->len && !memcmp(out, x->bytes, len));
   if (p.device == 2) {
    struct djl_identity xz;
    djl_identity_xz_deck(&xz, 2, UNIT_MAC, XZ_IP);
    len = djl_build_keepalive(&xz, 1, out);
    CHECK(len == x->len && !memcmp(out, x->bytes, len));
   }
  } else {
   beats++;
   CHECK(p.kind == DJL_BEAT && p.device == 1);
   CHECK(p.u.beat.next_beat_ms == 428 || p.u.beat.next_beat_ms == 429);
   CHECK(p.u.beat.pitch_q20 == 0x113333 && p.u.beat.bpm_x100 == 14000);
   CHECK(p.u.beat.beat_in_bar >= 1 && p.u.beat.beat_in_bar <= 4);
   if (previous_bar) CHECK(p.u.beat.beat_in_bar == previous_bar % 4 + 1);
   previous_bar = p.u.beat.beat_in_bar;
   struct djl_deck_facts f;
   CHECK(beat_facts(&p, &f));
   struct djl_identity id = stock_identity(1);
   size_t len = djl_build_beat(&id, &f, 0, out);
   CHECK(len == 96 && len == x->len && !memcmp(out, x->bytes, len));
  }
 }
 CHECK(keepalives == 12 && beats == 20 && devices == 7);
 struct djl_packet first;
 parse_ok(DJL_PORT_BEAT, fixtures[0].bytes, fixtures[0].len, &first);
 CHECK(first.u.beat.next_beat_ms == 429 && first.u.beat.second_beat_ms == 857 && first.u.beat.next_bar_ms == 857);
 CHECK(first.u.beat.fourth_beat_ms == 1715 && first.u.beat.second_bar_ms == 2572 && first.u.beat.eighth_beat_ms == 3429);
 CHECK(first.u.beat.beat_in_bar == 3);

 const uint8_t mac3[6] = {0x02, 0x00, 0x5e, 0x00, 0x53, 0x3d}, mac4[6] = {0x02, 0x00, 0x5e, 0x00, 0x53, 0x3e};
 const uint8_t mixer_mac[6] = {0x02, 0x00, 0x5e, 0x00, 0x53, 0x3c};
 for (uint8_t n = 3; n <= 4; n++) {
  struct djl_identity id;
  djl_identity_xz_deck(&id, n, UNIT_MAC, XZ_IP);
  CHECK(!memcmp(id.mac, n == 3 ? mac3 : mac4, 6));
  CHECK(memcmp(id.mac, mixer_mac, 6));
  CHECK(!strcmp(id.name, "XDJ-XZ") && id.device == n && !memcmp(id.ip, XZ_IP, 4));
  CHECK(id.structure == 3 && id.device_class == 1 && id.model == 0 && id.status_subtype == 3);
  CHECK(id.play2_playing == 0x9a && id.play2_stopped == 0x9e && id.nx == 0x1f && id.status_length == 0x124);
  CHECK(!memcmp(id.firmware, "1.26", 4));
 }
}

/* ---- 2. round trip and length variants --------------------------------------------------- */

static void loaded_facts(struct djl_deck_facts *f)
{
 empty_facts(f);
 f->content_epoch = 7;
 f->track = (struct djl_track_ref){1, DJL_SLOT_USB, DJL_TRACK_REKORDBOX, 4242};
 f->play = DJL_PLAY_PLAYING;
 f->master = 1;
 f->sync = 1;
 f->on_air = 1;
 f->track_bpm_x100 = 12800;
 f->pitch_q20 = 0x107ae1;
 f->fader_pitch_q20 = 0x105000;
 struct djl_timeline *t = &f->timeline;
 t->captured_us = 5000000;
 t->playhead_us = 10100000;
 t->speed_q20 = 0x107ae1;
 t->track_ms = 300500;
 t->next_beat = 23;
 for (unsigned i = 0; i < DJL_AHEAD; i++) {
  t->ahead_ms[i] = 10313 + 469 * i;
  t->ahead_bar[i] = (uint8_t)((2 + i) % 4 + 1);
 }
}

static void test_roundtrip(void)
{
 struct djl_identity id;
 djl_identity_xz_deck(&id, 3, UNIT_MAC, XZ_IP);
 uint8_t b[DJL_MAX_DATAGRAM];
 struct djl_packet p;
 size_t len;

 len = djl_build_hello(&id, b);
 CHECK(len == 0x26 && b[0x21] == 4 && b[0x24] == 1 && b[0x25] == 0x40);
 parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
 CHECK(p.kind == DJL_HELLO && !strcmp(p.name, "XDJ-XZ"));
 for (uint8_t n = 1; n <= 3; n++) {
  len = djl_build_claim1(&id, n, b);
  parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
  CHECK(len == 0x2c && p.kind == DJL_CLAIM1 && p.u.claim1.n == n && p.u.claim1.device_class == 1 && !memcmp(p.u.claim1.mac, id.mac, 6));
  len = djl_build_claim2(&id, n, b);
  parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
  CHECK(len == 0x32 && p.kind == DJL_CLAIM2 && p.device == 3 && p.u.claim2.device == 3 && p.u.claim2.n == n);
  CHECK(!memcmp(p.u.claim2.ip, XZ_IP, 4) && !memcmp(p.u.claim2.mac, id.mac, 6) && p.u.claim2.auto_assign == 2 && p.u.claim2.device_class == 1);
  len = djl_build_claim3(&id, n, b);
  parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
  CHECK(len == 0x26 && p.kind == DJL_CLAIM3 && p.u.claim3.device == 3 && p.u.claim3.n == n);
 }
 len = djl_build_keepalive(&id, 4, b);
 parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
 CHECK(p.kind == DJL_KEEPALIVE && p.device == 3 && p.u.keepalive.peers == 4 && !memcmp(p.u.keepalive.mac, id.mac, 6));
 CHECK(!memcmp(p.u.keepalive.ip, XZ_IP, 4) && p.u.keepalive.device_class == 1 && p.u.keepalive.model == 0);
 len = djl_build_defense(&id, b);
 parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
 CHECK(len == 0x29 && p.kind == DJL_DEFENSE && p.u.defense.device == 3 && !memcmp(p.u.defense.ip, XZ_IP, 4));
 len = djl_build_assign_finished(&id, b);
 parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
 CHECK(len == 0x26 && p.kind == DJL_ASSIGN_FINISHED && p.device == 3 && b[0x25] == 1);

 struct djl_deck_facts f;
 loaded_facts(&f);
 len = djl_build_beat(&id, &f, 0, b);
 parse_ok(DJL_PORT_BEAT, b, len, &p);
 CHECK(p.kind == DJL_BEAT && p.device == 3 && p.u.beat.next_beat_ms == 469 && p.u.beat.second_beat_ms == 938);
 CHECK(p.u.beat.beat_in_bar == 3 && p.u.beat.next_bar_ms == 938 && p.u.beat.second_bar_ms == 469 * 6);
 CHECK(p.u.beat.fourth_beat_ms == 469 * 4 && p.u.beat.eighth_beat_ms == 469 * 8);
 CHECK(p.u.beat.pitch_q20 == 0x107ae1 && p.u.beat.bpm_x100 == 12800);
 len = djl_build_beat(&id, &f, 5, b);
 parse_ok(DJL_PORT_BEAT, b, len, &p);
 CHECK(p.u.beat.eighth_beat_ms == DJL_NO_TIME && p.u.beat.fourth_beat_ms == 469 * 4);
 f.timeline.ahead_ms[3] = DJL_NO_TIME;
 CHECK(djl_build_beat(&id, &f, 3, b) == 0);
 loaded_facts(&f);

 len = djl_build_position(&id, &f, 6000000, b);
 parse_ok(DJL_PORT_BEAT, b, len, &p);
 CHECK(len == 0x3c && p.kind == DJL_POSITION && p.device == 3 && p.u.position.track_seconds == 300);
 CHECK(p.u.position.playhead_ms == 11129);
 CHECK(p.u.position.pitch_x100 == 300 && p.u.position.bpm_x10 == 1318);
 f.pitch_q20 = 0xf5c29; /* -4 % */
 f.track_bpm_x100 = 0xffff;
 len = djl_build_position(&id, &f, 6000000, b);
 parse_ok(DJL_PORT_BEAT, b, len, &p);
 CHECK(p.u.position.pitch_x100 == -400 && p.u.position.bpm_x10 == DJL_NO_TIME);
 loaded_facts(&f);

 struct djl_player pl;
 djl_player_init(&pl, &id);
 pl.sync_n = 9;
 pl.packet_counter = 77;
 pl.yielding_to = 2;
 memset(b, 0xaa, DJL_MAX_DATAGRAM);
 len = djl_build_status(&pl, &f, NULL, 5000000, b);
 parse_ok(DJL_PORT_STATUS, b, len, &p);
 CHECK(len == 0x124 && b[0x22] == 0x01 && b[0x23] == 0x00 && b[0x20] == 3 && b[0x1f] == 1);
 for (unsigned i = 0x110; i < 0x124; i++) CHECK(b[i] == 0);
 CHECK(p.kind == DJL_STATUS && p.device == 3 && b[0x24] == 3 && p.u.status.length == 0x124 && p.u.status.active == 1);
 CHECK(p.u.status.track.player == 1 && p.u.status.track.slot == 3 && p.u.status.track.type == 1 && p.u.status.track.rekordbox_id == 4242);
 CHECK(p.u.status.play == DJL_PLAY_PLAYING && !memcmp(p.u.status.firmware, "1.26", 4) && p.u.status.sync_n == 9);
 CHECK(p.u.status.flags == (0x84 | DJL_FLAG_PLAYING | DJL_FLAG_MASTER | DJL_FLAG_SYNC | DJL_FLAG_ON_AIR));
 CHECK(b[0x8b] == 0x9a && p.u.status.pitch_q20 == 0x107ae1 && p.u.status.tempo_valid == 0x8000 && p.u.status.bpm_x100 == 12800);
 CHECK(p.u.status.fader_pitch_q20 == 0x105000 && b[0x9d] == 0x09 && p.u.status.master_mode == 1 && p.u.status.handoff_to == 2);
 CHECK(p.u.status.beat_number == 22 && p.u.status.beat_in_bar == 2 && p.u.status.packet_counter == 77);
 CHECK(b[0xc0] == 0x00 && b[0xc1] == 0x10 && b[0xc2] == 0x7a && b[0xc3] == 0xe1 && b[0xc6] == 0x50 && b[0xcc] == 0x1f);
 CHECK(b[0x68] == 1 && b[0x75] == 1 && b[0xb6] == 1 && b[0x6f] == 4 && b[0x73] == 4 && b[0x8a] == 0xff);
 /* 0x78 is 00 in all 14,770 CDJ-2000nexus status packets in dysentery's captures, and python-prodj-link
    reads 0x78-0x7b as one 32-bit play state; 01 there made it decode "playing" as 0x01000003. */
 CHECK(b[0x76] == 0 && b[0x77] == 0 && b[0x78] == 0 && b[0x79] == 0 && b[0x7a] == 0);
 CHECK(b[0xd0] == 0x12 && b[0xd1] == 0x34 && b[0xd2] == 0x56 && b[0xd3] == 0x78 && b[0xd7] == 1);
 len = djl_build_status(&pl, &f, NULL, 5000000 + 300000, b);
 parse_ok(DJL_PORT_STATUS, b, len, &p);
 CHECK(p.u.status.beat_number == 23 && p.u.status.beat_in_bar == 3);
 f.play = DJL_PLAY_PAUSED;
 f.master = f.sync = f.on_air = 0;
 f.timeline.speed_q20 = 0;
 f.track.type = DJL_TRACK_UNANALYZED;
 pl.yielding_to = DJL_NO_DEVICE;
 len = djl_build_status(&pl, &f, NULL, 5000000, b);
 parse_ok(DJL_PORT_STATUS, b, len, &p);
 CHECK(p.u.status.flags == 0x84 && b[0x8b] == 0x9e && p.u.status.tempo_valid == 0 && b[0x9d] == 1 && p.u.status.handoff_to == 0xff);
 CHECK(p.u.status.active == 0 && p.u.status.master_mode == 0);
 empty_facts(&f);
 len = djl_build_status(&pl, &f, NULL, 0, b);
 parse_ok(DJL_PORT_STATUS, b, len, &p);
 CHECK(p.u.status.tempo_valid == 0x7fff && p.u.status.bpm_x100 == 0xffff && p.u.status.beat_number == DJL_NO_TIME && b[0x9d] == 0);
 CHECK(p.u.status.track.type == 0 && p.u.status.track.rekordbox_id == 0 && p.u.status.beat_in_bar == 0);

 len = djl_build_master_request(&id, b);
 parse_ok(DJL_PORT_BEAT, b, len, &p);
 CHECK(len == 0x28 && p.kind == DJL_MASTER_REQUEST && p.u.master_request.requester == 3 && p.device == 3);
 len = djl_build_master_response(&id, b);
 parse_ok(DJL_PORT_BEAT, b, len, &p);
 CHECK(len == 0x2c && p.kind == DJL_MASTER_RESPONSE && p.u.master_response.accepted && p.device == 3);
 parse_ok(DJL_PORT_STATUS, b, len, &p);
 CHECK(p.kind == DJL_MASTER_RESPONSE);
 CHECK(!djl_parse(DJL_PORT_ANNOUNCE, b, len, &p));
 len = djl_build_load_ack(&id, b);
 CHECK(len == 0x28 && b[0x0a] == 0x1a && b[0x21] == 3 && b[0x23] == 4 && b[0x24] == 3);
 CHECK(!djl_parse(DJL_PORT_STATUS, b, len, &p));

 /* Length variants (reference 2.4, 2.8). */
 pl.packet_counter = 1;
 loaded_facts(&f);
 djl_build_status(&pl, &f, NULL, 0, b);
 const size_t ok_lengths[] = {0xcc, 0xd0, 0xd4, 0x11b, 0x11c, 0x124, 0x200};
 for (unsigned i = 0; i < sizeof ok_lengths / sizeof ok_lengths[0]; i++) {
  CHECK(djl_parse(DJL_PORT_STATUS, b, ok_lengths[i], &p) && p.u.status.length == ok_lengths[i] && p.u.status.bpm_x100 == 12800);
 }
 CHECK(!djl_parse(DJL_PORT_STATUS, b, 0xcb, &p));
 len = djl_build_keepalive(&id, 1, b);
 CHECK(!djl_parse(DJL_PORT_ANNOUNCE, b, 0x35, &p));
 CHECK(!djl_parse(DJL_PORT_STATUS, b, len, &p));
 len = djl_build_beat(&id, &f, 0, b);
 CHECK(!djl_parse(DJL_PORT_BEAT, b, 0x5f, &p));
 b[3] ^= 1;
 CHECK(!djl_parse(DJL_PORT_BEAT, b, len, &p));
 raw_packet(0x03, 33, 0, 0x2d, b);
 b[0x24] = 1; b[0x26] = 1;
 parse_ok(DJL_PORT_BEAT, b, 0x2d, &p);
 CHECK(p.kind == DJL_ON_AIR && p.u.on_air.channels == 4 && p.u.on_air.on[0] == 1 && p.u.on_air.on[1] == 0 && p.u.on_air.on[2] == 1);
 raw_packet(0x03, 33, 3, 0x35, b);
 b[0x27] = 1; b[0x2e] = 1;
 parse_ok(DJL_PORT_BEAT, b, 0x35, &p);
 CHECK(p.u.on_air.channels == 6 && p.u.on_air.on[3] == 1 && p.u.on_air.on[4] == 0 && p.u.on_air.on[5] == 1);
 raw_packet(0x19, 0x11, 0, 0x58, b);
 b[0x24] = 0x11; b[0x28] = 1; b[0x29] = 3; b[0x2a] = 1; b[0x2f] = 99; b[0x40] = 2;
 parse_ok(DJL_PORT_STATUS, b, 0x58, &p);
 CHECK(p.kind == DJL_LOAD_TRACK && p.u.load_track.target_deck == 3 && p.u.load_track.source == 0x11 && p.u.load_track.track.rekordbox_id == 99);
 CHECK(!djl_parse(DJL_PORT_STATUS, b, 0x40, &p));
 raw_packet(0x29, 33, 0, 0x38, b);
 b[0x27] = 0xf0; b[0x2e] = 0x32; b[0x2f] = 0x00; b[0x36] = 0xff; b[0x37] = 2;
 parse_ok(DJL_PORT_STATUS, b, 0x38, &p);
 CHECK(p.kind == DJL_MIXER_STATUS && p.device == 33 && p.u.mixer_status.flags == 0xf0 && p.u.mixer_status.bpm_x100 == 0x3200 && p.u.mixer_status.beat_in_bar == 2);
 raw_packet(0x05, 2, 0, 0x30, b);
 memcpy(b + 0x24, PEER_IP, 4); b[0x2b] = 1; b[0x2f] = 3;
 parse_ok(DJL_PORT_STATUS, b, 0x30, &p);
 CHECK(p.kind == DJL_MEDIA_QUERY && !memcmp(p.u.media_query.reply_ip, PEER_IP, 4) && p.u.media_query.player == 1 && p.u.media_query.slot == 3);
 raw_packet(0x02, 33, 0, 0x28, b);
 b[0x26] = 1;
 parse_ok(DJL_PORT_BEAT, b, 0x28, &p);
 CHECK(p.kind == DJL_FADER_START && p.u.fader_start.command[2] == 1);
 raw_packet(0x2a, 5, 0, 0x2c, b);
 b[0x2b] = 0x10;
 parse_ok(DJL_PORT_BEAT, b, 0x2c, &p);
 CHECK(p.kind == DJL_SYNC_CONTROL && p.u.sync_control.command == 0x10 && p.device == 5);
 static uint8_t big[0x10000];
 memcpy(big, b, 0x2c);
 CHECK(!djl_parse(DJL_PORT_BEAT, big, sizeof big, &p));
 CHECK(!djl_parse(DJL_PORT_BEAT, NULL, 0x2c, &p));
}

/* ---- 3. robustness ----------------------------------------------------------------------- */

static uint64_t rng_state = 0x9e3779b97f4a7c15u;
static uint32_t rnd(void)
{
 rng_state ^= rng_state << 13;
 rng_state ^= rng_state >> 7;
 rng_state ^= rng_state << 17;
 return (uint32_t)(rng_state >> 16);
}

static void quiet_send(void *c, const struct djl_identity *f, const uint8_t *d, uint16_t p, const uint8_t *b, size_t l)
{
 (void)c; (void)f; (void)d; (void)p; (void)b;
 if (l > DJL_MAX_DATAGRAM) overflow++;
}
static void quiet_command(void *c, uint8_t l, const struct djl_command *d) { (void)c; (void)l; (void)d; }

struct corpus { uint16_t port; size_t len; uint8_t b[DJL_MAX_DATAGRAM]; };

static unsigned build_corpus(struct corpus *c)
{
 unsigned n = 0;
 for (unsigned i = 0; i < FIXTURES; i++) {
  c[n].port = (uint16_t)fixtures[i].port;
  c[n].len = fixtures[i].len;
  memcpy(c[n++].b, fixtures[i].bytes, fixtures[i].len);
 }
 struct djl_identity id;
 djl_identity_xz_deck(&id, 4, UNIT_MAC, PEER_IP);
 struct djl_deck_facts f;
 loaded_facts(&f);
 struct djl_player pl;
 djl_player_init(&pl, &id);
#define ADD(port_, expr) do { c[n].port = (port_); c[n].len = (expr); n++; } while (0)
 ADD(DJL_PORT_ANNOUNCE, djl_build_hello(&id, c[n].b));
 ADD(DJL_PORT_ANNOUNCE, djl_build_claim1(&id, 1, c[n].b));
 ADD(DJL_PORT_ANNOUNCE, djl_build_claim2(&id, 2, c[n].b));
 ADD(DJL_PORT_ANNOUNCE, djl_build_claim3(&id, 3, c[n].b));
 ADD(DJL_PORT_ANNOUNCE, djl_build_defense(&id, c[n].b));
 ADD(DJL_PORT_ANNOUNCE, djl_build_assign_finished(&id, c[n].b));
 ADD(DJL_PORT_BEAT, djl_build_position(&id, &f, 6000000, c[n].b));
 ADD(DJL_PORT_BEAT, djl_build_master_request(&id, c[n].b));
 ADD(DJL_PORT_BEAT, djl_build_master_response(&id, c[n].b));
 ADD(DJL_PORT_STATUS, djl_build_master_response(&id, c[n].b));
 ADD(DJL_PORT_STATUS, djl_build_status(&pl, &f, NULL, 0, c[n].b));
 ADD(DJL_PORT_STATUS, djl_build_load_ack(&id, c[n].b));
 ADD(DJL_PORT_BEAT, raw_packet(0x02, 33, 0, 0x28, c[n].b));
 ADD(DJL_PORT_BEAT, raw_packet(0x03, 33, 0, 0x2d, c[n].b));
 ADD(DJL_PORT_BEAT, raw_packet(0x03, 33, 3, 0x35, c[n].b));
 ADD(DJL_PORT_BEAT, raw_packet(0x2a, 5, 0, 0x2c, c[n].b));
 ADD(DJL_PORT_STATUS, raw_packet(0x19, 0x11, 0, 0x58, c[n].b));
 ADD(DJL_PORT_STATUS, raw_packet(0x29, 33, 0, 0x38, c[n].b));
 ADD(DJL_PORT_STATUS, raw_packet(0x05, 2, 0, 0x30, c[n].b));
#undef ADD
 return n;
}

static unsigned fuzz_iterations = 1000000;

static void test_fuzz(void)
{
 static struct corpus corpus[64];
 unsigned ncorpus = build_corpus(corpus), accepted = 0;
 const uint16_t ports[4] = {DJL_PORT_ANNOUNCE, DJL_PORT_BEAT, DJL_PORT_STATUS, 50004};
 struct djl_identity id;
 djl_identity_xz_deck(&id, 3, UNIT_MAC, XZ_IP);
 struct djl_player pl;
 djl_player_init(&pl, &id);
 pl.phase = DJL_RUNNING;
 struct djl_net net;
 djl_net_init(&net, XZ_IP);
 struct djl_deck_facts f;
 loaded_facts(&f);
 const struct djl_sink quiet = {NULL, quiet_send, quiet_command};
 uint8_t work[DJL_MAX_DATAGRAM + 64];
 for (unsigned it = 0; it < fuzz_iterations; it++) {
  const struct corpus *c = &corpus[rnd() % ncorpus];
  uint16_t port = (rnd() & 7) ? c->port : ports[rnd() % 4];
  size_t len;
  unsigned mode = rnd() % 4;
  if (mode == 0) {
   len = rnd() % (DJL_MAX_DATAGRAM + 32);
   for (size_t i = 0; i < len; i++) work[i] = (uint8_t)rnd();
   if (len >= 11 && (rnd() & 1)) { memcpy(work, "Qspt1WmJOL", 10); work[10] = c->b[10]; }
  } else {
   len = c->len;
   memcpy(work, c->b, len);
   if (mode == 1) len = rnd() % (len + 1);
   unsigned flips = mode == 3 ? 1 + rnd() % 8 : rnd() % 3;
   for (unsigned k = 0; k < flips && len; k++) work[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8));
   if (mode == 3 && len > 0x24 && (rnd() & 1)) { work[0x22] = (uint8_t)rnd(); work[0x23] = (uint8_t)rnd(); }
   if (mode == 2 && len < sizeof work - 32 && (rnd() & 1)) {
    size_t extra = rnd() % 32;
    for (size_t i = 0; i < extra; i++) work[len + i] = (uint8_t)rnd();
    len += extra;
   }
  }
  uint8_t *exact = malloc(len ? len : 1);
  if (!exact) { failures++; return; }
  memcpy(exact, work, len);
  struct djl_packet p;
  memset(&p, 0xa5, sizeof p);
  int ok = djl_parse(port, exact, len, &p);
  free(exact);
  if (!(ok == 0 || ok == 1)) failures++;
  if (!ok) { if (p.kind != DJL_UNKNOWN) failures++; continue; }
  accepted++;
  if (p.name[DJL_NAME] != 0) failures++;
  uint64_t now = 1000000 + (uint64_t)it * 1000;
  const uint8_t *src = (rnd() & 1) ? PEER_IP : PEER2_IP;
  djl_net_receive(&net, now, src, PEER_MAC, &p);
  f.master = (uint8_t)(rnd() & 1);
  f.wants_master = (uint8_t)(rnd() & 1);
  djl_player_receive(&pl, now, src, &p, &f, &net, &quiet);
  if ((it & 255) == 0) {
   djl_player_step(&pl, now, &f, &net, &quiet);
   djl_net_expire(&net, now);
  }
  if (pl.phase != DJL_RUNNING) pl.phase = DJL_RUNNING;
 }
 checks += fuzz_iterations;
 printf("  fuzz: %u datagrams, %u parsed as a known kind\n", fuzz_iterations, accepted);
 CHECK(accepted > fuzz_iterations / 10);
}

/* ---- 4. reducer -------------------------------------------------------------------------- */

static void run_to(struct djl_player *p, uint64_t *now, uint64_t until, const struct djl_deck_facts *f,
                   const struct djl_net *n)
{
 while (*now < until) {
  g_now = *now;
  uint64_t next = djl_player_step(p, *now, f, n, &sink);
  CHECK(next > *now);
  *now = next < until ? next : until;
 }
 g_now = *now;
 djl_player_step(p, *now, f, n, &sink);
}

static void add_peer(struct djl_net *n, uint64_t now, uint8_t device, const uint8_t ip[4])
{
 struct djl_identity id = remote_identity(device, ip);
 uint8_t b[DJL_MAX_DATAGRAM];
 struct djl_packet p;
 size_t len = djl_build_keepalive(&id, 2, b);
 parse_ok(DJL_PORT_ANNOUNCE, b, len, &p);
 djl_net_receive(n, now, ip, PEER_MAC, &p);
}

static void running_player(struct djl_player *p, uint64_t *now, uint8_t device)
{
 struct djl_identity id;
 djl_identity_xz_deck(&id, device, UNIT_MAC, XZ_IP);
 djl_player_init(p, &id);
 struct djl_deck_facts f;
 empty_facts(&f);
 djl_player_start(p, *now);
 run_to(p, now, *now + 3600000, &f, NULL);
 CHECK(p->phase == DJL_RUNNING);
 reset_rec();
}

static void test_claim(void)
{
 struct djl_player p;
 struct djl_identity id;
 djl_identity_xz_deck(&id, 3, UNIT_MAC, XZ_IP);
 djl_player_init(&p, &id);
 struct djl_deck_facts f;
 empty_facts(&f);
 reset_rec();
 uint64_t t0 = 1000000, now = t0;
 CHECK(djl_player_step(&p, now, &f, NULL, &sink) == UINT64_MAX && nsent == 0);
 djl_player_start(&p, now);
 run_to(&p, &now, t0 + 3600000, &f, NULL);
 const uint8_t types[4] = {0x0a, 0x00, 0x02, 0x04};
 CHECK(nsent == 13);
 for (unsigned i = 0; i < 12 && i < nsent; i++) {
  CHECK(sent[i].t == t0 + 300000ull * i && sent[i].broadcast && sent[i].port == DJL_PORT_ANNOUNCE);
  CHECK(sent[i].data[0x0a] == types[i / 3]);
  struct djl_packet pk;
  CHECK(sent_kind(&sent[i], &pk) >= 0);
  if (i >= 3 && i < 6) CHECK(pk.u.claim1.n == i - 2);
  if (i >= 6 && i < 9) CHECK(pk.u.claim2.n == i - 5 && pk.u.claim2.auto_assign == 2 && pk.u.claim2.device == 3);
  if (i >= 9) CHECK(pk.u.claim3.n == i - 8 && pk.u.claim3.device == 3);
 }
 CHECK(nsent == 13 && sent[12].t == t0 + 3600000 && sent[12].data[0x0a] == 0x06 && p.phase == DJL_RUNNING);
 djl_player_start(&p, now);
 CHECK(p.phase == DJL_RUNNING);

 /* Keep-alive cadence, and status only to known peers, one datagram per IP. */
 struct djl_net net;
 djl_net_init(&net, XZ_IP);
 reset_rec();
 run_to(&p, &now, now + 10000000, &f, &net);
 unsigned ka = 0;
 uint64_t last = 0;
 for (unsigned i = 0; i < nsent; i++) {
  if (sent[i].data[0x0a] == 0x06 && sent[i].port == DJL_PORT_ANNOUNCE) {
   if (ka) CHECK(sent[i].t - last == 2000000);
   last = sent[i].t;
   ka++;
   CHECK(sent[i].data[0x30] == 1);
  }
 }
 CHECK(ka == 5 && count_kind(DJL_STATUS) == 0 && nsent == ka);
 add_peer(&net, now, 1, PEER_IP);
 add_peer(&net, now, 2, PEER_IP);
 add_peer(&net, now, 33, PEER_IP);
 add_peer(&net, now, 2, PEER2_IP);
 add_peer(&net, now, 3, XZ_IP);
 reset_rec();
 uint64_t from = now;
 run_to(&p, &now, now + 2000000, &f, &net);
 unsigned to1 = 0, to2 = 0;
 uint32_t counter = 0;
 for (unsigned i = 0; i < nsent; i++) {
  struct djl_packet pk;
  if (sent_kind(&sent[i], &pk) == DJL_STATUS) {
   CHECK(!sent[i].broadcast && sent[i].port == DJL_PORT_STATUS);
   to1 += !memcmp(sent[i].ip, PEER_IP, 4);
   to2 += !memcmp(sent[i].ip, PEER2_IP, 4);
   CHECK((sent[i].t - from) % 200000 == 0);
   CHECK(pk.u.status.packet_counter >= counter);
   counter = pk.u.status.packet_counter;
  }
  if (sent_kind(&sent[i], &pk) == DJL_KEEPALIVE) CHECK(pk.u.keepalive.peers == 3);
 }
 CHECK(to1 == 10 && to2 == 10 && count_kind(DJL_STATUS) == 20);
 djl_net_expire(&net, now + 10000001);
 CHECK(djl_net_highest_sync_n(&net) == 0);
 unsigned in_use = 0;
 for (unsigned i = 0; i < DJL_PEERS; i++) in_use += net.peers[i].in_use;
 CHECK(in_use == 0);
 djl_player_stop(&p);
 CHECK(p.phase == DJL_OFF && djl_player_step(&p, now, &f, &net, &sink) == UINT64_MAX);
}

static void test_eviction(void)
{
 struct djl_identity id;
 djl_identity_xz_deck(&id, 3, UNIT_MAC, XZ_IP);
 struct djl_identity other = remote_identity(3, PEER_IP);
 struct djl_player p;
 struct djl_deck_facts f;
 empty_facts(&f);
 uint8_t b[DJL_MAX_DATAGRAM];
 struct djl_packet pk;
 uint64_t now = 1000000;
 djl_player_init(&p, &id);
 djl_player_start(&p, now);
 run_to(&p, &now, now + 2000000, &f, NULL);
 CHECK(p.phase == DJL_CLAIMING2);
 struct djl_identity four = remote_identity(4, PEER_IP);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_defense(&four, b), &pk);
 djl_player_receive(&p, now, PEER_IP, &pk, &f, NULL, &sink);
 CHECK(p.phase == DJL_CLAIMING2);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_defense(&id, b), &pk);
 djl_player_receive(&p, now, XZ_IP, &pk, &f, NULL, &sink);
 CHECK(p.phase == DJL_CLAIMING2);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_defense(&other, b), &pk);
 djl_player_receive(&p, now, PEER_IP, &pk, &f, NULL, &sink);
 CHECK(p.phase == DJL_EVICTED);
 reset_rec();
 CHECK(djl_player_step(&p, now + 300000, &f, NULL, &sink) == UINT64_MAX && nsent == 0);
 djl_player_start(&p, now);
 CHECK(p.phase == DJL_HELLOING);

 /* A settled player's 0x05 after our first 0x04 ends the claim at once. */
 djl_player_init(&p, &id);
 now = 1000000;
 djl_player_start(&p, now);
 reset_rec();
 run_to(&p, &now, now + 2700000, &f, NULL);
 CHECK(p.phase == DJL_CLAIMING3 && sent[nsent - 1].data[0x0a] == 0x04);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_assign_finished(&other, b), &pk);
 g_now = now + 2000;
 djl_player_receive(&p, now + 2000, PEER_IP, &pk, &f, NULL, &sink);
 CHECK(p.phase == DJL_RUNNING && sent[nsent - 1].data[0x0a] == 0x06);
 djl_player_receive(&p, now + 3000, PEER_IP, &pk, &f, NULL, &sink);
 unsigned before = nsent;
 run_to(&p, &now, now + 1000000, &f, NULL);
 CHECK(count_kind(DJL_CLAIM3) == 1 && count_kind(DJL_KEEPALIVE) == 1 && nsent == before);
}

static void test_defence(void)
{
 struct djl_player p;
 uint64_t now = 1000000;
 running_player(&p, &now, 3);
 struct djl_deck_facts f;
 empty_facts(&f);
 struct djl_identity intruder = remote_identity(3, PEER_IP), newcomer = remote_identity(5, PEER2_IP);
 uint8_t b[DJL_MAX_DATAGRAM];
 struct djl_packet pk;
 g_now = now;
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_claim2(&intruder, 1, b), &pk);
 djl_player_receive(&p, now, PEER_IP, &pk, &f, NULL, &sink);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_claim3(&intruder, 1, b), &pk);
 djl_player_receive(&p, now, PEER_IP, &pk, &f, NULL, &sink);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_keepalive(&intruder, 1, b), &pk);
 djl_player_receive(&p, now, PEER_IP, &pk, &f, NULL, &sink);
 CHECK(nsent == 3);
 for (unsigned i = 0; i < nsent; i++) {
  struct djl_packet d;
  CHECK(sent_kind(&sent[i], &d) == DJL_DEFENSE && !sent[i].broadcast && !memcmp(sent[i].ip, PEER_IP, 4));
  CHECK(sent[i].port == DJL_PORT_ANNOUNCE && d.u.defense.device == 3 && !memcmp(d.u.defense.ip, XZ_IP, 4));
 }
 reset_rec();
 djl_player_receive(&p, now, XZ_IP, &pk, &f, NULL, &sink);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_claim2(&newcomer, 1, b), &pk);
 djl_player_receive(&p, now, PEER2_IP, &pk, &f, NULL, &sink);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_defense(&intruder, b), &pk);
 djl_player_receive(&p, now, PEER_IP, &pk, &f, NULL, &sink);
 CHECK(nsent == 0 && p.phase == DJL_RUNNING);
 parse_ok(DJL_PORT_ANNOUNCE, b, djl_build_claim3(&newcomer, 1, b), &pk);
 djl_player_receive(&p, now, PEER2_IP, &pk, &f, NULL, &sink);
 djl_player_receive(&p, now, PEER2_IP, &pk, &f, NULL, &sink);
 CHECK(nsent == 2 && count_kind(DJL_ASSIGN_FINISHED) == 2 && !memcmp(sent[0].ip, PEER2_IP, 4));
 CHECK(sent[0].data[0x24] == 3 && sent[0].data[0x25] == 1 && sent[0].len == 0x26);
}

static void receive_raw(struct djl_player *p, uint64_t now, const uint8_t ip[4], uint16_t port, const uint8_t *b,
                        size_t len, const struct djl_deck_facts *f, const struct djl_net *n)
{
 struct djl_packet pk;
 parse_ok(port, b, len, &pk);
 g_now = now;
 djl_player_receive(p, now, ip, &pk, f, n, &sink);
}

static void test_commands(void)
{
 struct djl_player p;
 uint64_t now = 1000000;
 running_player(&p, &now, 3);
 struct djl_deck_facts f;
 empty_facts(&f);
 uint8_t b[DJL_MAX_DATAGRAM];
 const uint8_t sync_cmds[3] = {0x10, 0x20, 0x01};
 const enum djl_command_kind expect[3] = {DJL_CMD_SYNC_ON, DJL_CMD_SYNC_OFF, DJL_CMD_TAKE_MASTER};
 for (unsigned i = 0; i < 3; i++) {
  raw_packet(0x2a, 5, 0, 0x2c, b);
  b[0x27] = 5;
  b[0x2b] = sync_cmds[i];
  receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x2c, &f, NULL);
  CHECK(ncmds == i + 1 && cmds[i].c.kind == expect[i] && cmds[i].c.device == 5 && cmds[i].local == 3);
 }
 raw_packet(0x2a, 5, 0, 0x2c, b);
 b[0x2b] = 0x33;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x2c, &f, NULL);
 CHECK(ncmds == 3);

 reset_rec();
 raw_packet(0x19, 0x11, 0, 0x58, b);
 b[0x24] = 0x11; b[0x28] = 1; b[0x29] = 3; b[0x2a] = 1; b[0x2f] = 99; b[0x40] = 2;
 receive_raw(&p, now, PEER_IP, DJL_PORT_STATUS, b, 0x58, &f, NULL);
 receive_raw(&p, now + 1000, PEER_IP, DJL_PORT_STATUS, b, 0x58, &f, NULL);
 CHECK(count_cmd(DJL_CMD_LOAD) == 1 && cmds[0].c.track.rekordbox_id == 99 && cmds[0].c.track.slot == 3 && cmds[0].c.device == 0x11);
 CHECK(nsent == 2 && sent[0].data[0x0a] == 0x1a && sent[0].port == DJL_PORT_STATUS && !memcmp(sent[0].ip, PEER_IP, 4));
 b[0x2f] = 100;
 receive_raw(&p, now + 2000, PEER_IP, DJL_PORT_STATUS, b, 0x58, &f, NULL);
 CHECK(count_cmd(DJL_CMD_LOAD) == 2 && cmds[1].c.track.rekordbox_id == 100);
 b[0x40] = 0;
 receive_raw(&p, now + 3000, PEER_IP, DJL_PORT_STATUS, b, 0x58, &f, NULL);
 CHECK(count_cmd(DJL_CMD_LOAD) == 2 && nsent == 3);

 reset_rec();
 raw_packet(0x02, 33, 0, 0x28, b);
 memset(b + 0x24, 2, 4);
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x28, &f, NULL);
 CHECK(ncmds == 0);
 b[0x26] = 0;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x28, &f, NULL);
 b[0x26] = 1;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x28, &f, NULL);
 b[0x26] = 2; b[0x24] = 0;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x28, &f, NULL);
 CHECK(ncmds == 2 && cmds[0].c.kind == DJL_CMD_FADER_START && cmds[1].c.kind == DJL_CMD_FADER_STOP && nsent == 0);

 reset_rec();
 raw_packet(0x03, 33, 0, 0x2d, b);
 b[0x24] = 1; b[0x25] = 1; b[0x26] = 1;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x2d, &f, NULL);
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x2d, &f, NULL);
 CHECK(ncmds == 1 && cmds[0].c.kind == DJL_CMD_ON_AIR && cmds[0].c.value == 1 && cmds[0].c.device == 33);
 b[0x26] = 0;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x2d, &f, NULL);
 b[0x27] = 1;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x2d, &f, NULL);
 CHECK(ncmds == 2 && cmds[1].c.value == 0);
 raw_packet(0x03, 33, 3, 0x35, b);
 b[0x26] = 1;
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, 0x35, &f, NULL);
 CHECK(ncmds == 3 && cmds[2].c.value == 1);

 struct djl_net net;
 djl_net_init(&net, XZ_IP);
 struct djl_packet pk;
 parse_ok(DJL_PORT_BEAT, b, 0x35, &pk);
 djl_net_receive(&net, now, PEER_IP, NULL, &pk);
 CHECK(net.on_air_known && net.on_air[2] == 1 && net.on_air[0] == 0 && net.on_air_us == now);
 djl_net_receive(&net, now, XZ_IP, NULL, &pk);
}

/* A remote CDJ whose 0x26/0x27 and status the test plays by hand. */
static void test_handoff(void)
{
 struct djl_player p;
 uint64_t now = 1000000;
 running_player(&p, &now, 3);
 struct djl_net net;
 djl_net_init(&net, XZ_IP);
 add_peer(&net, now, 2, PEER_IP);
 struct djl_deck_facts f;
 loaded_facts(&f);
 f.master = 0;
 f.timeline.speed_q20 = 0;
 f.play = DJL_PLAY_PAUSED;
 struct djl_identity two = remote_identity(2, PEER_IP);
 uint8_t b[DJL_MAX_DATAGRAM];
 struct djl_packet st;

 /* Requester side. */
 remote_status(2, PEER_IP, 0x84 | DJL_FLAG_PLAYING | DJL_FLAG_MASTER, 0xff, 4, &st);
 djl_net_receive(&net, now, PEER_IP, NULL, &st);
 CHECK(djl_net_master(&net) && djl_net_master(&net)->device == 2);
 f.wants_master = 1;
 reset_rec();
 run_to(&p, &now, now + 2500000, &f, &net);
 unsigned requests = 0;
 for (unsigned i = 0; i < nsent; i++) {
  if (sent[i].data[0x0a] != 0x26) continue;
  CHECK(!memcmp(sent[i].ip, PEER_IP, 4) && sent[i].port == DJL_PORT_BEAT && sent[i].data[0x27] == 3);
  CHECK(sent[i].t == 1000000 + 3600000 + 1000000ull * requests);
  requests++;
 }
 CHECK(requests == 3 && ncmds == 0);
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, djl_build_master_response(&two, b), &f, &net);
 CHECK(p.granted == 1 && ncmds == 0);
 remote_status(2, PEER_IP, 0x84 | DJL_FLAG_PLAYING | DJL_FLAG_MASTER, 3, 4, &st);
 djl_net_receive(&net, now, PEER_IP, NULL, &st);
 g_now = now;
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 run_to(&p, &now, now + 300000, &f, &net);
 CHECK(ncmds == 1 && cmds[0].c.kind == DJL_CMD_MASTER_GRANTED && cmds[0].c.device == 2);
 f.master = 1;
 f.wants_master = 0;
 reset_rec();
 run_to(&p, &now, now + 400000, &f, &net);
 struct djl_packet mine;
 CHECK(last_status(&mine) && (mine.u.status.flags & DJL_FLAG_MASTER) && mine.u.status.master_mode == 1);
 CHECK(p.master_on_wire == 1 && count_kind(DJL_MASTER_REQUEST) == 0);
 remote_status(2, PEER_IP, 0x84, 0xff, 4, &st);
 djl_net_receive(&net, now, PEER_IP, NULL, &st);
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 CHECK(ncmds == 0 && djl_net_master(&net) == NULL);

 /* Master side: we hold master, device 2 asks for it. */
 reset_rec();
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, djl_build_master_request(&two, b), &f, &net);
 receive_raw(&p, now, PEER_IP, DJL_PORT_BEAT, b, djl_build_master_request(&two, b), &f, &net);
 CHECK(count_kind(DJL_MASTER_RESPONSE) == 2 && !memcmp(sent[0].ip, PEER_IP, 4) && sent[0].port == DJL_PORT_BEAT);
 CHECK(sent[0].data[0x21] == 3 && sent[0].data[0x2b] == 1 && p.yielding_to == 2);
 run_to(&p, &now, now + 250000, &f, &net);
 CHECK(last_status(&mine) && mine.u.status.handoff_to == 2 && (mine.u.status.flags & DJL_FLAG_MASTER));
 remote_status(5, PEER2_IP, 0x84, 0xff, 7, &st);
 djl_net_receive(&net, now, PEER2_IP, NULL, &st);
 remote_status(2, PEER_IP, 0x84 | DJL_FLAG_MASTER, 0xff, 4, &st);
 djl_net_receive(&net, now, PEER_IP, NULL, &st);
 g_now = now;
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 CHECK(ncmds == 1 && cmds[0].c.kind == DJL_CMD_YIELD_MASTER && cmds[0].c.device == 2 && p.sync_n == 8 && p.yielding_to == DJL_NO_DEVICE);
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 CHECK(p.sync_n == 8 && cmds[ncmds - 1].c.kind == DJL_CMD_YIELD_MASTER);
 f.master = 0;
 unsigned yields = ncmds;
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 run_to(&p, &now, now + 250000, &f, &net);
 CHECK(ncmds == yields && last_status(&mine) && mine.u.status.handoff_to == 0xff);
 CHECK(!(mine.u.status.flags & DJL_FLAG_MASTER) && mine.u.status.sync_n == 8);

 /* Unsolicited: a stopped master names us while we play in sync. */
 reset_rec();
 f.sync = 1;
 f.play = DJL_PLAY_PLAYING;
 f.timeline.speed_q20 = (int32_t)DJL_PITCH_UNITY;
 remote_status(2, PEER_IP, 0x84 | DJL_FLAG_MASTER, 3, 4, &st);
 g_now = now;
 djl_player_receive(&p, now, PEER_IP, &st, &f, &net, &sink);
 djl_player_receive(&p, now + 1000, PEER_IP, &st, &f, &net, &sink);
 CHECK(ncmds == 1 && cmds[0].c.kind == DJL_CMD_MASTER_GRANTED && cmds[0].c.device == 2);
 f.sync = 0;
 p.granted = 0;
 djl_player_receive(&p, now + 2000, PEER_IP, &st, &f, &net, &sink);
 remote_status(2, PEER_IP, 0x84 | DJL_FLAG_MASTER | DJL_FLAG_PLAYING, 3, 4, &st);
 f.sync = 1;
 djl_player_receive(&p, now + 3000, PEER_IP, &st, &f, &net, &sink);
 CHECK(ncmds == 1);

 /* The unsolicited grant above shows master on the wire for at most 3 s; let it run out first. */
 f.sync = 0;
 run_to(&p, &now, now + 3100000, &f, &net);

 /* Nobody is master: the request is granted at once, once. */
 djl_net_init(&net, XZ_IP);
 add_peer(&net, now, 2, PEER_IP);
 reset_rec();
 f.wants_master = 1;
 f.master = 0;
 p.granted = 0;
 run_to(&p, &now, now + 900000, &f, &net);
 CHECK(ncmds == 1 && cmds[0].c.kind == DJL_CMD_MASTER_GRANTED && cmds[0].c.device == DJL_NO_DEVICE && count_kind(DJL_MASTER_REQUEST) == 0);
}

/* ---- handoff wire state owned by the reducer, however slowly the player reacts ------------- */

/* Device 2 at PEER_IP, sending a status every 200 ms in two copies (beat-link sends one per device it
   knows at our address). With `adaptive`, it plays beat-link's part as master yielding to us: once it
   decodes a plain master status from us it drops master. */
struct remote { uint8_t flags, handoff, adaptive; uint64_t next_us; };

static void feed_remote(struct djl_player *p, uint64_t now, const struct djl_deck_facts *f, struct djl_net *net,
                        struct remote *r)
{
 struct djl_packet st, mine;
 if (r->adaptive && last_status(&mine) && (mine.u.status.flags & DJL_FLAG_MASTER) && mine.u.status.handoff_to == 0xff) {
  r->flags = 0x84 | DJL_FLAG_PLAYING;
  r->handoff = 0xff;
 }
 remote_status(2, PEER_IP, r->flags, r->handoff, 4, &st);
 djl_net_receive(net, now, PEER_IP, NULL, &st);
 g_now = now;
 djl_player_receive(p, now, PEER_IP, &st, f, net, &sink);
 djl_player_receive(p, now, PEER_IP, &st, f, net, &sink);
 r->next_us = now + 200000;
}

/* Steps the player and the remote until `until`; always at least one step at the starting time. */
static void run_remote(struct djl_player *p, uint64_t *now, uint64_t until, const struct djl_deck_facts *f,
                       struct djl_net *net, struct remote *r)
{
 for (;;) {
  if (*now >= r->next_us) feed_remote(p, *now, f, net, r);
  g_now = *now;
  uint64_t next = djl_player_step(p, *now, f, net, &sink);
  CHECK(next > *now);
  if (*now >= until) return;
  if (next > r->next_us) next = r->next_us;
  *now = next < until ? next : until;
 }
}

/* What our statuses sent from index `from` say about master. */
struct master_view { unsigned statuses, master, plain_master, not_master_clean; };
static struct master_view view_statuses(unsigned from, unsigned to)
{
 struct master_view v = {0, 0, 0, 0};
 struct djl_packet pk;
 for (unsigned i = from; i < to && i < nsent; i++) {
  if (sent_kind(&sent[i], &pk) != DJL_STATUS) continue;
  v.statuses++;
  int m = (pk.u.status.flags & DJL_FLAG_MASTER) != 0;
  v.master += m;
  v.plain_master += m && pk.u.status.handoff_to == 0xff && pk.u.status.master_mode == 1;
  v.not_master_clean += !m && pk.u.status.master_mode == 0 && pk.u.status.handoff_to == 0xff;
 }
 return v;
}
static unsigned first_sent_at(uint64_t t)
{
 unsigned i = 0;
 while (i < nsent && sent[i].t < t) i++;
 return i;
}
static unsigned count_cmd_between(enum djl_command_kind k, uint64_t t0, uint64_t t1)
{
 unsigned n = 0;
 for (unsigned i = 0; i < ncmds; i++) n += cmds[i].c.kind == k && cmds[i].t >= t0 && cmds[i].t < t1;
 return n;
}
static unsigned count_kind_from(enum djl_kind k, unsigned from)
{
 unsigned n = 0;
 struct djl_packet p;
 for (unsigned i = from; i < nsent; i++) n += sent_kind(&sent[i], &p) == (int)k;
 return n;
}

/* Our deck 3 is master and device 2 asks for it. Returns with our last status naming 2 in M_h and the
   remote about to send a master status, 50 ms before our next status is due. */
static void yield_setup(struct djl_player *p, uint64_t *now, struct djl_net *net, struct djl_deck_facts *f,
                        struct remote *r)
{
 running_player(p, now, 3);
 djl_net_init(net, XZ_IP);
 add_peer(net, *now, 2, PEER_IP);
 loaded_facts(f);
 *r = (struct remote){0x84 | DJL_FLAG_PLAYING, 0xff, 0, *now};
 run_remote(p, now, *now + 500000, f, net, r);
 struct djl_identity two = remote_identity(2, PEER_IP);
 uint8_t b[DJL_MAX_DATAGRAM];
 receive_raw(p, *now, PEER_IP, DJL_PORT_BEAT, b, djl_build_master_request(&two, b), f, net);
 CHECK(count_kind(DJL_MASTER_RESPONSE) == 1);
 run_remote(p, now, *now + 450000, f, net, r);
 struct djl_packet mine;
 CHECK(last_status(&mine) && (mine.u.status.flags & DJL_FLAG_MASTER) && mine.u.status.handoff_to == 2);
 run_remote(p, now, p->status_us - 50000, f, net, r);
 r->flags = 0x84 | DJL_FLAG_PLAYING | DJL_FLAG_MASTER;
 r->next_us = *now;
 reset_rec();
}

/* beat-link takes master; the player clears facts.master `hold_ms` later. */
static void yield_case(unsigned hold_ms)
{
 struct djl_player p;
 struct djl_net net;
 struct djl_deck_facts f;
 struct remote r;
 uint64_t now = 1000000;
 yield_setup(&p, &now, &net, &f, &r);
 uint64_t t0 = now, t1 = t0 + hold_ms * 1000ull;
 uint8_t b[DJL_MAX_DATAGRAM];
 if (hold_ms >= 2500) {
  /* While yielding the deck is not master for any rule: no 0x27 to another requester, no 0x26 of its own. */
  run_remote(&p, &now, t0 + 1000000, &f, &net, &r);
  struct djl_identity five = remote_identity(5, PEER2_IP);
  receive_raw(&p, now, PEER2_IP, DJL_PORT_BEAT, b, djl_build_master_request(&five, b), &f, &net);
  f.wants_master = 1;
 }
 run_remote(&p, &now, t1, &f, &net, &r);
 unsigned held = nsent;
 if (hold_ms >= 2500) {
  CHECK(count_kind(DJL_MASTER_RESPONSE) == 0);
  CHECK(count_kind(DJL_MASTER_REQUEST) == 0);
  f.wants_master = 0;
 }
 f.master = 0;
 run_remote(&p, &now, t1 + 1000000, &f, &net, &r);
 struct master_view span = view_statuses(0, held), all = view_statuses(0, nsent);
 printf("  yield, player %4u ms late: %u statuses while facts.master held, %u after; %u say master; %u YIELD_MASTER\n",
        hold_ms, span.statuses, all.statuses - span.statuses, all.master, count_cmd(DJL_CMD_YIELD_MASTER));
 CHECK(span.statuses >= (hold_ms >= 150 ? 1u : 0u));
 CHECK(all.statuses >= 5 && all.master == 0 && all.not_master_clean == all.statuses);
 /* One command at the yield, then one reminder for each whole second the player still holds master. */
 unsigned want = 1 + hold_ms / 1000;
 CHECK(count_cmd(DJL_CMD_YIELD_MASTER) == want && ncmds == want && cmds[0].c.device == 2 && cmds[0].t == t0);
 for (unsigned i = 1; i < ncmds; i++) CHECK(cmds[i].c.device == 2 && cmds[i].t - cmds[i - 1].t >= 1000000);
}

static void test_yield_latch(void)
{
 const unsigned holds[] = {0, 150, 450, 2500};
 for (unsigned i = 0; i < sizeof holds / sizeof holds[0]; i++) yield_case(holds[i]);
}

/* The player never clears facts.master while device 2 stays master: the deck never claims master against it and
   the player is reminded once a second. Once device 2 stops asserting, the hold ends 3 s later. */
static void test_yield_expiry(void)
{
 struct djl_player p;
 struct djl_net net;
 struct djl_deck_facts f;
 struct remote r;
 uint64_t now = 1000000;
 yield_setup(&p, &now, &net, &f, &r);
 uint64_t t0 = now;
 run_remote(&p, &now, t0 + 6500000, &f, &net, &r);
 struct master_view held = view_statuses(0, nsent);
 printf("  yield refused: %u statuses in 6.5 s against a live master (%u say master); YIELD_MASTER at", held.statuses,
        held.master);
 for (unsigned i = 0; i < ncmds; i++) printf(" +%llu ms", (unsigned long long)(cmds[i].t - t0) / 1000);
 printf("\n");
 /* A deck never claims master against a live master, however long its player takes to obey. */
 CHECK(held.statuses >= 30 && held.master == 0 && held.not_master_clean == held.statuses);
 CHECK(count_cmd(DJL_CMD_YIELD_MASTER) == ncmds && ncmds >= 6 && ncmds <= 8);
 for (unsigned i = 0; i < ncmds; i++) {
  CHECK(cmds[i].c.device == 2);
  if (i) CHECK(cmds[i].t - cmds[i - 1].t >= 1000000 && cmds[i].t - cmds[i - 1].t <= 1400000);
 }
 /* Device 2 stops asserting master: the hold lasts 3 s past its last assertion, then the wire follows the
    player again, and the player is not told anything more. */
 r.flags = 0x84 | DJL_FLAG_PLAYING;
 uint64_t stopped = now;
 unsigned before = ncmds, from = nsent;
 run_remote(&p, &now, stopped + 2500000, &f, &net, &r);
 struct master_view grace = view_statuses(from, nsent);
 CHECK(ncmds == before && grace.statuses >= 10 && grace.master == 0);
 run_remote(&p, &now, stopped + 3300000, &f, &net, &r);
 from = nsent;
 run_remote(&p, &now, stopped + 5500000, &f, &net, &r);
 struct master_view later = view_statuses(from, nsent);
 CHECK(ncmds == before && later.statuses >= 8 && later.plain_master == later.statuses);
}

/* Our deck 3 asks master device 2 for master and is granted it. Returns at the grant. */
static void grant_setup(struct djl_player *p, uint64_t *now, struct djl_net *net, struct djl_deck_facts *f,
                        struct remote *r)
{
 running_player(p, now, 3);
 djl_net_init(net, XZ_IP);
 add_peer(net, *now, 2, PEER_IP);
 loaded_facts(f);
 f->master = 0;
 f->sync = 0;
 *r = (struct remote){0x84 | DJL_FLAG_PLAYING | DJL_FLAG_MASTER, 0xff, 1, *now};
 run_remote(p, now, *now + 500000, f, net, r);
 f->wants_master = 1;
 run_remote(p, now, *now + 300000, f, net, r);
 CHECK(count_kind(DJL_MASTER_REQUEST) == 1 && ncmds == 0);
 struct djl_identity two = remote_identity(2, PEER_IP);
 uint8_t b[DJL_MAX_DATAGRAM];
 receive_raw(p, *now, PEER_IP, DJL_PORT_BEAT, b, djl_build_master_response(&two, b), f, net);
 run_remote(p, now, p->status_us - 50000, f, net, r);
 r->handoff = 3;
 r->next_us = *now;
 reset_rec();
}

/* The player sets facts.master `hold_ms` after MASTER_GRANTED. */
static void grant_case(unsigned hold_ms)
{
 struct djl_player p;
 struct djl_net net;
 struct djl_deck_facts f;
 struct remote r;
 uint64_t now = 1000000;
 grant_setup(&p, &now, &net, &f, &r);
 uint64_t t0 = now, t1 = t0 + hold_ms * 1000ull;
 run_remote(&p, &now, t1, &f, &net, &r);
 unsigned held = nsent;
 f.master = 1;
 f.wants_master = 0;
 run_remote(&p, &now, t1 + 1000000, &f, &net, &r);
 struct master_view span = view_statuses(0, held), all = view_statuses(0, nsent);
 printf("  grant, player %4u ms late: %u statuses while facts.master false, %u after; %u say master; %u MASTER_GRANTED\n",
        hold_ms, span.statuses, all.statuses - span.statuses, all.master, count_cmd(DJL_CMD_MASTER_GRANTED));
 CHECK(span.statuses >= (hold_ms >= 150 ? 1u : 0u));
 CHECK(all.statuses >= 5 && all.plain_master == all.statuses);
 CHECK(count_cmd(DJL_CMD_MASTER_GRANTED) == 1 && ncmds == 1 && cmds[0].c.device == 2 && cmds[0].t == t0);
 CHECK(count_kind(DJL_MASTER_REQUEST) == 0 && r.flags == (0x84 | DJL_FLAG_PLAYING));
}

static void test_grant_latch(void)
{
 const unsigned holds[] = {0, 150, 450, 2500};
 for (unsigned i = 0; i < sizeof holds / sizeof holds[0]; i++) grant_case(holds[i]);
}

/* The player never sets facts.master: master on the wire for 3 s, then the wire follows the facts;
   the request is retried as before, at most once a second, without claiming master again. */
static void test_grant_expiry(void)
{
 struct djl_player p;
 struct djl_net net;
 struct djl_deck_facts f;
 struct remote r;
 uint64_t now = 1000000;
 grant_setup(&p, &now, &net, &f, &r);
 uint64_t t0 = now;
 run_remote(&p, &now, t0 + 6500000, &f, &net, &r);
 unsigned at3 = first_sent_at(t0 + 3000000);
 struct master_view latched = view_statuses(0, at3), after = view_statuses(at3, nsent);
 printf("  grant ignored: %u statuses in the first 3 s (%u plain master), %u after (%u say master); MASTER_GRANTED at",
        latched.statuses, latched.plain_master, after.statuses, after.master);
 for (unsigned i = 0; i < ncmds; i++) printf(" +%llu ms", (unsigned long long)(cmds[i].t - t0) / 1000);
 printf("\n");
 CHECK(latched.statuses >= 14 && latched.plain_master == latched.statuses);
 CHECK(after.statuses >= 15 && after.master == 0 && after.not_master_clean == after.statuses);
 CHECK(ncmds >= 2 && count_cmd(DJL_CMD_MASTER_GRANTED) == ncmds && cmds[0].t == t0 && cmds[0].c.device == 2);
 CHECK(count_cmd_between(DJL_CMD_MASTER_GRANTED, t0 + 1, t0 + 3000000) == 0);
 for (unsigned i = 1; i < ncmds; i++) CHECK(cmds[i].t - cmds[i - 1].t >= 1000000);
 CHECK(count_kind_from(DJL_MASTER_REQUEST, 0) == 0);
}

/* ---- 60 s at 128 BPM, +3 % at 20 s, seek at 40 s ------------------------------------------ */

#define SIM_BPM 12800u
#define SIM_TRACK_MS 300000u
static uint32_t grid_ms(uint32_t k) { return (uint32_t)((uint64_t)(k - 1) * 6000000u / SIM_BPM); }
struct segment { uint64_t t; int64_t pos_us; int32_t speed; };
static int64_t seg_pos(const struct segment *s, uint64_t now)
{
 return s->pos_us + (int64_t)(((now - s->t) * (uint64_t)s->speed) >> 20);
}
static void sim_facts(struct djl_deck_facts *f, const struct segment *s, uint64_t captured, int playing)
{
 empty_facts(f);
 f->content_epoch = 3;
 f->track = (struct djl_track_ref){1, DJL_SLOT_USB, DJL_TRACK_REKORDBOX, 77};
 f->play = playing ? DJL_PLAY_PLAYING : DJL_PLAY_PAUSED;
 f->track_bpm_x100 = SIM_BPM;
 f->pitch_q20 = (uint32_t)s->speed;
 f->fader_pitch_q20 = (uint32_t)s->speed;
 struct djl_timeline *t = &f->timeline;
 t->captured_us = captured;
 t->playhead_us = playing ? seg_pos(s, captured) : s->pos_us;
 t->speed_q20 = playing ? s->speed : 0;
 t->track_ms = SIM_TRACK_MS;
 uint32_t k = 1;
 while ((int64_t)grid_ms(k) * 1000 < t->playhead_us) k++;
 t->next_beat = k;
 for (unsigned i = 0; i < DJL_AHEAD; i++) {
  uint32_t ms = grid_ms(k + i);
  t->ahead_ms[i] = ms <= SIM_TRACK_MS ? ms : DJL_NO_TIME;
  t->ahead_bar[i] = (uint8_t)((k + i - 1) % 4 + 1);
 }
}

struct expected_beat { uint32_t k; uint64_t t; int32_t speed; };

static void test_sixty_seconds(int fresh)
{
 struct djl_player p;
 uint64_t now = 1000000;
 running_player(&p, &now, 4);
 struct djl_net net;
 djl_net_init(&net, XZ_IP);
 add_peer(&net, now, 1, PEER_IP);
 const uint64_t start = now + 400000, end = start + 60000000;
 const int32_t unity = (int32_t)DJL_PITCH_UNITY, up = unity + unity * 3 / 100;
 struct segment seg[3] = {{start, 0, unity}, {start + 20000000, 0, up}, {start + 40000000, 12200000, up}};
 seg[1].pos_us = seg_pos(&seg[0], seg[1].t);

 static struct expected_beat expect[400];
 unsigned nexpect = 0;
 for (unsigned s = 0; s < 3; s++) {
  uint64_t until = s < 2 ? seg[s + 1].t : end;
  for (uint32_t k = 1; grid_ms(k) <= SIM_TRACK_MS; k++) {
   int64_t at = (int64_t)grid_ms(k) * 1000;
   if (at < seg[s].pos_us) continue;
   uint64_t d = (uint64_t)(at - seg[s].pos_us) << 20, t = seg[s].t + (d + (uint64_t)seg[s].speed - 1) / (uint64_t)seg[s].speed;
   if (t >= until) break;
   expect[nexpect++] = (struct expected_beat){k, t, seg[s].speed};
  }
 }

 struct djl_deck_facts f;
 sim_facts(&f, &seg[0], now, 0);
 run_to(&p, &now, start, &f, &net);
 reset_rec();
 uint64_t captured = start;
 unsigned current = 0, refreshes = 0;
 uint64_t sleep_jitter = 0;
 while (now < end) {
  unsigned s = now >= seg[2].t ? 2 : now >= seg[1].t ? 1 : 0;
  if (s != current) { current = s; captured = seg[s].t; }
  sim_facts(&f, &seg[s], fresh ? now : captured, 1);
  if (!fresh && f.timeline.ahead_ms[8] != DJL_NO_TIME && (int64_t)f.timeline.ahead_ms[8] * 1000 <= seg_pos(&seg[s], now)) {
   captured = now;
   refreshes++;
   sim_facts(&f, &seg[s], captured, 1);
  }
  g_now = now;
  uint64_t next = djl_player_step(&p, now, &f, &net, &sink);
  CHECK(next > now);
  sleep_jitter = rnd() % 800;
  next += sleep_jitter;
  uint64_t boundary = s < 2 ? seg[s + 1].t : end;
  now = next < boundary ? next : boundary;
 }

 unsigned nbeats = 0, nstatus = 0, npos = 0, bad_beats = 0, bad_status = 0, bad_pos = 0;
 uint64_t last_status = 0, last_pos = 0;
 for (unsigned i = 0; i < nsent; i++) {
  struct djl_packet pk;
  int kind = sent_kind(&sent[i], &pk);
  if (kind == DJL_BEAT) {
   if (nbeats < nexpect) {
    const struct expected_beat *e = &expect[nbeats];
    int ok = sent[i].t >= e->t && sent[i].t - e->t <= 1000 && pk.u.beat.beat_in_bar == (e->k - 1) % 4 + 1 &&
             pk.u.beat.next_beat_ms == grid_ms(e->k + 1) - grid_ms(e->k) && pk.u.beat.bpm_x100 == SIM_BPM &&
             pk.u.beat.pitch_q20 == (uint32_t)e->speed && pk.device == 4 && sent[i].broadcast;
    if (!ok) {
     if (++bad_beats <= 5) fprintf(stderr, "  beat %u: k %u expected at %llu, sent at %llu, bar %u\n", nbeats, e->k,
                                    (unsigned long long)e->t, (unsigned long long)sent[i].t, pk.u.beat.beat_in_bar);
    }
   }
   nbeats++;
  } else if (kind == DJL_STATUS) {
   uint64_t st = sent[i].t;
   for (unsigned j = 0; j < nexpect; j++) {
    /* 3 us of slack: the deck model floors positions, so the predicted beat may trail the exact one. */
    if ((st >= expect[j].t && st - expect[j].t < 10000) || (st < expect[j].t && expect[j].t - st <= 4997)) bad_status++;
   }
   if (last_status && (st - last_status < 199000 || st - last_status > 216000)) bad_status++;
   last_status = st;
   const struct segment *sg = &seg[st >= seg[2].t ? 2 : st >= seg[1].t ? 1 : 0];
   int64_t pos = seg_pos(sg, st);
   uint32_t k = 0;
   while ((int64_t)grid_ms(k + 1) * 1000 <= pos) k++;
   if (pk.u.status.beat_number != k || pk.u.status.beat_in_bar != (k - 1) % 4 + 1 || !(pk.u.status.flags & DJL_FLAG_PLAYING)) {
    if (++bad_status <= 5) fprintf(stderr, "  status at %llu: beat %u bar %u, expected %u\n", (unsigned long long)st,
                                    pk.u.status.beat_number, pk.u.status.beat_in_bar, k);
   }
   nstatus++;
  } else if (kind == DJL_POSITION) {
   if (last_pos && (sent[i].t - last_pos < 29000 || sent[i].t - last_pos > 31000)) bad_pos++;
   last_pos = sent[i].t;
   const struct segment *sg = &seg[sent[i].t >= seg[2].t ? 2 : sent[i].t >= seg[1].t ? 1 : 0];
   int64_t want = seg_pos(sg, sent[i].t) / 1000;
   if ((int64_t)pk.u.position.playhead_ms < want - 1 || (int64_t)pk.u.position.playhead_ms > want + 1) bad_pos++;
   npos++;
  }
 }
 printf("  60 s (%s timeline): %u beats sent / %u expected, %u status, %u position, %u refreshes\n",
        fresh ? "fresh every step" : "refreshed on change", nbeats, nexpect, nstatus, npos, refreshes);
 CHECK(nexpect > 128 && nbeats == nexpect && bad_beats == 0);
 CHECK(bad_status == 0 && nstatus >= 295 && nstatus <= 301);
 CHECK(bad_pos == 0 && npos >= 1990 && npos <= 2001);
 CHECK(overflow == 0);

 /* Paused: no beats, status shows paused, position continues. */
 reset_rec();
 struct segment held = {now, 30000000, unity};
 sim_facts(&f, &held, now, 0);
 run_to(&p, &now, now + 5000000, &f, &net);
 struct djl_packet last;
 CHECK(count_kind(DJL_BEAT) == 0 && count_kind(DJL_POSITION) >= 160 && count_kind(DJL_STATUS) >= 24);
 for (int i = (int)nsent - 1; i >= 0; i--) {
  if (sent_kind(&sent[i], &last) == DJL_STATUS) {
   CHECK(last.u.status.play == DJL_PLAY_PAUSED && !(last.u.status.flags & DJL_FLAG_PLAYING) && sent[i].data[0x8b] == 0x9e);
   break;
  }
 }
 /* Same content: a play from the paused position on a beat announces that beat once. */
 reset_rec();
 struct segment go = {now, (int64_t)grid_ms(9) * 1000, unity};
 sim_facts(&f, &go, now, 1);
 g_now = now;
 djl_player_step(&p, now, &f, &net, &sink);
 djl_player_step(&p, now, &f, &net, &sink);
 struct djl_packet bt;
 CHECK(count_kind(DJL_BEAT) == 1 && sent_kind(&sent[0], &bt) == DJL_BEAT && bt.u.beat.beat_in_bar == 1);
}

/* ---- timeline helpers and the status quiet window ----------------------------------------- */

static void test_timeline(void)
{
 struct djl_deck_facts f;
 loaded_facts(&f);
 struct djl_timeline *t = &f.timeline;
 const int32_t speeds[] = {(int32_t)DJL_PITCH_UNITY, 0x107ae1, 0xf5c29, 0x1fffff, 0x80000, 1};
 for (unsigned s = 0; s < sizeof speeds / sizeof speeds[0]; s++) {
  t->speed_q20 = speeds[s];
  for (unsigned i = 0; i < DJL_AHEAD; i++) {
   uint64_t at = djl_beat_time_us(t, i);
   uint64_t need = ((uint64_t)t->ahead_ms[i] * 1000 - (uint64_t)t->playhead_us) << 20, speed = (uint64_t)speeds[s];
   CHECK(at > t->captured_us);
   CHECK((at - t->captured_us) * speed >= need && (at - 1 - t->captured_us) * speed < need);
  }
 }
 t->speed_q20 = 0x107ae1;
 CHECK(djl_beat_time_us(t, DJL_AHEAD) == 0);
 t->ahead_ms[4] = DJL_NO_TIME;
 CHECK(djl_beat_time_us(t, 4) == 0);
 t->playhead_us = (int64_t)t->ahead_ms[1] * 1000 + 1;
 CHECK(djl_beat_time_us(t, 0) == 0 && djl_beat_time_us(t, 1) == 0 && djl_beat_time_us(t, 2) > t->captured_us);
 t->playhead_us = (int64_t)t->ahead_ms[1] * 1000;
 CHECK(djl_beat_time_us(t, 1) == t->captured_us);
 t->speed_q20 = 0;
 CHECK(djl_beat_time_us(t, 2) == 0);
 t->speed_q20 = -(int32_t)DJL_PITCH_UNITY;
 CHECK(djl_beat_time_us(t, 2) == 0);
 t->playhead_us = 2000000;
 CHECK(djl_playhead_ms(t, t->captured_us + 500000) == 1500 && djl_playhead_ms(t, t->captured_us + 5000000) == 0);
 CHECK(djl_playhead_ms(t, t->captured_us - 1000) == 2000);
 t->speed_q20 = (int32_t)DJL_PITCH_UNITY;
 t->playhead_us = -5000;
 CHECK(djl_playhead_ms(t, t->captured_us) == 0 && djl_playhead_ms(t, t->captured_us + 7000) == 2);
 CHECK(djl_playhead_ms(t, t->captured_us + 3600000000ull) == t->track_ms);
 t->playhead_us = INT64_MAX;
 CHECK(djl_playhead_ms(t, UINT64_MAX) == t->track_ms && djl_beat_time_us(t, 0) == 0);
 t->playhead_us = INT64_MIN;
 t->captured_us = UINT64_MAX - 10;
 CHECK(djl_beat_time_us(t, 0) == 0 && djl_playhead_ms(t, UINT64_MAX) == 0);

 /* A status due 3 ms before a beat waits until 10 ms after it; the beat goes first. */
 struct djl_player p;
 uint64_t now = 1000000;
 running_player(&p, &now, 3);
 struct djl_net net;
 djl_net_init(&net, XZ_IP);
 add_peer(&net, now, 1, PEER_IP);
 empty_facts(&f);
 run_to(&p, &now, now + 100000, &f, &net);
 uint64_t due = p.status_us, beat = due + 3000;
 loaded_facts(&f);
 t = &f.timeline;
 t->speed_q20 = (int32_t)DJL_PITCH_UNITY;
 t->captured_us = now;
 t->playhead_us = (int64_t)t->ahead_ms[0] * 1000 - (int64_t)(beat - now);
 reset_rec();
 run_to(&p, &now, due + 50000, &f, &net);
 unsigned beat_at = nsent, status_at = nsent;
 for (unsigned i = 0; i < nsent; i++) {
  struct djl_packet pk;
  int kind = sent_kind(&sent[i], &pk);
  if (kind == DJL_BEAT && beat_at == nsent) beat_at = i;
  if (kind == DJL_STATUS && status_at == nsent) status_at = i;
 }
 CHECK(beat_at < status_at && status_at < nsent);
 CHECK(beat_at < nsent && sent[beat_at].t == beat);
 CHECK(status_at < nsent && sent[status_at].t == beat + 10000);
}

struct area { const char *name; void (*run)(void); unsigned checks, failures; };
static void sixty_fresh(void) { test_sixty_seconds(1); }
static void sixty_change(void) { test_sixty_seconds(0); }

int main(int argc, char **argv)
{
 if (argc > 1) fuzz_iterations = (unsigned)strtoul(argv[1], NULL, 10);
 struct area areas[] = {
  {"capture fixtures", test_fixtures, 0, 0},  {"round trip + lengths", test_roundtrip, 0, 0},
  {"fuzz parse+receive", test_fuzz, 0, 0},    {"claim + cadence", test_claim, 0, 0},
  {"eviction + 0x05", test_eviction, 0, 0},   {"defence", test_defence, 0, 0},
  {"commands", test_commands, 0, 0},          {"master handoff", test_handoff, 0, 0},
  {"yield latch", test_yield_latch, 0, 0},    {"yield refused", test_yield_expiry, 0, 0},
  {"grant latch", test_grant_latch, 0, 0},    {"grant ignored", test_grant_expiry, 0, 0},
  {"timeline + quiet window", test_timeline, 0, 0},
  {"60 s beats, fresh", sixty_fresh, 0, 0},   {"60 s beats, on change", sixty_change, 0, 0},
 };
 unsigned total_failures = 0;
 for (unsigned i = 0; i < sizeof areas / sizeof areas[0]; i++) {
  unsigned c0 = checks, f0 = failures;
  areas[i].run();
  areas[i].checks = checks - c0;
  areas[i].failures = failures - f0;
  total_failures += areas[i].failures;
 }
 printf("%-24s %10s %8s  %s\n", "area", "checks", "failed", "result");
 for (unsigned i = 0; i < sizeof areas / sizeof areas[0]; i++)
  printf("%-24s %10u %8u  %s\n", areas[i].name, areas[i].checks, areas[i].failures, areas[i].failures ? "FAIL" : "PASS");
 printf("%s: %u checks, %u failed\n", total_failures ? "FAIL" : "PASS", checks, total_failures);
 return total_failures ? 1 : 0;
}
