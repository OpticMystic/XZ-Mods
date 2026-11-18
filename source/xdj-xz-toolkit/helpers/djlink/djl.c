/* SPDX-License-Identifier: MIT */
#include "djl.h"
#include <string.h>

#define CLAIM_US 300000u
#define KEEPALIVE_US 2000000u
#define STATUS_US 200000u
#define POSITION_US 30000u
#define AFTER_BEAT_US 10000u
#define BEFORE_BEAT_US 5000u
#define RETRY_US 1000000u
#define HANDOFF_US 3000000u /* longest the wire leads the player through a handoff */
#define PEER_AGE_US 10000000u
#define POS_LIMIT_US ((int64_t)1 << 50)
#define ELAPSED_LIMIT_US ((uint64_t)1 << 42)

static const uint8_t magic[10] = {0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v >> 16); wr16(p + 2, v); }
static int ip_eq(const uint8_t *a, const uint8_t *b) { return a && b && !memcmp(a, b, 4); }

static void put_name(uint8_t *dst, const char *name)
{
 for (unsigned i = 0; i < DJL_NAME && name[i]; i++) dst[i] = (uint8_t)name[i];
}
static void get_name(char *dst, const uint8_t *src)
{
 memcpy(dst, src, DJL_NAME);
 dst[DJL_NAME] = 0;
}

/* Port 50000 layout, reference 2.0. */
static size_t start_packet(const struct djl_identity *id, uint8_t kind, uint8_t structure, size_t len, uint8_t *out)
{
 memset(out, 0, len);
 memcpy(out, magic, 10);
 out[0x0a] = kind;
 put_name(out + 0x0c, id->name);
 out[0x20] = 1;
 out[0x21] = structure;
 wr16(out + 0x22, (uint32_t)len);
 return len;
}
/* Port 50001/50002 layout, reference 2.0: byte 0x22 counts the bytes after it. */
static size_t status_packet(const struct djl_identity *id, uint8_t kind, uint8_t b1f, uint8_t subtype, size_t len,
                            uint8_t *out)
{
 memset(out, 0, len);
 memcpy(out, magic, 10);
 out[0x0a] = kind;
 put_name(out + 0x0b, id->name);
 out[0x1f] = b1f;
 out[0x20] = subtype;
 out[0x21] = id->device;
 wr16(out + 0x22, (uint32_t)(len - 0x24));
 return len;
}
/* The XZ's 0x03 keep-alive marker is the CDJ-3000-era one, so claim with the CDJ-3000 variants
   (reference 2.1: hello 0x04/0x26/0x40, claims 0x03). */
static int modern(const struct djl_identity *id) { return id->structure >= 3; }

size_t djl_build_hello(const struct djl_identity *id, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t n = start_packet(id, 0x0a, modern(id) ? 0x04 : 0x02, modern(id) ? 0x26 : 0x25, out);
 out[0x24] = id->device_class;
 if (modern(id)) out[0x25] = 0x40;
 return n;
}
size_t djl_build_claim1(const struct djl_identity *id, uint8_t n, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = start_packet(id, 0x00, modern(id) ? 0x03 : 0x02, 0x2c, out);
 out[0x24] = n;
 out[0x25] = id->device_class;
 memcpy(out + 0x26, id->mac, 6);
 return len;
}
size_t djl_build_claim2(const struct djl_identity *id, uint8_t n, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = start_packet(id, 0x02, modern(id) ? 0x03 : 0x02, 0x32, out);
 memcpy(out + 0x24, id->ip, 4);
 memcpy(out + 0x28, id->mac, 6);
 out[0x2e] = id->device;
 out[0x2f] = n;
 out[0x30] = id->device_class;
 out[0x31] = 2; /* a = specific number */
 return len;
}
size_t djl_build_claim3(const struct djl_identity *id, uint8_t n, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = start_packet(id, 0x04, modern(id) ? 0x03 : 0x02, 0x26, out);
 out[0x24] = id->device;
 out[0x25] = n;
 return len;
}
size_t djl_build_keepalive(const struct djl_identity *id, uint8_t peers, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = start_packet(id, 0x06, id->structure, 0x36, out);
 out[0x24] = id->device;
 out[0x25] = 1; /* stock XZ sends 01 on all three devices (reference 2.1) */
 memcpy(out + 0x26, id->mac, 6);
 memcpy(out + 0x2c, id->ip, 4);
 out[0x30] = peers;
 out[0x34] = id->device_class;
 out[0x35] = id->model;
 return len;
}
size_t djl_build_defense(const struct djl_identity *id, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = start_packet(id, 0x08, 0x02, 0x29, out);
 out[0x24] = id->device;
 memcpy(out + 0x25, id->ip, 4);
 return len;
}
size_t djl_build_assign_finished(const struct djl_identity *id, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = start_packet(id, 0x05, 0x02, 0x26, out);
 out[0x24] = id->device;
 out[0x25] = 1;
 return len;
}

static uint32_t gap(const struct djl_timeline *t, unsigned from, unsigned to)
{
 if (to >= DJL_AHEAD || t->ahead_ms[to] == DJL_NO_TIME || t->ahead_ms[from] == DJL_NO_TIME) return DJL_NO_TIME;
 return t->ahead_ms[to] - t->ahead_ms[from];
}
size_t djl_build_beat(const struct djl_identity *id, const struct djl_deck_facts *f, unsigned index,
                      uint8_t out[DJL_MAX_DATAGRAM])
{
 const struct djl_timeline *t = &f->timeline;
 if (index >= DJL_AHEAD || t->ahead_ms[index] == DJL_NO_TIME) return 0;
 uint8_t bar = t->ahead_bar[index];
 unsigned to_bar = bar >= 1 && bar <= 4 ? 5u - bar : 4u;
 size_t len = status_packet(id, 0x28, 0x01, 0x00, 0x60, out);
 const uint32_t ms[6] = {gap(t, index, index + 1), gap(t, index, index + 2), gap(t, index, index + to_bar),
                         gap(t, index, index + 4), gap(t, index, index + to_bar + 4), gap(t, index, index + 8)};
 for (unsigned i = 0; i < 6; i++) wr32(out + 0x24 + 4 * i, ms[i]);
 memset(out + 0x3c, 0xff, 24);
 wr32(out + 0x54, f->pitch_q20);
 wr16(out + 0x5a, f->track_bpm_x100);
 out[0x5c] = bar;
 out[0x5f] = id->device;
 return len;
}

/* a * q / 2^20 rounded down, exact for a < 2^43 and any 32-bit q. */
static uint64_t mul_q20(uint64_t a, uint32_t q) { return a * (q >> 20) + ((a * (q & 0xfffffu)) >> 20); }
static int64_t clamp_pos(int64_t v) { return v > POS_LIMIT_US ? POS_LIMIT_US : v < -POS_LIMIT_US ? -POS_LIMIT_US : v; }
static int64_t heard_us(const struct djl_timeline *t, uint64_t now_us)
{
 int64_t pos = clamp_pos(t->playhead_us);
 if (now_us <= t->captured_us || !t->speed_q20) return pos;
 uint64_t elapsed = now_us - t->captured_us;
 if (elapsed > ELAPSED_LIMIT_US) elapsed = ELAPSED_LIMIT_US;
 int64_t speed = t->speed_q20;
 int64_t moved = (int64_t)mul_q20(elapsed, (uint32_t)(speed < 0 ? -speed : speed));
 return speed < 0 ? pos - moved : pos + moved;
}
uint32_t djl_playhead_ms(const struct djl_timeline *t, uint64_t now_us)
{
 int64_t pos = heard_us(t, now_us);
 if (pos <= 0) return 0;
 uint64_t ms = (uint64_t)pos / 1000u;
 return ms > t->track_ms ? t->track_ms : (uint32_t)ms;
}
uint64_t djl_beat_time_us(const struct djl_timeline *t, unsigned index)
{
 if (index >= DJL_AHEAD || t->ahead_ms[index] == DJL_NO_TIME || t->speed_q20 <= 0) return 0;
 int64_t pos = clamp_pos(t->playhead_us), beat = (int64_t)t->ahead_ms[index] * 1000;
 if (beat < pos) return 0;
 uint64_t delta = (uint64_t)(beat - pos), speed = (uint64_t)t->speed_q20, whole = delta / speed;
 if (whole >= (uint64_t)1 << 40) return 0;
 uint64_t real = (whole << 20) + ((((delta % speed) << 20) + speed - 1) / speed);
 return real > UINT64_MAX - t->captured_us ? 0 : t->captured_us + real;
}

static int is_playing(uint8_t play)
{
 return play == DJL_PLAY_PLAYING || play == DJL_PLAY_LOOPING || play == DJL_PLAY_CUE_PLAY;
}
static int is_active(uint8_t play)
{
 return is_playing(play) || play == DJL_PLAY_LOADING || play == DJL_PLAY_SEARCHING || play == DJL_PLAY_CUE_SCRATCH;
}
static int loaded(const struct djl_deck_facts *f) { return f->track.type != DJL_TRACK_NONE; }

size_t djl_build_position(const struct djl_identity *id, const struct djl_deck_facts *f, uint64_t now_us,
                          uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = status_packet(id, 0x0b, 0x02, 0x00, 0x3c, out);
 int64_t pitch = ((int64_t)f->pitch_q20 - (int64_t)DJL_PITCH_UNITY) * 10000;
 pitch = (pitch + (pitch < 0 ? -(int64_t)(DJL_PITCH_UNITY / 2) : (int64_t)(DJL_PITCH_UNITY / 2))) / (int64_t)DJL_PITCH_UNITY;
 uint32_t bpm = DJL_NO_TIME;
 if (f->track_bpm_x100 != 0xffff)
  bpm = (uint32_t)(((uint64_t)f->track_bpm_x100 * f->pitch_q20 + 5u * DJL_PITCH_UNITY) / (10u * DJL_PITCH_UNITY));
 wr32(out + 0x24, f->timeline.track_ms / 1000u);
 wr32(out + 0x28, djl_playhead_ms(&f->timeline, now_us));
 wr32(out + 0x2c, (uint32_t)pitch);
 wr32(out + 0x38, bpm);
 return len;
}

/* Status bytes with fixed values (reference 2.4); 0x68, 0x75 and 0xb6 must be 1 per PC. Byte 0x78
   stays 00 although DJL's diagram shows 01: real CDJ-2000nexus packets carry 00, and
   python-prodj-link reads 0x78-0x7b as one 32-bit play state. */
static const struct { uint16_t at; uint8_t value; } status_fixed[] = {
 {0x26, 0x01}, {0x68, 0x01}, {0x6a, 0x04}, {0x6b, 0x04}, {0x6f, 0x04}, {0x73, 0x04}, {0x75, 0x01},
 {0x8a, 0xff}, {0x94, 0x7f}, {0x95, 0xff}, {0x96, 0xff}, {0x97, 0xff}, {0xa4, 0x01},
 {0xa5, 0xff}, {0xb6, 0x01}, {0xd0, 0x12}, {0xd1, 0x34}, {0xd2, 0x56}, {0xd3, 0x78}, {0xd7, 0x01},
 {0xd8, 0x01}, {0xd9, 0x01}, {0xda, 0x01}, {0xdb, 0x01},
};

static unsigned passed_beats(const struct djl_timeline *t, uint64_t now_us)
{
 int64_t pos = heard_us(t, now_us);
 unsigned n = 0;
 while (n < DJL_AHEAD && t->ahead_ms[n] != DJL_NO_TIME && (int64_t)t->ahead_ms[n] * 1000 <= pos) n++;
 return n;
}

/* A handoff window started at `since` (0: none) is still open at now_us. A clock that went backwards
   keeps it open. */
static int window_open(uint64_t since, uint64_t now_us)
{
 return since && (now_us < since || now_us - since < HANDOFF_US);
}
/* Whether the deck is tempo master, on the wire and for every handoff rule: the reducer's decision
   while a handoff window is open, the player's facts otherwise. */
static int is_master(const struct djl_player *p, const struct djl_deck_facts *f, uint64_t now_us)
{
 if (window_open(p->yield_us, now_us)) return 0;
 if (window_open(p->grant_us, now_us)) return 1;
 return f->master != 0;
}

size_t djl_build_status(const struct djl_player *p, const struct djl_deck_facts *f, const struct djl_net *net,
                        uint64_t now_us, uint8_t out[DJL_MAX_DATAGRAM])
{
 (void)net;
 const struct djl_identity *id = &p->id;
 const struct djl_timeline *t = &f->timeline;
 size_t len = id->status_length >= 0xcc && id->status_length <= DJL_MAX_DATAGRAM ? id->status_length : 0x11c;
 status_packet(id, 0x0a, 0x01, id->status_subtype, DJL_MAX_DATAGRAM, out);
 wr16(out + 0x22, (uint32_t)(len - 0x24));
 for (unsigned i = 0; i < sizeof status_fixed / sizeof status_fixed[0]; i++) out[status_fixed[i].at] = status_fixed[i].value;
 int playing = is_playing(f->play), has = loaded(f), master = is_master(p, f, now_us);
 int tempo_ok = f->track.type == DJL_TRACK_REKORDBOX && f->track_bpm_x100 != 0xffff;
 out[0x24] = id->device;
 out[0x27] = is_active(f->play);
 if (has) {
  out[0x28] = f->track.player;
  out[0x29] = f->track.slot;
  out[0x2a] = f->track.type;
  wr32(out + 0x2c, f->track.rekordbox_id);
 }
 out[0x7b] = f->play;
 memcpy(out + 0x7c, id->firmware, 4);
 wr32(out + 0x84, p->sync_n);
 out[0x89] = (uint8_t)(0x84 | (playing ? DJL_FLAG_PLAYING : 0) | (master ? DJL_FLAG_MASTER : 0) |
                       (f->sync ? DJL_FLAG_SYNC : 0) | (f->on_air ? DJL_FLAG_ON_AIR : 0) |
                       (f->bpm_only_sync ? DJL_FLAG_BPM_SYNC : 0));
 out[0x8b] = playing ? id->play2_playing : id->play2_stopped;
 wr32(out + 0x8c, f->pitch_q20);
 /* M_v: reference 2.4 gives 0000 for a non-rekordbox track; 8000 only with a usable tempo. */
 wr16(out + 0x90, tempo_ok ? 0x8000 : has && f->track.type != DJL_TRACK_REKORDBOX ? 0x0000 : 0x7fff);
 wr16(out + 0x92, has ? f->track_bpm_x100 : 0xffff);
 wr32(out + 0x98, f->fader_pitch_q20);
 out[0x9d] = !has ? 0x00 : playing && t->speed_q20 > 0 ? 0x09 : 0x01;
 out[0x9e] = !master ? 0 : f->track.type == DJL_TRACK_REKORDBOX ? 1 : 2;
 out[0x9f] = master ? p->yielding_to : DJL_NO_DEVICE;
 uint32_t beat = DJL_NO_TIME;
 uint8_t bar = 0;
 if (has && t->next_beat) {
  unsigned n = passed_beats(t, now_us);
  beat = t->next_beat - 1 + n;
  if (beat && n) bar = t->ahead_bar[n - 1];
  else if (beat && t->ahead_bar[0] >= 1 && t->ahead_bar[0] <= 4) bar = (uint8_t)((t->ahead_bar[0] + 2) % 4 + 1);
 }
 wr32(out + 0xa0, beat);
 out[0xa6] = bar;
 wr32(out + 0xc0, f->pitch_q20);
 wr32(out + 0xc4, f->fader_pitch_q20);
 wr32(out + 0xc8, p->packet_counter);
 out[0xcc] = id->nx;
 return len;
}

size_t djl_build_master_request(const struct djl_identity *id, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = status_packet(id, 0x26, 0x01, 0x00, 0x28, out);
 out[0x27] = id->device;
 return len;
}
size_t djl_build_master_response(const struct djl_identity *id, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = status_packet(id, 0x27, 0x01, 0x00, 0x2c, out);
 out[0x27] = id->device;
 out[0x2b] = 1;
 return len;
}
/* Body unknown (reference 2.9); PPL models it as the common header, D again and one zero pad. */
size_t djl_build_load_ack(const struct djl_identity *id, uint8_t out[DJL_MAX_DATAGRAM])
{
 size_t len = status_packet(id, 0x1a, 0x01, 0x00, 0x28, out);
 out[0x24] = id->device;
 return len;
}

/* ---- parse ------------------------------------------------------------------------------- */

static const struct { uint16_t port; uint8_t type; uint16_t min; enum djl_kind kind; } kinds[] = {
 {DJL_PORT_ANNOUNCE, 0x0a, 0x25, DJL_HELLO},          {DJL_PORT_ANNOUNCE, 0x00, 0x2c, DJL_CLAIM1},
 {DJL_PORT_ANNOUNCE, 0x02, 0x32, DJL_CLAIM2},         {DJL_PORT_ANNOUNCE, 0x04, 0x26, DJL_CLAIM3},
 {DJL_PORT_ANNOUNCE, 0x05, 0x26, DJL_ASSIGN_FINISHED}, {DJL_PORT_ANNOUNCE, 0x06, 0x36, DJL_KEEPALIVE},
 {DJL_PORT_ANNOUNCE, 0x08, 0x29, DJL_DEFENSE},        {DJL_PORT_BEAT, 0x02, 0x28, DJL_FADER_START},
 {DJL_PORT_BEAT, 0x03, 0x2d, DJL_ON_AIR},             {DJL_PORT_BEAT, 0x0b, 0x3c, DJL_POSITION},
 {DJL_PORT_BEAT, 0x26, 0x28, DJL_MASTER_REQUEST},     {DJL_PORT_BEAT, 0x27, 0x2c, DJL_MASTER_RESPONSE},
 {DJL_PORT_STATUS, 0x27, 0x2c, DJL_MASTER_RESPONSE},  {DJL_PORT_BEAT, 0x28, 0x60, DJL_BEAT},
 {DJL_PORT_BEAT, 0x2a, 0x2c, DJL_SYNC_CONTROL},       {DJL_PORT_STATUS, 0x05, 0x30, DJL_MEDIA_QUERY},
 {DJL_PORT_STATUS, 0x0a, 0xcc, DJL_STATUS},           {DJL_PORT_STATUS, 0x19, 0x41, DJL_LOAD_TRACK},
 {DJL_PORT_STATUS, 0x29, 0x38, DJL_MIXER_STATUS},
};

static void track_at(struct djl_track_ref *r, const uint8_t *d)
{
 r->player = d[0];
 r->slot = d[1];
 r->type = d[2];
 r->rekordbox_id = rd32(d + 4);
}

int djl_parse(uint16_t port, const uint8_t *d, size_t len, struct djl_packet *o)
{
 if (!o) return 0;
 memset(o, 0, sizeof *o);
 if (!d || len < 0x24 || len > 0xffff || memcmp(d, magic, 10)) return 0;
 unsigned k = 0, n = sizeof kinds / sizeof kinds[0];
 while (k < n && (kinds[k].port != port || kinds[k].type != d[0x0a])) k++;
 if (k == n || len < kinds[k].min) return 0;
 o->kind = kinds[k].kind;
 if (port == DJL_PORT_ANNOUNCE) get_name(o->name, d + 0x0c);
 else {
  get_name(o->name, d + 0x0b);
  o->device = d[0x21];
 }
 switch (o->kind) {
 case DJL_CLAIM1:
  o->u.claim1.n = d[0x24];
  o->u.claim1.device_class = d[0x25];
  memcpy(o->u.claim1.mac, d + 0x26, 6);
  break;
 case DJL_CLAIM2:
  memcpy(o->u.claim2.ip, d + 0x24, 4);
  memcpy(o->u.claim2.mac, d + 0x28, 6);
  o->device = o->u.claim2.device = d[0x2e];
  o->u.claim2.n = d[0x2f];
  o->u.claim2.device_class = d[0x30];
  o->u.claim2.auto_assign = d[0x31];
  break;
 case DJL_CLAIM3:
  o->device = o->u.claim3.device = d[0x24];
  o->u.claim3.n = d[0x25];
  break;
 case DJL_ASSIGN_FINISHED: o->device = d[0x24]; break;
 case DJL_KEEPALIVE:
  o->device = o->u.keepalive.device = d[0x24];
  memcpy(o->u.keepalive.mac, d + 0x26, 6);
  memcpy(o->u.keepalive.ip, d + 0x2c, 4);
  o->u.keepalive.peers = d[0x30];
  o->u.keepalive.device_class = d[0x34];
  o->u.keepalive.model = d[0x35];
  break;
 case DJL_DEFENSE:
  o->device = o->u.defense.device = d[0x24];
  memcpy(o->u.defense.ip, d + 0x25, 4);
  break;
 case DJL_FADER_START: memcpy(o->u.fader_start.command, d + 0x24, 4); break;
 case DJL_ON_AIR:
  o->u.on_air.channels = len >= 0x35 && d[0x20] == 0x03 ? 6 : 4;
  memcpy(o->u.on_air.on, d + 0x24, 4);
  if (o->u.on_air.channels == 6) {
   o->u.on_air.on[4] = d[0x2d];
   o->u.on_air.on[5] = d[0x2e];
  }
  break;
 case DJL_POSITION: {
  uint32_t pitch = rd32(d + 0x2c);
  o->u.position.track_seconds = rd32(d + 0x24);
  o->u.position.playhead_ms = rd32(d + 0x28);
  o->u.position.pitch_x100 = pitch > 0x7fffffffu ? -(int32_t)(~pitch) - 1 : (int32_t)pitch;
  o->u.position.bpm_x10 = rd32(d + 0x38);
  break;
 }
 case DJL_MASTER_REQUEST: o->u.master_request.requester = d[0x27]; break;
 case DJL_MASTER_RESPONSE: o->u.master_response.accepted = d[0x2b] == 1; break;
 case DJL_BEAT:
  o->u.beat.next_beat_ms = rd32(d + 0x24);
  o->u.beat.second_beat_ms = rd32(d + 0x28);
  o->u.beat.next_bar_ms = rd32(d + 0x2c);
  o->u.beat.fourth_beat_ms = rd32(d + 0x30);
  o->u.beat.second_bar_ms = rd32(d + 0x34);
  o->u.beat.eighth_beat_ms = rd32(d + 0x38);
  o->u.beat.pitch_q20 = rd32(d + 0x54);
  o->u.beat.bpm_x100 = rd16(d + 0x5a);
  o->u.beat.beat_in_bar = d[0x5c];
  break;
 case DJL_SYNC_CONTROL: o->u.sync_control.command = d[0x2b]; break;
 case DJL_MEDIA_QUERY:
  memcpy(o->u.media_query.reply_ip, d + 0x24, 4);
  o->u.media_query.player = d[0x2b];
  o->u.media_query.slot = d[0x2f];
  break;
 case DJL_STATUS:
  o->u.status.length = (uint16_t)len;
  o->u.status.active = d[0x27];
  track_at(&o->u.status.track, d + 0x28);
  o->u.status.play = d[0x7b];
  memcpy(o->u.status.firmware, d + 0x7c, 4);
  o->u.status.sync_n = rd32(d + 0x84);
  o->u.status.flags = d[0x89];
  o->u.status.pitch_q20 = rd32(d + 0x8c);
  o->u.status.tempo_valid = rd16(d + 0x90);
  o->u.status.bpm_x100 = rd16(d + 0x92);
  o->u.status.fader_pitch_q20 = rd32(d + 0x98);
  o->u.status.master_mode = d[0x9e];
  o->u.status.handoff_to = d[0x9f];
  o->u.status.beat_number = rd32(d + 0xa0);
  o->u.status.beat_in_bar = d[0xa6];
  o->u.status.packet_counter = rd32(d + 0xc8);
  break;
 case DJL_LOAD_TRACK:
  o->u.load_track.source = d[0x24];
  o->u.load_track.target_deck = (uint8_t)(d[0x40] + 1);
  track_at(&o->u.load_track.track, d + 0x28);
  break;
 case DJL_MIXER_STATUS:
  o->u.mixer_status.flags = d[0x27];
  o->u.mixer_status.pitch_q20 = rd32(d + 0x28);
  o->u.mixer_status.bpm_x100 = rd16(d + 0x2e);
  o->u.mixer_status.handoff_to = d[0x36];
  o->u.mixer_status.beat_in_bar = d[0x37];
  break;
 default: break;
 }
 return 1;
}

/* ---- identity ---------------------------------------------------------------------------- */

void djl_identity_xz_deck(struct djl_identity *id, uint8_t number, const uint8_t unit_mac[6], const uint8_t ip[4])
{
 memset(id, 0, sizeof *id);
 memcpy(id->name, "XDJ-XZ", 7);
 id->device = number;
 memcpy(id->mac, unit_mac, 6);
 id->mac[0] |= 0x02;
 id->mac[5] = (uint8_t)(unit_mac[5] + number - 1 + (number >= 3 ? DJL_XZ_MAC_SKIP : 0));
 memcpy(id->ip, ip, 4);
 id->structure = 0x03;
 id->device_class = 1;
 id->model = 0x00;
 id->status_subtype = 0x03;
 id->play2_playing = 0x9a;
 id->play2_stopped = 0x9e;
 id->nx = 0x1f;
 memcpy(id->firmware, "1.26", 4);
 id->status_length = 0x124; /* stock XZ decks, research/native-inventory.md section 3 */
}

/* ---- network view ------------------------------------------------------------------------ */

void djl_net_init(struct djl_net *n, const uint8_t local_ip[4])
{
 memset(n, 0, sizeof *n);
 memcpy(n->local_ip, local_ip, 4);
}

static struct djl_peer *peer_for(struct djl_net *n, uint8_t device, const uint8_t ip[4], uint64_t now_us)
{
 struct djl_peer *free_slot = NULL, *oldest = &n->peers[0];
 for (unsigned i = 0; i < DJL_PEERS; i++) {
  struct djl_peer *q = &n->peers[i];
  if (q->in_use && q->device == device && ip_eq(q->ip, ip)) return q;
  if (!q->in_use && !free_slot) free_slot = q;
  if (q->seen_us < oldest->seen_us) oldest = q;
 }
 struct djl_peer *q = free_slot ? free_slot : oldest;
 memset(q, 0, sizeof *q);
 q->in_use = 1;
 q->device = device;
 memcpy(q->ip, ip, 4);
 q->handoff_to = DJL_NO_DEVICE;
 q->seen_us = now_us;
 return q;
}

void djl_net_receive(struct djl_net *n, uint64_t now_us, const uint8_t src_ip[4], const uint8_t src_mac[6],
                     const struct djl_packet *pkt)
{
 if (!n || !pkt || !src_ip || ip_eq(src_ip, n->local_ip)) return;
 struct djl_peer *q;
 switch (pkt->kind) {
 case DJL_KEEPALIVE:
  if (!pkt->device) return;
  q = peer_for(n, pkt->device, src_ip, now_us);
  memcpy(q->mac, pkt->u.keepalive.mac, 6);
  q->device_class = pkt->u.keepalive.device_class;
  memcpy(q->name, pkt->name, sizeof q->name);
  q->seen_us = now_us;
  break;
 case DJL_STATUS:
 case DJL_MIXER_STATUS:
  if (!pkt->device) return;
  q = peer_for(n, pkt->device, src_ip, now_us);
  if (!q->device_class) {
   q->device_class = pkt->kind == DJL_STATUS ? 1 : 2;
   memcpy(q->name, pkt->name, sizeof q->name);
   if (src_mac) memcpy(q->mac, src_mac, 6);
  }
  if (pkt->kind == DJL_STATUS) {
   q->flags = pkt->u.status.flags;
   q->master_mode = pkt->u.status.master_mode;
   q->handoff_to = pkt->u.status.handoff_to;
   q->sync_n = pkt->u.status.sync_n;
  } else {
   q->flags = pkt->u.mixer_status.flags;
   q->master_mode = pkt->u.mixer_status.flags & DJL_FLAG_MASTER ? 1 : 0;
   q->handoff_to = pkt->u.mixer_status.handoff_to;
  }
  q->seen_us = q->status_us = now_us;
  break;
 case DJL_ON_AIR:
  memset(n->on_air, 0, sizeof n->on_air);
  memcpy(n->on_air, pkt->u.on_air.on, pkt->u.on_air.channels);
  n->on_air_known = 1;
  n->on_air_us = now_us;
  break;
 default: break;
 }
}

void djl_net_expire(struct djl_net *n, uint64_t now_us)
{
 for (unsigned i = 0; i < DJL_PEERS; i++)
  if (n->peers[i].in_use && now_us > n->peers[i].seen_us && now_us - n->peers[i].seen_us > PEER_AGE_US)
   n->peers[i].in_use = 0;
}

/* M_h of a status packet; mixers use 00 for "no master" (reference 2.5). */
static uint8_t handoff_target(uint8_t h) { return h == 0 || h == 0xff ? DJL_NO_DEVICE : h; }

const struct djl_peer *djl_net_master(const struct djl_net *n)
{
 const struct djl_peer *yielding = NULL;
 for (unsigned i = 0; i < DJL_PEERS; i++) {
  const struct djl_peer *q = &n->peers[i];
  if (!q->in_use || !q->status_us || !(q->flags & DJL_FLAG_MASTER)) continue;
  if (handoff_target(q->handoff_to) == DJL_NO_DEVICE) return q;
  if (!yielding) yielding = q;
 }
 return yielding;
}

uint32_t djl_net_highest_sync_n(const struct djl_net *n)
{
 uint32_t high = 0;
 for (unsigned i = 0; i < DJL_PEERS; i++)
  if (n->peers[i].in_use && n->peers[i].sync_n > high) high = n->peers[i].sync_n;
 return high;
}

static unsigned distinct_ips(const struct djl_net *n, const uint8_t *ips[DJL_PEERS])
{
 unsigned count = 0;
 for (unsigned i = 0; i < DJL_PEERS; i++) {
  if (!n->peers[i].in_use) continue;
  unsigned j = 0;
  while (j < count && !ip_eq(ips[j], n->peers[i].ip)) j++;
  if (j == count) ips[count++] = n->peers[i].ip;
 }
 return count;
}

/* ---- player ------------------------------------------------------------------------------ */

void djl_player_init(struct djl_player *p, const struct djl_identity *id)
{
 memset(p, 0, sizeof *p);
 p->id = *id;
 p->phase = DJL_OFF;
 p->yielding_to = DJL_NO_DEVICE;
 p->asking = DJL_NO_DEVICE;
 p->on_air = 0xff;
}

void djl_player_start(struct djl_player *p, uint64_t now_us)
{
 if (p->phase != DJL_OFF && p->phase != DJL_EVICTED) return;
 p->phase = DJL_HELLOING;
 p->step = 0;
 p->due_us = now_us;
 p->yielding_to = DJL_NO_DEVICE;
 p->asking = DJL_NO_DEVICE;
 p->granted = 0;
 p->yield_us = p->yield_cmd_us = p->grant_us = 0;
 p->pending_beat = 0;
}

void djl_player_stop(struct djl_player *p) { p->phase = DJL_OFF; }

static void send(const struct djl_player *p, const struct djl_sink *s, const uint8_t *ip, uint16_t port,
                 const uint8_t *data, size_t len)
{
 if (len && s && s->send) s->send(s->context, &p->id, ip, port, data, len);
}
static void command(const struct djl_player *p, const struct djl_sink *s, enum djl_command_kind kind, uint8_t device,
                    uint8_t value, const struct djl_track_ref *track)
{
 struct djl_command c;
 memset(&c, 0, sizeof c);
 c.kind = kind;
 c.device = device;
 c.value = value;
 if (track) c.track = *track;
 if (s && s->command) s->command(s->context, p->id.device, &c);
}

/* Next deadline of a periodic timer; a timer that fell a whole period behind restarts from now. */
static uint64_t next_due(uint64_t due, uint64_t now_us, uint32_t period)
{
 due += period;
 return due <= now_us ? now_us + period : due;
}

static void send_keepalive(struct djl_player *p, const struct djl_net *n, const struct djl_sink *s)
{
 uint8_t buf[DJL_MAX_DATAGRAM];
 const uint8_t *ips[DJL_PEERS];
 unsigned peers = n ? distinct_ips(n, ips) + 1 : 1;
 send(p, s, NULL, DJL_PORT_ANNOUNCE, buf, djl_build_keepalive(&p->id, (uint8_t)(peers > 255 ? 255 : peers), buf));
}

static void enter_running(struct djl_player *p, uint64_t now_us)
{
 p->phase = DJL_RUNNING;
 p->step = 0;
 p->keepalive_us = p->status_us = p->position_us = now_us;
}

static void claim_step(struct djl_player *p, const struct djl_sink *s)
{
 uint8_t buf[DJL_MAX_DATAGRAM];
 uint8_t n = (uint8_t)(p->step + 1);
 size_t len = p->phase == DJL_HELLOING ? djl_build_hello(&p->id, buf)
            : p->phase == DJL_CLAIMING1 ? djl_build_claim1(&p->id, n, buf)
            : p->phase == DJL_CLAIMING2 ? djl_build_claim2(&p->id, n, buf)
            : djl_build_claim3(&p->id, n, buf);
 send(p, s, NULL, DJL_PORT_ANNOUNCE, buf, len);
 if (++p->step == 3 && p->phase != DJL_CLAIMING3) {
  p->step = 0;
  p->phase = (enum djl_phase)(p->phase + 1);
 }
}

static int beats_forward(const struct djl_deck_facts *f)
{
 return loaded(f) && is_playing(f->play) && f->timeline.speed_q20 > 0 && f->timeline.next_beat;
}

static void step_beats(struct djl_player *p, uint64_t now_us, const struct djl_deck_facts *f, const struct djl_sink *s)
{
 const struct djl_timeline *t = &f->timeline;
 if (f->content_epoch != p->beat_epoch) {
  p->beat_epoch = f->content_epoch;
  p->beat_sent = 0;
  p->pending_beat = 0;
 }
 if (!beats_forward(f)) {
  p->pending_beat = 0;
  return;
 }
 int last = -1;
 uint64_t last_us = 0;
 for (unsigned i = 0; i < DJL_AHEAD && t->ahead_ms[i] != DJL_NO_TIME; i++) {
  uint64_t at = djl_beat_time_us(t, i);
  if (!at) continue;
  if (at > now_us) break;
  last = (int)i;
  last_us = at;
 }
 uint8_t buf[DJL_MAX_DATAGRAM];
 size_t len = 0;
 uint32_t number = 0;
 if (last >= 0) {
  number = t->next_beat + (uint32_t)last;
  if (number != p->beat_sent) len = djl_build_beat(&p->id, f, (unsigned)last, buf);
 } else if (p->pending_beat && p->pending_beat != p->beat_sent && p->pending_us <= now_us &&
            t->next_beat == p->pending_beat + 1) {
  struct djl_deck_facts g = *f;
  memmove(g.timeline.ahead_ms + 1, t->ahead_ms, sizeof t->ahead_ms - sizeof t->ahead_ms[0]);
  memmove(g.timeline.ahead_bar + 1, t->ahead_bar, sizeof t->ahead_bar - sizeof t->ahead_bar[0]);
  g.timeline.ahead_ms[0] = p->pending_ms;
  g.timeline.ahead_bar[0] = p->pending_bar;
  number = p->pending_beat;
  last_us = p->pending_us;
  len = djl_build_beat(&p->id, &g, 0, buf);
 }
 if (len) {
  send(p, s, NULL, DJL_PORT_BEAT, buf, len);
  p->beat_sent = number;
  if (p->status_us < last_us + AFTER_BEAT_US) p->status_us = last_us + AFTER_BEAT_US;
 }
 p->pending_beat = 0;
 for (unsigned i = 0; i < DJL_AHEAD && t->ahead_ms[i] != DJL_NO_TIME; i++) {
  uint64_t at = djl_beat_time_us(t, i);
  if (at > now_us) {
   p->pending_beat = t->next_beat + i;
   p->pending_ms = t->ahead_ms[i];
   p->pending_bar = t->ahead_bar[i];
   p->pending_us = at;
   break;
  }
 }
}

static void step_status(struct djl_player *p, uint64_t now_us, const struct djl_deck_facts *f,
                        const struct djl_net *n, const struct djl_sink *s)
{
 if (now_us < p->status_us) return;
 if (p->pending_beat && p->pending_us > now_us && p->pending_us - now_us <= BEFORE_BEAT_US) {
  p->status_us = p->pending_us + AFTER_BEAT_US;
  return;
 }
 p->status_us = next_due(p->status_us, now_us, STATUS_US);
 const uint8_t *ips[DJL_PEERS];
 unsigned count = n ? distinct_ips(n, ips) : 0;
 if (!count) return;
 uint8_t buf[DJL_MAX_DATAGRAM];
 p->packet_counter++;
 size_t len = djl_build_status(p, f, n, now_us, buf);
 for (unsigned i = 0; i < count; i++) send(p, s, ips[i], DJL_PORT_STATUS, buf, len);
 p->master_on_wire = (uint8_t)is_master(p, f, now_us);
}

/* Ends the handoff windows the player has caught up with, or given up on. */
static void settle(struct djl_player *p, uint64_t now_us, const struct djl_deck_facts *f)
{
 if (!f->master) p->yield_us = 0;
 if (f->master || (!f->wants_master && !window_open(p->grant_us, now_us))) p->grant_us = 0;
}

/* A grant opens the master window once per want of master: a grant the player let run out is
   retried as before but not claimed on the wire again. */
static void grant(struct djl_player *p, uint64_t now_us, const struct djl_sink *s, uint8_t from)
{
 p->granted = 2;
 p->asked_us = now_us;
 p->asking = DJL_NO_DEVICE;
 p->yield_us = 0;
 if (!p->grant_us) p->grant_us = now_us ? now_us : 1;
 command(p, s, DJL_CMD_MASTER_GRANTED, from, 0, NULL);
}

static void request_master(struct djl_player *p, uint64_t now_us, const struct djl_peer *m, const struct djl_sink *s)
{
 uint8_t buf[DJL_MAX_DATAGRAM];
 if (p->asking != m->device) p->granted = 0;
 p->asking = m->device;
 p->asked_us = now_us;
 send(p, s, m->ip, DJL_PORT_BEAT, buf, djl_build_master_request(&p->id, buf));
}

/* The yield window ran out: no master has asserted for 3 s (each assertion renews it in master_status),
   so the wire follows the player's facts again. A master still listed is named to the player once a second. */
static void remind_yield(struct djl_player *p, uint64_t now_us, const struct djl_net *n, const struct djl_sink *s)
{
 const struct djl_peer *m = n ? djl_net_master(n) : NULL;
 if (!m) {
  p->yield_us = 0;
  return;
 }
 if (now_us < p->yield_cmd_us || now_us - p->yield_cmd_us < RETRY_US) return;
 p->yield_cmd_us = now_us ? now_us : 1;
 command(p, s, DJL_CMD_YIELD_MASTER, m->device, 0, NULL);
}

static void step_handoff(struct djl_player *p, uint64_t now_us, const struct djl_deck_facts *f,
                         const struct djl_net *n, const struct djl_sink *s)
{
 settle(p, now_us, f);
 int master = is_master(p, f, now_us), yielding = window_open(p->yield_us, now_us);
 if (!master) p->yielding_to = DJL_NO_DEVICE;
 if (p->yield_us && !yielding) remind_yield(p, now_us, n, s);
 if (master || yielding || (p->granted == 2 && now_us - p->asked_us >= RETRY_US) || (!f->wants_master && p->granted != 2)) {
  p->asking = DJL_NO_DEVICE;
  p->granted = 0;
  return;
 }
 if (p->granted == 2 || !f->wants_master) return;
 const struct djl_peer *m = n ? djl_net_master(n) : NULL;
 if (!m) grant(p, now_us, s, DJL_NO_DEVICE);
 else if (p->asking != m->device) request_master(p, now_us, m, s);
 else if (m->handoff_to == p->id.device) grant(p, now_us, s, m->device);
 else if (now_us - p->asked_us >= RETRY_US) request_master(p, now_us, m, s);
}

static uint64_t earliest(uint64_t a, uint64_t b) { return a < b ? a : b; }

uint64_t djl_player_step(struct djl_player *p, uint64_t now_us, const struct djl_deck_facts *f,
                         const struct djl_net *n, const struct djl_sink *s)
{
 if (!p || p->phase == DJL_OFF || p->phase == DJL_EVICTED) return UINT64_MAX;
 if (p->phase < DJL_RUNNING) {
  if (now_us < p->due_us) return p->due_us;
  if (!(p->phase == DJL_CLAIMING3 && p->step >= 3)) {
   claim_step(p, s);
   p->due_us = next_due(p->due_us, now_us, CLAIM_US);
   return p->due_us;
  }
  enter_running(p, now_us);
 }
 if (!f) return now_us + POSITION_US;
 step_beats(p, now_us, f, s);
 if (now_us >= p->keepalive_us) {
  send_keepalive(p, n, s);
  p->keepalive_us = next_due(p->keepalive_us, now_us, KEEPALIVE_US);
 }
 step_handoff(p, now_us, f, n, s);
 step_status(p, now_us, f, n, s);
 if (!loaded(f)) p->position_us = now_us;
 else if (now_us >= p->position_us) {
  uint8_t buf[DJL_MAX_DATAGRAM];
  send(p, s, NULL, DJL_PORT_BEAT, buf, djl_build_position(&p->id, f, now_us, buf));
  p->position_us = next_due(p->position_us, now_us, POSITION_US);
 }
 uint64_t wake = earliest(p->keepalive_us, p->status_us);
 if (loaded(f)) wake = earliest(wake, p->position_us);
 if (p->pending_beat && p->pending_us > now_us) wake = earliest(wake, p->pending_us);
 if (p->granted == 2 || (p->asking != DJL_NO_DEVICE && f->wants_master)) wake = earliest(wake, p->asked_us + RETRY_US);
 if (window_open(p->yield_us, now_us)) wake = earliest(wake, p->yield_us + HANDOFF_US);
 else if (p->yield_us) wake = earliest(wake, p->yield_cmd_us + RETRY_US);
 if (window_open(p->grant_us, now_us)) wake = earliest(wake, p->grant_us + HANDOFF_US);
 return wake;
}

/* A status from a tempo master (reference 3.2). */
static void master_status(struct djl_player *p, uint64_t now_us, uint8_t sender, uint8_t flags, uint8_t handoff,
                          uint32_t sync_n, const struct djl_deck_facts *f, const struct djl_net *n,
                          const struct djl_sink *s)
{
 uint8_t to = handoff_target(handoff);
 if (!(flags & DJL_FLAG_MASTER) || sender == p->id.device) return;
 if (p->yield_us && f->master && to == DJL_NO_DEVICE) {
  /* The player has not cleared its master state yet and the new master is still there. Each of its
     statuses renews the hold, so this deck never claims master against a live master however slow
     the player is; the player is told again once a second. */
  p->yield_us = now_us ? now_us : 1;
  if (now_us >= p->yield_cmd_us && now_us - p->yield_cmd_us >= RETRY_US) {
   p->yield_cmd_us = p->yield_us;
   command(p, s, DJL_CMD_YIELD_MASTER, sender, 0, NULL);
  }
  return;
 }
 if (is_master(p, f, now_us)) {
  /* A plain master assertion from another device: yield, once per handoff. A yield the player
     refused is reminded from djl_player_step. */
  if (to != DJL_NO_DEVICE || p->yield_us) return;
  if (sender == p->yielding_to) {
   uint32_t high = n ? djl_net_highest_sync_n(n) : 0;
   p->sync_n = (sync_n > high ? sync_n : high) + 1;
  }
  p->yielding_to = DJL_NO_DEVICE;
  p->yield_us = p->yield_cmd_us = now_us ? now_us : 1;
  p->grant_us = 0;
  command(p, s, DJL_CMD_YIELD_MASTER, sender, 0, NULL);
  return;
 }
 if (to != p->id.device || (p->granted == 2 && now_us - p->asked_us < RETRY_US)) return;
 int unsolicited = !(flags & DJL_FLAG_PLAYING) && f->sync && is_playing(f->play) && f->timeline.speed_q20 > 0;
 if (p->asking == sender || unsolicited) grant(p, now_us, s, sender);
}

static int same_track(const struct djl_track_ref *a, const struct djl_track_ref *b)
{
 return a->player == b->player && a->slot == b->slot && a->type == b->type && a->rekordbox_id == b->rekordbox_id;
}

void djl_player_receive(struct djl_player *p, uint64_t now_us, const uint8_t src_ip[4], const struct djl_packet *pkt,
                        const struct djl_deck_facts *f, const struct djl_net *n, const struct djl_sink *s)
{
 if (!p || !pkt || !src_ip) return;
 uint8_t me = p->id.device, buf[DJL_MAX_DATAGRAM];
 int self = ip_eq(src_ip, p->id.ip);
 if (p->phase >= DJL_HELLOING && p->phase <= DJL_CLAIMING3) {
  if (self) return;
  if (pkt->kind == DJL_DEFENSE && pkt->u.defense.device == me) p->phase = DJL_EVICTED;
  else if (pkt->kind == DJL_ASSIGN_FINISHED && p->phase == DJL_CLAIMING3 && p->step >= 1) {
   enter_running(p, now_us);
   send_keepalive(p, n, s);
   p->keepalive_us = now_us + KEEPALIVE_US;
  }
  return;
 }
 if (p->phase != DJL_RUNNING || !f) return;
 settle(p, now_us, f);
 switch (pkt->kind) {
 case DJL_CLAIM2:
  if (!self && pkt->u.claim2.device == me) send(p, s, src_ip, DJL_PORT_ANNOUNCE, buf, djl_build_defense(&p->id, buf));
  break;
 case DJL_CLAIM3:
  if (self || !pkt->u.claim3.device) break;
  if (pkt->u.claim3.device == me) send(p, s, src_ip, DJL_PORT_ANNOUNCE, buf, djl_build_defense(&p->id, buf));
  else send(p, s, src_ip, DJL_PORT_ANNOUNCE, buf, djl_build_assign_finished(&p->id, buf));
  break;
 case DJL_KEEPALIVE:
  if (!self && pkt->u.keepalive.device == me) send(p, s, src_ip, DJL_PORT_ANNOUNCE, buf, djl_build_defense(&p->id, buf));
  break;
 case DJL_MASTER_REQUEST:
  if (!is_master(p, f, now_us) || pkt->u.master_request.requester == me || !pkt->u.master_request.requester) break;
  p->yielding_to = pkt->u.master_request.requester;
  send(p, s, src_ip, DJL_PORT_BEAT, buf, djl_build_master_response(&p->id, buf));
  break;
 case DJL_MASTER_RESPONSE:
  if (pkt->device == p->asking && pkt->u.master_response.accepted && !p->granted) p->granted = 1;
  break;
 case DJL_STATUS:
  master_status(p, now_us, pkt->device, pkt->u.status.flags, pkt->u.status.handoff_to, pkt->u.status.sync_n, f, n, s);
  break;
 case DJL_MIXER_STATUS:
  master_status(p, now_us, pkt->device, pkt->u.mixer_status.flags, pkt->u.mixer_status.handoff_to, 0, f, n, s);
  break;
 case DJL_SYNC_CONTROL: {
  uint8_t c = pkt->u.sync_control.command;
  if (c == DJL_SYNC_ON) command(p, s, DJL_CMD_SYNC_ON, pkt->device, 0, NULL);
  else if (c == DJL_SYNC_OFF) command(p, s, DJL_CMD_SYNC_OFF, pkt->device, 0, NULL);
  else if (c == DJL_BECOME_MASTER) command(p, s, DJL_CMD_TAKE_MASTER, pkt->device, 0, NULL);
  break;
 }
 case DJL_LOAD_TRACK:
  if (pkt->u.load_track.target_deck != me) break;
  if (!(p->load_us && now_us - p->load_us < RETRY_US && same_track(&p->load_track, &pkt->u.load_track.track))) {
   p->load_track = pkt->u.load_track.track;
   p->load_us = now_us ? now_us : 1;
   command(p, s, DJL_CMD_LOAD, pkt->device, 0, &pkt->u.load_track.track);
  }
  send(p, s, src_ip, DJL_PORT_STATUS, buf, djl_build_load_ack(&p->id, buf));
  break;
 case DJL_FADER_START:
  if (me >= 1 && me <= 4) {
   uint8_t c = pkt->u.fader_start.command[me - 1];
   if (c == 0) command(p, s, DJL_CMD_FADER_START, pkt->device, 0, NULL);
   else if (c == 1) command(p, s, DJL_CMD_FADER_STOP, pkt->device, 0, NULL);
  }
  break;
 case DJL_ON_AIR:
  if (me >= 1 && me <= pkt->u.on_air.channels) {
   uint8_t v = pkt->u.on_air.on[me - 1] ? 1 : 0;
   if (v != p->on_air) {
    p->on_air = v;
    command(p, s, DJL_CMD_ON_AIR, pkt->device, v, NULL);
   }
  }
  break;
 default: break;
 }
}
