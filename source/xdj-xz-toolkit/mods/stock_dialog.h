/* SPDX-License-Identifier: MIT */
#ifndef XZ_STOCK_DIALOG_H
#define XZ_STOCK_DIALOG_H
#include <stddef.h>
#include <stdint.h>
typedef int (*xz_stock_dialog_read)(uint32_t,void *,size_t);
/* 0: no warning; 1: native warning/window; -1: observation unavailable/torn.
   Both nonzero results yield the entire display and input to stock. No cached acknowledgement. */
int xz_stock_dialog_observe(xz_stock_dialog_read read);
int xz_stock_dialog_active(void);
#endif
