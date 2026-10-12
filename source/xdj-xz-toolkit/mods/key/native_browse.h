/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_BROWSE_H
#define XZ_NATIVE_BROWSE_H
#include <stddef.h>
#include <stdint.h>
typedef int (*xz_browse_read)(void *,uint32_t,void *,size_t);
typedef int (*xz_browse_menu_call)(void *,uint32_t input[3]);
int xz_native_browse_state(xz_browse_read,void *,unsigned *mode);
int xz_native_browse_sort_call(xz_browse_read,void *,xz_browse_menu_call,void *);
/* Called only on the existing native UI/touch thread. Uses the same stock
   handler as clicking MENU; it does not synthesize hardware or audio events. */
int xz_native_browse_open_sort(void);
unsigned xz_native_browse_mode(void);
#endif
