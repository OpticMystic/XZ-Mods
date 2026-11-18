/* SPDX-License-Identifier: MIT */
/* The player half of the xz-djlink bus: lifetime, publication under the sequence lock, helper supervision and
   command intake. The test plays the helper's part on its own mapping of the bus. */
#include "../djlink_runtime.h"
#include "../djlink_facts.h"
#include "../djlink/djl_bus.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- the rest of the player, as this module sees it ---------------------------------------------- */

static unsigned log_lines;
static char last_log[160];
void xz_log(const char *line)
{
 log_lines++;
 snprintf(last_log, sizeof last_log, "%s", line);
}

#define EVENTS 64
static struct xz_extra_deck_event events[EVENTS];
static unsigned event_count;
static int input_accepts = 1;
int xz_extra_deck_input(const struct xz_extra_deck_event *e)
{
 if (event_count < EVENTS) events[event_count++] = *e;
 return input_accepts;
}
uint32_t xz_extra_decks_epoch(void) { return 9; }
uint64_t xz_extra_source_generation(int source) { return 100u + (uint64_t)source; }

static struct xz_extra_decks fixture;
static unsigned snapshots;
int xz_extra_decks_snapshot_held(struct xz_extra_decks *out, struct xz_extra_analysis_hold *hold)
{
 (void)hold;
 snapshots++;
 *out = fixture;
 xz_djlink_runtime_publish(out);
 return out->route_up;
}
void xz_extra_decks_release_analysis(struct xz_extra_decks *out, struct xz_extra_analysis_hold *hold)
{
 (void)out;
 (void)hold;
}

static struct xz_master_observation master = {.deck = XZ_MASTER_UNKNOWN};
int xz_sync_link_runtime_master_observe(struct xz_master_observation *out)
{
 *out = master;
 return master.deck != XZ_MASTER_UNKNOWN;
}

/* ---- process and clock fakes --------------------------------------------------------------------- */

static uint64_t clock_us = 1000000000ull;
static uint64_t fake_now(void) { return __atomic_load_n(&clock_us, __ATOMIC_ACQUIRE); }
static int32_t next_pid = 4001, spawned[8], terminated[8];
static unsigned spawn_count, terminate_count;
static int announce_at_terminate[8][2];
static struct djl_bus *helper_view; /* the test's own mapping: what a helper would see */

static int32_t fake_spawn(void)
{
 if (spawn_count < 8) spawned[spawn_count] = next_pid;
 spawn_count++;
 return next_pid++;
}
static int fake_terminate(int32_t pid)
{
 if (terminate_count < 8) {
  terminated[terminate_count] = pid;
  for (unsigned d = 0; d < 2; d++)
   announce_at_terminate[terminate_count][d] = helper_view ? __atomic_load_n(&helper_view->deck[d].announce, __ATOMIC_ACQUIRE) : -1;
 }
 terminate_count++;
 return 1;
}
static int was_terminated(int32_t pid)
{
 for (unsigned i = 0; i < terminate_count && i < 8; i++)
  if (terminated[i] == pid) return 1;
 return 0;
}
/* A terminated helper exits; a running one does not. */
static int fake_reap(int32_t pid) { return was_terminated(pid); }

static int exists(const char *path)
{
 struct stat st;
 return !stat(path, &st);
}

static struct djl_bus *map_bus(void)
{
 int fd = open(XZ_DJLINK_BUS, O_RDWR);
 if (fd < 0) return NULL;
 void *m = mmap(NULL, sizeof(struct djl_bus), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 close(fd);
 return m == MAP_FAILED ? NULL : m;
}

static void helper_net(int32_t pid, uint64_t heartbeat, int identity, uint8_t phase3, uint8_t phase4, unsigned peers)
{
 struct djl_bus_net n;
 memset(&n, 0, sizeof n);
 n.helper_pid = pid;
 n.heartbeat_us = heartbeat;
 n.identity_ok = (uint8_t)identity;
 n.unit_ip[0] = 192;
 n.unit_ip[1] = 168;
 n.unit_ip[2] = 12;
 n.unit_ip[3] = 147;
 n.phase[0] = phase3;
 n.phase[1] = phase4;
 for (unsigned i = 0; i < peers; i++) n.peers[i].in_use = 1;
 djl_bus_write_net(&helper_view->net, &n);
}

static struct xz_djlink_status status(void)
{
 struct xz_djlink_status s;
 memset(&s, 0xee, sizeof s);
 CHECK(xz_djlink_runtime_status(&s));
 return s;
}

static void advance(uint64_t us) { __atomic_add_fetch(&clock_us, us, __ATOMIC_ACQ_REL); }

/* ---- fixtures ------------------------------------------------------------------------------------ */

static struct xz_extra_beat grid[8];

static struct xz_extra_deck loaded_deck(uint32_t track_id, uint32_t load_revision)
{
 struct xz_extra_deck d;
 memset(&d, 0, sizeof d);
 d.known = XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL) |
           XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
 d.loaded = 1;
 d.source = 1;
 d.track_id = track_id;
 d.total_ms = 4000;
 d.track_bpm_x100 = d.bpm_x100 = 12000;
 d.grid = grid;
 d.grid_count = 8;
 d.timeline.source_rate = 44100;
 d.timeline.source_frame = 44100;
 d.timeline.flags = XZ_EXTRA_TIMELINE_ANCHOR;
 d.timeline.at_ms = (uint32_t)(fake_now() / 1000u);
 d.timeline.route_epoch = 2;
 d.timeline.load_revision = load_revision;
 d.timeline.transport_generation = 5;
 return d;
}

/* ---- tests --------------------------------------------------------------------------------------- */

static void test_off_leaves_nothing(void)
{
 xz_djlink_runtime_start();
 for (int i = 0; i < 3; i++) xz_djlink_runtime_service();
 xz_djlink_runtime_publish(&fixture);
 CHECK(!exists(XZ_DJLINK_BUS) && !exists(XZ_DJLINK_DIR));
 CHECK(spawn_count == 0 && snapshots == 0);
 struct xz_djlink_status s = status();
 CHECK(!s.enabled && s.state == XZ_DJLINK_OFF && !s.bus_ready && s.publishes == 0);
}

static void test_bus_created(void)
{
 xz_djlink_runtime_set_enabled(1);
 CHECK(status().state == XZ_DJLINK_STARTING);
 xz_djlink_runtime_service();
 struct stat st;
 CHECK(!stat(XZ_DJLINK_BUS, &st) && st.st_size == (off_t)sizeof(struct djl_bus) && (st.st_mode & 0777) == 0600);
 helper_view = map_bus();
 CHECK(helper_view != NULL);
 if (!helper_view) exit(1);
 CHECK(djl_bus_header_ok(helper_view));
 CHECK(helper_view->header.magic == DJL_BUS_MAGIC && helper_view->header.version == DJL_BUS_VERSION &&
       helper_view->header.size == 1264u);
 for (unsigned d = 0; d < 2; d++) {
  struct djl_bus_deck c;
  CHECK(djl_bus_read_deck(&helper_view->deck[d], &c) != 1u);
  CHECK(c.device == 3 + d && c.announce == 0 && c.facts.play == DJL_PLAY_EMPTY && c.facts.track.type == DJL_TRACK_NONE);
  CHECK(c.facts.timeline.ahead_ms[0] == DJL_NO_TIME && c.facts.track_bpm_x100 == 0xffff);
 }
 CHECK(helper_view->ring.head == 0 && helper_view->ring.tail == 0 && helper_view->net.helper_pid == 0);
 /* no helper staged in this folder */
 CHECK(spawn_count == 0);
 struct xz_djlink_status s = status();
 CHECK(s.enabled && s.bus_ready && !s.helper_present && s.state == XZ_DJLINK_NO_HELPER);
 unsigned lines = log_lines;
 xz_djlink_runtime_service();
 CHECK(log_lines == lines); /* the missing helper is reported once */
}

static void test_helper_start_and_states(void)
{
 int fd = open(XZ_DJLINK_HELPER, O_WRONLY | O_CREAT | O_TRUNC, 0700);
 CHECK(fd >= 0);
 if (fd >= 0) close(fd);
 xz_djlink_runtime_service();
 CHECK(spawn_count == 1 && spawned[0] == 4001);
 CHECK(status().state == XZ_DJLINK_STARTING && status().helper_starts == 1);
 advance(1000000);
 xz_djlink_runtime_service();
 CHECK(spawn_count == 1);

 helper_net(4001, fake_now(), 0, DJL_OFF, DJL_OFF, 0);
 xz_djlink_runtime_service();
 struct xz_djlink_status s = status();
 CHECK(s.helper_alive && s.helper_pid == 4001 && s.heartbeat_age_ms == 0 && s.state == XZ_DJLINK_STARTING);
 for (int i = 0; i < 7; i++) {
  advance(1000000);
  helper_net(4001, fake_now() - 50000, 0, DJL_OFF, DJL_OFF, 0);
  xz_djlink_runtime_service();
 }
 s = status();
 CHECK(s.heartbeat_age_ms == 50 && s.helper_up_ms >= 6000 && s.state == XZ_DJLINK_NO_LINK);
 helper_net(4001, fake_now(), 1, DJL_OFF, DJL_OFF, 3);
 xz_djlink_runtime_service();
 s = status();
 CHECK(s.identity_ok && s.unit_ip[3] == 147 && s.peers == 3 && s.state == XZ_DJLINK_IDLE);
 helper_net(4001, fake_now(), 1, DJL_RUNNING, DJL_CLAIMING2, 3);
 xz_djlink_runtime_service();
 CHECK(status().state == XZ_DJLINK_ONLINE && status().phase[0] == DJL_RUNNING && status().phase[1] == DJL_CLAIMING2);
 helper_net(4001, fake_now(), 1, DJL_RUNNING, DJL_EVICTED, 3);
 xz_djlink_runtime_service();
 CHECK(status().state == XZ_DJLINK_NUMBER_IN_USE);
 helper_net(4001, fake_now(), 1, DJL_OFF, DJL_OFF, 0);
 CHECK(spawn_count == 1 && terminate_count == 0);
}

static void test_publish(void)
{
 memset(&fixture, 0, sizeof fixture);
 fixture.route_up = 1;
 fixture.deck[0] = loaded_deck(55, 1);
 unsigned before = status().publishes;
 xz_djlink_runtime_publish(&fixture);
 struct djl_bus_deck c[2];
 for (unsigned d = 0; d < 2; d++) CHECK(djl_bus_read_deck(&helper_view->deck[d], &c[d]) != 1u);
 struct djl_deck_facts want;
 xz_djlink_facts_from_deck(0, &fixture.deck[0], 1, NULL, fake_now(), &want);
 CHECK(c[0].announce == 1 && c[0].device == 3 && !memcmp(&c[0].facts, &want, sizeof want));
 CHECK(c[0].facts.track.rekordbox_id == 55 && c[0].facts.play == DJL_PLAY_PAUSED && c[0].facts.timeline.next_beat == 4);
 CHECK(c[1].announce == 1 && c[1].device == 4 && c[1].facts.play == DJL_PLAY_EMPTY); /* route up: empty deck shown */
 xz_djlink_runtime_service();
 CHECK(status().publishes == before + 1);

 fixture.route_up = 0; /* route lost: the loaded deck keeps its number, the empty one leaves */
 xz_djlink_runtime_publish(&fixture);
 for (unsigned d = 0; d < 2; d++) CHECK(djl_bus_read_deck(&helper_view->deck[d], &c[d]) != 1u);
 CHECK(c[0].announce == 1 && c[1].announce == 0 && !c[0].facts.on_air);

 master = (struct xz_master_observation){.deck = XZ_MASTER_DECK3, .revision = 1, .observed_ms = (uint32_t)(fake_now() / 1000u),
                                        .operator_epoch = 1, .route_epoch = 2, .load_revision = 1, .transport_generation = 5};
 xz_djlink_runtime_publish(&fixture);
 CHECK(djl_bus_read_deck(&helper_view->deck[0], &c[0]) != 1u && c[0].facts.master == 1);
 master.deck = XZ_MASTER_DECK2;
 xz_djlink_runtime_publish(&fixture);
 CHECK(djl_bus_read_deck(&helper_view->deck[0], &c[0]) != 1u && c[0].facts.master == 0);
 master.deck = XZ_MASTER_UNKNOWN;
}

/* Two publishers and one reader at once: every consistent read must be one of the two published states. */
static struct xz_extra_decks state_a, state_b;
static struct djl_bus_deck want_a, want_b;
static int stop_threads;
static unsigned torn, good_a, good_b;

static void *writer(void *arg)
{
 unsigned i = (unsigned)(uintptr_t)arg;
 while (!__atomic_load_n(&stop_threads, __ATOMIC_ACQUIRE)) xz_djlink_runtime_publish(i++ & 1 ? &state_a : &state_b);
 return NULL;
}
static void *reader(void *arg)
{
 (void)arg;
 for (unsigned n = 0; n < 200000; n++) {
  struct djl_bus_deck c;
  if (djl_bus_read_deck(&helper_view->deck[0], &c) == 1u) continue;
  c.seq = 0;
  if (!memcmp(&c, &want_a, sizeof c)) good_a++;
  else if (!memcmp(&c, &want_b, sizeof c)) good_b++;
  else torn++;
 }
 __atomic_store_n(&stop_threads, 1, __ATOMIC_RELEASE);
 return NULL;
}

static void test_sequence_lock(void)
{
 memset(&state_a, 0, sizeof state_a);
 memset(&state_b, 0, sizeof state_b);
 state_a.route_up = state_b.route_up = 1;
 state_a.deck[0] = loaded_deck(1111, 10);
 state_b.deck[0] = loaded_deck(2222, 11);
 state_b.deck[0].timeline.source_frame = 3 * 44100;
 xz_djlink_runtime_publish(&state_a);
 CHECK(djl_bus_read_deck(&helper_view->deck[0], &want_a) != 1u);
 xz_djlink_runtime_publish(&state_b);
 CHECK(djl_bus_read_deck(&helper_view->deck[0], &want_b) != 1u);
 want_a.seq = want_b.seq = 0;
 CHECK(memcmp(&want_a, &want_b, sizeof want_a) != 0);
 unsigned skipped = status().publish_skipped;
 pthread_t w1, w2, r;
 pthread_create(&w1, NULL, writer, (void *)(uintptr_t)0);
 pthread_create(&w2, NULL, writer, (void *)(uintptr_t)1);
 pthread_create(&r, NULL, reader, NULL);
 pthread_join(r, NULL);
 pthread_join(w1, NULL);
 pthread_join(w2, NULL);
 xz_djlink_runtime_service();
 printf("sequence lock: %u reads of A, %u of B, %u torn, %u publishes skipped on contention\n", good_a, good_b, torn,
        status().publish_skipped - skipped);
 CHECK(torn == 0 && good_a > 0 && good_b > 0);
}

static void push(uint8_t local, enum djl_command_kind kind, uint8_t device, uint8_t value, struct djl_track_ref track)
{
 struct djl_command c = {kind, device, value, track};
 struct djl_bus_command b;
 djl_bus_command_from(&b, local, &c);
 CHECK(djl_bus_ring_push(&helper_view->ring, &b));
}

static void test_commands(void)
{
 const struct djl_track_ref sd = {1, DJL_SLOT_SD, DJL_TRACK_REKORDBOX, 55}, usb = {1, DJL_SLOT_USB, DJL_TRACK_REKORDBOX, 66},
                            none = {0, 0, 0, 0};
 event_count = 0;
 push(3, DJL_CMD_LOAD, 7, 0, sd);
 push(4, DJL_CMD_LOAD, 7, 0, usb);
 push(3, DJL_CMD_LOAD, 7, 0, (struct djl_track_ref){2, DJL_SLOT_USB, DJL_TRACK_REKORDBOX, 66});  /* another player's media */
 push(3, DJL_CMD_LOAD, 7, 0, (struct djl_track_ref){1, DJL_SLOT_CD, DJL_TRACK_REKORDBOX, 66});
 push(3, DJL_CMD_LOAD, 7, 0, (struct djl_track_ref){1, DJL_SLOT_SD, DJL_TRACK_UNANALYZED, 66});
 push(3, DJL_CMD_LOAD, 7, 0, (struct djl_track_ref){1, DJL_SLOT_SD, DJL_TRACK_REKORDBOX, 0});
 push(4, DJL_CMD_SYNC_ON, 0, 0, none);
 push(4, DJL_CMD_SYNC_OFF, 0, 0, none);
 push(3, DJL_CMD_TAKE_MASTER, 7, 0, none);
 push(3, DJL_CMD_MASTER_GRANTED, 7, 0, none);
 push(3, DJL_CMD_MASTER_GRANTED, 7, 0, none);
 push(4, DJL_CMD_ON_AIR, 0, 1, none);
 push(3, DJL_CMD_FADER_START, 0, 0, none);
 push(9, DJL_CMD_SYNC_ON, 0, 0, none);
 unsigned lines = log_lines;
 xz_djlink_runtime_service();
 CHECK(helper_view->ring.tail == helper_view->ring.head);
 CHECK(event_count == 5);
 const struct xz_extra_deck_event *e = events;
 CHECK(e[0].deck == 0 && e[0].kind == XZ_EXTRA_IN_LOAD && e[0].pressed && e[0].load.source == 1 && e[0].load.track_id == 55 &&
       e[0].load.media_generation == 101 && e[0].load.route_epoch == 9 && !e[0].load.order);
 CHECK(e[1].deck == 1 && e[1].kind == XZ_EXTRA_IN_LOAD && e[1].load.source == 2 && e[1].load.track_id == 66 &&
       e[1].load.media_generation == 102);
 CHECK(e[2].deck == 1 && e[2].kind == XZ_EXTRA_IN_SYNC && e[2].pressed && e[2].hot_cue == XZ_EXTRA_SYNC_SET_ON && !e[2].shift);
 CHECK(e[3].deck == 1 && e[3].kind == XZ_EXTRA_IN_SYNC && e[3].hot_cue == XZ_EXTRA_SYNC_SET_OFF);
 CHECK(e[4].deck == 0 && e[4].kind == XZ_EXTRA_IN_MASTER && e[4].pressed && !e[4].shift);
 struct xz_djlink_status s = status();
 CHECK(s.commands == 14 && s.loads == 2 && s.loads_rejected == 4 && s.loads_refused == 0 && s.sync_commands == 2 &&
       s.master_commands == 1 && s.ignored == 2);
 CHECK(s.external_master_device == 3 && s.on_air[1] == 1 && s.on_air[0] == 0xff);
 CHECK(log_lines == lines + 1);

 push(3, DJL_CMD_YIELD_MASTER, 5, 0, none);
 push(3, DJL_CMD_YIELD_MASTER, 5, 0, none);
 input_accepts = 0;
 push(3, DJL_CMD_LOAD, 7, 0, sd);
 xz_djlink_runtime_service();
 input_accepts = 1;
 s = status();
 CHECK(s.external_master_device == 5 && log_lines == lines + 2 && s.loads_refused == 1 && s.loads == 2);
}

static void test_restart(void)
{
 uint64_t heartbeat = fake_now();
 helper_net(4001, heartbeat, 1, DJL_OFF, DJL_OFF, 0);
 xz_djlink_runtime_service();
 /* the first start was 8 s ago; a stale heartbeat waits for 30 s since that start */
 advance(6000000);
 xz_djlink_runtime_service();
 CHECK(!status().helper_alive && spawn_count == 1);
 advance(15000000);
 xz_djlink_runtime_service();
 CHECK(spawn_count == 1);
 advance(1000000);
 xz_djlink_runtime_service();
 CHECK(spawn_count == 2 && spawned[1] == 4002 && terminate_count == 1 && terminated[0] == 4001);
 CHECK(status().helper_starts == 2);
 advance(29000000);
 xz_djlink_runtime_service();
 CHECK(spawn_count == 2);
 advance(1000000);
 xz_djlink_runtime_service();
 /* the silent 4002 is stopped; 4001, still named on the bus, is signalled again (it was verified gone on the unit) */
 CHECK(spawn_count == 3 && spawned[2] == 4003 && was_terminated(4002));
 helper_net(4003, fake_now(), 1, DJL_OFF, DJL_OFF, 0);
 xz_djlink_runtime_service();
 CHECK(status().helper_alive && status().helper_pid == 4003);
}

static void test_refresh(void)
{
 memset(&fixture, 0, sizeof fixture);
 unsigned before = snapshots;
 helper_net(4003, fake_now(), 1, DJL_OFF, DJL_OFF, 0);
 xz_djlink_runtime_service(); /* published moments ago by the threads: nothing to do */
 advance(500000);
 helper_net(4003, fake_now(), 1, DJL_OFF, DJL_OFF, 0);
 xz_djlink_runtime_service();
 CHECK(snapshots == before);
 advance(600000);
 helper_net(4003, fake_now(), 1, DJL_OFF, DJL_OFF, 0);
 xz_djlink_runtime_service();
 CHECK(snapshots == before + 1);
 struct djl_bus_deck c;
 CHECK(djl_bus_read_deck(&helper_view->deck[0], &c) != 1u && c.announce == 0 && c.facts.play == DJL_PLAY_EMPTY);
}

static void test_off_withdraws(void)
{
 fixture.route_up = 1;
 fixture.deck[0] = loaded_deck(55, 1);
 xz_djlink_runtime_publish(&fixture);
 CHECK(helper_view->deck[0].announce == 1 && helper_view->deck[1].announce == 1);
 unsigned t = terminate_count;
 xz_djlink_runtime_set_enabled(0);
 CHECK(status().state == XZ_DJLINK_OFF);
 xz_djlink_runtime_publish(&fixture);
 xz_djlink_runtime_service();
 CHECK(terminate_count == t + 1 && terminated[t] == 4003);
 CHECK(announce_at_terminate[t][0] == 0 && announce_at_terminate[t][1] == 0);
 CHECK(!exists(XZ_DJLINK_BUS));
 CHECK(status().state == XZ_DJLINK_OFF && !status().bus_ready);
 unsigned published = status().publishes;
 xz_djlink_runtime_publish(&fixture);
 xz_djlink_runtime_service();
 CHECK(status().publishes == published && !exists(XZ_DJLINK_BUS));
 munmap(helper_view, sizeof *helper_view);
 helper_view = NULL;
}

static void test_reenable_and_stale_helper(void)
{
 /* A bus left by an earlier player names its helper; turning on stops that one before starting ours. */
 int fd = open(XZ_DJLINK_BUS, O_RDWR | O_CREAT | O_TRUNC, 0600);
 struct djl_bus old;
 memset(&old, 0, sizeof old);
 old.header = (struct djl_bus_header){DJL_BUS_MAGIC, DJL_BUS_VERSION, sizeof old, 0};
 old.net.helper_pid = 777;
 CHECK(fd >= 0 && write(fd, &old, sizeof old) == (ssize_t)sizeof old);
 if (fd >= 0) close(fd);
 unsigned t = terminate_count, sp = spawn_count;
 xz_djlink_runtime_set_enabled(1);
 xz_djlink_runtime_service();
 CHECK(terminate_count == t + 1 && terminated[t] == 777);
 CHECK(spawn_count == sp + 1); /* off and on again starts at once, without the 30 s wait */
 helper_view = map_bus();
 CHECK(helper_view && helper_view->net.helper_pid == 0 && djl_bus_header_ok(helper_view));
}

static void test_stop(void)
{
 fixture.route_up = 1;
 xz_djlink_runtime_publish(&fixture);
 CHECK(helper_view->deck[0].announce == 1);
 unsigned t = terminate_count;
 xz_djlink_runtime_stop();
 CHECK(terminate_count == t + 1 && announce_at_terminate[t][0] == 0 && announce_at_terminate[t][1] == 0);
 CHECK(!exists(XZ_DJLINK_BUS) && status().state == XZ_DJLINK_OFF);
 xz_djlink_runtime_service();
 xz_djlink_runtime_publish(&fixture);
 CHECK(!exists(XZ_DJLINK_BUS));
}

static void test_state_table(void)
{
 struct xz_djlink_status s;
 memset(&s, 0, sizeof s);
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_OFF);
 s.enabled = 1;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_NO_HELPER);
 s.start_failed = 1;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_FAILED);
 s.bus_ready = s.helper_present = 1;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_FAILED);
 s.start_failed = 0;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_STARTING);
 s.helper_alive = 1;
 s.helper_up_ms = 1000;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_STARTING);
 s.helper_up_ms = 7000;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_NO_LINK);
 s.identity_ok = 1;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_IDLE);
 s.announce[1] = 1;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_STARTING);
 s.phase[1] = DJL_RUNNING;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_ONLINE);
 s.phase[0] = DJL_EVICTED;
 CHECK(xz_djlink_state_of(&s) == XZ_DJLINK_NUMBER_IN_USE);
}

int main(void)
{
 for (unsigned k = 0; k < 8; k++) grid[k] = (struct xz_extra_beat){(int32_t)(500 * k), (uint8_t)(k % 4 + 1), 12000};
 if (system("rm -rf " XZ_DJLINK_DIR)) return 2;
 struct xz_djlink_test_ops ops = {fake_now, fake_spawn, fake_terminate, fake_reap};
 xz_djlink_runtime_test_install(&ops);
 test_off_leaves_nothing();
 test_bus_created();
 test_helper_start_and_states();
 test_publish();
 test_sequence_lock();
 test_commands();
 test_restart();
 test_refresh();
 test_off_withdraws();
 test_reenable_and_stale_helper();
 test_stop();
 test_state_table();
 if (system("rm -rf " XZ_DJLINK_DIR)) return 2;
 printf("%s djlink runtime: %u checks, %u failed\n", failures ? "FAIL" : "PASS", checks, failures);
 return failures != 0;
}
