/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef XZ_EXTRA_THREAD_PLACEMENT_H
#define XZ_EXTRA_THREAD_PLACEMENT_H
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static inline int xz_place_field(const char *line, const char *key, int base, unsigned long *value) {
 size_t length = strlen(key);
 if (strncmp(line, key, length)) return 0;
 const char *p = line + length;
 while (*p == ' ' || *p == '\t') ++p;
 if (*p++ != ':') return 0;
 while (*p == ' ' || *p == '\t') ++p;
 char *end; errno = 0; unsigned long parsed = strtoul(p, &end, base);
 if (errno || end == p || (*end && *end != '\n' && *end != ' ' && *end != '\t')) return 0;
 *value = parsed; return 1;
}
static inline int xz_place_cpu_supported(const char *text) {
 unsigned long implementer = 0, architecture = 0, part = 0;
 for (const char *line = text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
  xz_place_field(line, "CPU implementer", 16, &implementer);
  xz_place_field(line, "CPU architecture", 10, &architecture);
  xz_place_field(line, "CPU part", 16, &part);
 }
 return implementer == 0x41 && architecture == 7 && part == 0xc09;
}

/* Self-only operations. There is deliberately no PID/TID parameter to set(). */
struct xz_place_ops { int (*gate)(void); int (*get)(unsigned long *); int (*set)(unsigned long); };
static inline int xz_place_apply(const struct xz_place_ops *ops, unsigned long *before, unsigned long *after) {
 *before = *after = 0;
 if (ops->get(before)) return -1;
 *after = *before;
 if (!ops->gate()) { errno = ENOTSUP; return -1; }
 if (ops->set(1ul << 2)) return -1;
 int read_failed = ops->get(after);
 if (!read_failed && *after == (1ul << 2)) return 0;
 int error = read_failed ? errno : EIO;
 if (ops->set(*before) || ops->get(after) || *after != *before) { errno = EIO; return -2; }
 errno = error; return -1;
}

#if defined(XZ_PLACEMENT_CONNECTOR_TEST)
void xz_extra_thread_place(const char *role);
#elif defined(__linux__) && defined(__arm__)
#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
static int xz_place_read(const char *path, char *text, size_t capacity) {
 int fd = open(path, O_RDONLY | O_CLOEXEC);
 if (fd < 0) return 0;
 ssize_t n = read(fd, text, capacity - 1); close(fd);
 if (n <= 0) return 0;
 text[n] = 0; return 1;
}
static int xz_place_gate(void) {
 char online[32], cpu2[8], cpuinfo[8192];
 /* Native firmware identity is checked before extra_decks_start in runtime.c.
  * The extra gate excludes host/QEMU-user tests and unknown CPU topology. */
 return sched_getscheduler(0) == SCHED_OTHER &&
  xz_place_read("/sys/devices/system/cpu/online", online, sizeof online) && !strcmp(online, "0-3\n") &&
  xz_place_read("/sys/devices/system/cpu/cpu2/online", cpu2, sizeof cpu2) && !strcmp(cpu2, "1\n") &&
  xz_place_read("/proc/cpuinfo", cpuinfo, sizeof cpuinfo) &&
  xz_place_cpu_supported(cpuinfo);
}
static int xz_place_get(unsigned long *mask) { return syscall(SYS_sched_getaffinity, 0, sizeof(*mask), mask) < 0 ? -1 : 0; }
static int xz_place_set(unsigned long mask) { return (int)syscall(SYS_sched_setaffinity, 0, sizeof(mask), &mask); }
static void xz_extra_thread_place(const char *role) {
 const struct xz_place_ops ops = {xz_place_gate, xz_place_get, xz_place_set};
 unsigned long before, after;
 int result = xz_place_apply(&ops, &before, &after), error = result ? errno : 0;
 int policy = sched_getscheduler(0); errno = 0; int nice_value = getpriority(PRIO_PROCESS, 0);
 char text[256];
 snprintf(text, sizeof text, "DECK 3/4 placement role=%s tid=%ld before=0x%lx after=0x%lx policy=%d nice=%d result=%d errno=%d", role, (long)syscall(SYS_gettid), before, after, policy, nice_value, result, error);
 xz_log(text);
}
#else
static void xz_extra_thread_place(const char *role) {
 char text[160]; snprintf(text, sizeof text, "DECK 3/4 placement role=%s unsupported platform; inherited affinity/policy/nice retained", role); xz_log(text);
}
#endif
#endif
