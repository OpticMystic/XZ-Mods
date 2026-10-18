#ifndef XZ_SHELL_INFO_H
#define XZ_SHELL_INFO_H
/* Track INFO screen: the model behind the shell model's info slot.

   One track record per deck (the loaded track) and, while a list is open, the
   browse-highlighted track. Every value has a bit in known; a clear bit renders
   the flagship blank ("--"). A known field with a NULL string is treated as
   unknown. Strings and the artwork are runtime-owned and live for the frame.

   Shell state, not model: which tab is shown (the stack view's focus:
   0 = the highlighted track when there is one, else the active deck;
   1 = DECK 1, 2 = DECK 2, 3 = the highlighted track) and the focused value
   (view cursor: 0 = none, n = the nth focusable value). The DECK tabs only
   change what INFO shows; they never move the active deck. */
#include "shell.h"

enum xz_shell_info_field {
 XZ_SHELL_I_LOADED, XZ_SHELL_I_TRACK_ID, XZ_SHELL_I_TITLE, XZ_SHELL_I_ARTIST, XZ_SHELL_I_ALBUM, XZ_SHELL_I_GENRE,
 XZ_SHELL_I_LABEL, XZ_SHELL_I_KEY, XZ_SHELL_I_BPM, XZ_SHELL_I_DURATION, XZ_SHELL_I_RATING, XZ_SHELL_I_COLOR,
 XZ_SHELL_I_COMMENT, XZ_SHELL_I_DATE_ADDED, XZ_SHELL_I_BITRATE, XZ_SHELL_I_FILE_NAME, XZ_SHELL_I_SOURCE,
 XZ_SHELL_I_ARTWORK, XZ_SHELL_INFO_FIELD_COUNT
};
/* The library's eight track colours, in catalog id order (1..8). */
enum xz_shell_color_tag {
 XZ_SHELL_COLOR_NONE, XZ_SHELL_COLOR_PINK, XZ_SHELL_COLOR_RED, XZ_SHELL_COLOR_ORANGE, XZ_SHELL_COLOR_YELLOW,
 XZ_SHELL_COLOR_GREEN, XZ_SHELL_COLOR_AQUA, XZ_SHELL_COLOR_BLUE, XZ_SHELL_COLOR_PURPLE, XZ_SHELL_COLOR_COUNT
};
#define XZ_SHELL_RATING_MAX 5

struct xz_shell_info_track {
 uint64_t known;                     /* XZ_SHELL_KNOWN(enum xz_shell_info_field) */
 /* Fields the runtime can open in BROWSE (information jump): ARTIST, ALBUM,
    GENRE, KEY or BPM bits. A bit asserts the matching id below is valid. */
 uint64_t links;
 int rating_writable;                /* EDIT RATING: stars become a control */
 int loaded;
 int32_t track_id;
 const char *title, *artist, *album, *genre, *label, *key, *comment;
 const char *date_added;             /* as the library stores it, e.g. "2026-10-18" */
 const char *file_name;
 int32_t artist_id, album_id, genre_id, key_id;  /* catalog ids for the jump */
 int32_t bpm_x100;                   /* original (analysed) BPM */
 int32_t duration_ms;
 int32_t bitrate_kbps;
 int rating;                         /* 0..XZ_SHELL_RATING_MAX */
 enum xz_shell_color_tag color;
 enum xz_shell_source source;
 const uint16_t *artwork;            /* RGB565 square, or NULL */
 int artwork_side;
};

struct xz_shell_info_model {
 struct xz_shell_info_track deck[XZ_SHELL_DECKS];
 int has_highlight;                  /* a list is open and supplied its highlighted track */
 struct xz_shell_info_track highlight;
};

/* Actions this screen adds to enum xz_shell_action_kind (struct xz_shell_action fields):
   INFO_JUMP   deck = the shown track's deck, or -1 for the highlighted track;
               id = XZ_SHELL_I_ARTIST/ALBUM/GENRE/KEY/BPM; value = that field's
               catalog id (artist_id, album_id, genre_id, key_id) or bpm_x100.
               The runtime opens BROWSE at that category filtered to that value.
               The shell follows it with the BROWSE navigation (OPEN, or HAND_OFF
               when BROWSE is unavailable). Needs XZ_SHELL_CAP_BROWSE and the link bit.
   SET_RATING  deck as above; id = track_id; value = 0..XZ_SHELL_RATING_MAX.
               Needs rating_writable and a known track id. */

/* INFO geometry, for the runtime's hit tests and the host tests. */
enum xz_shell_info_region {
 XZ_SHELL_INFO_R_TAB,       /* index 0 = DECK 1, 1 = DECK 2, 2 = LIST (highlighted track) */
 XZ_SHELL_INFO_R_CLOSE,
 XZ_SHELL_INFO_R_ARTWORK,
 XZ_SHELL_INFO_R_TITLE,
 XZ_SHELL_INFO_R_VALUE,     /* index = enum xz_shell_info_field */
 XZ_SHELL_INFO_R_STAR,      /* index 0..XZ_SHELL_RATING_MAX-1 */
 XZ_SHELL_INFO_R_STRIP      /* index = deck */
};
/* An empty rect for a field the screen does not draw as its own cell. */
struct xz_shell_rect xz_shell_info_rect(enum xz_shell_info_region, int index);
#endif
