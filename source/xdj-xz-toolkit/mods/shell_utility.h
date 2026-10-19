/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_UTILITY_H
#define XZ_SHELL_UTILITY_H
/* The stock UTILITY items, read and written through the firmware's own UiGetUtil<item> /
   UiSetUtil<item> pairs (the functions stock's UTILITY menu calls). The table comes from
   gen_shell_utility.py; this unit is the pure part: index <-> setter argument, the published item
   rows and their notes. The firmware calls stay in shell_runtime.c (owner tick, code-guarded).

   Proof (static, firmware 1.26, rbp): UiSetUtil<item> -> StatWatcher::setUtil<item> ->
   KeyValueSet::setKeyValue(Settings + 0x14c) then Settings::onUpdated(id), which applies the setting and
   starts a 20 ms UiTimer; its callback hands the whole set to FlashWriter::writeReq, whose thread writes
   the settings file. So a set is persisted by stock itself, the same way as from stock's menu. Stock
   calls these setters from Ui_EventTask (Util_EnterMenu_*), TotalCnt_SentinelTASK and the MySetting
   unpack path; setKeyValue takes no lock and updates an existing key in place. The shell calls them
   from the HMI event loop (the owner tick), which already runs UiSetUtilQuantizeValue for the shell. */
#include <stdint.h>
#include "shell/shell_settings.h"

/* Runtime ids for stock items the shell has no id for (agreed with the shell: 0x100 + n, stable). */
#define XZ_SHRT_UTIL_ID(n) (0x100 + (n))

struct xz_shut_def {
 const char *key, *label;
 int id;                                   /* enum xz_shell_item_id or XZ_SHRT_UTIL_ID(n) */
 enum xz_shell_setting_category category;
 const char *const *choices;
 const int32_t *args;                      /* setter argument per choice; the getter returns one of them */
 int count;
 uint32_t get, set;                        /* 0: none (the item is stock-only) */
 uint32_t apply;                           /* 0 or Pub_GuiCom_Change<item>: stock's live apply, called first */
 uint8_t get_code[32], set_code[32], apply_code[32];
 int mapped;                               /* both handlers follow a recognised form (gen_shell_utility.py) */
};

/* The choice index a getter reading stands for, or -1. */
int xz_shut_index(const struct xz_shut_def *, int32_t raw);
/* The setter argument for a choice index; 0 when the index is out of range. */
int xz_shut_arg(const struct xz_shut_def *, int index, int32_t *arg);

/* Why an item cannot be written (NULL: it can). getter_ok/setter_ok: the code guards passed.
   trial: a --ram-settings run (XZ_MODS_SETTINGS_READONLY=1) without the explicit opt-in. */
const char *xz_shut_reason(const struct xz_shut_def *, int getter_ok, int setter_ok, int trial);
#define XZ_SHUT_NOTE_TRIAL "TRIAL RUN: STOCK WOULD SAVE THIS AT ONCE"
#define XZ_SHUT_NOTE_FIRMWARE "NOT AVAILABLE ON THIS FIRMWARE"
#define XZ_SHUT_NOTE_STOCK "OPENS IN THE STOCK UTILITY: PUSH IT"

/* One published row per def, in table order. raw/known: the last reading per def. */
void xz_shut_items(const struct xz_shut_def *defs, int n, const int32_t *raw, const uint8_t *known,
                   const uint8_t *getter_ok, const uint8_t *setter_ok, int trial,
                   struct xz_shell_setting_item *items, const char **notes);
/* The def an item id is published under, or -1. */
int xz_shut_find(const struct xz_shut_def *defs, int n, int id);
#endif
