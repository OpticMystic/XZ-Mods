/* SPDX-License-Identifier: MIT */
#ifndef XZ_STOCK_DIALOG_H
#define XZ_STOCK_DIALOG_H
#include <stddef.h>
#include <stdint.h>
#include "stock_counter.h"
typedef int (*xz_stock_dialog_read)(uint32_t,void *,size_t);
enum xz_stock_dialog_failure {
 XZ_STOCK_OK, XZ_STOCK_NO_READER, XZ_STOCK_BROWSE_GUARD, XZ_STOCK_STATUS_GUARD,
 XZ_STOCK_WINDOW_READ, XZ_STOCK_WINDOW_TORN, XZ_STOCK_COUNTER_GUARD,
 XZ_STOCK_COUNTER_READ, XZ_STOCK_COUNTER_TORN, XZ_STOCK_BROWSE_CODE_READ,
 XZ_STOCK_STATUS_CODE_READ, XZ_STOCK_COUNTER_CODE_READ
};
struct xz_stock_dialog_state { unsigned window; uint32_t counter[2]; enum xz_stock_dialog_failure failure; };
/* 1: live native browse/status window; 0: no window; -1: observation failed.
   Failures and counter IDs never cause full-screen handoff. */
int xz_stock_dialog_snapshot(xz_stock_dialog_read read,struct xz_stock_dialog_state *out);
int xz_stock_dialog_observe(xz_stock_dialog_read read);
int xz_stock_dialog_active(void);
/* Known error bits; out receives current IDs. Failure/unknown IDs return no bits. */
unsigned xz_stock_counter_ids(uint32_t out[2]);
#endif
