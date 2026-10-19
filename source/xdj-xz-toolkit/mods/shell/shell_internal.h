#ifndef XZ_SHELL_INTERNAL_H
#define XZ_SHELL_INTERNAL_H
/* Shared by the shell's screens. Not part of the runtime contract. */
#include "shell.h"
#include "../ui/ui_skin.h"
#include "../ui/theme_flagship.h"

enum {
 SH_BLACK=0x000000, SH_WHITE=0xffffff, SH_CHROME=XZ_FLAGSHIP_CHROME, SH_RAIL=XZ_FLAGSHIP_RAIL,
 SH_ROW_ALT=XZ_FLAGSHIP_ROW_ALT, SH_DIM=XZ_FLAGSHIP_DIM, SH_BLUE=XZ_FLAGSHIP_BLUE, SH_RED=XZ_FLAGSHIP_RED,
 SH_GREEN=XZ_FLAGSHIP_GREEN, SH_LOOP=XZ_FLAGSHIP_LOOP_ORANGE, SH_WAVE_LOW=XZ_FLAGSHIP_WAVE_LOW,
 SH_WAVE_LOW_DEEP=XZ_FLAGSHIP_WAVE_LOW_DEEP, SH_WAVE_MID=XZ_FLAGSHIP_WAVE_MID, SH_WAVE_HIGH=XZ_FLAGSHIP_WAVE_HIGH,
 SH_PLAYHEAD=0xff2020, SH_MEMORY=0xff3c3c, SH_SEPARATOR=0x303030
};
/* Atlas faces: 0 = 12 px (cap 9), 1 = 20 px (cap 14). Numeral faces are shell_font.h. */
enum sh_face { SH_SMALL=1, SH_TEXT=2 };
enum sh_num_face { SH_NUM_MID, SH_NUM_BPM, SH_NUM_BIG };
enum { SH_CAP_SMALL=9, SH_CAP_TEXT=14 };

struct sh_screen {
 void (*render)(const struct xz_shell *, const struct xz_shell_model *, struct xz_canvas);
 size_t (*input)(struct xz_shell *, const struct xz_shell_model *, const struct xz_shell_event *, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
};
extern const struct sh_screen xz_shell_play_screen, xz_shell_placeholder_screen;
/* One per screen; shell_browse.c, shell_settings.c, shell_info.c and shell_source.c define them. */
extern const struct sh_screen xz_shell_browse_screen, xz_shell_playlist_screen, xz_shell_tag_list_screen, xz_shell_search_screen;
extern const struct sh_screen xz_shell_shortcut_screen, xz_shell_utility_screen, xz_shell_info_screen, xz_shell_source_screen;
/* The navigation table applied to one key (shell.c); screens use it for on-screen buttons. */
size_t sh_key(struct xz_shell *, const struct xz_shell_model *, enum xz_shell_key, int deck, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
/* BACK, for the key and every on-screen back control alike (shell.c). */
size_t sh_back(struct xz_shell *, const struct xz_shell_model *, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
/* A screen's own step up for BACK: sets *taken when it used the key (closed a panel, climbed a level). */
size_t sh_browse_back_step(struct xz_shell *, const struct xz_shell_model *, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken);
size_t sh_settings_back_step(struct xz_shell *, const struct xz_shell_model *, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken);

static inline int sh_has(const struct xz_shell_deck *d, enum xz_shell_field f) { return (d->known>>f)&1; }
static inline int sh_setting(const struct xz_shell_model *m, enum xz_shell_setting s) { return (m->settings.known>>s)&1; }
static inline int sh_writable(const struct xz_shell_model *m, enum xz_shell_setting s) {
 return (m->capabilities&XZ_SHELL_CAP_SET_SETTING)&&((m->settings.writable>>s)&1);
}
static inline int sh_min(int a, int b) { return a<b?a:b; }
static inline int sh_max(int a, int b) { return a>b?a:b; }
static inline int sh_inside(struct xz_shell_rect r, int x, int y) { return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h; }

/* Text. Every routine clips to width where one is given; nothing is drawn past it. */
int sh_text_width(struct xz_canvas c, const char *s, enum sh_face face);
void sh_text(struct xz_canvas c, int x, int y, const char *s, enum sh_face face, uint32_t color);
/* Longest whole-character prefix that fits width. */
void sh_text_fit(struct xz_canvas c, int x, int y, int width, const char *s, enum sh_face face, uint32_t color);
void sh_text_right(struct xz_canvas c, int right, int y, const char *s, enum sh_face face, uint32_t color);
void sh_text_center(struct xz_canvas c, int cx, int y, const char *s, enum sh_face face, uint32_t color);
/* Numerals: digits are tabular (every digit takes the width of 0). Only
   XZ_SHELL_FONT_CHARSET characters draw; others take a space. */
int sh_num_width(enum sh_num_face face, const char *s);
int sh_num_cap(enum sh_num_face face);
void sh_num(struct xz_canvas c, int x, int y, const char *s, enum sh_num_face face, uint32_t color);
void sh_num_right(struct xz_canvas c, int right, int y, const char *s, enum sh_num_face face, uint32_t color);

/* Small marks. */
void sh_triangle(struct xz_canvas c, int cx, int y, int size, int down, uint32_t color);
void sh_note(struct xz_canvas c, int x, int y, uint32_t color);
void sh_usb_icon(struct xz_canvas c, int x, int y, uint32_t color);
void sh_info_icon(struct xz_canvas c, int cx, int cy, uint32_t color);
/* Caption chip: 1 px outline, caps text, height 16. Returns its width. */
int sh_chip(struct xz_canvas c, int x, int y, const char *s, uint32_t outline, uint32_t ink, int filled);
/* Shell-wide placeholder page for screens later units draw. */
void sh_placeholder(struct xz_canvas c, const char *title, const char *note);

/* Shell-owned display settings, clamped so a corrupt value draws the default. */
static inline enum xz_shell_wave_color sh_wave_color(const struct xz_shell *u) {
 return (unsigned)u->display.wave_color<=XZ_SHELL_WAVE_STEMS?u->display.wave_color:XZ_SHELL_WAVE_3BAND;
}
static inline enum xz_shell_wave_position sh_wave_position(const struct xz_shell *u) {
 return u->display.wave_position==XZ_SHELL_POSITION_LEFT?XZ_SHELL_POSITION_LEFT:XZ_SHELL_POSITION_CENTER;
}
static inline enum xz_shell_key_display sh_key_display(const struct xz_shell *u) {
 return (unsigned)u->display.key_display<=XZ_SHELL_KEY_ALPHANUMERIC?u->display.key_display:XZ_SHELL_KEY_AS_DECK;
}
/* Waveforms (shell_wave.c). A lane is mirrored about the middle of r, amp px each way at full scale;
   first_px is the absolute track pixel (ms x pps / 1000) drawn at r.x. An overview rises from the
   bottom of r and greys out left of played_x (-1: nothing played). */
void sh_wave_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int64_t first_px, int pps);
void sh_wave_overview(struct xz_canvas c, struct xz_shell_rect r, const struct xz_shell_wave *w, enum xz_shell_wave_color mode, int played_x);
/* A deck's lane or overview in the shell's waveform colour, from the data the deck has: STEMS draws
   the stem layer, or 3BAND while the deck has none (sh_stem_wave_ready). */
int sh_stem_wave_ready(const struct xz_shell_deck *d);
void sh_deck_lane(struct xz_canvas c, struct xz_shell_rect r, int amp, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int64_t first_px, int pps);
void sh_deck_overview(struct xz_canvas c, struct xz_shell_rect r, const struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_deck *d, int played_x);
/* Traffic-light ink for a key against the master deck's key (shell_wave.c): green, amber or plain. With
   no master key read it falls back to the runtime's matches_master flag. */
enum { SH_KEY_GREEN=0x52d794, SH_KEY_AMBER=0xffbd59 };
uint32_t sh_key_ink(const struct xz_shell_model *m, const char *key, int matches_master, uint32_t plain);
/* The key in the chosen notation; key itself when it is NULL, already in it, or neither notation. */
const char *sh_key_text(const char *key, enum xz_shell_key_display mode, char *buffer, size_t cap);
/* Mod header buttons (shell.c): drawn over every screen; hit tests padded to XZ_SHELL_HEADER_HIT_H. */
void sh_header_buttons(const struct xz_shell_model *m, struct xz_canvas c);
enum xz_shell_action_kind sh_header_hit(int x, int y);
/* Runtime notice toast (shell_draw.c): shown, its box, and how many lines it wraps to (1 or 2). */
int sh_notice_shown(const struct xz_shell_model *m);
struct xz_shell_rect sh_notice_rect(const char *text, int *lines);
void sh_notice(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c);
#endif
