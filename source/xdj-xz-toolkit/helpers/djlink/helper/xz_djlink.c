/* SPDX-License-Identifier: MIT */
/* xz-djlink: announces the XDJ-XZ software decks 3 and 4 on Pro DJ Link.

   Deck facts come from the player through the shared-memory bus (djl_bus.h); the wire core (djl.c)
   turns them into packets. Receiving uses only a packet socket: a second UDP socket bound to
   50000-50002 could take unicast packets meant for the stock player, a packet socket only sees
   copies. The unit's address comes from the stock deck 1 keep-alive seen leaving the interface. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../djl.h"
#include "../djl_bus.h"

#define IDENTITY_TIMEOUT_US 6000000u
#define STALE_US 3000000u
#define PUBLISH_US 100000u
#define BUS_POLL_US 10000u
#define BUS_CHECK_US 500000u
#define RETRY_US 1000000u
#define EVICT_RETRY_US 10000000u
#define EXPIRE_US 1000000u
#define STATS_US 10000000u
#define EXTRA_AGE_US 10000000u
#define RX_BATCH 256
#define FRAME_MAX 2048

#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif

static struct {
 const char *iface, *bus_path, *log_path;
 int realtime, verbose;
} opt = {"eth0", DJL_BUS_PATH, NULL, 1, 0};

static struct {
 uint64_t rx_frames, rx_rejected, rx_own, rx_unknown, rx_drops, tx_packets, tx_errors, sync_dropped, ring_dropped,
  bus_read_fail, evictions, log_dropped, log_suppressed;
 uint64_t beats[DJL_BUS_DECKS], status[DJL_BUS_DECKS];
} count, count_logged;

static volatile sig_atomic_t stop_requested;
static int log_fd = 2;
static uint64_t chatter_window_us;
static unsigned chatter_lines;
static char log_buf[1024];

static uint64_t mono_us(void)
{
 struct timespec ts;
 clock_gettime(CLOCK_MONOTONIC, &ts);
 return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* One JSON object per line; `fields` continues the object. */
static void logj(const char *event, const char *fields, ...)
{
 size_t cap = sizeof log_buf - 2;
 int n = snprintf(log_buf, cap, "{\"t_us\":%llu,\"event\":\"%s\"", (unsigned long long)mono_us(), event);
 if (n < 0) return;
 size_t len = (size_t)n < cap ? (size_t)n : cap;
 if (fields && *fields && len + 1 < cap) {
  log_buf[len++] = ',';
  va_list ap;
  va_start(ap, fields);
  n = vsnprintf(log_buf + len, cap - len, fields, ap);
  va_end(ap);
  if (n > 0) len += (size_t)n < cap - len ? (size_t)n : cap - len - 1;
 }
 log_buf[len++] = '}';
 log_buf[len++] = '\n';
 for (size_t off = 0; off < len;) {
  ssize_t w = write(log_fd, log_buf + off, len - off);
  if (w <= 0) {
   if (w < 0 && errno == EINTR) continue;
   if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) count.log_dropped++;
   return;
  }
  off += (size_t)w;
 }
}

/* Per-packet lines (commands, dropped sync packets) are capped so a flood cannot fill the log. */
static int chatter_ok(void)
{
 uint64_t now = mono_us();
 if (now - chatter_window_us >= 1000000u) {
  chatter_window_us = now;
  chatter_lines = 0;
 }
 if (chatter_lines >= 50) {
  count.log_suppressed++;
  return 0;
 }
 chatter_lines++;
 return 1;
}

static void ip_str(const uint8_t ip[4], char out[16])
{
 snprintf(out, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

/* ---- unit identity, from the stock deck 1 keep-alive ------------------------------------ */

static struct {
 int ok;
 uint8_t mac[6], ip[4], bcast[4];
 uint64_t seen_us;
} unit;

static int tx_fd = -1;
static uint64_t tx_retry_us;

/* ---- bus -------------------------------------------------------------------------------- */

static struct djl_bus *bus;
static int bus_fd = -1;
static dev_t bus_dev;
static ino_t bus_ino;
static int bus_valid_logged = -1;

struct deck {
 struct djl_player player;
 uint64_t evicted_us;
 struct djl_bus_deck copy;    /* last consistent copy from the bus */
 uint32_t seq;
 int have;
 uint64_t seq_changed_us;
 int stale;
 struct djl_deck_facts facts; /* what the core is given */
 int dirty;
 uint64_t wake_us;
 int last_phase;
 int beat_tx;
 uint64_t beat_tx_us;
 uint8_t beat_tx_bar;
};
static struct deck decks[DJL_BUS_DECKS];
static struct djl_net net;

/* Per-peer values djl_net does not keep: tempo, and beat timing for the master clock. */
struct peer_extra {
 uint8_t in_use, device, ip[4], beat_in_bar;
 uint16_t bpm_x100;
 uint32_t pitch_q20, next_beat_ms, beats;
 uint64_t beat_rx_us, seen_us;
};
static struct peer_extra extras[DJL_PEERS];

static void bus_detach(const char *why)
{
 if (bus) munmap(bus, sizeof *bus);
 if (bus_fd >= 0) close(bus_fd);
 bus = NULL;
 bus_fd = -1;
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) decks[d].have = 0;
 if (why) logj("bus_detached", "\"reason\":\"%s\"", why);
}

static void bus_check(uint64_t now)
{
 struct stat st;
 if (stat(opt.bus_path, &st) != 0) {
  if (bus) bus_detach("missing");
  return;
 }
 if (bus && (st.st_dev != bus_dev || st.st_ino != bus_ino)) bus_detach("replaced");
 if (bus) return;
 int fd = open(opt.bus_path, O_RDWR | O_CLOEXEC);
 if (fd < 0) return;
 struct stat fst;
 if (fstat(fd, &fst) != 0 || fst.st_size != (off_t)sizeof(struct djl_bus)) {
  static off_t reported = -1;
  if (fst.st_size != reported) logj("bus_rejected", "\"reason\":\"size\",\"size\":%lld,\"expected\":%u",
                                    (long long)fst.st_size, (unsigned)sizeof(struct djl_bus));
  reported = fst.st_size;
  close(fd);
  return;
 }
 void *map = mmap(NULL, sizeof(struct djl_bus), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 if (map == MAP_FAILED) {
  close(fd);
  return;
 }
 bus = map;
 bus_fd = fd;
 bus_dev = fst.st_dev;
 bus_ino = fst.st_ino;
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  decks[d].have = 0;
  decks[d].seq = 1;
  decks[d].seq_changed_us = now;
 }
 logj("bus_attached", "\"path\":\"%s\"", opt.bus_path);
}

static int bus_valid(void)
{
 int ok = bus && djl_bus_header_ok(bus);
 if (ok != bus_valid_logged) {
  if (bus) logj("bus_header", "\"valid\":%d,\"magic\":%u,\"version\":%u,\"size\":%u", ok, bus->header.magic,
                bus->header.version, bus->header.size);
  bus_valid_logged = ok;
 }
 return ok;
}

static int is_playing(uint8_t play)
{
 return play == DJL_PLAY_PLAYING || play == DJL_PLAY_LOOPING || play == DJL_PLAY_CUE_PLAY;
}

static void read_decks(uint64_t now)
{
 int valid = bus_valid();
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  struct deck *k = &decks[d];
  if (!valid) {
   k->have = 0;
   continue;
  }
  struct djl_bus_deck tmp;
  uint32_t seq = djl_bus_read_deck(&bus->deck[d], &tmp);
  if (seq == 1u) {
   count.bus_read_fail++;
  } else if (!k->have || seq != k->seq) {
   k->copy = tmp;
   k->seq = seq;
   k->have = 1;
   k->seq_changed_us = now;
   if (k->stale) logj("deck_fresh", "\"deck\":%u", tmp.device);
   k->stale = 0;
   k->facts = tmp.facts;
   k->dirty = 1;
  }
  if (!k->have || k->stale) continue;
  const struct djl_deck_facts *f = &k->copy.facts;
  if ((is_playing(f->play) || f->timeline.speed_q20) && now - k->seq_changed_us >= STALE_US) {
   /* The player stopped writing while the deck plays: hold the deck where it would be now so a
      dead player leaves no phantom beats, and keep announcing it as paused. */
   k->stale = 1;
   k->facts = *f;
   uint32_t at = djl_playhead_ms(&f->timeline, now);
   k->facts.play = DJL_PLAY_PAUSED;
   k->facts.timeline.playhead_us = (int64_t)at * 1000;
   k->facts.timeline.captured_us = now;
   k->facts.timeline.speed_q20 = 0;
   k->dirty = 1;
   logj("deck_stale", "\"deck\":%u,\"silent_ms\":%llu,\"held_at_ms\":%u", k->copy.device,
        (unsigned long long)((now - k->seq_changed_us) / 1000u), at);
  }
 }
}

/* ---- sending ---------------------------------------------------------------------------- */

static int deck_index(uint8_t device)
{
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++)
  if (decks[d].player.id.device == device) return (int)d;
 return -1;
}

static void sink_send(void *ctx, const struct djl_identity *from, const uint8_t *dst, uint16_t port, const uint8_t *data,
                      size_t len)
{
 (void)ctx;
 if (tx_fd < 0) return;
 struct sockaddr_in to;
 memset(&to, 0, sizeof to);
 to.sin_family = AF_INET;
 to.sin_port = htons(port);
 memcpy(&to.sin_addr, dst ? dst : unit.bcast, 4);
 uint64_t t = mono_us();
 if (sendto(tx_fd, data, len, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)len) count.tx_packets++;
 else count.tx_errors++;
 if (port == DJL_PORT_STATUS && len >= 0xcc && data[0x0a] == 0x0a) {
  int d = deck_index(from->device);
  if (d >= 0) count.status[d]++;
 }
 if (port == DJL_PORT_BEAT && len >= 0x60 && data[0x0a] == 0x28) {
  int d = deck_index(from->device);
  if (d >= 0) {
   decks[d].beat_tx = 1;
   decks[d].beat_tx_us = t;
   decks[d].beat_tx_bar = data[0x5c];
  }
 }
}

static const char *command_name(enum djl_command_kind k)
{
 static const char *names[] = {"SYNC_ON", "SYNC_OFF", "TAKE_MASTER", "MASTER_GRANTED", "YIELD_MASTER",
                               "LOAD", "FADER_START", "FADER_STOP", "ON_AIR"};
 return (unsigned)k < sizeof names / sizeof names[0] ? names[k] : "?";
}

static void sink_command(void *ctx, uint8_t local_device, const struct djl_command *c)
{
 (void)ctx;
 struct djl_bus_command bc;
 djl_bus_command_from(&bc, local_device, c);
 int ok = bus && djl_bus_header_ok(bus) && djl_bus_ring_push(&bus->ring, &bc);
 if (!ok) count.ring_dropped++;
 if ((opt.verbose || !ok) && chatter_ok())
  logj("command", "\"deck\":%u,\"kind\":\"%s\",\"device\":%u,\"value\":%u,\"track\":[%u,%u,%u,%u],\"queued\":%d",
       local_device, command_name(c->kind), c->device, c->value, c->track.player, c->track.slot, c->track.type,
       c->track.rekordbox_id, ok);
}

static const struct djl_sink sink = {NULL, sink_send, sink_command};

static void tx_close(void)
{
 if (tx_fd >= 0) close(tx_fd);
 tx_fd = -1;
}

static int tx_open(void)
{
 tx_close();
 int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0), one = 1;
 if (fd < 0) return 0;
 struct sockaddr_in a;
 memset(&a, 0, sizeof a);
 a.sin_family = AF_INET;
 memcpy(&a.sin_addr, unit.ip, 4);
 const char *step = "SO_BROADCAST";
 if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one) != 0) goto fail;
 step = "SO_BINDTODEVICE";
 if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, opt.iface, (socklen_t)strlen(opt.iface) + 1) != 0) goto fail;
 step = "bind";
 if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) goto fail;
 tx_fd = fd;
 return 1;
fail:;
 char ip[16];
 ip_str(unit.ip, ip);
 logj("tx_socket_failed", "\"step\":\"%s\",\"ip\":\"%s\",\"errno\":%d", step, ip, errno);
 close(fd);
 return 0;
}

/* ---- players ---------------------------------------------------------------------------- */

static int running(const struct deck *k) { return k->player.phase != DJL_OFF && k->player.phase != DJL_EVICTED; }

static void identity_changed(void)
{
 char ip[16], bc[16];
 ip_str(unit.ip, ip);
 ip_str(unit.bcast, bc);
 logj("identity", "\"ip\":\"%s\",\"broadcast\":\"%s\",\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\"", ip, bc, unit.mac[0],
      unit.mac[1], unit.mac[2], unit.mac[3], unit.mac[4], unit.mac[5]);
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  decks[d].player.phase = DJL_OFF;
  decks[d].player.id.device = 0;
 }
 memset(extras, 0, sizeof extras);
 djl_net_init(&net, unit.ip);
 tx_open();
 tx_retry_us = mono_us() + RETRY_US;
}

static void manage_players(uint64_t now)
{
 if (unit.ok && now - unit.seen_us > IDENTITY_TIMEOUT_US) {
  unit.ok = 0;
  logj("identity_lost", "\"silent_ms\":%llu", (unsigned long long)((now - unit.seen_us) / 1000u));
 }
 if (unit.ok && tx_fd < 0 && now >= tx_retry_us) {
  tx_open();
  tx_retry_us = now + RETRY_US;
 }
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  struct deck *k = &decks[d];
  uint8_t dev = k->copy.device;
  if (k->player.phase == DJL_EVICTED && k->last_phase != DJL_EVICTED) {
   k->evicted_us = now;
   count.evictions++;
  }
  int other_same = k->have && decks[d ^ 1].have && decks[d ^ 1].copy.device == dev;
  int want = unit.ok && tx_fd >= 0 && k->have && k->copy.announce && (dev == 3 || dev == 4) && !other_same;
  if (!want) {
   if (k->player.phase != DJL_OFF) djl_player_stop(&k->player);
  } else {
   if (k->player.id.device != dev) {
    struct djl_identity id;
    djl_identity_xz_deck(&id, dev, unit.mac, unit.ip);
    djl_player_init(&k->player, &id);
   }
   if (k->player.phase == DJL_OFF) {
    djl_player_start(&k->player, now);
    k->dirty = 1;
   } else if (k->player.phase == DJL_EVICTED && now - k->evicted_us >= EVICT_RETRY_US) {
    /* A defence can be spoofed, or come from a device that has since left: try again, slowly. */
    logj("claim_retry", "\"deck\":%u,\"device\":%u", d, dev);
    djl_player_start(&k->player, now);
    k->dirty = 1;
   }
  }
  if ((int)k->player.phase != k->last_phase) {
   logj("phase", "\"deck\":%u,\"device\":%u,\"phase\":%d,\"previous\":%d", d, k->player.id.device, k->player.phase,
        k->last_phase);
   k->last_phase = (int)k->player.phase;
  }
  if (!running(k)) k->wake_us = UINT64_MAX;
 }
}

static void step_deck(struct deck *k, unsigned d, uint64_t now)
{
 struct djl_player *p = &k->player;
 uint32_t pend_beat = p->pending_beat;
 uint64_t pend_us = p->pending_us;
 k->beat_tx = 0;
 k->wake_us = djl_player_step(p, now, &k->facts, &net, &sink);
 k->dirty = 0;
 if (!k->beat_tx) return;
 count.beats[d]++;
 if (!opt.verbose) return;
 const struct djl_timeline *t = &k->facts.timeline;
 uint32_t number = p->beat_sent;
 uint64_t due = 0;
 if (t->next_beat && number >= t->next_beat && number - t->next_beat < DJL_AHEAD) {
  uint64_t at = djl_beat_time_us(t, number - t->next_beat);
  if (at && at <= now) due = at;
 }
 if (!due && number == pend_beat) due = pend_us;
 logj("beat_sent", "\"deck\":%u,\"beat\":%u,\"bar\":%u,\"epoch\":%u,\"due_us\":%llu,\"sent_us\":%llu", p->id.device,
      number, k->beat_tx_bar, k->facts.content_epoch, (unsigned long long)due, (unsigned long long)k->beat_tx_us);
}

/* ---- receiving -------------------------------------------------------------------------- */

static int rx_fd = -1;
static uint8_t frame[FRAME_MAX];
static union {
 struct cmsghdr align;
 uint8_t bytes[CMSG_SPACE(sizeof(struct tpacket_auxdata))];
} rx_control;

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static uint32_t sum16(const uint8_t *p, size_t len, uint32_t sum)
{
 for (size_t i = 0; i + 1 < len; i += 2) sum += be16(p + i);
 if (len & 1) sum += (uint32_t)p[len - 1] << 8;
 return sum;
}
static uint16_t fold(uint32_t sum)
{
 while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
 return (uint16_t)sum;
}

static struct peer_extra *extra_for(uint8_t device, const uint8_t ip[4], uint64_t now)
{
 struct peer_extra *free_slot = NULL, *oldest = &extras[0];
 for (unsigned i = 0; i < DJL_PEERS; i++) {
  struct peer_extra *e = &extras[i];
  if (e->in_use && e->device == device && !memcmp(e->ip, ip, 4)) return e;
  if (!e->in_use && !free_slot) free_slot = e;
  if (e->seen_us < oldest->seen_us) oldest = e;
 }
 struct peer_extra *e = free_slot ? free_slot : oldest;
 memset(e, 0, sizeof *e);
 e->in_use = 1;
 e->device = device;
 memcpy(e->ip, ip, 4);
 e->bpm_x100 = 0xffff;
 e->seen_us = now;
 return e;
}

static void publish(uint64_t now);

static void note_extra(const struct djl_packet *pkt, const uint8_t src[4], uint64_t now)
{
 if (!pkt->device) return;
 struct peer_extra *e;
 switch (pkt->kind) {
 case DJL_BEAT: {
  e = extra_for(pkt->device, src, now);
  e->bpm_x100 = pkt->u.beat.bpm_x100;
  e->pitch_q20 = pkt->u.beat.pitch_q20;
  e->beat_in_bar = pkt->u.beat.beat_in_bar;
  e->next_beat_ms = pkt->u.beat.next_beat_ms;
  e->beat_rx_us = now;
  e->beats++;
  e->seen_us = now;
  const struct djl_peer *m = djl_net_master(&net);
  if (m && m->device == pkt->device && !memcmp(m->ip, src, 4)) publish(now);
  break;
 }
 case DJL_STATUS:
  e = extra_for(pkt->device, src, now);
  e->bpm_x100 = pkt->u.status.bpm_x100;
  e->pitch_q20 = pkt->u.status.pitch_q20;
  e->seen_us = now;
  break;
 case DJL_MIXER_STATUS:
  e = extra_for(pkt->device, src, now);
  e->bpm_x100 = pkt->u.mixer_status.bpm_x100;
  e->pitch_q20 = pkt->u.mixer_status.pitch_q20;
  e->seen_us = now;
  break;
 default: break;
 }
}

/* 0x2a names no target deck and the unit's four decks share one address, so it is delivered
   only when exactly one software deck has a track loaded. */
static void deliver_sync_control(uint64_t now, const uint8_t src[4], const struct djl_packet *pkt)
{
 int loaded = 0;
 struct deck *only = NULL;
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++)
  if (decks[d].have && decks[d].facts.track.type != DJL_TRACK_NONE) {
   loaded++;
   only = &decks[d];
  }
 if (loaded == 1 && only->player.phase == DJL_RUNNING) {
  djl_player_receive(&only->player, now, src, pkt, &only->facts, &net, &sink);
  only->dirty = 1;
  return;
 }
 count.sync_dropped++;
 if (opt.verbose && chatter_ok())
  logj("sync_dropped", "\"command\":%u,\"from\":%u,\"loaded_decks\":%d,\"total\":%llu", pkt->u.sync_control.command,
       pkt->device, loaded, (unsigned long long)count.sync_dropped);
}

static void handle_frame(size_t n, uint8_t pkttype, uint32_t aux_status, uint64_t now)
{
 count.rx_frames++;
 if (pkttype == PACKET_OTHERHOST || n < 14 + 20 + 8 || be16(frame + 12) != ETH_P_IP) goto reject;
 const uint8_t *ip = frame + 14;
 size_t ihl = (size_t)(ip[0] & 0x0f) * 4u, total = be16(ip + 2);
 if (ip[0] >> 4 != 4 || ihl < 20 || total < ihl + 8 || 14 + total > n || ip[9] != IPPROTO_UDP) goto reject;
 if (be16(ip + 6) & 0x3fff) goto reject;
 if (fold(sum16(ip, ihl, 0)) != 0xffff) goto reject;
 const uint8_t *udp = ip + ihl, *src = ip + 12;
 uint16_t port = be16(udp + 2), ulen = be16(udp + 4);
 if (port < DJL_PORT_ANNOUNCE || port > DJL_PORT_STATUS || ulen < 8 || ulen > total - ihl) goto reject;
 if (be16(udp + 6) && !(aux_status & TP_STATUS_CSUMNOTREADY)) {
  uint32_t sum = sum16(ip + 12, 8, 0) + IPPROTO_UDP + ulen;
  if (fold(sum16(udp, ulen, sum)) != 0xffff) goto reject;
 }
 const uint8_t *payload = udp + 8;
 size_t plen = ulen - 8u;
 static struct djl_packet pkt;

 if (pkttype == PACKET_OUTGOING) {
  if (port != DJL_PORT_ANNOUNCE || !djl_parse(port, payload, plen, &pkt)) return;
  if (pkt.kind != DJL_KEEPALIVE || pkt.device != 1 || strcmp(pkt.name, "XDJ-XZ")) return;
  if (!unit.ok || memcmp(unit.mac, pkt.u.keepalive.mac, 6) || memcmp(unit.ip, pkt.u.keepalive.ip, 4)) {
   memcpy(unit.mac, pkt.u.keepalive.mac, 6);
   memcpy(unit.ip, pkt.u.keepalive.ip, 4);
   memcpy(unit.bcast, ip + 16, 4);
   unit.ok = 1;
   unit.seen_us = now;
   identity_changed();
  } else if (memcmp(unit.bcast, ip + 16, 4)) {
   char bc[16];
   memcpy(unit.bcast, ip + 16, 4);
   ip_str(unit.bcast, bc);
   logj("broadcast_changed", "\"broadcast\":\"%s\"", bc);
  }
  unit.seen_us = now;
  return;
 }
 if (!unit.ok) return;
 if (!memcmp(src, unit.ip, 4)) {
  count.rx_own++;
  return;
 }
 if (!djl_parse(port, payload, plen, &pkt)) {
  count.rx_unknown++;
  return;
 }
 djl_net_receive(&net, now, src, frame + 6, &pkt);
 note_extra(&pkt, src, now);
 if (pkt.kind == DJL_SYNC_CONTROL) {
  deliver_sync_control(now, src, &pkt);
  return;
 }
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  struct deck *k = &decks[d];
  if (!running(k)) continue;
  djl_player_receive(&k->player, now, src, &pkt, &k->facts, &net, &sink);
  k->dirty = 1;
 }
 return;
reject:
 count.rx_rejected++;
}

/* IPv4, UDP, not a fragment, destination port 50000-50002. */
static struct sock_filter port_filter[] = {
 BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12),
 BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, 9),
 BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 23),
 BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IPPROTO_UDP, 0, 7),
 BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 20),
 BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x3fff, 5, 0),
 BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 14),
 BPF_STMT(BPF_LD | BPF_H | BPF_IND, 16),
 BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, DJL_PORT_ANNOUNCE, 0, 2),
 BPF_JUMP(BPF_JMP | BPF_JGT | BPF_K, DJL_PORT_STATUS, 1, 0),
 BPF_STMT(BPF_RET | BPF_K, 0x40000),
 BPF_STMT(BPF_RET | BPF_K, 0),
};

static int rx_open(void)
{
 unsigned ifindex = if_nametoindex(opt.iface);
 if (!ifindex) return 0;
 /* Protocol 0 receives nothing until bind, so no unfiltered frame is queued before the filter. */
 int fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, 0), one = 1, rcvbuf = 4 << 20;
 if (fd < 0) return 0;
 struct sock_fprog prog = {sizeof port_filter / sizeof port_filter[0], port_filter};
 struct sockaddr_ll a;
 memset(&a, 0, sizeof a);
 a.sll_family = AF_PACKET;
 a.sll_protocol = htons(ETH_P_ALL); /* ETH_P_ALL: outgoing frames reach only all-protocol taps */
 a.sll_ifindex = (int)ifindex;
 if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof prog) != 0 ||
     setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &one, sizeof one) != 0 ||
     bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
  logj("rx_socket_failed", "\"iface\":\"%s\",\"errno\":%d", opt.iface, errno);
  close(fd);
  return 0;
 }
 /* SO_RCVBUFFORCE (root) passes rmem_max: a short stall under a flood must not cost the stock keep-alive. */
 if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof rcvbuf) != 0)
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
 socklen_t got_len = sizeof rcvbuf;
 getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &got_len);
 rx_fd = fd;
 logj("rx_socket", "\"iface\":\"%s\",\"ifindex\":%u,\"rcvbuf\":%d", opt.iface, ifindex, rcvbuf);
 return 1;
}

static int rx_one(void)
{
 struct sockaddr_ll from;
 struct iovec iov = {frame, sizeof frame};
 struct msghdr m;
 memset(&m, 0, sizeof m);
 m.msg_name = &from;
 m.msg_namelen = sizeof from;
 m.msg_iov = &iov;
 m.msg_iovlen = 1;
 m.msg_control = rx_control.bytes;
 m.msg_controllen = sizeof rx_control.bytes;
 ssize_t n = recvmsg(rx_fd, &m, MSG_DONTWAIT | MSG_TRUNC);
 if (n < 0) return 0;
 uint64_t now = mono_us();
 if ((size_t)n > sizeof frame || (m.msg_flags & MSG_TRUNC)) {
  count.rx_frames++;
  count.rx_rejected++;
  return 1;
 }
 /* PACKET_AUXDATA is the only control message enabled. CMSG_NXTHDR is avoided: musl's version
    trips -Wsign-compare. */
 uint32_t status = 0;
 struct cmsghdr *c = CMSG_FIRSTHDR(&m);
 if (c && c->cmsg_level == SOL_PACKET && c->cmsg_type == PACKET_AUXDATA &&
     c->cmsg_len >= CMSG_LEN(sizeof(struct tpacket_auxdata))) {
  struct tpacket_auxdata aux;
  memcpy(&aux, CMSG_DATA(c), sizeof aux);
  status = aux.tp_status;
 }
 handle_frame((size_t)n, from.sll_pkttype, status, now);
 return 1;
}

/* ---- publishing ------------------------------------------------------------------------- */

static struct djl_bus_net out_net;

static void publish(uint64_t now)
{
 if (!bus || !djl_bus_header_ok(bus)) return;
 struct djl_bus_net *o = &out_net;
 memset(o, 0, sizeof *o);
 o->helper_pid = (int32_t)getpid();
 o->heartbeat_us = now;
 memcpy(o->unit_ip, unit.ip, 4);
 memcpy(o->broadcast_ip, unit.bcast, 4);
 memcpy(o->unit_mac, unit.mac, 6);
 o->identity_ok = (uint8_t)unit.ok;
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) o->phase[d] = (uint8_t)decks[d].player.phase;
 o->sync_dropped = (uint32_t)count.sync_dropped;
 o->ring_dropped = (uint32_t)count.ring_dropped;
 o->rx_frames = (uint32_t)count.rx_frames;
 o->rx_drops = (uint32_t)count.rx_drops;
 o->rx_rejected = (uint32_t)count.rx_rejected;
 unsigned n = 0;
 for (unsigned i = 0; i < DJL_PEERS && n < DJL_BUS_PEERS; i++) {
  const struct djl_peer *q = &net.peers[i];
  if (!q->in_use) continue;
  struct djl_bus_peer *b = &o->peers[n++];
  b->in_use = 1;
  b->device = q->device;
  b->device_class = q->device_class;
  b->flags = q->flags;
  memcpy(b->ip, q->ip, 4);
  b->master_mode = q->master_mode;
  b->handoff_to = q->handoff_to;
  memcpy(b->name, q->name, DJL_NAME);
  b->bpm_x100 = 0xffff;
  for (unsigned j = 0; j < DJL_PEERS; j++)
   if (extras[j].in_use && extras[j].device == q->device && !memcmp(extras[j].ip, q->ip, 4)) b->bpm_x100 = extras[j].bpm_x100;
 }
 const struct djl_peer *m = unit.ok ? djl_net_master(&net) : NULL;
 for (unsigned j = 0; m && j < DJL_PEERS; j++) {
  const struct peer_extra *e = &extras[j];
  if (!e->in_use || e->device != m->device || memcmp(e->ip, m->ip, 4)) continue;
  o->master.valid = 1;
  o->master.device = e->device;
  o->master.bpm_x100 = e->bpm_x100;
  o->master.pitch_q20 = e->pitch_q20;
  o->master.beat_in_bar = e->beat_in_bar;
  o->master.next_beat_ms = e->next_beat_ms;
  o->master.beats = e->beats;
  o->master.beat_rx_us = e->beat_rx_us;
 }
 djl_bus_write_net(&bus->net, o);
}

static void read_drops(void)
{
 struct tpacket_stats st;
 socklen_t len = sizeof st;
 /* The kernel resets these counters on every read. */
 if (rx_fd >= 0 && getsockopt(rx_fd, SOL_PACKET, PACKET_STATISTICS, &st, &len) == 0) count.rx_drops += st.tp_drops;
}

static void log_stats(int force)
{
 read_drops();
 if (!force && !memcmp(&count, &count_logged, sizeof count)) return;
 count_logged = count;
 struct rusage ru;
 getrusage(RUSAGE_SELF, &ru);
 unsigned long long cpu_ms = ((unsigned long long)ru.ru_utime.tv_sec + (unsigned long long)ru.ru_stime.tv_sec) * 1000u +
                             (unsigned long long)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000u;
 logj("stats",
      "\"rx_frames\":%llu,\"rx_rejected\":%llu,\"rx_own\":%llu,\"rx_unknown\":%llu,\"rx_drops\":%llu,"
      "\"tx_packets\":%llu,\"tx_errors\":%llu,\"sync_dropped\":%llu,\"ring_dropped\":%llu,\"bus_read_fail\":%llu,"
      "\"evictions\":%llu,\"log_dropped\":%llu,\"log_suppressed\":%llu,\"beats\":[%llu,%llu],\"status\":[%llu,%llu],"
      "\"cpu_ms\":%llu",
      (unsigned long long)count.rx_frames, (unsigned long long)count.rx_rejected, (unsigned long long)count.rx_own,
      (unsigned long long)count.rx_unknown, (unsigned long long)count.rx_drops, (unsigned long long)count.tx_packets,
      (unsigned long long)count.tx_errors, (unsigned long long)count.sync_dropped, (unsigned long long)count.ring_dropped,
      (unsigned long long)count.bus_read_fail, (unsigned long long)count.evictions, (unsigned long long)count.log_dropped,
      (unsigned long long)count.log_suppressed, (unsigned long long)count.beats[0],
      (unsigned long long)count.beats[1], (unsigned long long)count.status[0], (unsigned long long)count.status[1], cpu_ms);
}

/* ---- start-up --------------------------------------------------------------------------- */

static void on_signal(int sig)
{
 (void)sig;
 stop_requested = 1;
}

static void realtime_setup(void)
{
 prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
 if (!opt.realtime) return;
 if (sysconf(_SC_NPROCESSORS_ONLN) == 4) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  CPU_SET(2, &set);
  if (sched_setaffinity(0, sizeof set, &set) != 0) logj("affinity_failed", "\"errno\":%d", errno);
  else logj("affinity", "\"cpus\":[0,2]");
 }
 /* The raw system call, read back: musl stubs sched_setscheduler with ENOSYS, and glibc 2.13 without
    libpthread loaded turns pthread_setschedparam into a no-op that reports success. */
 struct sched_param sp;
 memset(&sp, 0, sizeof sp);
 sp.sched_priority = 11;
 if (syscall(SYS_sched_setscheduler, 0, SCHED_FIFO, &sp) != 0) logj("sched_fifo_failed", "\"errno\":%d", errno);
 else if (syscall(SYS_sched_getscheduler, 0) != SCHED_FIFO) logj("sched_fifo_failed", "\"errno\":0");
 else logj("sched_fifo", "\"priority\":11");
#ifdef SANITIZED
 /* ASan reserves terabytes of shadow memory; locking it would exhaust the machine. */
 logj("mlockall_skipped", "\"reason\":\"sanitizer build\"");
#else
 if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) logj("mlockall_failed", "\"errno\":%d", errno);
#endif
}

static void usage(int to_stderr)
{
 static const char text[] =
  "usage: xz-djlink [--iface NAME] [--bus PATH] [--log PATH] [--no-realtime] [--verbose]\n"
  "  --iface NAME    interface the stock player announces on (default eth0)\n"
  "  --bus PATH      shared-memory bus file the player creates (default " DJL_BUS_PATH ")\n"
  "  --log PATH      append JSON log lines here (default stderr)\n"
  "  --no-realtime   keep the default scheduler, affinity and memory locking\n"
  "  --verbose       one log line per beat sent and per command (lab use)\n";
 ssize_t w = write(to_stderr ? 2 : 1, text, sizeof text - 1);
 (void)w;
}

static int parse_args(int argc, char **argv)
{
 for (int i = 1; i < argc; i++) {
  const char *a = argv[i];
  if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
   usage(0);
   exit(0);
  } else if (!strcmp(a, "--no-realtime")) opt.realtime = 0;
  else if (!strcmp(a, "--verbose")) opt.verbose = 1;
  else if (i + 1 < argc && !strcmp(a, "--iface")) opt.iface = argv[++i];
  else if (i + 1 < argc && !strcmp(a, "--bus")) opt.bus_path = argv[++i];
  else if (i + 1 < argc && !strcmp(a, "--log")) opt.log_path = argv[++i];
  else {
   usage(1);
   return 0;
  }
 }
 return strlen(opt.iface) < IFNAMSIZ;
}

int main(int argc, char **argv)
{
 if (!parse_args(argc, argv)) return 2;
 if (opt.log_path) {
  log_fd = open(opt.log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (log_fd < 0) return 2;
  /* A full log sink must cost log lines, never beat timing. */
  fcntl(log_fd, F_SETFL, fcntl(log_fd, F_GETFL) | O_NONBLOCK);
 }
 signal(SIGPIPE, SIG_IGN);
 struct sigaction sa;
 memset(&sa, 0, sizeof sa);
 sa.sa_handler = on_signal;
 sigaction(SIGTERM, &sa, NULL);
 sigaction(SIGINT, &sa, NULL);
 sigset_t block, wait_mask;
 sigemptyset(&block);
 sigaddset(&block, SIGTERM);
 sigaddset(&block, SIGINT);
 sigprocmask(SIG_BLOCK, &block, &wait_mask);
 sigdelset(&wait_mask, SIGTERM);
 sigdelset(&wait_mask, SIGINT);

 logj("start", "\"pid\":%d,\"iface\":\"%s\",\"bus\":\"%s\",\"realtime\":%d,\"verbose\":%d,\"bus_size\":%u", (int)getpid(),
      opt.iface, opt.bus_path, opt.realtime, opt.verbose, (unsigned)sizeof(struct djl_bus));
 realtime_setup();
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  decks[d].player.phase = DJL_OFF;
  decks[d].last_phase = DJL_OFF;
  decks[d].wake_us = UINT64_MAX;
  decks[d].seq = 1;
 }
 static const uint8_t no_ip[4];
 djl_net_init(&net, no_ip);

 while (!stop_requested && !rx_open()) {
  struct timespec ts = {1, 0};
  ppoll(NULL, 0, &ts, &wait_mask);
 }
 uint64_t now = mono_us(), next_bus = now, next_publish = now, next_expire = now + EXPIRE_US, next_stats = now + STATS_US;
 while (!stop_requested) {
  now = mono_us();
  if (now >= next_bus) {
   bus_check(now);
   next_bus = now + BUS_CHECK_US;
  }
  read_decks(now);
  manage_players(now);
  for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
   struct deck *k = &decks[d];
   if (running(k) && (k->dirty || now >= k->wake_us)) step_deck(k, d, mono_us());
  }
  now = mono_us();
  if (now >= next_publish) {
   publish(now);
   next_publish = now + PUBLISH_US;
  }
  if (now >= next_expire) {
   djl_net_expire(&net, now);
   for (unsigned i = 0; i < DJL_PEERS; i++)
    if (extras[i].in_use && now - extras[i].seen_us > EXTRA_AGE_US) extras[i].in_use = 0;
   next_expire = now + EXPIRE_US;
  }
  if (now >= next_stats) {
   log_stats(0);
   next_stats = now + STATS_US;
  }

  uint64_t deadline = now + BUS_POLL_US;
  for (unsigned d = 0; d < DJL_BUS_DECKS; d++)
   if (decks[d].wake_us < deadline) deadline = decks[d].wake_us;
  if (next_publish < deadline) deadline = next_publish;
  now = mono_us();
  uint64_t wait = deadline > now ? deadline - now : 0;
  struct timespec ts = {(time_t)(wait / 1000000u), (long)(wait % 1000000u) * 1000L};
  struct pollfd pfd = {rx_fd, POLLIN, 0};
  int r = ppoll(&pfd, 1, &ts, &wait_mask);
  if (r <= 0 || !(pfd.revents & POLLIN)) continue;
  for (int i = 0; i < RX_BATCH && rx_one(); i++) {
   uint64_t t = mono_us();
   int due = 0;
   for (unsigned d = 0; d < DJL_BUS_DECKS; d++)
    if (running(&decks[d]) && t >= decks[d].wake_us) due = 1;
   if (due) break;
  }
 }

 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) djl_player_stop(&decks[d].player);
 publish(mono_us());
 log_stats(1);
 logj("exit", "\"reason\":\"signal\"");
 bus_detach(NULL);
 tx_close();
 if (rx_fd >= 0) close(rx_fd);
 return 0;
}
