/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "djlink_runtime.h"
#include "djlink_facts.h"
#include "djlink/djl_bus.h"
#include "runtime.h"
#include "clock_compat.h"
#include "xz_mutex.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern int xz_sync_link_runtime_master_observe(struct xz_master_observation *) __attribute__((weak));
#pragma weak xz_extra_source_generation

/* pub_mutex: the bus pointer and the deck sections. The service holds it to swap or retire the bus; a
   publisher only tries it. sup_mutex: every service pass and stop, so helper and ring state have one owner. */
static pthread_mutex_t pub_mutex = XZ_MUTEX_INITIALIZER;
static pthread_mutex_t sup_mutex = XZ_MUTEX_INITIALIZER;
static pthread_mutex_t status_mutex = XZ_MUTEX_INITIALIZER;
static struct djl_bus *bus;
static int started, enabled;
static uint64_t last_publish_us;
static uint32_t publishes, publish_skipped;
static struct xz_djlink_test_ops ops;
static struct xz_djlink_status shown;

static struct {
 int32_t child, dying;       /* our helper, and the one we replaced, both still to reap */
 uint64_t last_start_us, alive_since_us, bus_retry_us;
 int helper_present, start_failed, bus_failed, logged_missing;
 int external_master;
 uint8_t on_air[2];
 uint32_t helper_starts, commands, loads, loads_rejected, loads_refused, sync_commands, master_commands, ignored;
} sup = {.on_air = {0xff, 0xff}};

static uint64_t now_us(void)
{
 if (ops.now_us) return ops.now_us();
 struct timespec t;
 if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 0;
 return (uint64_t)t.tv_sec * 1000000u + (uint64_t)(t.tv_nsec / 1000);
}

void xz_djlink_runtime_test_install(const struct xz_djlink_test_ops *test)
{
 ops = test ? *test : (struct xz_djlink_test_ops){0};
}

static void say(const char *line)
{
 char text[160];
 snprintf(text, sizeof text, "PRO DJ LINK: %s", line);
 xz_log(text);
}

/* ---- the helper process ----------------------------------------------------------------------- */

/* pid must still be our helper: a pid read from the bus may have been reused since. */
static int is_helper(int32_t pid)
{
 char path[32], line[sizeof XZ_DJLINK_HELPER + 1];
 snprintf(path, sizeof path, "/proc/%d/cmdline", (int)pid);
 int fd = open(path, O_RDONLY | O_CLOEXEC);
 if (fd < 0) return 0;
 ssize_t n = read(fd, line, sizeof line);
 close(fd);
 return n == (ssize_t)sizeof line && !memcmp(line, XZ_DJLINK_HELPER, sizeof XZ_DJLINK_HELPER);
}

static int terminate(int32_t pid)
{
 if (pid <= 0) return 0;
 if (ops.terminate) return ops.terminate(pid);
 return is_helper(pid) && !kill(pid, SIGTERM);
}

static int reap(int32_t pid)
{
 if (pid <= 0) return 1;
 if (ops.reap) return ops.reap(pid);
 int status;
 pid_t r = waitpid(pid, &status, WNOHANG);
 return r == pid || (r < 0 && errno == ECHILD);
}

/* vfork, never fork: the child shares the player's memory until execve, so nothing is copied or
   write-protected and only this thread waits. The child leaves the player's session and descriptors. */
static int32_t spawn(void)
{
 if (ops.spawn) return ops.spawn();
 static char *const argv[] = {(char *)XZ_DJLINK_HELPER, (char *)"--iface", (char *)"eth0", (char *)"--bus",
                              (char *)XZ_DJLINK_BUS, (char *)"--log", (char *)XZ_DJLINK_LOG, NULL};
 static char *const envp[] = {(char *)"PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL};
 long open_max = sysconf(_SC_OPEN_MAX);
 int last = open_max > 3 && open_max < 65536 ? (int)open_max : 1024;
 pid_t pid = vfork();
 if (pid == 0) {
  setsid();
  for (int fd = 3; fd < last; fd++) close(fd);
  execve(argv[0], argv, envp);
  _exit(127);
 }
 return pid > 0 ? (int32_t)pid : -1;
}

static int helper_file_ok(void)
{
 struct stat st;
 return !stat(XZ_DJLINK_HELPER, &st) && S_ISREG(st.st_mode) && !access(XZ_DJLINK_HELPER, X_OK);
}

/* ---- the bus ------------------------------------------------------------------------------------ */

static void empty_deck(unsigned d, struct djl_bus_deck *out)
{
 struct xz_extra_deck none;
 memset(&none, 0, sizeof none);
 memset(out, 0, sizeof *out);
 out->device = (uint8_t)(3 + d);
 xz_djlink_facts_from_deck((int)d, &none, 0, NULL, now_us(), &out->facts);
}

/* Sections zeroed, both decks silent, header last: a helper that sees the magic sees a complete layout. */
static int create_bus(void)
{
 struct stat st;
 if (mkdir(XZ_DJLINK_DIR, 0755) && errno != EEXIST) return 0;
 if (lstat(XZ_DJLINK_DIR, &st) || !S_ISDIR(st.st_mode)) return 0;
 int old = open(XZ_DJLINK_BUS, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
 if (old >= 0) {
  /* A helper left by an earlier player instance would keep announcing next to ours. */
  struct djl_bus prev;
  if (pread(old, &prev, sizeof prev, 0) == (ssize_t)sizeof prev && prev.header.magic == DJL_BUS_MAGIC &&
      prev.net.helper_pid > 0 && terminate(prev.net.helper_pid))
   say("stopped the helper of an earlier player");
  close(old);
 }
 unlink(XZ_DJLINK_BUS);
 int fd = open(XZ_DJLINK_BUS, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
 if (fd < 0) return 0;
 if (ftruncate(fd, sizeof(struct djl_bus))) {
  close(fd);
  unlink(XZ_DJLINK_BUS);
  return 0;
 }
 void *map = mmap(NULL, sizeof(struct djl_bus), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 close(fd);
 if (map == MAP_FAILED) {
  unlink(XZ_DJLINK_BUS);
  return 0;
 }
 struct djl_bus *b = map;
 /* Touch and pin the page here, so a publisher on a real-time thread never takes the fault. */
 memset(b, 0, sizeof *b);
 (void)mlock(b, sizeof *b);
 for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
  struct djl_bus_deck tmp;
  empty_deck(d, &tmp);
  djl_bus_write_deck(&b->deck[d], &tmp);
 }
 b->header.version = DJL_BUS_VERSION;
 b->header.size = sizeof(struct djl_bus);
 __atomic_store_n(&b->header.magic, DJL_BUS_MAGIC, __ATOMIC_RELEASE);
 pthread_mutex_lock(&pub_mutex);
 bus = b;
 pthread_mutex_unlock(&pub_mutex);
 return 1;
}

static int32_t bus_helper_pid(void)
{
 struct djl_bus_net n;
 return bus && djl_bus_read_net(&bus->net, &n) != 1u ? n.helper_pid : 0;
}

/* Both decks off the network, then the helper told to stop, then the bus gone. */
static void retire(const char *why)
{
 int had = bus || sup.child > 0;
 pthread_mutex_lock(&pub_mutex);
 for (unsigned d = 0; bus && d < DJL_BUS_DECKS; d++) {
  struct djl_bus_deck tmp;
  if (djl_bus_read_deck(&bus->deck[d], &tmp) == 1u) empty_deck(d, &tmp);
  tmp.announce = 0;
  djl_bus_write_deck(&bus->deck[d], &tmp);
 }
 pthread_mutex_unlock(&pub_mutex);
 int32_t attached = bus_helper_pid();
 if (sup.child > 0) {
  terminate(sup.child);
  if (sup.dying > 0 && !reap(sup.dying)) terminate(sup.dying);
  sup.dying = sup.child;
  sup.child = 0;
 }
 if (attached > 0 && attached != sup.dying) terminate(attached);
 pthread_mutex_lock(&pub_mutex);
 struct djl_bus *old = bus;
 bus = NULL;
 pthread_mutex_unlock(&pub_mutex);
 if (old) {
  munmap(old, sizeof *old);
  unlink(XZ_DJLINK_BUS);
 }
 sup.alive_since_us = sup.last_start_us = 0;
 if (had) say(why);
}

/* ---- publishing --------------------------------------------------------------------------------- */

void xz_djlink_runtime_publish(const struct xz_extra_decks *held)
{
 if (!held || !__atomic_load_n(&enabled, __ATOMIC_ACQUIRE)) return;
 if (pthread_mutex_trylock(&pub_mutex)) {
  __atomic_add_fetch(&publish_skipped, 1u, __ATOMIC_RELAXED);
  return;
 }
 struct djl_bus *b = bus;
 if (b) {
  uint64_t now = now_us();
  struct xz_master_observation master;
  const struct xz_master_observation *m = NULL;
  if (xz_sync_link_runtime_master_observe && xz_sync_link_runtime_master_observe(&master)) m = &master;
  for (unsigned d = 0; d < DJL_BUS_DECKS; d++) {
   struct djl_bus_deck tmp;
   memset(&tmp, 0, sizeof tmp);
   tmp.device = (uint8_t)(3 + d);
   tmp.announce = (uint8_t)(held->route_up || held->deck[d].loaded);
   xz_djlink_facts_from_deck((int)d, &held->deck[d], held->route_up, m, now, &tmp.facts);
   djl_bus_write_deck(&b->deck[d], &tmp);
  }
  __atomic_store_n(&last_publish_us, now, __ATOMIC_RELEASE);
  __atomic_add_fetch(&publishes, 1u, __ATOMIC_RELAXED);
 }
 pthread_mutex_unlock(&pub_mutex);
}

/* No snapshot runs while nobody draws decks 3/4 and the route is down, yet an empty deck must still leave
   the network then. */
static void refresh(uint64_t now)
{
 if (__atomic_load_n(&last_publish_us, __ATOMIC_ACQUIRE) + XZ_DJLINK_REFRESH_US > now) return;
 struct xz_extra_decks snap;
 struct xz_extra_analysis_hold hold = {0};
 (void)xz_extra_decks_snapshot_held(&snap, &hold);
 if (__atomic_load_n(&last_publish_us, __ATOMIC_ACQUIRE) + XZ_DJLINK_REFRESH_US <= now) xz_djlink_runtime_publish(&snap);
 xz_extra_decks_release_analysis(&snap, &hold);
}

/* ---- commands ----------------------------------------------------------------------------------- */

static int load_ref_ok(const struct djl_track_ref *t)
{
 return t->player == 1 && (t->slot == DJL_SLOT_SD || t->slot == DJL_SLOT_USB) && t->type == DJL_TRACK_REKORDBOX &&
        t->rekordbox_id;
}

static void external_master(int device, const char *what)
{
 if (sup.external_master == device) return; /* the helper may deliver one handoff twice */
 sup.external_master = device;
 char line[80];
 snprintf(line, sizeof line, "%s, tempo master now player %d (recorded, not acted on)", what, device);
 say(line);
}

static void act(const struct djl_bus_command *c)
{
 sup.commands++;
 if (c->local_device != 3 && c->local_device != 4) {
  sup.ignored++;
  return;
 }
 struct xz_extra_deck_event e;
 memset(&e, 0, sizeof e);
 e.deck = c->local_device - 3;
 e.pressed = 1;
 switch (c->kind) {
 case DJL_CMD_LOAD:
  if (!load_ref_ok(&c->track)) {
   sup.loads_rejected++;
   return;
  }
  e.kind = XZ_EXTRA_IN_LOAD;
  e.load.source = c->track.slot == DJL_SLOT_SD ? 1 : 2;
  e.load.track_id = c->track.rekordbox_id;
  e.load.media_generation = xz_extra_source_generation ? xz_extra_source_generation(e.load.source) : 0;
  e.load.route_epoch = xz_extra_decks_epoch();
  if (xz_extra_deck_input(&e)) sup.loads++;
  else sup.loads_refused++;
  return;
 case DJL_CMD_SYNC_ON:
 case DJL_CMD_SYNC_OFF:
  e.kind = XZ_EXTRA_IN_SYNC;
  e.hot_cue = c->kind == DJL_CMD_SYNC_ON ? XZ_EXTRA_SYNC_SET_ON : XZ_EXTRA_SYNC_SET_OFF;
  sup.sync_commands++;
  (void)xz_extra_deck_input(&e);
  return;
 case DJL_CMD_TAKE_MASTER:
  e.kind = XZ_EXTRA_IN_MASTER;
  sup.master_commands++;
  (void)xz_extra_deck_input(&e);
  return;
 case DJL_CMD_MASTER_GRANTED:
  external_master(c->local_device, "handoff granted");
  return;
 case DJL_CMD_YIELD_MASTER:
  external_master(c->device, "another player took master");
  return;
 case DJL_CMD_ON_AIR:
  sup.on_air[e.deck] = c->value ? 1 : 0;
  return;
 default:
  sup.ignored++;
 }
}

static void intake(void)
{
 struct djl_bus_command c;
 for (unsigned n = 0; n < DJL_BUS_RING && djl_bus_ring_pop(&bus->ring, &c); n++) act(&c);
}

/* ---- supervision -------------------------------------------------------------------------------- */

static int helper_alive(const struct djl_bus_net *n, int have, uint64_t now)
{
 return have && n->helper_pid > 0 && n->heartbeat_us &&
        (n->heartbeat_us >= now || now - n->heartbeat_us < XZ_DJLINK_STALE_HEARTBEAT_US);
}

static void supervise(uint64_t now)
{
 struct djl_bus_net n;
 int have = djl_bus_read_net(&bus->net, &n) != 1u;
 int alive = helper_alive(&n, have, now);
 if (!alive) sup.alive_since_us = 0;
 else if (!sup.alive_since_us) sup.alive_since_us = now;
 sup.helper_present = helper_file_ok();
 if (!sup.helper_present && !alive) {
  if (!sup.logged_missing) say("helper " XZ_DJLINK_HELPER " is not staged; decks 3/4 stay off the network");
  sup.logged_missing = 1;
  return;
 }
 sup.logged_missing = 0;
 if (alive || (sup.last_start_us && now - sup.last_start_us < XZ_DJLINK_RESTART_US)) return;
 if (sup.child > 0) {
  terminate(sup.child);
  if (sup.dying > 0 && !reap(sup.dying)) terminate(sup.dying);
  sup.dying = sup.child;
  sup.child = 0;
 }
 if (have && n.helper_pid > 0 && n.helper_pid != sup.dying) terminate(n.helper_pid);
 int32_t pid = spawn();
 sup.last_start_us = now;
 sup.helper_starts++;
 sup.start_failed = pid <= 0;
 char line[96];
 if (pid > 0) {
  sup.child = pid;
  snprintf(line, sizeof line, "helper %s, pid %d", sup.helper_starts > 1 ? "restarted" : "started", (int)pid);
 } else {
  snprintf(line, sizeof line, "helper could not be started (errno %d)", errno);
 }
 say(line);
}

enum xz_djlink_state xz_djlink_state_of(const struct xz_djlink_status *s)
{
 if (!s->enabled) return XZ_DJLINK_OFF;
 if (!s->bus_ready && s->start_failed) return XZ_DJLINK_FAILED;
 if (!s->helper_alive) {
  if (!s->helper_present) return XZ_DJLINK_NO_HELPER;
  return s->start_failed ? XZ_DJLINK_FAILED : XZ_DJLINK_STARTING;
 }
 for (unsigned d = 0; d < 2; d++)
  if (s->phase[d] == DJL_EVICTED) return XZ_DJLINK_NUMBER_IN_USE;
 for (unsigned d = 0; d < 2; d++)
  if (s->phase[d] == DJL_RUNNING) return XZ_DJLINK_ONLINE;
 /* The helper needs one stock keep-alive (every 2 s) before it knows the unit's address. */
 if (!s->identity_ok) return s->helper_up_ms < 6000u ? XZ_DJLINK_STARTING : XZ_DJLINK_NO_LINK;
 if (!s->announce[0] && !s->announce[1]) return XZ_DJLINK_IDLE;
 return XZ_DJLINK_STARTING;
}

static void update_status(uint64_t now, int on)
{
 struct xz_djlink_status s;
 memset(&s, 0, sizeof s);
 s.enabled = on;
 s.bus_ready = bus != NULL;
 s.start_failed = sup.start_failed || sup.bus_failed;
 s.helper_present = sup.helper_present;
 s.heartbeat_age_ms = UINT32_MAX;
 s.external_master_device = sup.external_master;
 memcpy(s.on_air, sup.on_air, sizeof s.on_air);
 struct djl_bus_net n;
 if (bus && djl_bus_read_net(&bus->net, &n) != 1u) {
  s.helper_alive = helper_alive(&n, 1, now);
  s.helper_pid = n.helper_pid;
  if (n.heartbeat_us) {
   uint64_t age = n.heartbeat_us >= now ? 0 : (now - n.heartbeat_us) / 1000u;
   s.heartbeat_age_ms = age < UINT32_MAX ? (uint32_t)age : UINT32_MAX - 1u;
  }
  s.helper_up_ms = s.helper_alive && sup.alive_since_us ? (uint32_t)((now - sup.alive_since_us) / 1000u) : 0;
  s.identity_ok = n.identity_ok;
  memcpy(s.unit_ip, n.unit_ip, 4);
  memcpy(s.phase, n.phase, 2);
  for (unsigned i = 0; i < DJL_BUS_PEERS; i++) s.peers += n.peers[i].in_use ? 1u : 0u;
  s.ring_dropped = n.ring_dropped;
  s.sync_dropped = n.sync_dropped;
 }
 for (unsigned d = 0; bus && d < DJL_BUS_DECKS; d++) s.announce[d] = __atomic_load_n(&bus->deck[d].announce, __ATOMIC_RELAXED);
 s.helper_starts = sup.helper_starts;
 s.publishes = __atomic_load_n(&publishes, __ATOMIC_RELAXED);
 s.publish_skipped = __atomic_load_n(&publish_skipped, __ATOMIC_RELAXED);
 s.commands = sup.commands;
 s.loads = sup.loads;
 s.loads_rejected = sup.loads_rejected;
 s.loads_refused = sup.loads_refused;
 s.sync_commands = sup.sync_commands;
 s.master_commands = sup.master_commands;
 s.ignored = sup.ignored;
 s.state = xz_djlink_state_of(&s);
 pthread_mutex_lock(&status_mutex);
 shown = s;
 pthread_mutex_unlock(&status_mutex);
}

void xz_djlink_runtime_service(void)
{
 if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return;
 if (pthread_mutex_trylock(&sup_mutex)) return;
 uint64_t now = now_us();
 int on = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
 if (sup.dying > 0 && reap(sup.dying)) sup.dying = 0;
 if (sup.child > 0 && reap(sup.child)) sup.child = 0;
 if (on) {
  if (!bus && now >= sup.bus_retry_us) {
   sup.bus_failed = !create_bus();
   if (sup.bus_failed) {
    sup.bus_retry_us = now + XZ_DJLINK_STALE_HEARTBEAT_US;
    say("could not create the bus " XZ_DJLINK_BUS);
   }
  }
  if (bus) {
   supervise(now);
   intake();
   refresh(now);
  }
 } else if (bus || sup.child > 0) {
  retire("off; decks 3/4 withdrawn and the helper stopped");
 }
 update_status(now, on);
 pthread_mutex_unlock(&sup_mutex);
}

int xz_djlink_runtime_status(struct xz_djlink_status *out)
{
 if (!out || pthread_mutex_trylock(&status_mutex)) return 0;
 *out = shown;
 pthread_mutex_unlock(&status_mutex);
 int on = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
 if (on != out->enabled) {
  /* The setting just changed and no service pass has run since. */
  out->enabled = on;
  out->state = on ? XZ_DJLINK_STARTING : XZ_DJLINK_OFF;
 }
 return 1;
}

void xz_djlink_runtime_set_enabled(int on)
{
 __atomic_store_n(&enabled, on ? 1 : 0, __ATOMIC_RELEASE);
}

int xz_djlink_runtime_start(void)
{
 __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
 return 0;
}

void xz_djlink_runtime_stop(void)
{
 if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return;
 pthread_mutex_lock(&sup_mutex);
 __atomic_store_n(&started, 0, __ATOMIC_RELEASE);
 __atomic_store_n(&enabled, 0, __ATOMIC_RELEASE);
 retire("stopped with the player");
 if (sup.dying > 0 && reap(sup.dying)) sup.dying = 0;
 update_status(now_us(), 0);
 pthread_mutex_unlock(&sup_mutex);
}
