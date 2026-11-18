/* SPDX-License-Identifier: MIT */
#ifndef XZ_TRACKLIST_FILE_H
#define XZ_TRACKLIST_FILE_H
/* Carries out one tracklist file operation (tracklist.h) on the recording stick.

   Each operation opens the folder, checks the recording is still there, opens the file without following a
   link, writes the whole text, fsyncs it, and closes everything again: no descriptor stays open on the stick,
   so the stock player can unmount it at any time. A file is written only beside an existing recording (its final
   name, or RECTMP.WAV while stock is still writing it), never
   on the unit's own flash. After a file's first failure (read-only, full, removed, I/O error) its later
   operations are skipped; the next recording starts clean. A failed append is cut back to its old length so
   the file keeps whole lines. */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "tracklist.h"

enum xz_tl_write_result { XZ_TL_WROTE=0, XZ_TL_SKIPPED=1, XZ_TL_FAILED=-1 };
/* The name the stock recorder writes under until the file closes (WavWriter +0x90, recorder/SEAM.md). */
#define XZ_TL_RECORDING_NAME "RECTMP.WAV"
struct xz_tl_writer {
 dev_t forbid_dev; int forbid_dev_set;   /* the unit's root filesystem: never written */
 uint32_t failed_seq; int failed;        /* the file whose operations are skipped */
 int last_errno;
 uint32_t ops, writes, syncs, failures, skipped;
 int no_sync;                             /* set for a USB STOP's write: the unmount that follows writes everything out */
 /* Test hooks; NULL in the runtime. */
 ssize_t (*write_fn)(int fd,const void *data,size_t n);
 int (*sync_fn)(int fd);
};
void xz_tl_writer_init(struct xz_tl_writer *);
enum xz_tl_write_result xz_tl_write(struct xz_tl_writer *, const struct xz_tl_op *);
#endif
