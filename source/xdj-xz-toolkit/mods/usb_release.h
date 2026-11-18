/* SPDX-License-Identifier: MIT */
#ifndef XZ_USB_RELEASE_H
#define XZ_USB_RELEASE_H
/* USB STOP with the mods running: let go of the stick before the stock player unmounts it.

   Stock (firmware 1.26, evidence in stick-removal/REPORT.md): holding USB STOP (key 0x8002, 1500 ms) runs
   UsbStorageManager::request_usb_stop, which unloads the decks, stops a recording and detaches the database; then
   UsbMountManager::umount(slot, 0) writes "do_umount0 <mount>" for every partition of the slot to
   /proc/udev_usbN, and the manager's own thread calls umount(<mount>) (read_sf_rbp 0x2dfdc0). A refused umount is
   silently dropped; ten seconds later stock asks again with "do_umount1", which is umount2(<mount>, MNT_DETACH).
   One partition that unmounts is enough for stock to report the stick stopped.

   This module, inside the player:
   - xz_usb_release_begin(slot), called from the stock stop (ram/removal.c), hides the slot's mounts from the
     shell (shell_runtime.c current_media), so the shell drops its export.pdb readers and refuses new loads from
     that stick, and wakes the shell's worker.
   - umount() and umount2() are interposed (the runtime is LD_PRELOADed into rbp). For a mount under
     /media/usb1 or /media/usb2: sync, then unmount every partition of that slot, retrying a busy one for up to
     XZ_UR_RETRY_MS while the mods let go; loop devices whose backing file lies on the stick (the stock
     decryptor's /mnt/iso of autoexec.bin) are released; what still holds the stick is written to the mods log.
     Stock is told "unmounted" only when every partition of the slot is gone. Any other path, and every
     MNT_DETACH, goes straight to the kernel after the loop release and a sync.
   - The holders that keep files open for as long as a track is loaded (DECK 3/4, extra_decks.c; the stem streams
     of decks 1/2, audio/runtime.c) are asked at xz_usb_release_begin to let go on their own threads; the first
     unmount of the stop waits up to XZ_UR_ANSWER_MS for their answers, then goes on and logs who did not answer.
   Nothing here reads player memory, so it is the same on any firmware. */
#include <stddef.h>
#include <stdint.h>

#define XZ_UR_MOUNTS 8
#define XZ_UR_POINT 192
#define XZ_UR_RETRY_MS 3000u /* per stock request; stock's lazy fallback comes at 10 s */
#define XZ_UR_STEP_MS 100u
#define XZ_UR_HIDE_MS 60000u /* a hidden mount that never went away is shown again after this */

struct xz_ur_mount { unsigned long id; unsigned major, minor; char point[XZ_UR_POINT]; char source[64]; };

/* 1 or 2 for "/media/usbN/<name>" (exactly one component below /media/usbN), else 0. */
int xz_ur_slot_of(const char *mount_point);
/* 1 when path is mount or lies below it. */
int xz_ur_under(const char *path, const char *mount);
/* Mount records from /proc/self/mountinfo text (or /proc/mounts text, then id/major/minor are 0). slot 1/2 keeps
   only mounts under /media/usbN; slot 0 keeps every record. Returns the count, at most cap. */
int xz_ur_parse_mounts(const char *text, size_t length, int slot, struct xz_ur_mount *out, int cap);
/* The loop device a source names ("/dev/loop3" -> 3), else -1. */
int xz_ur_loop_index(const char *source);

/* Mounts hidden from the shell while a stop runs. */
struct xz_ur_hide { struct xz_ur_mount mount; uint32_t at_ms; int used; };
struct xz_ur_hidden_set { struct xz_ur_hide entry[XZ_UR_MOUNTS]; };
void xz_ur_hide_add(struct xz_ur_hidden_set *, const struct xz_ur_mount *, int count, uint32_t now_ms);
void xz_ur_hide_clear(struct xz_ur_hidden_set *, const char *point);
/* 1 when this mount (point, and id/device when both sides know them) is hidden at now_ms. */
int xz_ur_hidden(const struct xz_ur_hidden_set *, uint32_t now_ms, const char *point, unsigned long id,
                 unsigned major, unsigned minor);

/* Holders: parts of the mods that keep files open on a stick for as long as a track is loaded (DECK 3/4 and the
   stem streams of decks 1/2). A stop asks each to let go of the slot; the holder posts the request to its own
   threads and says how many answers will follow, and each answer comes from the thread that let go. Before the
   first unmount of the stop, the unmount waits for the answers, at most XZ_UR_ANSWER_MS after the stop began, then
   goes on and logs who has not answered. */
#define XZ_UR_HOLDERS 4
#define XZ_UR_ANSWER_MS 1000u
#define XZ_UR_ANSWER_STEP_MS 10u
/* ask must not wait for anything: it runs on the thread that began the stop (stock's UI or mount thread). */
typedef unsigned (*xz_ur_ask)(void *ctx, int slot, uint32_t stop);
struct xz_ur_holder { const char *name; xz_ur_ask ask; void *ctx; };
struct xz_ur_asked { uint32_t stop, at_ms; unsigned asked[XZ_UR_HOLDERS]; int waiting[XZ_UR_HOLDERS]; };
struct xz_ur_board { struct xz_ur_holder holder[XZ_UR_HOLDERS]; int holders; uint32_t serial; struct xz_ur_asked slot[2]; };
/* Registers a holder once; its index, or -1 when the board is full. Not concurrent with ask. */
int xz_ur_board_add(struct xz_ur_board *, const char *name, xz_ur_ask ask, void *ctx);
/* A stop of slot 1 or 2 began: asks every holder. Returns the stop's number (never 0). Not concurrent with add. */
uint32_t xz_ur_board_ask(struct xz_ur_board *, int slot, uint32_t now_ms);
/* One answer from a holder's thread. Atomics only; an answer to an older stop of the slot is ignored. */
void xz_ur_board_answer(struct xz_ur_board *, int holder, int slot, uint32_t stop);
/* Answers still missing from one holder for the slot's latest stop (0 when it answered them all). */
int xz_ur_board_waiting(const struct xz_ur_board *, int slot, int holder);

/* The unmount policy over an injected environment, so it runs on a PC. Every callback is bounded. */
struct xz_ur_env;
/* Waits until every holder answered the slot's latest stop, or XZ_UR_ANSWER_MS after it began, through the
   environment's clock and sleep; logs one line per holder. Returns the holders that did not answer in time. */
int xz_ur_board_settle(const struct xz_ur_board *, const struct xz_ur_env *, int slot);
struct xz_ur_env {
 void *ctx;
 int (*umount2)(void *ctx, const char *target, int flags);              /* 0 or -errno */
 int (*mounts)(void *ctx, int slot, struct xz_ur_mount *out, int cap);  /* the slot's mounts now, -1 unreadable */
 uint32_t (*now_ms)(void *ctx);
 void (*sleep_ms)(void *ctx, unsigned ms);
 void (*sync)(void *ctx);
 int (*release_loops)(void *ctx, const char *point);                    /* loops released */
 void (*holders)(void *ctx, const char *point);                         /* writes what holds point */
 void (*log)(void *ctx, const char *line);
 void (*begin)(void *ctx, int slot);                                    /* xz_usb_release_begin */
 void (*gone)(void *ctx, const char *point);                            /* point unmounted: unhide it */
 void (*settle)(void *ctx, int slot);       /* waits for the holders' answers (xz_ur_board_settle); NULL: none */
};
/* One stop per slot, as the wrapper sees it. Only the wrapper's caller touches it. */
struct xz_ur_stop { uint32_t at_ms; int active, clean, count, settled; char ours[XZ_UR_MOUNTS][XZ_UR_POINT]; };
struct xz_ur_wrapper { struct xz_ur_stop stop[2]; };
/* Stock asks to unmount target with flags. Returns 0 or -errno, as the kernel would. */
int xz_ur_unmount(struct xz_ur_wrapper *, const struct xz_ur_env *, const char *target, int flags);

#ifndef XZ_USB_RELEASE_NO_RUNTIME
/* The stock stop began for slot 1 or 2 (ram/removal.c, UI thread). Idempotent. */
void xz_usb_release_begin(int slot);
/* For the shell's media probe (worker thread): 1 when this mount is being stopped. dev is the mount root's
   st_dev (0 unknown), mount_id from mountinfo (0 or 1 unknown). */
int xz_usb_release_hidden(const char *point, unsigned long mount_id, unsigned long long dev);
/* Counters for the bench and the check: stops begun, partitions unmounted, busy answers, loops released. */
struct xz_usb_release_counts { uint32_t begun, unmounted, busy, loops, detached; };
void xz_usb_release_counts(struct xz_usb_release_counts *out);
/* Holders (see xz_ur_holder): register once at start-up; -1 when the board is full. answer() from any thread. */
int xz_usb_release_holder(const char *name, xz_ur_ask ask, void *ctx);
void xz_usb_release_answer(int holder, int slot, uint32_t stop);
#endif
#endif
