/* Host test for usb_release.c's core: mount parsing, the hidden set, and the unmount policy stock's USB STOP
   meets, over a fake kernel with partitions that stay busy, loop devices on the stick and lazy unmounts. */
#define _GNU_SOURCE
#undef NDEBUG
#include "../usb_release.h"
#include "../xz_cloexec.h"
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

/* ---- a fake kernel ---- */
struct part { const char *point; int mounted; uint32_t busy_until; int loop_holds; int busy_forever; };
static struct fake {
 struct part part[4]; int nparts;
 uint32_t clock; unsigned sleeps, syncs, holders, begins, loops_released, gones, settles;
 int loop_bound; const char *loop_on; int unreadable;
 char calls[256][96]; int ncalls; int flags[256];
 char log[64][320]; int nlog;
} f;
static void reset(void) { memset(&f, 0, sizeof f); }
static struct part *find(const char *point) {
 for (int i = 0; i < f.nparts; i++) if (!strcmp(f.part[i].point, point)) return &f.part[i];
 return NULL;
}
static int k_umount2(void *c, const char *target, int flags) {
 (void)c;
 assert(f.ncalls < 256);
 snprintf(f.calls[f.ncalls], sizeof f.calls[0], "%s", target ? target : "(null)");
 f.flags[f.ncalls++] = flags;
 struct part *p = target ? find(target) : NULL;
 if (!p || !p->mounted) return -EINVAL;
 if (flags & MNT_DETACH) { p->mounted = 0; return 0; }
 if (p->busy_forever || (p->loop_holds && f.loop_bound) || (int32_t)(f.clock - p->busy_until) < 0) return -EBUSY;
 p->mounted = 0;
 return 0;
}
static int k_mounts(void *c, int slot, struct xz_ur_mount *out, int cap) {
 (void)c;
 if (f.unreadable) return -1;
 int n = 0;
 for (int i = 0; i < f.nparts && n < cap; i++) {
  if (!f.part[i].mounted || xz_ur_slot_of(f.part[i].point) != slot) continue;
  memset(&out[n], 0, sizeof out[n]);
  snprintf(out[n].point, sizeof out[n].point, "%s", f.part[i].point);
  n++;
 }
 return n;
}
static uint32_t k_now(void *c) { (void)c; return f.clock; }
static void k_sleep(void *c, unsigned ms) { (void)c; f.sleeps++; f.clock += ms; }
static void k_sync(void *c) { (void)c; f.syncs++; }
static int k_loops(void *c, const char *point) {
 (void)c;
 if (f.loop_bound && f.loop_on && xz_ur_under(f.loop_on, point)) { f.loop_bound = 0; f.loops_released++; return 1; }
 return 0;
}
static void k_holders(void *c, const char *point) { (void)c; (void)point; f.holders++; }
static void k_log(void *c, const char *line) {
 (void)c;
 if (f.nlog < 64) snprintf(f.log[f.nlog++], sizeof f.log[0], "%s", line);
}
static void k_begin(void *c, int slot) { (void)c; assert(slot == 1 || slot == 2); f.begins++; }
static void k_gone(void *c, const char *point) { (void)c; (void)point; f.gones++; }
/* The holders' board the wrapper waits for, when a test sets it. */
static struct xz_ur_board board;
static const struct xz_ur_board *settle_board;
static void k_settle(void *c, int slot) {
 f.settles++;
 if (settle_board) xz_ur_board_settle(settle_board, c, slot);
}
static const struct xz_ur_env env = {(void *)&env, k_umount2, k_mounts, k_now, k_sleep, k_sync, k_loops, k_holders, k_log, k_begin, k_gone,
                                     k_settle};
static int logged(const char *needle) {
 for (int i = 0; i < f.nlog; i++) if (strstr(f.log[i], needle)) return 1;
 return 0;
}

static void test_names(void) {
 assert(xz_ur_slot_of("/media/usb1/sda1") == 1);
 assert(xz_ur_slot_of("/media/usb2/sdb2") == 2);
 assert(xz_ur_slot_of("/media/usb2/mtp") == 2);
 assert(!xz_ur_slot_of("/media/usb2"));
 assert(!xz_ur_slot_of("/media/usb2/"));
 assert(!xz_ur_slot_of("/media/usb3/sda1"));
 assert(!xz_ur_slot_of("/media/usb1/sda1/PIONEER"));
 assert(!xz_ur_slot_of("/mnt/iso"));
 assert(!xz_ur_slot_of(NULL));
 assert(xz_ur_under("/media/usb2/sda2/autoexec.bin", "/media/usb2/sda2"));
 assert(xz_ur_under("/media/usb2/sda2", "/media/usb2/sda2/"));
 assert(!xz_ur_under("/media/usb2/sda22/x", "/media/usb2/sda2"));
 assert(xz_ur_loop_index("/dev/loop0") == 0 && xz_ur_loop_index("/dev/loop7") == 7 && xz_ur_loop_index("/dev/loop12") == 12);
 assert(xz_ur_loop_index("/dev/loop") == -1 && xz_ur_loop_index("/dev/sda1") == -1 && xz_ur_loop_index("/dev/loopX") == -1);
}

static void test_parse(void) {
 static const char info[] =
  "15 1 0:13 / / rw,relatime - ubifs ubi0:rootfs rw\n"
  "40 15 8:1 / /media/usb2/sda1 rw,relatime - vfat /dev/sda1 rw,flush\n"
  "41 15 0:22 / /media/usb2/sda2 rw,relatime - fuseblk /dev/sda2 rw,user_id=0\n"
  "42 15 7:0 / /mnt/iso ro,relatime - iso9660 /dev/loop0 ro\n"
  "43 15 8:17 / /media/usb1/sdb1 rw shared:4 - vfat /dev/sdb1 rw\n"
  "44 15 8:18 / /media/usb2/sda3";  /* cut off: not a record */
 struct xz_ur_mount m[8];
 int n = xz_ur_parse_mounts(info, strlen(info), 2, m, 8);
 assert(n == 2);
 assert(m[0].id == 40 && m[0].major == 8 && m[0].minor == 1 && !strcmp(m[0].point, "/media/usb2/sda1") && !strcmp(m[0].source, "/dev/sda1"));
 assert(m[1].id == 41 && m[1].major == 0 && m[1].minor == 22 && !strcmp(m[1].source, "/dev/sda2"));
 n = xz_ur_parse_mounts(info, strlen(info), 1, m, 8);
 assert(n == 1 && m[0].id == 43 && !strcmp(m[0].point, "/media/usb1/sdb1"));
 n = xz_ur_parse_mounts(info, strlen(info), 0, m, 8);
 assert(n == 5 && xz_ur_loop_index(m[3].source) == 0 && !strcmp(m[3].point, "/mnt/iso"));
 assert(xz_ur_parse_mounts(info, strlen(info), 0, m, 2) == 2);
 static const char mounts[] =
  "/dev/sda1 /media/usb2/sda1 vfat rw,flush 0 0\n"
  "/dev/sda2 /media/usb2/sda2 fuseblk rw,relatime,user_id=0 0 0\n"
  "/dev/loop0 /mnt/iso iso9660 ro 0 0\n";
 n = xz_ur_parse_mounts(mounts, strlen(mounts), 2, m, 8);
 assert(n == 2 && !m[0].id && !m[0].major && !strcmp(m[1].point, "/media/usb2/sda2") && !strcmp(m[1].source, "/dev/sda2"));
}

static void test_hidden(void) {
 struct xz_ur_hidden_set set;
 memset(&set, 0, sizeof set);
 struct xz_ur_mount m[2];
 memset(m, 0, sizeof m);
 snprintf(m[0].point, sizeof m[0].point, "/media/usb2/sda1"); m[0].id = 40; m[0].major = 8; m[0].minor = 1;
 snprintf(m[1].point, sizeof m[1].point, "/media/usb2/sda2"); m[1].id = 41; m[1].minor = 22;
 xz_ur_hide_add(&set, m, 2, 1000);
 assert(xz_ur_hidden(&set, 1000, "/media/usb2/sda1", 40, 8, 1));
 assert(xz_ur_hidden(&set, 1000, "/media/usb2/sda2", 41, 0, 22));
 assert(xz_ur_hidden(&set, 1000, "/media/usb2/sda2", 0, 0, 0));      /* nothing known: the path decides */
 assert(xz_ur_hidden(&set, 1000, "/media/usb2/sda2", 1, 0, 22));     /* /proc/mounts fallback id */
 assert(!xz_ur_hidden(&set, 1000, "/media/usb2/sda2", 57, 0, 22));   /* a new mount at the same place */
 assert(!xz_ur_hidden(&set, 1000, "/media/usb2/sda1", 40, 8, 17));   /* another device */
 assert(!xz_ur_hidden(&set, 1000, "/media/usb1/sda1", 40, 8, 1));
 assert(xz_ur_hidden(&set, 1000 + XZ_UR_HIDE_MS, "/media/usb2/sda1", 40, 8, 1));
 assert(!xz_ur_hidden(&set, 1001 + XZ_UR_HIDE_MS, "/media/usb2/sda1", 40, 8, 1)); /* never hidden for good */
 xz_ur_hide_clear(&set, "/media/usb2/sda1");
 assert(!xz_ur_hidden(&set, 1000, "/media/usb2/sda1", 40, 8, 1));
 assert(xz_ur_hidden(&set, 1000, "/media/usb2/sda2", 41, 0, 22));
 m[1].id = 58;
 xz_ur_hide_add(&set, &m[1], 1, 5000); /* the same place again replaces its entry */
 assert(xz_ur_hidden(&set, 5000, "/media/usb2/sda2", 58, 0, 22) && !xz_ur_hidden(&set, 5000, "/media/usb2/sda2", 41, 0, 22));
 struct xz_ur_mount many[XZ_UR_MOUNTS + 2];
 memset(many, 0, sizeof many);
 for (int i = 0; i < XZ_UR_MOUNTS + 2; i++) snprintf(many[i].point, sizeof many[i].point, "/media/usb1/sd%c1", 'a' + i);
 xz_ur_hide_add(&set, many, XZ_UR_MOUNTS + 2, 6000); /* bounded: no overflow */
}

static void two_partitions(void) {
 reset();
 f.nparts = 3;
 f.part[0] = (struct part){"/media/usb2/sda1", 1, 0, 0, 0};
 f.part[1] = (struct part){"/media/usb2/sda2", 1, 0, 0, 0};
 f.part[2] = (struct part){"/media/usb1/sdb1", 1, 0, 0, 0};
 f.clock = 1000;
}

static void test_pass_through(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 f.nparts = 4; f.part[3] = (struct part){"/mnt/iso", 1, 0, 0, 1};
 assert(xz_ur_unmount(&w, &env, "/mnt/iso", 0) == -EBUSY);           /* not a stick: the kernel's answer */
 assert(xz_ur_unmount(&w, &env, "/mnt/iso", MNT_DETACH) == 0);
 assert(f.ncalls == 2 && !f.begins && !f.syncs && !f.sleeps);
}

static void test_clean_stop(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 /* Stock's first request names sda1; the whole stick goes, sda1 last, before stock hears "unmounted". */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", 0) == 0);
 assert(f.begins == 1 && f.syncs == 1 && !f.sleeps);
 assert(f.ncalls == 2 && !strcmp(f.calls[0], "/media/usb2/sda2") && !strcmp(f.calls[1], "/media/usb2/sda1"));
 assert(!f.part[0].mounted && !f.part[1].mounted && f.part[2].mounted); /* the other slot is untouched */
 assert(f.gones == 2);
 /* Stock's queued request for sda2 finds it already gone: success, no kernel call. */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda2", 0) == 0);
 assert(f.ncalls == 2 && f.begins == 1);
 /* The same for its ten-second lazy request. */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda2", MNT_DETACH) == 0 && f.ncalls == 2);
 /* A stop a minute later is a new stop. */
 f.clock += XZ_UR_HIDE_MS + 1;
 f.part[0].mounted = 1;
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", 0) == 0 && f.begins == 2);
}

static void test_busy_then_free(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 f.part[1].busy_until = 1000 + 1250; /* the shell drops export.pdb 1.25 s later */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", 0) == 0);
 assert(!f.part[0].mounted && !f.part[1].mounted);
 assert(f.sleeps >= 12 && f.sleeps <= 14 && f.holders == 1);
 assert(logged("unmounted /media/usb2/sda2 attempts=") && logged("unmounted /media/usb2/sda1 attempts=1"));
}

static void test_busy_forever(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 f.part[1].busy_forever = 1;
 uint32_t start = f.clock;
 /* sda1 goes, sda2 cannot: stock must not hear "unmounted". */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", 0) == -EBUSY);
 assert(!f.part[0].mounted && f.part[1].mounted);
 assert(f.clock - start >= XZ_UR_RETRY_MS && f.clock - start <= XZ_UR_RETRY_MS + XZ_UR_STEP_MS);
 assert(f.holders == 2 && logged("still busy"));
 /* stock's queued request for sda2: another bounded wait, still busy. */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda2", 0) == -EBUSY);
 /* stock's lazy fallback ten seconds on: sda1 is already gone, sda2 is detached after the loop check and a sync. */
 unsigned syncs = f.syncs;
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", MNT_DETACH) == 0);
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda2", MNT_DETACH) == 0);
 assert(!f.part[1].mounted && f.syncs == syncs + 1 && (f.flags[f.ncalls - 1] & MNT_DETACH));
 assert(f.begins == 1);
}

static void test_loop_release(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 /* The leaked /mnt/iso: a loop device reads autoexec.bin from sda2. Released at the first busy answer. */
 f.part[1].loop_holds = 1; f.loop_bound = 1; f.loop_on = "/media/usb2/sda2/autoexec.bin";
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda2", 0) == 0);
 assert(f.loops_released == 1 && !f.sleeps && !f.holders && !f.part[1].mounted && !f.part[0].mounted);
 assert(logged("released 1 loop device"));
}

static void test_unreadable_table(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 f.unreadable = 1;
 /* Without the mount table only the named partition is handled; its success is reported. */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", 0) == 0);
 assert(f.ncalls == 1 && f.part[1].mounted);
 /* A request for a partition that is not mounted is not an error for a stop. */
 assert(xz_ur_unmount(&w, &env, "/media/usb1/sdc1", 0) == 0);
}

/* ---- holders: DECK 3/4 and the stem streams let go on their own threads ---- */
static int answer_inside; /* DECK 3/4 answers once inside its ask, before the board has added its count */
static uint32_t asked_stop[2];
static int asked_slot[2];
static unsigned ask_decks(void *c, int slot, uint32_t stop) {
 (void)c; asked_slot[0] = slot; asked_stop[0] = stop;
 if (answer_inside) xz_ur_board_answer(&board, 0, slot, stop);
 return 2;
}
static unsigned ask_stems(void *c, int slot, uint32_t stop) { (void)c; asked_slot[1] = slot; asked_stop[1] = stop; return 1; }

static void test_holders(void) {
 reset();
 memset(&board, 0, sizeof board);
 assert(xz_ur_board_add(&board, "DECK 3/4", ask_decks, NULL) == 0 && xz_ur_board_add(&board, "stems", ask_stems, NULL) == 1);
 for (int i = 2; i < XZ_UR_HOLDERS; i++) assert(xz_ur_board_add(&board, "spare", ask_stems, NULL) == i);
 assert(xz_ur_board_add(&board, "one too many", ask_stems, NULL) == -1 && xz_ur_board_add(&board, "none", NULL, NULL) == -1);
 board.holders = 2;
 /* Nothing asked yet: nothing to wait for. */
 assert(xz_ur_board_settle(&board, &env, 2) == 0 && !f.sleeps && !f.nlog);
 f.clock = 5000; answer_inside = 1;
 uint32_t stop = xz_ur_board_ask(&board, 2, f.clock);
 assert(stop && asked_stop[0] == stop && asked_slot[0] == 2 && asked_stop[1] == stop && asked_slot[1] == 2);
 /* The early answer counted: one of two decks still to answer, the one stream too, nothing for slot 1. */
 assert(xz_ur_board_waiting(&board, 2, 0) == 1 && xz_ur_board_waiting(&board, 2, 1) == 1 && !xz_ur_board_waiting(&board, 1, 0));
 xz_ur_board_answer(&board, 0, 2, stop + 7); /* another stop's answer */
 xz_ur_board_answer(&board, 0, 1, stop);     /* another slot */
 assert(xz_ur_board_waiting(&board, 2, 0) == 1);
 xz_ur_board_answer(&board, 0, 2, stop);
 assert(!xz_ur_board_waiting(&board, 2, 0));
 /* The stream never answers: the wait ends XZ_UR_ANSWER_MS after the stop began, not after the wait began. */
 f.clock = 5200;
 assert(xz_ur_board_settle(&board, &env, 2) == 1);
 assert(f.clock - 5000 >= XZ_UR_ANSWER_MS && f.clock - 5000 < XZ_UR_ANSWER_MS + XZ_UR_ANSWER_STEP_MS && f.sleeps);
 assert(logged("USB_RELEASE: slot 2: stems: 1 of 1 part(s) did not let go within 1000 ms; the unmount goes on"));
 assert(logged("USB_RELEASE: slot 2: DECK 3/4: 2 part(s) answered"));
 /* A new stop where everyone answers: no wait at all, and a late answer to the old stop changes nothing. */
 answer_inside = 0; f.nlog = 0;
 unsigned sleeps = f.sleeps;
 uint32_t next = xz_ur_board_ask(&board, 2, f.clock);
 assert(next != stop && xz_ur_board_waiting(&board, 2, 0) == 2 && xz_ur_board_waiting(&board, 2, 1) == 1);
 xz_ur_board_answer(&board, 1, 2, stop);
 assert(xz_ur_board_waiting(&board, 2, 1) == 1);
 xz_ur_board_answer(&board, 0, 2, next); xz_ur_board_answer(&board, 1, 2, next); xz_ur_board_answer(&board, 0, 2, next);
 assert(xz_ur_board_settle(&board, &env, 2) == 0 && f.sleeps == sleeps && !logged("did not let go"));
}

/* The wrapper waits for the holders once per stop, before its first unmount, and the busy retry after it still gets
   its full XZ_UR_RETRY_MS: a partition that frees 2.5 s after the wait still unmounts. */
static void test_unmount_waits_for_holders(void) {
 struct xz_ur_wrapper w;
 memset(&w, 0, sizeof w);
 two_partitions();
 memset(&board, 0, sizeof board);
 assert(xz_ur_board_add(&board, "DECK 3/4", ask_decks, NULL) == 0 && xz_ur_board_add(&board, "stems", ask_stems, NULL) == 1);
 answer_inside = 0;
 uint32_t stop = xz_ur_board_ask(&board, 2, f.clock); /* xz_usb_release_begin, at stock's request_usb_stop */
 xz_ur_board_answer(&board, 0, 2, stop); xz_ur_board_answer(&board, 0, 2, stop); /* both decks; the stream is stuck */
 f.part[1].busy_until = f.clock + XZ_UR_ANSWER_MS + 2500;
 settle_board = &board;
 uint32_t start = f.clock;
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda1", 0) == 0);
 assert(f.settles == 1 && !f.part[0].mounted && !f.part[1].mounted);
 assert(f.clock - start >= XZ_UR_ANSWER_MS + 2500 && f.clock - start < XZ_UR_ANSWER_MS + XZ_UR_RETRY_MS);
 assert(logged("stems: 1 of 1 part(s) did not let go"));
 /* Stock's queued request for the other partition: no second wait. */
 assert(xz_ur_unmount(&w, &env, "/media/usb2/sda2", 0) == 0 && f.settles == 1);
 settle_board = NULL;
}

/* The stick files that stay open are close-on-exec on glibc (xz_cloexec.h). */
static void test_cloexec(void) {
#ifdef __GLIBC__
 assert(!strcmp(XZ_FOPEN_READ_CLOEXEC, "rbe"));
 FILE *file = fopen("/dev/null", XZ_FOPEN_READ_CLOEXEC); /* not /proc/self: qemu-user answers those itself */
 assert(file && (fcntl(fileno(file), F_GETFD) & FD_CLOEXEC));
 fclose(file);
#endif
}

int main(void) {
 test_cloexec();
 test_names();
 test_parse();
 test_hidden();
 test_pass_through();
 test_clean_stop();
 test_busy_then_free();
 test_busy_forever();
 test_loop_release();
 test_unreadable_table();
 test_holders();
 test_unmount_waits_for_holders();
 puts("test_usb_release: all passed");
 return 0;
}
