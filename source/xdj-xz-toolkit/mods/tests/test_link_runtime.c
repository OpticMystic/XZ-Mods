/* SPDX-License-Identifier: MIT */
/* The player half of the Ableton Link helper's record: lifetime, helper supervision, the master picked from the
   stock clock or a DECK 3/4 view, the policy's commands under the sequence lock, status, and turning off.
   The test plays the helper's part on its own mapping of the record. */
#include "../link_runtime.h"
#include "../link_clock.h"
#include "../link_policy.h"
#include "../link/xz_link_shm.h"
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks, failures, glances;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- the rest of the player, as this module sees it ---------------------------------------------- */

static unsigned log_lines;
static char last_log[160], all_log[4096];
void xz_log(const char *line)
{
 log_lines++;
 snprintf(last_log, sizeof last_log, "%s", line);
 size_t n = strlen(all_log);
 if (n + strlen(line) + 2 < sizeof all_log) snprintf(all_log + n, sizeof all_log - n, "%s\n", line);
}

static uint64_t clock_us = 2000000000ull;
static uint64_t fake_now(void) { return __atomic_load_n(&clock_us, __ATOMIC_ACQUIRE); }
static void advance(uint64_t us) { __atomic_add_fetch(&clock_us, us, __ATOMIC_ACQ_REL); }

static struct xz_master_observation master = {.deck = XZ_MASTER_UNKNOWN};
static int fake_observe(struct xz_master_observation *out)
{
 *out = master;
 return master.deck != XZ_MASTER_UNKNOWN;
}

/* The stock master: deck 1 at stock_bpm, its bar phase sampled 10 ms before each service pass. */
static int stock_on;
static double stock_bpm = 128.0, stock_phase0 = 0.7;
static uint64_t stock_t0;
static double stock_phase_at(uint64_t t) { return stock_phase0 + ((double)t - (double)stock_t0) * stock_bpm / 60e6; }
static int fake_stock(struct xz_sync_native_snapshot *s)
{
 memset(s, 0, sizeof *s);
 s->native_deck = -1;
 if (!stock_on) return 0;
 uint64_t at = fake_now() - 10000;
 s->available = s->phase_available = 1;
 s->native_deck = 0;
 s->master_id = 0;
 s->bpm = (float)stock_bpm;
 s->bar_phase = (float)fmod(stock_phase_at(at), 4.0);
 s->captured_us = at;
 s->captured_ms = (uint32_t)(at / 1000);
 s->uncertainty_us = 500;
 s->playing = 1;
 s->source_revision = 3;
 s->track_identity = 99;
 return 1;
}

static int32_t next_pid = 5001, spawned[8], terminated[16];
static unsigned spawn_count, terminate_count;
static int32_t fake_spawn(void)
{
 if (spawn_count < 8) spawned[spawn_count] = next_pid;
 spawn_count++;
 return next_pid++;
}
static int fake_terminate(int32_t pid)
{
 if (terminate_count < 16) terminated[terminate_count] = pid;
 terminate_count++;
 return 1;
}
static int was_terminated(int32_t pid)
{
 for (unsigned i = 0; i < terminate_count && i < 16; i++)
  if (terminated[i] == pid) return 1;
 return 0;
}
static int fake_reap(int32_t pid) { return was_terminated(pid); }

static int exists(const char *path)
{
 struct stat st;
 return !stat(path, &st);
}

/* ---- the helper, played by the test --------------------------------------------------------------- */

static struct xz_link_shm *helper_view;
static struct { double tempo, beat0; uint64_t t0; uint32_t applied; unsigned tempo_ops, force_ops; struct xz_link_cmd last; } session;

static struct xz_link_shm *map_record(void)
{
 int fd = open(XZ_LINK_SHM_PATH, O_RDWR);
 if (fd < 0) return NULL;
 void *m = mmap(NULL, sizeof(struct xz_link_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 close(fd);
 return m == MAP_FAILED ? NULL : m;
}

static double session_beat(uint64_t t) { return session.beat0 + ((double)t - (double)session.t0) * session.tempo / 60e6; }

/* One helper loop: apply a new command, publish the session and the heartbeat. */
static void helper_tick(int32_t pid, unsigned peers)
{
 uint64_t now = fake_now();
 struct xz_link_cmd c;
 if (xz_link_seq_read(&helper_view->cmd, &c, sizeof c, 8) && c.serial != session.applied) {
  session.applied = c.serial;
  session.last = c;
  uint64_t at = c.at_monotonic_us ? (uint64_t)c.at_monotonic_us : now;
  if (c.ops & XZ_LINK_OP_SET_TEMPO) {
   session.beat0 = session_beat(at);
   session.t0 = at;
   session.tempo = c.tempo_bpm;
   session.tempo_ops++;
  }
  if (c.ops & XZ_LINK_OP_FORCE_BEAT) {
   session.beat0 = c.beat;
   session.t0 = at;
   session.force_ops++;
  }
 }
 struct xz_link_state st;
 memset(&st, 0, sizeof st);
 st.peers = peers;
 st.flags = XZ_LINK_STATE_ENABLED;
 st.applied_serial = session.applied;
 st.link_host_us = st.monotonic_us = (int64_t)now;
 st.tempo_bpm = session.tempo;
 st.beat = session_beat(now);
 st.quantum = 4;
 xz_link_seq_write(&helper_view->state, &st, sizeof st);
 __atomic_store_n(&helper_view->helper_pid, pid, __ATOMIC_RELEASE);
 __atomic_store_n(&helper_view->heartbeat_us, now, __ATOMIC_RELEASE);
}

static struct xz_link_status status(void)
{
 struct xz_link_status s;
 memset(&s, 0xee, sizeof s);
 CHECK(xz_link_runtime_status(&s));
 /* The PLAY screen's glance (one atomic word) says what status says, at every step of every test. */
 struct xz_link_glance g;
 memset(&g, 0xee, sizeof g);
 xz_link_runtime_glance(&g);
 CHECK(g.enabled == s.enabled && g.state == s.state);
 if (s.state == XZ_LINK_RUN_ENABLED || s.state == XZ_LINK_RUN_NO_MASTER) CHECK(g.peers == s.peers);
 glances++;
 return s;
}

static void stage_helper(int present)
{
 if (!present) {
  unlink(XZ_LINK_HELPER);
  return;
 }
 int fd = open(XZ_LINK_HELPER, O_WRONLY | O_CREAT | O_TRUNC, 0700);
 CHECK(fd >= 0);
 if (fd >= 0) {
  (void)!write(fd, "#!/bin/sh\n", 10);
  close(fd);
 }
}

/* ---- tests --------------------------------------------------------------------------------------- */

static void test_off_leaves_nothing(void)
{
 xz_link_runtime_start();
 for (int i = 0; i < 3; i++) CHECK(xz_link_runtime_service() == 0);
 struct xz_extra_decks none;
 memset(&none, 0, sizeof none);
 xz_link_runtime_publish(&none);
 CHECK(!exists(XZ_LINK_SHM_PATH) && !exists(XZ_LINK_DIR));
 struct xz_link_status s = status();
 CHECK(!s.enabled && s.state == XZ_LINK_RUN_OFF && !s.shm_ready && s.soft_publishes == 0 && spawn_count == 0);
}

static void test_record_without_helper(void)
{
 xz_link_runtime_set_enabled(2); /* not a mode: stays off */
 CHECK(status().state == XZ_LINK_RUN_OFF);
 xz_link_runtime_set_enabled(1);
 CHECK(status().state == XZ_LINK_RUN_STARTING);
 CHECK(xz_link_runtime_service() == XZ_LINK_SERVICE_MS);
 struct stat st;
 CHECK(!stat(XZ_LINK_SHM_PATH, &st) && st.st_size == 256 && (st.st_mode & 0777) == 0600);
 helper_view = map_record();
 CHECK(helper_view != NULL);
 if (!helper_view) exit(1);
 CHECK(helper_view->magic == XZ_LINK_SHM_MAGIC && helper_view->version == XZ_LINK_SHM_VERSION && helper_view->size == 256);
 CHECK(helper_view->helper_pid == 0 && helper_view->heartbeat_us == 0 && helper_view->cmd.serial == 0);
 struct xz_link_status s = status();
 CHECK(s.enabled && s.shm_ready && !s.helper_present && s.state == XZ_LINK_RUN_NO_HELPER && spawn_count == 0);
 CHECK(strstr(all_log, "is not staged") != NULL);
}

static void test_helper_started_and_alive(void)
{
 stage_helper(1);
 xz_link_runtime_service();
 CHECK(spawn_count == 1);
 struct xz_link_status s = status();
 CHECK(s.helper_present && !s.helper_alive && s.state == XZ_LINK_RUN_STARTING && s.helper_starts == 1);
 CHECK(strstr(last_log, "helper started, pid 5001") != NULL);
 session.tempo = 120.0;
 session.beat0 = 0.25;
 session.t0 = fake_now();
 helper_tick(spawned[0], 2);
 advance(40000);
 xz_link_runtime_service();
 s = status();
 CHECK(s.helper_alive && s.helper_pid == spawned[0] && s.peers == 2 && fabs(s.session_bpm - 120.0) < 1e-9);
 CHECK(s.state == XZ_LINK_RUN_NO_MASTER && s.hold == XZ_LINK_HOLD_NO_MASTER && s.heartbeat_age_ms == 40);
 CHECK(helper_view->cmd.serial == 0); /* no master: nothing sent */
 struct xz_link_glance g;
 xz_link_runtime_glance(&g);
 CHECK(g.enabled && g.state == XZ_LINK_RUN_NO_MASTER && g.peers == 2);
 /* The setting goes off: the glance says OFF before any service pass, as status does; back on, STARTING. */
 xz_link_runtime_set_enabled(0);
 xz_link_runtime_glance(&g);
 CHECK(!g.enabled && g.state == XZ_LINK_RUN_OFF && g.peers == 0);
 xz_link_runtime_set_enabled(1);
 xz_link_runtime_glance(&g);
 CHECK(g.enabled && g.state == XZ_LINK_RUN_NO_MASTER && g.peers == 2);
}

/* Runs service and helper side by side for `ms`, 40 ms apart. */
static void run(unsigned ms, unsigned peers)
{
 for (unsigned t = 0; t < ms; t += 40) {
  advance(20000);
  helper_tick(spawned[spawn_count - 1], peers);
  advance(20000);
  CHECK(xz_link_runtime_service() == XZ_LINK_SERVICE_MS);
 }
}

static double phase_error_us(void)
{
 uint64_t t = fake_now();
 double e = fmod(session_beat(t) - stock_phase_at(t) + 2.0 + 400.0, 4.0) - 2.0;
 return e * 60e6 / stock_bpm;
}

static void test_stock_master(void)
{
 stock_on = 1;
 stock_t0 = fake_now();
 run(2000, 2);
 struct xz_link_status s = status();
 CHECK(session.tempo_ops >= 2 && session.force_ops == 1);
 CHECK(fabs(session.tempo - 128.0) < 0.01);
 CHECK(fabs(phase_error_us()) < 1500.0);
 CHECK(s.state == XZ_LINK_RUN_ENABLED && s.source == XZ_LINK_SOURCE_DECK1 && s.phase_known && abs(s.phase_error_us) < 2000);
 CHECK(s.force_commands == 1 && s.tempo_commands >= 1 && fabs(s.session_bpm - 128.0) < 0.01);
 CHECK(session.last.quantum == 4.0);
 /* The DJ moves the pitch: a tempo command, no force. */
 unsigned forces = session.force_ops;
 stock_phase0 = stock_phase_at(fake_now());
 stock_t0 = fake_now();
 stock_bpm = 129.28;
 run(400, 2);
 CHECK(session.force_ops == forces && fabs(session.tempo - 129.28) < 0.01);
 CHECK(strstr(all_log, "2 other apps in the session") != NULL);
}

static struct xz_extra_beat grid[64];

/* DECK 3 playing at 1.0 with an exact completed-audio coordinate 5 ms old, 120 BPM grid. */
static struct xz_extra_decks deck3_master(int platter)
{
 static struct xz_extra_decks held;
 memset(&held, 0, sizeof held);
 for (unsigned k = 0; k < 64; k++) grid[k] = (struct xz_extra_beat){(int32_t)(100 + 500 * k), (uint8_t)(k % 4 + 1), 12000};
 struct xz_extra_deck *d = &held.deck[0];
 uint32_t now_ms = (uint32_t)(fake_now() / 1000u);
 d->known = XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID) | XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL) |
            XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM);
 d->loaded = d->playing = 1;
 d->platter_contact = platter;
 d->source = 1;
 d->track_id = 77;
 d->total_ms = 60000;
 d->track_bpm_x100 = d->bpm_x100 = 12000;
 d->grid = grid;
 d->grid_count = 64;
 d->timeline.source_rate = 44100;
 d->timeline.source_frame = 441000; /* 10 s */
 d->timeline.at_ms = now_ms - 5;
 d->timeline.flags = XZ_EXTRA_TIMELINE_ANCHOR | XZ_EXTRA_TIMELINE_RATE | XZ_EXTRA_TIMELINE_MOTION |
                     XZ_EXTRA_TIMELINE_SINK_FRAME | XZ_EXTRA_TIMELINE_APPLIED_RATE;
 d->timeline.moving = 1;
 d->timeline.sink_frame = 100000;
 d->timeline.observed_wire_frame = 100441;
 d->timeline.completed_rate = 1.0;
 d->timeline.rate_ppm = 1000000;
 d->timeline.route_epoch = 3;
 d->timeline.load_revision = 7;
 d->timeline.transport_generation = 11;
 held.route_up = 1;
 master = (struct xz_master_observation){.deck = XZ_MASTER_DECK3, .revision = 5, .observed_ms = now_ms, .operator_epoch = 2,
                                         .route_epoch = 3, .load_revision = 7, .transport_generation = 11};
 return held;
}

static void test_deck3_master(void)
{
 stock_on = 0;
 struct xz_extra_decks held = deck3_master(0);
 uint32_t before = status().soft_publishes;
 xz_link_runtime_publish(&held);
 advance(1000);
 xz_link_runtime_service();
 struct xz_link_status s = status();
 CHECK(s.soft_publishes == before + 1);
 CHECK(s.source == XZ_LINK_SOURCE_DECK3 && s.hold != XZ_LINK_HOLD_NO_MASTER && s.hold != XZ_LINK_HOLD_UNCERTAIN);
 /* Platter held: the policy holds instead of chasing. */
 held = deck3_master(1);
 xz_link_runtime_publish(&held);
 advance(1000);
 xz_link_runtime_service();
 s = status();
 CHECK(s.hold == XZ_LINK_HOLD_MANUAL || s.hold == XZ_LINK_HOLD_PENDING);
 /* DECK 4 named but this snapshot's deck 4 is empty: no shape, no master to send. */
 master.deck = XZ_MASTER_DECK4;
 xz_link_runtime_publish(&held);
 advance(41000);
 helper_tick(spawned[spawn_count - 1], 2);
 xz_link_runtime_service();
 s = status();
 CHECK(s.state == XZ_LINK_RUN_NO_MASTER && s.source == XZ_LINK_SOURCE_DECK4);
 master.deck = XZ_MASTER_UNKNOWN;
}

static void test_restart_on_stale_heartbeat(void)
{
 unsigned spawns = spawn_count;
 int32_t old = spawned[spawn_count - 1];
 advance(6000000); /* heartbeat 6 s old, but the last start was under 30 s ago */
 xz_link_runtime_service();
 CHECK(spawn_count == spawns && !was_terminated(old));
 advance(25000000);
 xz_link_runtime_service();
 CHECK(spawn_count == spawns + 1 && was_terminated(old));
 CHECK(strstr(last_log, "helper restarted") != NULL);
 /* The new one never beats: no second restart before 30 s. */
 advance(20000000);
 xz_link_runtime_service();
 CHECK(spawn_count == spawns + 1);
 advance(11000000);
 xz_link_runtime_service();
 CHECK(spawn_count == spawns + 2);
 helper_tick(spawned[spawn_count - 1], 1);
 advance(40000);
 xz_link_runtime_service();
 CHECK(status().helper_alive);
}

static void test_off_at_runtime(void)
{
 int32_t pid = spawned[spawn_count - 1];
 xz_link_runtime_set_enabled(0);
 CHECK(status().state == XZ_LINK_RUN_OFF);
 CHECK(xz_link_runtime_service() == XZ_LINK_SERVICE_MS); /* the stopped child is reaped on the next pass */
 CHECK(was_terminated(pid) && !exists(XZ_LINK_SHM_PATH));
 CHECK(strstr(last_log, "off; left the session") != NULL);
 CHECK(xz_link_runtime_service() == 0);
 munmap(helper_view, sizeof *helper_view);
 helper_view = NULL;
 struct xz_link_status s = status();
 CHECK(!s.enabled && !s.shm_ready && s.state == XZ_LINK_RUN_OFF);
}

static void test_earlier_player_helper_stopped(void)
{
 int fd = open(XZ_LINK_SHM_PATH, O_RDWR | O_CREAT | O_TRUNC, 0600);
 struct xz_link_shm old;
 memset(&old, 0, sizeof old);
 old.magic = XZ_LINK_SHM_MAGIC;
 old.version = XZ_LINK_SHM_VERSION;
 old.size = sizeof old;
 old.helper_pid = 7777;
 CHECK(fd >= 0 && write(fd, &old, sizeof old) == (ssize_t)sizeof old);
 close(fd);
 xz_link_runtime_set_enabled(1);
 xz_link_runtime_service();
 CHECK(was_terminated(7777) && strstr(all_log, "stopped the helper of an earlier player") != NULL);
 helper_view = map_record();
 CHECK(helper_view && helper_view->helper_pid == 0 && helper_view->magic == XZ_LINK_SHM_MAGIC);
 xz_link_runtime_stop();
 CHECK(!exists(XZ_LINK_SHM_PATH) && strstr(last_log, "stopped with the player") != NULL);
 if (helper_view) munmap(helper_view, sizeof *helper_view);
}

/* ---- the sequence lock, against a writer on another thread --------------------------------------- */

static struct xz_link_shm torture;
static volatile int torture_stop;
static void *writer(void *unused)
{
 (void)unused;
 for (uint32_t n = 1; !torture_stop; n++) {
  struct xz_link_state st;
  memset(&st, 0, sizeof st);
  st.peers = n;
  st.applied_serial = ~n;
  st.link_host_us = (int64_t)n * 3;
  st.tempo_bpm = n * 0.5;
  st.beat = n * 0.25;
  st.updates = n;
  xz_link_seq_write(&torture.state, &st, sizeof st);
 }
 return NULL;
}

static void test_sequence_lock(void)
{
 pthread_t t;
 torture_stop = 0;
 CHECK(!pthread_create(&t, NULL, writer, NULL));
 unsigned reads = 0, torn = 0, busy = 0;
 for (unsigned i = 0; i < 200000; i++) {
  struct xz_link_state st;
  if (!xz_link_seq_read(&torture.state, &st, sizeof st, 64)) {
   busy++;
   continue;
  }
  reads++;
  uint32_t n = st.peers;
  if (n && (st.applied_serial != ~n || st.link_host_us != (int64_t)n * 3 || st.tempo_bpm != n * 0.5 ||
            st.beat != n * 0.25 || st.updates != n))
   torn++;
 }
 torture_stop = 1;
 pthread_join(t, NULL);
 printf("sequence lock: %u consistent reads, %u torn, %u gave up after 64 tries\n", reads, torn, busy);
 CHECK(torn == 0 && reads > 1000);
}

int main(void)
{
 static const struct xz_link_test_ops fakes = {fake_now, fake_spawn, fake_terminate, fake_reap, fake_observe, fake_stock};
 xz_link_runtime_test_install(&fakes);
 unlink(XZ_LINK_SHM_PATH);
 unlink(XZ_LINK_HELPER);
 rmdir(XZ_LINK_DIR);
 test_off_leaves_nothing();
 test_record_without_helper();
 test_helper_started_and_alive();
 test_stock_master();
 test_deck3_master();
 test_restart_on_stale_heartbeat();
 test_off_at_runtime();
 test_earlier_player_helper_stopped();
 test_sequence_lock();
 unlink(XZ_LINK_HELPER);
 rmdir(XZ_LINK_DIR);
 printf("%s link runtime: %u checks (%u with the glance compared), %u failed\n", failures ? "FAIL" : "PASS", checks,
        glances, failures);
 return failures ? 1 : 0;
}
