/* SPDX-License-Identifier: MIT */
#ifndef XZ_DIARY_STORE_H
#define XZ_DIARY_STORE_H
#include "diary.h"
#include <sys/types.h>

struct xz_diary_store {
 char root[1024],pending[XZ_DIARY_BATCH_BYTES];
 dev_t device;ino_t inode,file_inode;
 unsigned long mount_id;
 unsigned used,dropped,generation,segment,bytes,pause_reason;
 uint32_t last_attempt;
 int scanned,active,disabled,attempted;
 /* Worker-owned dependencies. Guard: positive safe, zero busy, negative gone. */
 void *context;
 int (*guard)(void *,const struct xz_diary_store *);
 int (*space)(void *,int,uint64_t *,int *);
 ssize_t (*write_once)(void *,int,const void *,size_t);
 int (*sync_once)(void *,int);
 unsigned writes,syncs;uint64_t written;
};
int xz_diary_store_init(struct xz_diary_store *,const char *,unsigned long);
void xz_diary_store_queue(struct xz_diary_store *,const char *,size_t);
/* 1 wrote, 0 deferred, -1 disabled. No syscall retries or caller-owned locks. */
int xz_diary_store_flush(struct xz_diary_store *,const struct xz_diary_session *,uint32_t);
#endif
