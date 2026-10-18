#ifndef XZ_SHELL_SOURCE_H
#define XZ_SHELL_SOURCE_H
/* SOURCE screen: the model behind the shell model's source slot.

   One tile per device slot, in xz_shell_source order (USB 1, USB 2, LINK,
   COMPUTER). Which deck plays from a device is read from the deck model's
   source field, not repeated here. Strings are runtime-owned.

   Shell state, not model: the highlighted tile (the stack view's cursor:
   0 = the current browse source, or the first connected device; n = tile n-1). */
#include "shell.h"

#define XZ_SHELL_SOURCE_TILES 4
enum xz_shell_device_status { XZ_SHELL_DEVICE_NOT_CONNECTED, XZ_SHELL_DEVICE_LOADING, XZ_SHELL_DEVICE_READY };
enum xz_shell_device_field {
 XZ_SHELL_D_STATUS, XZ_SHELL_D_NAME, XZ_SHELL_D_COLOR, XZ_SHELL_D_FREE, XZ_SHELL_D_TOTAL, XZ_SHELL_D_TRACKS,
 XZ_SHELL_DEVICE_FIELD_COUNT
};
struct xz_shell_device {
 uint32_t known;                     /* bit per enum xz_shell_device_field */
 enum xz_shell_device_status status;
 const char *name;                   /* volume or host name; the slot name is drawn regardless */
 uint32_t color;                     /* 0xRRGGBB background colour the owner chose for this device */
 uint64_t free_bytes, total_bytes;
 int32_t tracks;
};
struct xz_shell_source_model {
 struct xz_shell_device device[XZ_SHELL_SOURCE_TILES];  /* index = enum xz_shell_source - 1 */
 int current_known;
 enum xz_shell_source current;       /* the device BROWSE reads now */
};

/* Action this screen adds to enum xz_shell_action_kind:
   SET_SOURCE  id = enum xz_shell_source (USB1..COMPUTER), deck = -1. The runtime
               makes that device the browse source. The shell follows it with the
               BROWSE navigation (OPEN, or HAND_OFF when BROWSE is unavailable).
               Emitted only for a READY device. */

/* Tile n's rect (n = enum xz_shell_source - 1); the close button for n = -1. */
struct xz_shell_rect xz_shell_source_rect(int tile);
#endif
