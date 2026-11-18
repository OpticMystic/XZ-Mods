/* SPDX-License-Identifier: MIT */
/* Lab stand-in for the player side of the xz-djlink bus.

   Creates the bus file, writes deck facts for devices 3 and 4 from a scripted model every 20 ms,
   reads commands as JSON lines on stdin and prints every command the helper queues on the ring.

   stdin, one flat JSON object per line:
     {"cmd":"deck","deck":3, any of:
        "announce":1, "load":true (new content epoch, playhead 0, paused), "unload":true,
        "track_player":1, "track_slot":2, "track_type":1, "track_id":1234,
        "bpm":128.0, "first_beat_ms":50, "length_ms":300000, "pitch":-2.5 (percent),
        "play":true, "seek_ms":61000, "master":true, "sync":true, "wants_master":true, "on_air":true}
     {"cmd":"freeze","deck":3,"on":true}       stop (or resume) writing that deck's section
     {"cmd":"react","on":false}                keep printing ring commands but stop acting on them
     {"cmd":"react_delay","ms":300}            act on YIELD_MASTER and MASTER_GRANTED this long after they
                                               come off the ring (a player whose worker is slow); commands
                                               already waiting keep their time
     {"cmd":"header","magic":0,"version":1}    rewrite the header, to test a helper's validation
     {"cmd":"net"}                             print the helper's net section once
     {"cmd":"quit"}
   stdout, JSON lines: "ready", "model" (after every change: the anchor the beat grid hangs on),
   "ring" (each command from the helper, when it comes off the ring), "react_delay", "net", "error".
   Options: --bus PATH, --keep (leave the bus file at exit), --react-delay-ms N (default 0). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../djl.h"
#include "../djl_bus.h"

#define TICK_US 20000u
#define RING_POLL_MS 2

struct model {
 uint8_t device, announce, frozen;
 struct djl_track_ref track;
 uint32_t epoch, bpm_x100, length_ms;
 int64_t first_beat_us;
 int32_t pitch_x100;          /* percent x 100 */
 uint8_t playing, master, sync, wants_master, on_air;
 uint64_t anchor_us;          /* CLOCK_MONOTONIC */
 int64_t anchor_pos_us;       /* track time at anchor_us */
};

static struct model decks[DJL_BUS_DECKS];
static struct djl_bus *bus;
static const char *bus_path = "/dev/shm/xz-djlink/bus";
static int react = 1;
static unsigned react_delay_ms;

/* Master commands waiting out the reaction delay, oldest first. */
#define DELAYED 32
static struct delayed { uint64_t due_us; uint8_t local_device, kind; } delayed[DELAYED];
static unsigned ndelayed;

static uint64_t mono_us(void)
{
 struct timespec ts;
 clock_gettime(CLOCK_MONOTONIC, &ts);
 return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t pitch_q20(const struct model *m)
{
 return (uint32_t)((int64_t)DJL_PITCH_UNITY + (int64_t)DJL_PITCH_UNITY * m->pitch_x100 / 10000);
}
static int loaded(const struct model *m) { return m->track.type != DJL_TRACK_NONE; }
static int moving(const struct model *m) { return loaded(m) && m->playing; }

static int64_t position_us(const struct model *m, uint64_t now)
{
 if (!moving(m) || now <= m->anchor_us) return m->anchor_pos_us;
 int64_t pos = m->anchor_pos_us + (int64_t)(((uint64_t)(now - m->anchor_us) * pitch_q20(m)) >> 20);
 int64_t end = (int64_t)m->length_ms * 1000;
 return pos > end ? end : pos;
}

/* Track time in ms of grid beat k (1-based), rounded to the nearest ms like an exported grid. */
static uint32_t beat_ms(const struct model *m, uint32_t k)
{
 int64_t us = m->first_beat_us + (int64_t)(((uint64_t)(k - 1) * 6000000000ull) / m->bpm_x100);
 return (uint32_t)((us + 500) / 1000);
}

static void print_model(const struct model *m, const char *why)
{
 printf("{\"event\":\"model\",\"why\":\"%s\",\"deck\":%u,\"announce\":%u,\"epoch\":%u,\"track\":[%u,%u,%u,%u],"
        "\"bpm_x100\":%u,\"pitch_x100\":%d,\"pitch_q20\":%u,\"first_beat_us\":%lld,\"length_ms\":%u,\"playing\":%u,"
        "\"master\":%u,\"sync\":%u,\"wants_master\":%u,\"on_air\":%u,\"anchor_us\":%llu,\"anchor_pos_us\":%lld,"
        "\"speed_q20\":%u}\n",
        why, m->device, m->announce, m->epoch, m->track.player, m->track.slot, m->track.type, m->track.rekordbox_id,
        m->bpm_x100, m->pitch_x100, pitch_q20(m), (long long)m->first_beat_us, m->length_ms, m->playing, m->master,
        m->sync, m->wants_master, m->on_air, (unsigned long long)m->anchor_us, (long long)m->anchor_pos_us,
        moving(m) ? pitch_q20(m) : 0u);
 fflush(stdout);
}

static void reanchor(struct model *m, uint64_t now)
{
 m->anchor_pos_us = position_us(m, now);
 m->anchor_us = now;
}

static void facts_of(const struct model *m, uint64_t now, struct djl_bus_deck *out)
{
 memset(out, 0, sizeof *out);
 out->announce = m->announce;
 out->device = m->device;
 struct djl_deck_facts *f = &out->facts;
 f->content_epoch = m->epoch;
 f->track_bpm_x100 = 0xffff;
 f->pitch_q20 = f->fader_pitch_q20 = pitch_q20(m);
 f->master = m->master;
 f->sync = m->sync;
 f->wants_master = m->wants_master;
 f->on_air = m->on_air;
 struct djl_timeline *t = &f->timeline;
 t->captured_us = now;
 for (unsigned i = 0; i < DJL_AHEAD; i++) t->ahead_ms[i] = DJL_NO_TIME;
 if (!loaded(m)) {
  f->play = DJL_PLAY_EMPTY;
  return;
 }
 f->track = m->track;
 f->track_bpm_x100 = (uint16_t)m->bpm_x100;
 int64_t pos = position_us(m, now);
 int ended = pos >= (int64_t)m->length_ms * 1000;
 f->play = ended ? DJL_PLAY_ENDED : m->playing ? DJL_PLAY_PLAYING : DJL_PLAY_PAUSED;
 t->playhead_us = pos;
 t->speed_q20 = m->playing && !ended ? (int32_t)pitch_q20(m) : 0;
 t->track_ms = m->length_ms;
 if (!m->bpm_x100) return;
 int64_t period = (int64_t)(6000000000ull / m->bpm_x100);
 int64_t k = pos > m->first_beat_us ? (pos - m->first_beat_us) / period : 0;
 uint32_t next = k > 2 ? (uint32_t)k - 1 : 1;
 while ((int64_t)beat_ms(m, next) * 1000 <= pos) next++;
 t->next_beat = next;
 for (unsigned i = 0; i < DJL_AHEAD; i++) {
  uint32_t ms = beat_ms(m, next + i);
  if (ms > m->length_ms) break;
  t->ahead_ms[i] = ms;
  t->ahead_bar[i] = (uint8_t)((next + i - 1) % 4 + 1);
 }
}

static void write_decks(uint64_t now)
{
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  if (decks[d].frozen) continue;
  struct djl_bus_deck tmp;
  facts_of(&decks[d], now, &tmp);
  djl_bus_write_deck(&bus->deck[d], &tmp);
 }
}

/* ---- tiny flat JSON reader -------------------------------------------------------------- */

static const char *value_of(const char *line, const char *key)
{
 char pat[48];
 snprintf(pat, sizeof pat, "\"%s\"", key);
 for (const char *p = strstr(line, pat); p; p = strstr(p + 1, pat)) {
  const char *q = p + strlen(pat);
  while (*q == ' ') q++;
  if (*q != ':') continue;
  q++;
  while (*q == ' ') q++;
  return q;
 }
 return NULL;
}
static int jnum(const char *line, const char *key, double *out)
{
 const char *p = value_of(line, key);
 if (!p) return 0;
 if (!strncmp(p, "true", 4)) *out = 1;
 else if (!strncmp(p, "false", 5)) *out = 0;
 else {
  char *end;
  *out = strtod(p, &end);
  if (end == p) return 0;
 }
 return 1;
}
static int jstr(const char *line, const char *key, const char *want)
{
 const char *p = value_of(line, key);
 size_t n = strlen(want);
 return p && *p == '"' && !strncmp(p + 1, want, n) && p[1 + n] == '"';
}

static struct model *deck_arg(const char *line)
{
 double v;
 if (!jnum(line, "deck", &v)) return NULL;
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++)
  if (decks[d].device == (uint8_t)v) return &decks[d];
 return NULL;
}

static void cmd_deck(struct model *m, const char *line, uint64_t now)
{
 double v;
 reanchor(m, now);
 if (jnum(line, "announce", &v)) m->announce = (uint8_t)(v != 0);
 if (jnum(line, "track_player", &v)) m->track.player = (uint8_t)v;
 if (jnum(line, "track_slot", &v)) m->track.slot = (uint8_t)v;
 if (jnum(line, "track_type", &v)) m->track.type = (uint8_t)v;
 if (jnum(line, "track_id", &v)) m->track.rekordbox_id = (uint32_t)v;
 if (jnum(line, "bpm", &v)) m->bpm_x100 = (uint32_t)(v * 100 + 0.5);
 if (jnum(line, "first_beat_ms", &v)) m->first_beat_us = (int64_t)(v * 1000);
 if (jnum(line, "length_ms", &v)) m->length_ms = (uint32_t)v;
 if (jnum(line, "pitch", &v)) m->pitch_x100 = (int32_t)(v * 100 + (v < 0 ? -0.5 : 0.5));
 if (jnum(line, "load", &v) && v) {
  m->epoch++;
  m->anchor_pos_us = 0;
  m->playing = 0;
 }
 if (jnum(line, "unload", &v) && v) {
  m->epoch++;
  m->track = (struct djl_track_ref){0, 0, 0, 0};
  m->anchor_pos_us = 0;
  m->playing = 0;
 }
 if (jnum(line, "seek_ms", &v)) m->anchor_pos_us = (int64_t)(v * 1000);
 if (jnum(line, "play", &v)) m->playing = (uint8_t)(v != 0);
 if (jnum(line, "master", &v)) m->master = (uint8_t)(v != 0);
 if (jnum(line, "sync", &v)) m->sync = (uint8_t)(v != 0);
 if (jnum(line, "wants_master", &v)) m->wants_master = (uint8_t)(v != 0);
 if (jnum(line, "on_air", &v)) m->on_air = (uint8_t)(v != 0);
 print_model(m, "stdin");
}

static void print_net(void)
{
 struct djl_bus_net n;
 if (djl_bus_read_net(&bus->net, &n) == 1u) {
  printf("{\"event\":\"error\",\"what\":\"net read\"}\n");
  fflush(stdout);
  return;
 }
 printf("{\"event\":\"net\",\"now_us\":%llu,\"helper_pid\":%d,\"heartbeat_us\":%llu,\"unit_ip\":\"%u.%u.%u.%u\","
        "\"broadcast_ip\":\"%u.%u.%u.%u\",\"unit_mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"identity_ok\":%u,"
        "\"phase\":[%u,%u],\"sync_dropped\":%u,\"ring_dropped\":%u,\"rx_frames\":%u,\"rx_rejected\":%u,"
        "\"rx_drops\":%u,\"peers\":[",
        (unsigned long long)mono_us(), n.helper_pid, (unsigned long long)n.heartbeat_us, n.unit_ip[0], n.unit_ip[1],
        n.unit_ip[2], n.unit_ip[3], n.broadcast_ip[0], n.broadcast_ip[1], n.broadcast_ip[2], n.broadcast_ip[3],
        n.unit_mac[0], n.unit_mac[1], n.unit_mac[2], n.unit_mac[3], n.unit_mac[4], n.unit_mac[5], n.identity_ok,
        n.phase[0], n.phase[1], n.sync_dropped, n.ring_dropped, n.rx_frames, n.rx_rejected, n.rx_drops);
 int first = 1;
 for (unsigned i = 0; i < DJL_BUS_PEERS; i++) {
  const struct djl_bus_peer *q = &n.peers[i];
  if (!q->in_use) continue;
  char name[DJL_NAME + 1];
  memcpy(name, q->name, DJL_NAME);
  name[DJL_NAME] = 0;
  for (char *c = name; *c; c++)
   if (*c == '"' || *c == '\\' || (unsigned char)*c < 0x20 || (unsigned char)*c > 0x7e) *c = '?';
  printf("%s{\"device\":%u,\"ip\":\"%u.%u.%u.%u\",\"class\":%u,\"name\":\"%s\",\"flags\":%u,\"bpm_x100\":%u,"
         "\"master_mode\":%u,\"handoff_to\":%u}",
         first ? "" : ",", q->device, q->ip[0], q->ip[1], q->ip[2], q->ip[3], q->device_class, name, q->flags,
         q->bpm_x100, q->master_mode, q->handoff_to);
  first = 0;
 }
 printf("],\"master\":{\"valid\":%u,\"device\":%u,\"bpm_x100\":%u,\"pitch_q20\":%u,\"beat_in_bar\":%u,"
        "\"next_beat_ms\":%u,\"beats\":%u,\"beat_rx_us\":%llu}}\n",
        n.master.valid, n.master.device, n.master.bpm_x100, n.master.pitch_q20, n.master.beat_in_bar,
        n.master.next_beat_ms, n.master.beats, (unsigned long long)n.master.beat_rx_us);
 fflush(stdout);
}

static int handle_line(const char *line, uint64_t now)
{
 double v;
 if (jstr(line, "cmd", "quit")) return 0;
 if (jstr(line, "cmd", "deck")) {
  struct model *m = deck_arg(line);
  if (m) cmd_deck(m, line, now);
 } else if (jstr(line, "cmd", "freeze")) {
  struct model *m = deck_arg(line);
  if (m && jnum(line, "on", &v)) {
   m->frozen = (uint8_t)(v != 0);
   print_model(m, m->frozen ? "frozen" : "thawed");
  }
 } else if (jstr(line, "cmd", "react")) {
  if (jnum(line, "on", &v)) react = v != 0;
  printf("{\"event\":\"react\",\"on\":%d}\n", react);
  fflush(stdout);
 } else if (jstr(line, "cmd", "react_delay")) {
  if (jnum(line, "ms", &v) && v >= 0 && v <= 60000) react_delay_ms = (unsigned)v;
  printf("{\"event\":\"react_delay\",\"ms\":%u}\n", react_delay_ms);
  fflush(stdout);
 } else if (jstr(line, "cmd", "header")) {
  if (jnum(line, "magic", &v)) __atomic_store_n(&bus->header.magic, (uint32_t)v, __ATOMIC_RELEASE);
  if (jnum(line, "version", &v)) __atomic_store_n(&bus->header.version, (uint32_t)v, __ATOMIC_RELEASE);
  printf("{\"event\":\"header\",\"magic\":%u,\"version\":%u,\"size\":%u}\n", bus->header.magic, bus->header.version,
         bus->header.size);
  fflush(stdout);
 } else if (jstr(line, "cmd", "net")) {
  print_net();
 } else {
  printf("{\"event\":\"error\",\"what\":\"unknown command\"}\n");
  fflush(stdout);
 }
 return 1;
}

/* ---- ring ------------------------------------------------------------------------------- */

static const char *kind_name(uint8_t k)
{
 static const char *names[] = {"SYNC_ON", "SYNC_OFF", "TAKE_MASTER", "MASTER_GRANTED", "YIELD_MASTER",
                               "LOAD", "FADER_START", "FADER_STOP", "ON_AIR"};
 return k < sizeof names / sizeof names[0] ? names[k] : "?";
}

static struct model *model_of(uint8_t device)
{
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++)
  if (decks[d].device == device) return &decks[d];
 return NULL;
}

static void act_master(struct model *m, uint8_t kind, uint64_t now)
{
 reanchor(m, now);
 if (kind == DJL_CMD_MASTER_GRANTED) {
  m->master = 1;
  m->wants_master = 0;
 } else {
  m->master = 0;
 }
 print_model(m, kind_name(kind));
}

/* Acts on the master commands whose reaction delay has run out, in the order they arrived. */
static void run_delayed(uint64_t now)
{
 unsigned done = 0;
 while (done < ndelayed && delayed[done].due_us <= now) {
  struct model *m = model_of(delayed[done].local_device);
  if (m) act_master(m, delayed[done].kind, now);
  done++;
 }
 if (!done) return;
 memmove(delayed, delayed + done, (ndelayed - done) * sizeof delayed[0]);
 ndelayed -= done;
}

static void drain_ring(uint64_t now)
{
 struct djl_bus_command c;
 while (djl_bus_ring_pop(&bus->ring, &c)) {
  printf("{\"event\":\"ring\",\"now_us\":%llu,\"local_device\":%u,\"kind\":\"%s\",\"device\":%u,\"value\":%u,"
         "\"track\":[%u,%u,%u,%u]}\n",
         (unsigned long long)now, c.local_device, kind_name(c.kind), c.device, c.value, c.track.player, c.track.slot,
         c.track.type, c.track.rekordbox_id);
  fflush(stdout);
  struct model *m = model_of(c.local_device);
  if (!m || !react) continue;
  if (c.kind == DJL_CMD_MASTER_GRANTED || c.kind == DJL_CMD_YIELD_MASTER) {
   if (!react_delay_ms && !ndelayed) {
    act_master(m, c.kind, now);
   } else if (ndelayed < DELAYED) {
    delayed[ndelayed++] = (struct delayed){now + react_delay_ms * 1000ull, c.local_device, c.kind};
   } else {
    printf("{\"event\":\"error\",\"what\":\"delayed queue full\"}\n");
    fflush(stdout);
   }
   continue;
  }
  reanchor(m, now);
  switch (c.kind) {
  case DJL_CMD_SYNC_ON: m->sync = 1; break;
  case DJL_CMD_SYNC_OFF: m->sync = 0; break;
  case DJL_CMD_TAKE_MASTER: m->wants_master = 1; break;
  case DJL_CMD_LOAD:
   m->track = c.track;
   m->epoch++;
   m->anchor_pos_us = 0;
   m->playing = 0;
   break;
  case DJL_CMD_FADER_START: m->playing = 1; break;
  case DJL_CMD_FADER_STOP: m->playing = 0; break;
  case DJL_CMD_ON_AIR: m->on_air = c.value; break;
  default: continue;
  }
  print_model(m, kind_name(c.kind));
 }
}

/* ---- bus file --------------------------------------------------------------------------- */

static int create_bus(void)
{
 char dir[256];
 snprintf(dir, sizeof dir, "%s", bus_path);
 char *slash = strrchr(dir, '/');
 if (slash && slash != dir) {
  *slash = 0;
  for (char *p = dir + 1; *p; p++)
   if (*p == '/') {
    *p = 0;
    mkdir(dir, 0755);
    *p = '/';
   }
  mkdir(dir, 0755);
 }
 unlink(bus_path);
 int fd = open(bus_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
 if (fd < 0 || ftruncate(fd, sizeof(struct djl_bus)) != 0) return 0;
 void *map = mmap(NULL, sizeof(struct djl_bus), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 close(fd);
 if (map == MAP_FAILED) return 0;
 bus = map;
 memset(bus, 0, sizeof *bus);
 write_decks(mono_us());
 bus->header.version = DJL_BUS_VERSION;
 bus->header.size = sizeof(struct djl_bus);
 __atomic_store_n(&bus->header.magic, DJL_BUS_MAGIC, __ATOMIC_RELEASE);
 return 1;
}

int main(int argc, char **argv)
{
 int keep = 0;
 for (int i = 1; i < argc; i++) {
  if (!strcmp(argv[i], "--bus") && i + 1 < argc) bus_path = argv[++i];
  else if (!strcmp(argv[i], "--keep")) keep = 1;
  else if (!strcmp(argv[i], "--react-delay-ms") && i + 1 < argc && atoi(argv[i + 1]) >= 0 && atoi(argv[i + 1]) <= 60000)
   react_delay_ms = (unsigned)atoi(argv[++i]);
  else {
   fprintf(stderr, "usage: xz_djlink_sim [--bus PATH] [--keep] [--react-delay-ms N]   (commands on stdin, see source)\n");
   return 2;
  }
 }
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  decks[d].device = (uint8_t)(3 + d);
  decks[d].bpm_x100 = 12000;
  decks[d].length_ms = 300000;
  decks[d].anchor_us = mono_us();
 }
 if (!create_bus()) {
  fprintf(stderr, "cannot create bus %s: %s\n", bus_path, strerror(errno));
  return 1;
 }
 printf("{\"event\":\"ready\",\"bus\":\"%s\",\"size\":%u,\"now_us\":%llu,\"react_delay_ms\":%u}\n", bus_path,
        (unsigned)sizeof(struct djl_bus), (unsigned long long)mono_us(), react_delay_ms);
 fflush(stdout);

 static char line[4096];
 size_t used = 0;
 int open_in = 1, run = 1;
 uint64_t next_tick = mono_us();
 while (run) {
  uint64_t now = mono_us();
  if (now >= next_tick) {
   write_decks(now);
   next_tick += TICK_US;
   if (next_tick <= now) next_tick = now + TICK_US;
  }
  drain_ring(now);
  run_delayed(mono_us());
  struct pollfd pfd = {0, POLLIN, 0};
  int r = poll(&pfd, open_in ? 1 : 0, RING_POLL_MS);
  if (r > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
   ssize_t n = read(0, line + used, sizeof line - 1 - used);
   if (n <= 0) {
    open_in = 0;
    continue;
   }
   used += (size_t)n;
   line[used] = 0;
   char *start = line, *nl;
   while (run && (nl = strchr(start, '\n'))) {
    *nl = 0;
    run = handle_line(start, mono_us());
    start = nl + 1;
   }
   used = strlen(start);
   memmove(line, start, used + 1);
   if (used == sizeof line - 1) used = 0;
  }
 }
 if (!keep) unlink(bus_path);
 printf("{\"event\":\"exit\"}\n");
 return 0;
}
