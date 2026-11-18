/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "link_runtime.h"
#include "link_clock.h"
#include "link_policy.h"
#include "link/xz_link_shm.h"
#include "djlink_facts.h"
#include "extra_timeline_projection.h"
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
extern int xz_extra_native_sync_clock(struct xz_sync_native_snapshot *) __attribute__((weak));

/* soft_mutex: the DECK 3/4 master shapes the snapshot path builds; a publisher only tries it.
   sup_mutex: every service pass and stop, so the record, the helper and the policy have one owner. */
static pthread_mutex_t soft_mutex = XZ_MUTEX_INITIALIZER;
static pthread_mutex_t sup_mutex = XZ_MUTEX_INITIALIZER;
static pthread_mutex_t status_mutex = XZ_MUTEX_INITIALIZER;
static struct xz_link_shm *shm;
static int started, enabled;
static struct xz_link_master soft[2];
static uint32_t soft_publishes, soft_skipped;
static struct xz_link_test_ops ops;
static struct xz_link_status shown;
/* shown's glance in one word, stored by update_status (glance_pack): bits 0-3 the state, bit 4 enabled, bits 16-31
   the peer count. */
static uint32_t glance_word;
static struct xz_link_policy policy;

static struct {
 int32_t child, dying;       /* our helper, and the one we replaced, both still to reap */
 uint64_t last_start_us, retry_us;
 int helper_present, start_failed, shm_failed, logged_missing, peers_logged, source;
 uint32_t helper_starts, services;
 struct xz_link_view view;
} sup = {.peers_logged = -1};

static uint64_t now_us(void)
{
 if (ops.now_us) return ops.now_us();
 struct timespec t;
 if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 0;
 return (uint64_t)t.tv_sec * 1000000u + (uint64_t)(t.tv_nsec / 1000);
}

void xz_link_runtime_test_install(const struct xz_link_test_ops *test)
{
 ops = test ? *test : (struct xz_link_test_ops){0};
}

static int observe(struct xz_master_observation *o)
{
 if (ops.observe) return ops.observe(o);
 return xz_sync_link_runtime_master_observe && xz_sync_link_runtime_master_observe(o);
}

static int stock_clock(struct xz_sync_native_snapshot *s)
{
 if (ops.stock_clock) return ops.stock_clock(s);
 return xz_extra_native_sync_clock && xz_extra_native_sync_clock(s);
}

static void say(const char *line)
{
 char text[160];
 snprintf(text, sizeof text, "ABLETON LINK: %s", line);
 xz_log(text);
}

/* ---- the helper process ----------------------------------------------------------------------- */

/* pid must still be our helper: a pid read from the record may have been reused since. */
static int is_helper(int32_t pid)
{
 char path[32], line[sizeof XZ_LINK_HELPER + 1];
 snprintf(path, sizeof path, "/proc/%d/cmdline", (int)pid);
 int fd = open(path, O_RDONLY | O_CLOEXEC);
 if (fd < 0) return 0;
 ssize_t n = read(fd, line, sizeof line);
 close(fd);
 return n == (ssize_t)sizeof line && !memcmp(line, XZ_LINK_HELPER, sizeof XZ_LINK_HELPER);
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
 static char *const argv[] = {(char *)XZ_LINK_HELPER, (char *)"--shm", (char *)XZ_LINK_SHM_PATH, (char *)"--cpus",
                              (char *)XZ_LINK_HELPER_CPUS, (char *)"--log", (char *)XZ_LINK_LOG, NULL};
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
 return !stat(XZ_LINK_HELPER, &st) && S_ISREG(st.st_mode) && !access(XZ_LINK_HELPER, X_OK);
}

/* ---- the record --------------------------------------------------------------------------------- */

/* Zeroed, header last: a helper that sees the magic sees a complete layout. */
static int create_shm(void)
{
 struct stat st;
 if (mkdir(XZ_LINK_DIR, 0755) && errno != EEXIST) return 0;
 if (lstat(XZ_LINK_DIR, &st) || !S_ISDIR(st.st_mode)) return 0;
 int old = open(XZ_LINK_SHM_PATH, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
 if (old >= 0) {
  /* A helper left by an earlier player instance would keep sending its last tempo next to ours. */
  struct xz_link_shm prev;
  if (pread(old, &prev, sizeof prev, 0) == (ssize_t)sizeof prev && prev.magic == XZ_LINK_SHM_MAGIC &&
      prev.helper_pid > 0 && terminate(prev.helper_pid))
   say("stopped the helper of an earlier player");
  close(old);
 }
 unlink(XZ_LINK_SHM_PATH);
 int fd = open(XZ_LINK_SHM_PATH, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
 if (fd < 0) return 0;
 if (ftruncate(fd, sizeof(struct xz_link_shm))) {
  close(fd);
  unlink(XZ_LINK_SHM_PATH);
  return 0;
 }
 void *map = mmap(NULL, sizeof(struct xz_link_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 close(fd);
 if (map == MAP_FAILED) {
  unlink(XZ_LINK_SHM_PATH);
  return 0;
 }
 struct xz_link_shm *s = map;
 memset(s, 0, sizeof *s);
 (void)mlock(s, sizeof *s);
 s->version = XZ_LINK_SHM_VERSION;
 s->size = sizeof *s;
 __atomic_store_n(&s->magic, XZ_LINK_SHM_MAGIC, __ATOMIC_RELEASE);
 shm = s;
 xz_link_policy_init(&policy);
 return 1;
}

static int32_t shm_helper_pid(void)
{
 return shm ? __atomic_load_n(&shm->helper_pid, __ATOMIC_ACQUIRE) : 0;
}

/* The helper told to stop, then the record gone. The helper also leaves by itself within a second of the
   record being unlinked. */
static void retire(const char *why)
{
 int had = shm || sup.child > 0;
 int32_t attached = shm_helper_pid();
 if (sup.child > 0) {
  terminate(sup.child);
  if (sup.dying > 0 && !reap(sup.dying)) terminate(sup.dying);
  sup.dying = sup.child;
  sup.child = 0;
 }
 if (attached > 0 && attached != sup.dying) terminate(attached);
 if (shm) {
  munmap(shm, sizeof *shm);
  shm = NULL;
  unlink(XZ_LINK_SHM_PATH);
 }
 sup.last_start_us = 0;
 sup.peers_logged = -1;
 memset(&sup.view, 0, sizeof sup.view);
 pthread_mutex_lock(&soft_mutex);
 memset(soft, 0, sizeof soft);
 pthread_mutex_unlock(&soft_mutex);
 if (had) say(why);
}

/* ---- the snapshot path -------------------------------------------------------------------------- */

void xz_link_runtime_publish(const struct xz_extra_decks *held)
{
 if (!held || !__atomic_load_n(&enabled, __ATOMIC_ACQUIRE)) return;
 if (pthread_mutex_trylock(&soft_mutex)) {
  __atomic_add_fetch(&soft_skipped, 1u, __ATOMIC_RELAXED);
  return;
 }
 uint64_t now = now_us();
 struct xz_master_observation o;
 int observed = observe(&o);
 for (unsigned d = 0; d < 2; d++) {
  xz_link_master_none(&soft[d]);
  soft[d].source = XZ_LINK_SOURCE_DECK3 + (int)d;
  if (!observed || o.deck != XZ_MASTER_DECK3 + (int)d) continue;
  const struct xz_extra_deck *deck = &held->deck[d];
  struct djl_deck_facts f;
  xz_djlink_facts_from_deck((int)d, deck, held->route_up, &o, now, &f);
  if (!f.master) continue;
  uint64_t heard;
  /* Only the completed-audio anchor is exact; its time carries the 1 ms grain of at_ms. */
  uint32_t uncertainty = xz_extra_timeline_heard_us(&deck->timeline, now, &heard) ? 1000u : 5000u;
  xz_link_master_from_facts((int)d, &f, deck->platter_contact, uncertainty, deck->timeline.transport_generation, &soft[d]);
 }
 __atomic_add_fetch(&soft_publishes, 1u, __ATOMIC_RELAXED);
 pthread_mutex_unlock(&soft_mutex);
}

/* ---- supervision -------------------------------------------------------------------------------- */

static uint64_t heartbeat(void)
{
 return shm ? __atomic_load_n(&shm->heartbeat_us, __ATOMIC_ACQUIRE) : 0;
}

static int helper_alive(uint64_t now)
{
 uint64_t beat = heartbeat();
 return shm_helper_pid() > 0 && beat && (beat >= now || now - beat < XZ_LINK_STALE_HEARTBEAT_US);
}

static void supervise(uint64_t now)
{
 int alive = helper_alive(now);
 sup.helper_present = helper_file_ok();
 if (!sup.helper_present && !alive) {
  if (!sup.logged_missing) say("helper " XZ_LINK_HELPER " is not staged; the unit stays out of the session");
  sup.logged_missing = 1;
  return;
 }
 sup.logged_missing = 0;
 if (alive || (sup.last_start_us && now - sup.last_start_us < XZ_LINK_RESTART_US)) return;
 if (sup.child > 0) {
  terminate(sup.child);
  if (sup.dying > 0 && !reap(sup.dying)) terminate(sup.dying);
  sup.dying = sup.child;
  sup.child = 0;
 }
 int32_t attached = shm_helper_pid();
 if (attached > 0 && attached != sup.dying) terminate(attached);
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

/* ---- the policy pass ---------------------------------------------------------------------------- */

static void read_view(uint64_t now)
{
 struct xz_link_state st;
 memset(&sup.view, 0, sizeof sup.view);
 if (!helper_alive(now) || !xz_link_seq_read(&shm->state, &st, sizeof st, 8) || st.link_host_us <= 0) return;
 sup.view.valid = (uint64_t)st.link_host_us + XZ_LINK_STATE_FRESH_US > now;
 sup.view.enabled = !!(st.flags & XZ_LINK_STATE_ENABLED);
 sup.view.peers = st.peers;
 sup.view.applied_serial = st.applied_serial;
 sup.view.tempo_bpm = st.tempo_bpm;
 sup.view.beat = st.beat;
 sup.view.at_us = (uint64_t)st.link_host_us;
}

static void put_command(const struct xz_link_command *c)
{
 struct xz_link_cmd cmd;
 memset(&cmd, 0, sizeof cmd);
 cmd.serial = c->serial;
 cmd.tempo_bpm = c->tempo_bpm;
 cmd.quantum = XZ_LINK_QUANTUM;
 cmd.ops = XZ_LINK_OP_SET_TEMPO;
 if (c->kind == XZ_LINK_CMD_FORCE) {
  cmd.ops |= XZ_LINK_OP_FORCE_BEAT;
  cmd.beat = c->beat;
  cmd.at_monotonic_us = (int64_t)c->at_us;
 }
 xz_link_seq_write(&shm->cmd, &cmd, sizeof cmd);
}

static void step(uint64_t now)
{
 read_view(now);
 if (sup.view.valid && (int)sup.view.peers != sup.peers_logged) {
  char line[64];
  snprintf(line, sizeof line, "%u other app%s in the session", sup.view.peers, sup.view.peers == 1 ? "" : "s");
  say(line);
  sup.peers_logged = (int)sup.view.peers;
 }
 struct xz_master_observation o;
 struct xz_sync_native_snapshot clock;
 memset(&clock, 0, sizeof clock);
 int observed = observe(&o), stock_ok = stock_clock(&clock);
 int deck = xz_link_pick_master(&o, observed, now, &clock, stock_ok);
 struct xz_link_master shapes[2], m;
 pthread_mutex_lock(&soft_mutex);
 memcpy(shapes, soft, sizeof shapes);
 pthread_mutex_unlock(&soft_mutex);
 xz_link_master_select(deck, &clock, stock_ok, shapes, &m);
 sup.source = m.source;
 struct xz_link_command c;
 if (xz_link_policy_step(&policy, &m, &sup.view, now, &c)) put_command(&c);
}

/* ---- status ------------------------------------------------------------------------------------- */

enum xz_link_run xz_link_run_of(const struct xz_link_status *s)
{
 if (!s->enabled) return XZ_LINK_RUN_OFF;
 if (!s->shm_ready && s->start_failed) return XZ_LINK_RUN_FAILED;
 if (!s->helper_alive) {
  if (!s->helper_present) return XZ_LINK_RUN_NO_HELPER;
  return s->start_failed ? XZ_LINK_RUN_FAILED : XZ_LINK_RUN_STARTING;
 }
 if (s->hold == XZ_LINK_HOLD_NO_MASTER) return XZ_LINK_RUN_NO_MASTER;
 return XZ_LINK_RUN_ENABLED;
}

enum { GLANCE_STATE = 0xfu, GLANCE_ENABLED = 0x10u, GLANCE_PEERS_SHIFT = 16 };
_Static_assert(XZ_LINK_RUN_FAILED <= (int)GLANCE_STATE, "every state fits the glance word");

static uint32_t glance_pack(const struct xz_link_status *s)
{
 uint32_t peers = s->peers > 0xffffu ? 0xffffu : (uint32_t)s->peers;
 return ((uint32_t)s->state & GLANCE_STATE) | (s->enabled ? GLANCE_ENABLED : 0u) | peers << GLANCE_PEERS_SHIFT;
}

static void update_status(uint64_t now, int on)
{
 struct xz_link_status s;
 memset(&s, 0, sizeof s);
 s.enabled = on;
 s.shm_ready = shm != NULL;
 s.start_failed = sup.start_failed || sup.shm_failed;
 s.helper_present = sup.helper_present;
 s.heartbeat_age_ms = UINT32_MAX;
 if (shm) {
  s.helper_alive = helper_alive(now);
  s.helper_pid = shm_helper_pid();
  uint64_t beat = heartbeat();
  if (beat) {
   uint64_t age = beat >= now ? 0 : (now - beat) / 1000u;
   s.heartbeat_age_ms = age < UINT32_MAX ? (uint32_t)age : UINT32_MAX - 1u;
  }
 }
 if (sup.view.valid) {
  s.peers = sup.view.peers;
  s.session_bpm = sup.view.tempo_bpm;
 }
 s.hold = shm ? policy.hold : XZ_LINK_HOLD_NO_LINK;
 s.source = shm ? sup.source : XZ_LINK_SOURCE_NONE;
 s.phase_known = shm && policy.hold == XZ_LINK_HOLD_NONE;
 double e = policy.error_us;
 s.phase_error_us = e > 2e9 ? INT32_MAX : e < -2e9 ? INT32_MIN : (int32_t)e;
 s.tempo_commands = policy.tempo_commands;
 s.force_commands = policy.force_commands;
 s.helper_starts = sup.helper_starts;
 s.soft_publishes = __atomic_load_n(&soft_publishes, __ATOMIC_RELAXED);
 s.soft_skipped = __atomic_load_n(&soft_skipped, __ATOMIC_RELAXED);
 s.services = sup.services;
 s.state = xz_link_run_of(&s);
 pthread_mutex_lock(&status_mutex);
 shown = s;
 pthread_mutex_unlock(&status_mutex);
 __atomic_store_n(&glance_word, glance_pack(&s), __ATOMIC_RELEASE);
}

unsigned xz_link_runtime_service(void)
{
 if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) return 0;
 if (pthread_mutex_trylock(&sup_mutex)) return XZ_LINK_SERVICE_MS;
 uint64_t now = now_us();
 int on = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
 sup.services++;
 if (sup.dying > 0 && reap(sup.dying)) sup.dying = 0;
 if (sup.child > 0 && reap(sup.child)) sup.child = 0;
 if (on) {
  if (!shm && now >= sup.retry_us) {
   sup.shm_failed = !create_shm();
   if (sup.shm_failed) {
    sup.retry_us = now + XZ_LINK_STALE_HEARTBEAT_US;
    say("could not create the record " XZ_LINK_SHM_PATH);
   }
  }
  if (shm) {
   supervise(now);
   step(now);
  }
 } else if (shm || sup.child > 0) {
  retire("off; left the session and stopped the helper");
 }
 update_status(now, on);
 unsigned wake = on || sup.child > 0 || sup.dying > 0 ? XZ_LINK_SERVICE_MS : 0;
 pthread_mutex_unlock(&sup_mutex);
 return wake;
}

int xz_link_runtime_status(struct xz_link_status *out)
{
 if (!out || pthread_mutex_trylock(&status_mutex)) return 0;
 *out = shown;
 pthread_mutex_unlock(&status_mutex);
 int on = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
 if (on != out->enabled) {
  /* The setting just changed and no service pass has run since. */
  out->enabled = on;
  out->state = on ? XZ_LINK_RUN_STARTING : XZ_LINK_RUN_OFF;
 }
 return 1;
}

void xz_link_runtime_glance(struct xz_link_glance *out)
{
 if (!out) return;
 uint32_t word = __atomic_load_n(&glance_word, __ATOMIC_ACQUIRE);
 out->enabled = !!(word & GLANCE_ENABLED);
 out->state = (enum xz_link_run)(word & GLANCE_STATE);
 out->peers = word >> GLANCE_PEERS_SHIFT;
 int on = __atomic_load_n(&enabled, __ATOMIC_ACQUIRE);
 if (on != out->enabled) {
  /* As xz_link_runtime_status: the setting just changed and no service pass has run since. */
  out->enabled = on;
  out->state = on ? XZ_LINK_RUN_STARTING : XZ_LINK_RUN_OFF;
  out->peers = 0;
 }
}

void xz_link_runtime_set_enabled(int mode)
{
 __atomic_store_n(&enabled, mode == 1 ? 1 : 0, __ATOMIC_RELEASE);
}

int xz_link_runtime_start(void)
{
 __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
 return 0;
}

void xz_link_runtime_stop(void)
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
