/* SPDX-License-Identifier: MIT */
/* USB STOP with the mods running (usb_release.h). The core above the runtime marker runs on a PC. */
#define _GNU_SOURCE
#include "usb_release.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#ifndef MNT_DETACH
#define MNT_DETACH 2
#endif

int xz_ur_slot_of(const char *point) {
 static const char media[] = "/media/usb";
 if (!point || strncmp(point, media, sizeof media - 1)) return 0;
 const char *s = point + sizeof media - 1;
 if ((*s != '1' && *s != '2') || s[1] != '/' || !s[2]) return 0;
 int slot = *s - '0';
 for (s += 2; *s; s++) if (*s == '/') return 0;
 return slot;
}

int xz_ur_under(const char *path, const char *mount) {
 if (!path || !mount || !*mount) return 0;
 size_t n = strlen(mount);
 while (n > 1 && mount[n - 1] == '/') n--;
 return !strncmp(path, mount, n) && (path[n] == 0 || path[n] == '/');
}

int xz_ur_loop_index(const char *source) {
 static const char loop[] = "/dev/loop";
 if (!source || strncmp(source, loop, sizeof loop - 1)) return -1;
 const char *s = source + sizeof loop - 1;
 if (!*s) return -1;
 int value = 0;
 for (; *s; s++) {
  if (*s < '0' || *s > '9' || value > 255) return -1;
  value = value * 10 + (*s - '0');
 }
 return value;
}

static void copy_field(char *out, size_t cap, const char *in) { snprintf(out, cap, "%s", in); }

int xz_ur_parse_mounts(const char *text, size_t length, int slot, struct xz_ur_mount *out, int cap) {
 if (!text || !out || cap <= 0) return 0;
 int count = 0;
 size_t at = 0;
 while (at < length && count < cap) {
  const char *end = memchr(text + at, '\n', length - at);
  if (!end) break; /* a cut-off last line is not a record */
  size_t n = (size_t)(end - (text + at));
  char line[1024];
  if (n >= sizeof line) { at += n + 1; continue; }
  memcpy(line, text + at, n);
  line[n] = 0;
  at += n + 1;
  char *field[32], *save = NULL;
  int fields = 0, sep = -1;
  for (char *t = strtok_r(line, " \t", &save); t && fields < 32; t = strtok_r(NULL, " \t", &save)) {
   if (!strcmp(t, "-") && sep < 0) sep = fields;
   field[fields++] = t;
  }
  struct xz_ur_mount m;
  memset(&m, 0, sizeof m);
  const char *point, *source;
  if (sep >= 6 && fields >= sep + 3) { /* mountinfo: id parent maj:min root point options ... - type source */
   char *e = NULL;
   m.id = strtoul(field[0], &e, 10);
   if (!e || *e) continue;
   /* maj:min by hand: sscanf binds to __isoc99_sscanf (GLIBC_2.7), above the player's GLIBC_2.4 floor. */
   char *colon = NULL;
   unsigned long major = strtoul(field[2], &colon, 10), minor = 0;
   if (colon && *colon == ':') {
    char *rest = NULL;
    minor = strtoul(colon + 1, &rest, 10);
    if (rest && !*rest) { m.major = (unsigned)major; m.minor = (unsigned)minor; }
   }
   point = field[4]; source = field[sep + 2];
  } else if (fields >= 4 && sep < 0) { /* /proc/mounts: source point type options */
   point = field[1]; source = field[0];
  } else continue;
  if (slot && xz_ur_slot_of(point) != slot) continue;
  if (strlen(point) >= sizeof m.point) continue;
  copy_field(m.point, sizeof m.point, point);
  copy_field(m.source, sizeof m.source, source);
  out[count++] = m;
 }
 return count;
}

void xz_ur_hide_add(struct xz_ur_hidden_set *set, const struct xz_ur_mount *mounts, int count, uint32_t now) {
 if (!set || !mounts) return;
 for (int i = 0; i < count; i++) {
  int slot = -1;
  for (int k = 0; k < XZ_UR_MOUNTS; k++)
   if (set->entry[k].used && !strcmp(set->entry[k].mount.point, mounts[i].point)) { slot = k; break; }
  for (int k = 0; slot < 0 && k < XZ_UR_MOUNTS; k++)
   if (!set->entry[k].used || now - set->entry[k].at_ms > XZ_UR_HIDE_MS) slot = k;
  if (slot < 0) return;
  set->entry[slot].mount = mounts[i];
  set->entry[slot].at_ms = now;
  set->entry[slot].used = 1;
 }
}

void xz_ur_hide_clear(struct xz_ur_hidden_set *set, const char *point) {
 if (!set || !point) return;
 for (int k = 0; k < XZ_UR_MOUNTS; k++)
  if (set->entry[k].used && !strcmp(set->entry[k].mount.point, point)) set->entry[k].used = 0;
}

int xz_ur_hidden(const struct xz_ur_hidden_set *set, uint32_t now, const char *point, unsigned long id,
                 unsigned major, unsigned minor) {
 if (!set || !point) return 0;
 for (int k = 0; k < XZ_UR_MOUNTS; k++) {
  const struct xz_ur_hide *h = &set->entry[k];
  if (!h->used || now - h->at_ms > XZ_UR_HIDE_MS || strcmp(h->mount.point, point)) continue;
  /* A mount id of 0 or 1 is a /proc/mounts fallback, not an identity. */
  if (h->mount.id > 1 && id > 1 && h->mount.id != id) continue;
  if ((h->mount.major || h->mount.minor) && (major || minor) && (h->mount.major != major || h->mount.minor != minor)) continue;
  return 1;
 }
 return 0;
}

/* ---- holders that let go on their own threads ---- */
int xz_ur_board_add(struct xz_ur_board *b, const char *name, xz_ur_ask ask, void *ctx) {
 if (!b || !ask || b->holders >= XZ_UR_HOLDERS) return -1;
 b->holder[b->holders] = (struct xz_ur_holder){name ? name : "holder", ask, ctx};
 return b->holders++;
}

uint32_t xz_ur_board_ask(struct xz_ur_board *b, int slot, uint32_t now) {
 if (!b || (slot != 1 && slot != 2)) return 0;
 struct xz_ur_asked *a = &b->slot[slot - 1];
 uint32_t stop = ++b->serial;
 if (!stop) stop = ++b->serial;
 /* The new stop is published before anyone is asked: an answer can come before ask() returns its count, and the
    count added afterwards nets it out. An answer to the older stop no longer matches and is dropped. */
 for (int h = 0; h < XZ_UR_HOLDERS; h++) { __atomic_store_n(&a->waiting[h], 0, __ATOMIC_RELAXED); a->asked[h] = 0; }
 a->at_ms = now;
 __atomic_store_n(&a->stop, stop, __ATOMIC_RELEASE);
 for (int h = 0; h < b->holders; h++) {
  unsigned n = b->holder[h].ask(b->holder[h].ctx, slot, stop);
  a->asked[h] = n;
  __atomic_add_fetch(&a->waiting[h], (int)n, __ATOMIC_ACQ_REL);
 }
 return stop;
}

void xz_ur_board_answer(struct xz_ur_board *b, int holder, int slot, uint32_t stop) {
 if (!b || holder < 0 || holder >= XZ_UR_HOLDERS || (slot != 1 && slot != 2)) return;
 struct xz_ur_asked *a = &b->slot[slot - 1];
 if (__atomic_load_n(&a->stop, __ATOMIC_ACQUIRE) != stop) return;
 __atomic_sub_fetch(&a->waiting[holder], 1, __ATOMIC_ACQ_REL);
}

int xz_ur_board_waiting(const struct xz_ur_board *b, int slot, int holder) {
 if (!b || holder < 0 || holder >= XZ_UR_HOLDERS || (slot != 1 && slot != 2)) return 0;
 int n = __atomic_load_n(&b->slot[slot - 1].waiting[holder], __ATOMIC_ACQUIRE);
 return n > 0 ? n : 0;
}

/* ---- the unmount policy ---- */
static void say(const struct xz_ur_env *e, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void say(const struct xz_ur_env *e, const char *format, ...) {
 char line[320];
 va_list args;
 va_start(args, format);
 vsnprintf(line, sizeof line, format, args);
 va_end(args);
 e->log(e->ctx, line);
}

int xz_ur_board_settle(const struct xz_ur_board *b, const struct xz_ur_env *e, int slot) {
 if (!b || !e || (slot != 1 && slot != 2) || !__atomic_load_n(&b->slot[slot - 1].stop, __ATOMIC_ACQUIRE)) return 0;
 const struct xz_ur_asked *a = &b->slot[slot - 1];
 for (;;) {
  int waiting = 0;
  for (int h = 0; h < b->holders; h++) waiting += xz_ur_board_waiting(b, slot, h) > 0;
  uint32_t spent = e->now_ms(e->ctx) - a->at_ms;
  if (!waiting || spent >= XZ_UR_ANSWER_MS) {
   for (int h = 0; h < b->holders; h++) {
    int left = xz_ur_board_waiting(b, slot, h);
    if (left)
     say(e, "USB_RELEASE: slot %d: %s: %d of %u part(s) did not let go within %u ms; the unmount goes on", slot,
         b->holder[h].name, left, a->asked[h], XZ_UR_ANSWER_MS);
    else
     say(e, "USB_RELEASE: slot %d: %s: %u part(s) answered, %lu ms after the stop began", slot, b->holder[h].name,
         a->asked[h], (unsigned long)spent);
   }
   return waiting;
  }
  e->sleep_ms(e->ctx, XZ_UR_ANSWER_STEP_MS);
 }
}
static int ours(const struct xz_ur_stop *s, const char *point) {
 for (int i = 0; i < s->count; i++) if (!strcmp(s->ours[i], point)) return 1;
 return 0;
}
static void mark(struct xz_ur_stop *s, const struct xz_ur_env *e, const char *point) {
 if (!ours(s, point) && s->count < XZ_UR_MOUNTS) snprintf(s->ours[s->count++], XZ_UR_POINT, "%s", point);
 e->gone(e->ctx, point);
}
/* One partition: plain unmount, retried while busy until the request's deadline. */
static int unmount_one(struct xz_ur_stop *s, const struct xz_ur_env *e, const char *point, uint32_t start, uint32_t deadline) {
 unsigned attempts = 0;
 int first = 1;
 for (;;) {
  int rc = e->umount2(e->ctx, point, 0);
  attempts++;
  if (!rc) {
   mark(s, e, point);
   say(e, "USB_RELEASE: unmounted %s attempts=%u after_ms=%lu", point, attempts, (unsigned long)(e->now_ms(e->ctx) - start));
   return 0;
  }
  if (rc == -EINVAL || rc == -ENOENT) { /* not (or no longer) a mount: nothing of it is left to write */
   mark(s, e, point);
   say(e, "USB_RELEASE: %s is not mounted (errno %d)", point, -rc);
   return 0;
  }
  if (rc != -EBUSY) {
   say(e, "USB_RELEASE: unmount of %s refused, errno %d", point, -rc);
   return rc;
  }
  if (first) {
   first = 0;
   int loops = e->release_loops(e->ctx, point);
   if (loops > 0) { say(e, "USB_RELEASE: %s: released %d loop device(s) backed by it", point, loops); continue; }
   e->holders(e->ctx, point);
  }
  if ((int32_t)(e->now_ms(e->ctx) - deadline) >= 0) {
   e->holders(e->ctx, point);
   say(e, "USB_RELEASE: %s still busy after %u attempts, %lu ms; stock decides next", point, attempts,
       (unsigned long)(e->now_ms(e->ctx) - start));
   return -EBUSY;
  }
  e->sleep_ms(e->ctx, XZ_UR_STEP_MS);
 }
}

int xz_ur_unmount(struct xz_ur_wrapper *w, const struct xz_ur_env *e, const char *target, int flags) {
 int slot = target ? xz_ur_slot_of(target) : 0;
 if (!w || !slot) return e->umount2(e->ctx, target, flags);
 struct xz_ur_stop *s = &w->stop[slot - 1];
 uint32_t now = e->now_ms(e->ctx);
 if (!s->active || now - s->at_ms > XZ_UR_HIDE_MS) {
  memset(s, 0, sizeof *s);
  s->active = 1; s->at_ms = now; s->clean = 1;
  e->begin(e->ctx, slot);
 }
 /* DECK 3/4 and the stem streams let go on their own threads: give them up to XZ_UR_ANSWER_MS, once per stop. */
 if (!s->settled) {
  s->settled = 1;
  if (e->settle) { e->settle(e->ctx, slot); now = e->now_ms(e->ctx); } /* the busy retry still gets its full time */
 }
 if (flags & MNT_DETACH) {
  if (ours(s, target)) return 0;
  int loops = e->release_loops(e->ctx, target);
  e->sync(e->ctx);
  int rc = e->umount2(e->ctx, target, flags);
  say(e, "USB_RELEASE: lazy unmount of %s: result %d, loops released %d", target, rc, loops);
  if (!rc) mark(s, e, target);
  return rc;
 }
 if (ours(s, target)) return s->clean ? 0 : -EBUSY;
 /* Every partition of the slot, the requested one last: stock reports the stick stopped on the first success. */
 struct xz_ur_mount m[XZ_UR_MOUNTS];
 int n = e->mounts(e->ctx, slot, m, XZ_UR_MOUNTS);
 if (n < 0) n = 0;
 e->sync(e->ctx);
 uint32_t deadline = now + XZ_UR_RETRY_MS;
 int failed = 0;
 for (int i = 0; i < n; i++) {
  if (!strcmp(m[i].point, target) || ours(s, m[i].point)) continue;
  if (unmount_one(s, e, m[i].point, now, deadline)) failed = 1;
 }
 int rc = unmount_one(s, e, target, now, deadline);
 int left = e->mounts(e->ctx, slot, m, XZ_UR_MOUNTS);
 s->clean = left == 0 || (left < 0 && !failed && !rc);
 if (rc) return rc;
 if (!s->clean) say(e, "USB_RELEASE: %s unmounted but %d other partition(s) of the stick are still mounted", target, left);
 return s->clean ? 0 : -EBUSY;
}

#ifndef XZ_USB_RELEASE_NO_RUNTIME
/* ---- inside the player ---- */
#include "runtime.h"
#include "xz_mutex.h"
#include "clock_compat.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern void xz_shell_runtime_usb_stopping(int slot) __attribute__((weak));

static pthread_mutex_t hide_lock = XZ_MUTEX_INITIALIZER;
static struct xz_ur_hidden_set hidden_set;
static pthread_mutex_t wrap_lock = XZ_MUTEX_INITIALIZER;
static struct xz_ur_wrapper wrapper;
static struct xz_usb_release_counts totals;

static uint32_t mono_ms(void) {
 struct timespec t;
 if (xz_clock_gettime_compat(CLOCK_MONOTONIC, &t)) return 0;
 return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}
static void count(uint32_t *field) { __atomic_add_fetch(field, 1, __ATOMIC_RELAXED); }
static int read_text(const char *path, char *buffer, size_t cap) {
 int fd = open(path, O_RDONLY | O_CLOEXEC);
 if (fd < 0) return -1;
 size_t used = 0;
 while (used + 1 < cap) {
  ssize_t n = read(fd, buffer + used, cap - 1 - used);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) break;
  used += (size_t)n;
 }
 close(fd);
 buffer[used] = 0;
 return (int)used;
}
/* /proc/self/mountinfo, or /proc/mounts where a kernel shows an empty mountinfo. */
static int mounts_now(int slot, struct xz_ur_mount *out, int cap) {
 char text[16384];
 int n = read_text("/proc/self/mountinfo", text, sizeof text);
 if (n > 0 && strstr(text, " - ")) return xz_ur_parse_mounts(text, (size_t)n, slot, out, cap);
 n = read_text("/proc/mounts", text, sizeof text);
 return n < 0 ? -1 : xz_ur_parse_mounts(text, (size_t)n, slot, out, cap);
}
static void log_line(void *ctx, const char *line) { (void)ctx; xz_log(line); }
static int raw_umount2(void *ctx, const char *target, int flags) {
 (void)ctx;
 return syscall(SYS_umount2, target, flags) ? -errno : 0;
}
static int env_mounts(void *ctx, int slot, struct xz_ur_mount *out, int cap) { (void)ctx; return mounts_now(slot, out, cap); }
static uint32_t env_now(void *ctx) { (void)ctx; return mono_ms(); }
static void env_sleep(void *ctx, unsigned ms) {
 (void)ctx;
 struct timespec t = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
 while (nanosleep(&t, &t) && errno == EINTR) {}
}
static void env_sync(void *ctx) { (void)ctx; sync(); }
/* The file a loop device reads, from sysfs or (a loop-AES driver without the sysfs node) its status. */
static int loop_backing(int index, char *out, size_t cap) {
 char path[64];
 snprintf(path, sizeof path, "/sys/block/loop%d/loop/backing_file", index);
 int n = read_text(path, out, cap);
 if (n > 0) { out[strcspn(out, "\n")] = 0; return 0; }
 snprintf(path, sizeof path, "/dev/loop%d", index);
 int fd = open(path, O_RDONLY | O_CLOEXEC);
 if (fd < 0) return -1;
 struct loop_info64 info;
 memset(&info, 0, sizeof info);
 int rc = ioctl(fd, LOOP_GET_STATUS64, &info);
 close(fd);
 if (rc) return -1;
 info.lo_file_name[sizeof info.lo_file_name - 1] = 0;
 snprintf(out, cap, "%s", (const char *)info.lo_file_name);
 return 0;
}
/* A loop device whose file lies on the stick cannot outlive it: unmount what is mounted from it (never by
   force) and unbind it. The stock decryptor's /mnt/iso of autoexec.bin is the one met in practice. */
static int env_release_loops(void *ctx, const char *point) {
 (void)ctx;
 int released = 0;
 for (int k = 0; k < 16; k++) {
  char backing[512];
  if (loop_backing(k, backing, sizeof backing) || !xz_ur_under(backing, point)) continue;
  struct xz_ur_mount all[32];
  int n = mounts_now(0, all, 32), stuck = 0;
  for (int i = 0; i < n; i++) {
   if (xz_ur_loop_index(all[i].source) != k) continue;
   int rc = raw_umount2(NULL, all[i].point, 0);
   char line[320];
   snprintf(line, sizeof line, "USB_RELEASE: loop%d (%.120s) mounted at %.100s: unmount %.40s", k, backing, all[i].point,
            rc ? strerror(-rc) : "ok");
   xz_log(line);
   if (rc) stuck = 1;
  }
  if (stuck) continue;
  char device[32];
  snprintf(device, sizeof device, "/dev/loop%d", k);
  int fd = open(device, O_RDONLY | O_CLOEXEC);
  int rc = fd < 0 || ioctl(fd, LOOP_CLR_FD, 0) ? -errno : 0;
  if (fd >= 0) close(fd);
  char line[320];
  snprintf(line, sizeof line, "USB_RELEASE: loop%d (%.200s) unbound: %.60s", k, backing, rc ? strerror(-rc) : "ok");
  xz_log(line);
  if (!rc) { released++; count(&totals.loops); }
 }
 return released;
}
/* What of the player still names the stick: its descriptors (never its memory map, which is large). */
static void env_holders(void *ctx, const char *point) {
 (void)ctx;
 DIR *dir = opendir("/proc/self/fd");
 if (!dir) return;
 int shown = 0, total = 0;
 struct dirent *entry;
 while ((entry = readdir(dir))) {
  char link[300], target[512];
  if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
  snprintf(link, sizeof link, "/proc/self/fd/%s", entry->d_name);
  ssize_t n = readlink(link, target, sizeof target - 1);
  if (n <= 0) continue;
  target[n] = 0;
  if (!xz_ur_under(target, point)) continue;
  total++;
  if (shown++ < 12) {
   char line[640];
   snprintf(line, sizeof line, "USB_RELEASE: held by the player: fd %.16s -> %.500s", entry->d_name, target);
   xz_log(line);
  }
 }
 closedir(dir);
 char line[320];
 snprintf(line, sizeof line, "USB_RELEASE: %s: %d player descriptor(s) on it", point, total);
 xz_log(line);
}
static void env_begin(void *ctx, int slot) { (void)ctx; xz_usb_release_begin(slot); }
static void env_gone(void *ctx, const char *point) {
 (void)ctx;
 pthread_mutex_lock(&hide_lock);
 xz_ur_hide_clear(&hidden_set, point);
 pthread_mutex_unlock(&hide_lock);
 count(&totals.unmounted);
}
/* DECK 3/4 and the stem streams (usb_release.h, holders). Registered at start-up under board_lock; a stop asks
   them under it too. Answers are atomics only: they come from the holders' worker threads. */
static pthread_mutex_t board_lock = XZ_MUTEX_INITIALIZER;
static struct xz_ur_board board;
static void env_settle(void *ctx, int slot) { xz_ur_board_settle(&board, ctx, slot); }
static const struct xz_ur_env player_env = {(void *)&player_env, raw_umount2, env_mounts, env_now, env_sleep, env_sync,
                                            env_release_loops, env_holders, log_line, env_begin, env_gone, env_settle};

int xz_usb_release_holder(const char *name, xz_ur_ask ask, void *ctx) {
 pthread_mutex_lock(&board_lock);
 int id = xz_ur_board_add(&board, name, ask, ctx);
 pthread_mutex_unlock(&board_lock);
 return id;
}
void xz_usb_release_answer(int holder, int slot, uint32_t stop) { xz_ur_board_answer(&board, holder, slot, stop); }

void xz_usb_release_begin(int slot) {
 if (slot != 1 && slot != 2) return;
 struct xz_ur_mount m[XZ_UR_MOUNTS];
 int n = mounts_now(slot, m, XZ_UR_MOUNTS);
 if (n < 0) n = 0;
 uint32_t now = mono_ms();
 pthread_mutex_lock(&hide_lock);
 /* The same stop seen again (stock's stop and then its unmount both begin it): every mount is hidden already. */
 int seen = n > 0;
 for (int i = 0; i < n && seen; i++) seen = xz_ur_hidden(&hidden_set, now, m[i].point, m[i].id, m[i].major, m[i].minor);
 xz_ur_hide_add(&hidden_set, m, n, now);
 pthread_mutex_unlock(&hide_lock);
 count(&totals.begun);
 char line[160];
 snprintf(line, sizeof line, "USB_RELEASE: USB STOP on slot %d: %d partition(s) hidden from the mods", slot, n);
 xz_log(line);
 if (xz_shell_runtime_usb_stopping) xz_shell_runtime_usb_stopping(slot);
 /* DECK 3/4 and the stem streams let go of the slot on their own threads; the unmount waits for their answers. A
    pulled stick may have no mount left to compare: it is asked again unless asked moments ago. */
 pthread_mutex_lock(&board_lock);
 int recent = !n && board.slot[slot - 1].stop && now - board.slot[slot - 1].at_ms < 2000u;
 if (!seen && !recent) xz_ur_board_ask(&board, slot, now);
 pthread_mutex_unlock(&board_lock);
}

int xz_usb_release_hidden(const char *point, unsigned long mount_id, unsigned long long dev) {
 /* glibc's dev_t layout: major bits 8..19 and 32.., minor bits 0..7 and 12..31. */
 unsigned major = (unsigned)(((dev >> 8) & 0xfffu) | ((dev >> 32) & ~0xfffu));
 unsigned minor = (unsigned)((dev & 0xffu) | ((dev >> 12) & ~0xffu));
 pthread_mutex_lock(&hide_lock);
 int hidden = xz_ur_hidden(&hidden_set, mono_ms(), point, mount_id, major, minor);
 pthread_mutex_unlock(&hide_lock);
 return hidden;
}

void xz_usb_release_counts(struct xz_usb_release_counts *out) {
 if (!out) return;
 out->begun = __atomic_load_n(&totals.begun, __ATOMIC_RELAXED);
 out->unmounted = __atomic_load_n(&totals.unmounted, __ATOMIC_RELAXED);
 out->busy = __atomic_load_n(&totals.busy, __ATOMIC_RELAXED);
 out->loops = __atomic_load_n(&totals.loops, __ATOMIC_RELAXED);
 out->detached = __atomic_load_n(&totals.detached, __ATOMIC_RELAXED);
}

static int enabled(void) {
 const char *value = getenv("XZ_MODS_USB_RELEASE");
 return !value || strcmp(value, "0");
}

/* rbp calls umount/umount2 through its PLT; the preloaded runtime's definitions come first. */
__attribute__((visibility("default"))) int umount2(const char *target, int flags) {
 int rc;
 if (!enabled()) rc = raw_umount2(NULL, target, flags);
 else {
  pthread_mutex_lock(&wrap_lock);
  rc = xz_ur_unmount(&wrapper, &player_env, target, flags);
  pthread_mutex_unlock(&wrap_lock);
  if (rc == -EBUSY && xz_ur_slot_of(target)) count(&totals.busy);
  if (!rc && (flags & MNT_DETACH) && xz_ur_slot_of(target)) count(&totals.detached);
 }
 if (rc) { errno = -rc; return -1; }
 return 0;
}
__attribute__((visibility("default"))) int umount(const char *target) { return umount2(target, 0); }
#endif
