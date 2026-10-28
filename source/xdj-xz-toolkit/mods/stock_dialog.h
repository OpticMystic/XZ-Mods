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
/* usb_hub: 1 or 2 while a live window's text starts with the E-8307 hub text n (stock_dialog.c). Such a window
   does not set window: the player raises it when it refuses the loopback cable's own hub and keeps it up while
   the cable is in. It is no error for a DJ, so XZ Mods never shows it as one; CLASSIC paints a calm card over it.
   text_at: the live window's text buffer (XZ_STOCK_BROWSE_TEXT or XZ_STOCK_STATUS_TEXT) while window is set. */
struct xz_stock_dialog_state { unsigned window, usb_hub; uint32_t text_at, counter[2]; enum xz_stock_dialog_failure failure; };
/* 1: live native browse/status window; 0: no window; -1: observation failed.
   Failures and counter IDs never cause full-screen handoff. A live window takes the screen only in CLASSIC
   with MODS closed; MODERN and an open MODS keep the screen and show the window's text themselves. */
int xz_stock_dialog_snapshot(xz_stock_dialog_read read,struct xz_stock_dialog_state *out);
int xz_stock_dialog_observe(xz_stock_dialog_read read);
int xz_stock_dialog_active(void);
/* Known error bits; out receives current IDs. Failure/unknown IDs return no bits. */
unsigned xz_stock_counter_ids(uint32_t out[2]);
/* The firmware's E-8307 hub text a live hub window starts with, as of the last poll, or NULL. No memory reads:
   every frame already polls through xz_stock_dialog_active. */
const char *xz_stock_usb_hub_text(void);
/* The live window's own text as of the last poll, in printable ASCII, into out: "E-8304: USB ACCESS ERROR".
   Returns its length, or 0 with no live window. A window whose text cannot be read gives XZ_STOCK_WINDOW_UNREAD.
   No memory reads; any thread. */
#define XZ_STOCK_WINDOW_TEXT 64
#define XZ_STOCK_WINDOW_UNREAD "THE PLAYER SHOWS A WARNING"
size_t xz_stock_window_text(char out[XZ_STOCK_WINDOW_TEXT]);
/* The caution text buffers (NATIVE_STOCK_DIALOG.md): browse sCautionBrowseString, status sCautionStatusString. */
#define XZ_STOCK_BROWSE_TEXT 0x1ada958u
#define XZ_STOCK_STATUS_TEXT 0x1ada718u
/* 1 or 2 when the UTF-16 text at text reads exactly XZ_STOCK_USB_HUB_TEXT[n-1] (two stable reads), else 0. Reads only. */
unsigned xz_stock_usb_hub_at(xz_stock_dialog_read read,uint32_t text);
#endif
