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
/* STOCK: the slot is browsed in the stock screen (LINK: linked players' media and DJ software control;
   COMPUTER: export mode playback, a laptop's or phone's library over the network). Selecting it emits
   SET_SOURCE {id = the slot, value 0} and nothing else: the runtime hands the screen to stock and runs the
   stock key for that slot, so stock opens it at once.
   UNREADABLE: a stick whose rekordbox library could not be read (not selectable).
   LOOPBACK: this top USB port holds the DECK 3/4 loopback cable (not selectable). The runtime sets it only when it
   knows: the route is READY and the other top port holds the music (the helper refuses music on the cable's port). */
enum xz_shell_device_status { XZ_SHELL_DEVICE_NOT_CONNECTED, XZ_SHELL_DEVICE_LOADING, XZ_SHELL_DEVICE_READY,
 XZ_SHELL_DEVICE_STOCK, XZ_SHELL_DEVICE_UNREADABLE, XZ_SHELL_DEVICE_LOOPBACK };
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
 const uint16_t *logo; /* 160 x 160 RGB565 active DJ logo, model-owned */
 const char *profile_name;
};
/* One medium in another LINK player (XZ manual pp.67, 114). */
enum xz_shell_link_slot { XZ_SHELL_LINK_USB, XZ_SHELL_LINK_SD, XZ_SHELL_LINK_COMPUTER };
struct xz_shell_link_media {
 int player;                         /* player number on the network */
 enum xz_shell_link_slot slot;
 const char *name;                   /* volume name; NULL draws the slot */
 uint32_t color;                     /* background colour, 0 = none */
 int ready;                          /* browsable now */
};
struct xz_shell_source_model {
 struct xz_shell_device device[XZ_SHELL_SOURCE_TILES];  /* index = enum xz_shell_source - 1 */
 int current_known;
 enum xz_shell_source current;       /* the device BROWSE reads now */
 const struct xz_shell_link_media *link_media;   /* the LINK tile's media; count 0 keeps the tile a device */
 int link_media_count;
 int link_current;                   /* index of the medium BROWSE reads when current is LINK, -1 none */
};

/* Action this screen adds to enum xz_shell_action_kind:
   SET_SOURCE  id = enum xz_shell_source (USB1..COMPUTER), deck = -1. The runtime
               makes that device the browse source. The shell follows it with the
               BROWSE navigation (OPEN, or HAND_OFF when BROWSE is unavailable).
               Emitted only for a READY device, or alone (no BROWSE) for a STOCK slot.
               For LINK with media listed, the LINK
               tile opens a picker of player and medium; picking one emits
               SET_SOURCE {id LINK, value = 1 + media index}. Rows then load onto
               either deck with the row LOAD chips as for a local stick. */

/* The rear USB bar under the tiles: what holds the rear USB port (the port a computer uses for DJ software
   and rekordbox over a USB cable), read from the model's route state (shell.h route_state, route_fail_part,
   route_reason; enum xz_extra_route_state). Drawn only; it takes no input. */
#define XZ_SHELL_SOURCE_BAR (-2)
/* Tile n's rect (n = enum xz_shell_source - 1); the close button for n = -1; the rear USB bar for
   XZ_SHELL_SOURCE_BAR. */
struct xz_shell_rect xz_shell_source_rect(int tile);
/* The rear USB port as the SOURCE screen tells it, from the route state alone. */
enum xz_shell_rear_port {
 XZ_SHELL_REAR_UNUSED,   /* decks 3/4 are not installed or not running (NO_ENGINE): the bar names the port only */
 XZ_SHELL_REAR_FREE,     /* no loopback cable found (not detected, scanning, lost): a computer can use the port */
 XZ_SHELL_REAR_STARTING, /* decks 3/4 are starting or wait for decks 1/2 to stop. The connector reports this before it
                            checks the cable (ui/ui_sections.c), so the screen does not claim the cable is in */
 XZ_SHELL_REAR_CABLE,    /* decks 3/4 play through the cable (READY) */
 XZ_SHELL_REAR_FAILED    /* decks 3/4 did not start; the cable may or may not be in */
};
enum xz_shell_rear_port xz_shell_source_rear(const struct xz_shell_model *m);
/* For the runtime: with the route READY (route_state), a top port that shows nothing while the other shows a
   stick (ready, loading or unreadable) holds the loopback cable, because the helper refuses music on the
   cable's port. Marks that tile LOOPBACK and returns its index, or -1 when it cannot know. */
int xz_shell_source_mark_loopback(struct xz_shell_source_model *sm, int route_state);
/* The LINK picker: visible row i (44 px tall). The picker is shell state (the view's scroll: 0 shut,
   k = open with medium k-1 highlighted); BACK shuts it. */
#define XZ_SHELL_LINK_ROWS 6
struct xz_shell_rect xz_shell_source_link_rect(int row);
#endif
