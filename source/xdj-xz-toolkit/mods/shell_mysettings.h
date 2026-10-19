/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_MYSETTINGS_H
#define XZ_SHELL_MYSETTINGS_H
/* MY SETTINGS for the FLAGSHIP shell: the USB stick's MYSETTING.DAT, MYSETTING2.DAT, DJMMYSETTING.DAT and
   DEVSETTING.DAT (my_settings.c parses and writes them) mapped onto the stock UTILITY items the shell already
   reads and writes (shell_utility.h).

   The map in shell_mysettings.c is written by hand, one entry per field, each naming the stock setter it
   reaches. It agrees with stock's own MY SETTINGS load (TotalMySet_UtilityPac_Unpac, read as a reference):
   the same setter per field and the same value translation, including the CmnFunc_CmnInfo_SetUtility*
   tables (AUTO CUE LEVEL 7..0 then MEMORY, LCD BRIGHTNESS 1..5, QUANTIZE BEAT 0..3, HOT CUE AUTO LOAD
   0, 1 then 2). A field with no proven setter is "not applied" and only counted.

   Pure: no firmware calls and no locking. The runtime runs the plan on its owner tick and the file I/O
   on its worker. */
#include "my_settings.h"
#include "shell_utility.h"

#define XZ_SHMS_ID(field) (0x200 + (int)(field))
enum { XZ_SHMS_MISSING = 0, XZ_SHMS_OK = 1, XZ_SHMS_BAD = -1 };
struct xz_shms_files { int state[MS_KIND_COUNT]; struct ms_settings s[MS_KIND_COUNT]; };

/* Reads root/PIONEER/<file> for every kind. Returns how many were read. */
int xz_shms_read(const char *root, struct xz_shms_files *);
int xz_shms_any(const struct xz_shms_files *);
/* Writes one kind back atomically (my_settings ms_write_atomic). MS_OK or an ms_error. */
int xz_shms_write(const char *root, const struct xz_shms_files *, enum ms_kind kind);

/* LOAD: what applying the files means. def >= 0: the UTILITY item (index into defs) and its setter
   argument. def == XZ_SHMS_STEP_TIME_MODE: the deck's TIME MODE, arg 1 = REMAIN. */
#define XZ_SHMS_STEP_TIME_MODE (-2)
struct xz_shms_step { int def; int32_t arg; };
int xz_shms_plan(const struct xz_shms_files *, const struct xz_shut_def *defs, int ndefs,
                 struct xz_shms_step *out, int cap, int *not_applied);

/* SAVE: writes the deck's current values (UTILITY readings and TIME MODE) into the files' fields that the
   map covers; other fields keep what the stick holds. Returns how many fields changed. */
int xz_shms_capture(struct xz_shms_files *, const struct xz_shut_def *defs, int ndefs,
                    const int32_t *raw, const uint8_t *known, int time_known, int remain);

/* Published rows: one per field of every file read, id XZ_SHMS_ID(field), the codec's choices. */
struct xz_shms_rows {
 struct xz_shell_setting_item item[MS_FIELD_COUNT];
 const char *choice[MS_FIELD_COUNT][24];
 char label[MS_FIELD_COUNT][40];
 int count;
};
void xz_shms_rows(const struct xz_shms_files *, int writable, struct xz_shms_rows *);
/* SET: field id and choice index. 0 and *kind = the file to write, -1 when it cannot be set. */
int xz_shms_set(struct xz_shms_files *, int id, int index, enum ms_kind *kind);
/* DEVSETTING's waveform colour as enum xz_shell_wave_color, or -1 when the stick sets none. */
int xz_shms_wave_color(const struct xz_shms_files *);
/* The map's entry for a field: the UTILITY item key, or NULL when the field is not applied. */
const char *xz_shms_utility_key(enum ms_field);
#endif
