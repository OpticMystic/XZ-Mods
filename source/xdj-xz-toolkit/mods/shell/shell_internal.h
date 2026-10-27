#ifndef XZ_SHELL_INTERNAL_H
#define XZ_SHELL_INTERNAL_H
/* Shared by the shell's screens. Not part of the runtime contract. */
#include "shell.h"
void sh_play_key_publish(struct xz_shell *,const struct xz_shell_model *);
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
/* SH_NUM_KEY is the MID size with key-name letters (shell_font.h XZ_SHELL_FONT_KEY_CHARSET) instead of signs. */
enum sh_num_face { SH_NUM_MID, SH_NUM_BPM, SH_NUM_BIG, SH_NUM_KEY };
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
size_t sh_source_back_step(struct xz_shell *, const struct xz_shell_model *, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken);

/* The only reader of model.deck_count (shell.h). */
static inline int sh_deck_count(const struct xz_shell_model *m) { return m->deck_count==4?4:2; }
/* PLAY geometry can show two slots while four logical audio decks remain available, and four slots
   while only two play (DECK VIEW 4 DECKS with decks 3 and 4 not connected). */
static inline int sh_play_deck_count(const struct xz_shell_model *m) {
 return m&&m->play_view_count==2?2:m&&m->play_view_count==4?4:m?sh_deck_count(m):2;
}
static inline int sh_play_deck(const struct xz_shell_model *m, int slot) {
 return sh_deck_count(m)==4&&sh_play_deck_count(m)==2?slot+2*((m->sides_extra>>slot)&1):slot;
}
static inline int sh_play_visible(const struct xz_shell_model *m, int deck) {
 if(sh_play_deck_count(m)==4)return (unsigned)deck<4u;
 return (unsigned)deck<(unsigned)sh_deck_count(m)&&(sh_deck_count(m)==2||deck==sh_play_deck(m,deck&1));
}
/* A four-deck PLAY view draws decks 3 and 4 as NOT CONNECTED placeholders (projection only). */
static inline int sh_play_placeholder(const struct xz_shell_model *m, int deck) {
 return m&&(unsigned)deck<4u&&((m->play_placeholders>>deck)&1);
}
/* The four-deck look: four decks play, or the PLAY view shows four slots. */
static inline int sh_four_view(const struct xz_shell_model *m) { return sh_deck_count(m)==4||sh_play_deck_count(m)==4; }
/* Logical deck to horizontal mixer column: DECK 3, 1, 2, 4. */
static inline int sh_strip_column(int count, int deck) {
 static const unsigned char columns[4]={1,2,0,3};
 return count==4&&(unsigned)deck<4?columns[deck]:deck;
}
void sh_browse_remember_return(struct xz_shell *, const struct xz_shell_model *, int track_focus);
int sh_browse_restore_return(struct xz_shell *, const struct xz_shell_model *);
int sh_browse_load_pending(const struct xz_shell *);
size_t sh_browse_load_input(struct xz_shell *, const struct xz_shell_model *, const struct xz_shell_event *, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
/* The active deck, or deck 1 when the shell's choice is past the decks the model plays now. */
static inline int sh_active_deck(const struct xz_shell *u, const struct xz_shell_model *m) {
 return u->deck>=0&&u->deck<sh_deck_count(m)?u->deck:0;
}
/* PLAY edits use the visible equivalent of the stored focus, without changing browse focus. */
static inline int sh_play_active_deck(const struct xz_shell *u, const struct xz_shell_model *m) {
 int deck=sh_active_deck(u,m);
 return sh_play_deck_count(m)==2&&sh_deck_count(m)==4?sh_play_deck(m,deck&1):deck;
}
/* Physical control assignment is independent of the shell's browse/grid focus. */
static inline int sh_control_active(const struct xz_shell_model *m, int deck) {
 if(sh_deck_count(m)!=4||!(m->capabilities&XZ_SHELL_CAP_DECK_SELECT)||(unsigned)deck>=4)return 0;
 return deck==(deck&1)+2*((m->sides_extra>>(deck&1))&1);
}
/* Point toward the assigned physical controls: left for 1/3, right for 2/4. */
static inline void sh_control_arrow(struct xz_canvas c,int x,int y,int right) {
 for(int row=0;row<9;row++){
  int distance=row<4?4-row:row-4,from=2*distance,to=row>=2&&row<=6?25:8;
  xz_rect(c,x+(right?26-to:from),y+row,to-from+1,1,SH_BLACK);
 }
 for(int row=2;row<=6;row++){
  int distance=row<4?4-row:row-4,from=2+2*distance,to=distance==2?8:24;
  xz_rect(c,x+(right?26-to:from),y+row,to-from+1,1,SH_WHITE);
 }
}
static inline int sh_has(const struct xz_shell_deck *d, enum xz_shell_field f) { return (d->known>>f)&1; }
/* The model's capabilities this deck takes (xz_shell_deck.caps_off). */
static inline uint32_t sh_deck_caps(const struct xz_shell_model *m, int deck) {
 return deck>=0&&deck<XZ_SHELL_DECKS?m->capabilities&~m->deck[deck].caps_off:0;
}
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
/* Numerals: digits are tabular (every digit takes the width of 0). Only the
   face's charset draws; others take a space (no width on SH_NUM_KEY). */
int sh_num_width(enum sh_num_face face, const char *s);
int sh_num_cap(enum sh_num_face face);
void sh_num(struct xz_canvas c, int x, int y, const char *s, enum sh_num_face face, uint32_t color);
void sh_num_right(struct xz_canvas c, int right, int y, const char *s, enum sh_num_face face, uint32_t color);
/* Every character of s is in the face's charset (a key name the KEY face can draw). */
int sh_num_drawable(enum sh_num_face face, const char *s);

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
static inline enum xz_shell_key_display sh_key_display(const struct xz_shell *u,const struct xz_shell_model *m) {
 if(m&&m->mods_settings_known)return m->key_format==1?XZ_SHELL_KEY_CLASSIC:m->key_format==2?XZ_SHELL_KEY_ALPHANUMERIC:XZ_SHELL_KEY_AS_DECK;
 return (unsigned)u->display.key_display<=XZ_SHELL_KEY_ALPHANUMERIC?u->display.key_display:XZ_SHELL_KEY_AS_DECK;
}
static inline enum xz_shell_usb_colors sh_usb_colors(const struct xz_shell *u) {
 return (unsigned)u->display.usb_colors<=XZ_SHELL_USB_COLORS_OFF?u->display.usb_colors:XZ_SHELL_USB_COLORS_ALL;
}
static inline int sh_indicator_shown(const struct xz_shell *u, enum xz_shell_indicator i) { return !((u->display.hidden>>i)&1); }
/* A deck's waveform colour: its stick's when the deck follows it (USB COLORS ALL), else the shell's. */
static inline enum xz_shell_wave_color sh_deck_wave_color(const struct xz_shell *u, const struct xz_shell_deck *d) {
 if(u->display.wave_color_override==XZ_SHELL_WAVE_3BAND||u->display.wave_color_override==XZ_SHELL_WAVE_STEMS)return u->display.wave_color_override;
 if(sh_usb_colors(u)==XZ_SHELL_USB_COLORS_ALL&&sh_has(d,XZ_SHELL_F_SOURCE_LOOK)
    &&d->look.wave_color>=XZ_SHELL_WAVE_BLUE&&d->look.wave_color<=XZ_SHELL_WAVE_STEMS)return (enum xz_shell_wave_color)d->look.wave_color;
 return sh_wave_color(u);
}
/* A deck's tile colour: its stick's background colour unless USB COLORS is OFF, else fallback. */
static inline uint32_t sh_deck_accent(const struct xz_shell *u, const struct xz_shell_deck *d, uint32_t fallback) {
 if(sh_usb_colors(u)!=XZ_SHELL_USB_COLORS_OFF&&sh_has(d,XZ_SHELL_F_SOURCE_LOOK)&&d->look.has_background)return d->look.background_rgb&0xffffff;
 return fallback;
}
/* Text on a coloured fill: black on light fills, white otherwise. */
static inline uint32_t sh_ink_on(uint32_t rgb) {
 unsigned r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
 return r*299+g*587+b*114>=150000?SH_BLACK:SH_WHITE;
}
/* Shell-owned settings for screens other than SHORTCUT / UTILITY (shell_settings.c): the item as those
   screens show it, and setting it there (same actions, same local-override rule). */
struct xz_shell_setting_item;
/* Descriptions (shell_settings.h): what an item does, and what choice index does; NULL when none. sh_wrap
   breaks text into lines of the small face no wider than width; it returns the line count. */
enum { SH_WRAP_CHARS=120 };
int sh_wrap(const char *text, int width, char out[][SH_WRAP_CHARS], int max);
const char *sh_item_help(const struct xz_shell_model *, int scope, const struct xz_shell_setting_item *);
const char *sh_choice_help(const struct xz_shell_model *, int scope, const struct xz_shell_setting_item *, int index);
int sh_settings_item(const struct xz_shell *, const struct xz_shell_model *, int id, struct xz_shell_setting_item *out);
/* A moving picture's frame step: 60 fps while the native decks stand still, 30 while one plays, 15 when
   frames run late. */
uint32_t sh_picture_step_ms(const struct xz_shell_model *);
size_t sh_settings_set(struct xz_shell *, int id, int index, struct xz_shell_action out[XZ_SHELL_ACTIONS]);
/* Stem colours, two tones as the stem buttons draw them: the bright outline and label, and the dark
   interior (3/16 of the bright, the ratio the reference stem buttons use). A colour the DJ picked
   (stem_rgb_custom) replaces the default. Stems: 0 drums, 1 harmonics, 2 vocal. */
static inline uint32_t sh_stem_bright(const struct xz_shell_model *m, int stem) {
 static const uint32_t fallback[3]={0x4b4ace,0xac1f12,0x15b634};
 if(stem<0||stem>2)return SH_DIM;
 return m->stem_rgb_known&&((m->stem_rgb_custom>>stem)&1)?m->stem_rgb[stem]&0xffffff:fallback[stem];
}
static inline uint32_t sh_stem_dark(uint32_t bright) {
 uint32_t r=(bright>>16)&255,g=(bright>>8)&255,b=bright&255;
 return ((r*3>>4)<<16)|((g*3>>4)<<8)|(b*3>>4);
}
/* The stem waveform's interior is lighter than the button's: 9/16 of the bright, about halfway
   between the button interior and the outline, so the shape reads at a glance and the outline still shows. */
static inline uint32_t sh_stem_wave_fill(uint32_t bright) {
 uint32_t r=(bright>>16)&255,g=(bright>>8)&255,b=bright&255;
 return ((r*9>>4)<<16)|((g*9>>4)<<8)|(b*9>>4);
}
/* KEY SYNC blinks the deck's MASTER TEMPO lamp: lit for the first half of each period of the model clock.
   Pure in clock_ms, so a frame is the same however often it is drawn. */
enum { SH_KEY_SYNC_BLINK_MS=500 };
static inline int sh_key_sync_lit(uint32_t clock_ms) { return !((clock_ms/SH_KEY_SYNC_BLINK_MS)&1); }
/* NEEDLE LOCK holds for this deck now: set, and the deck plays (shell_play.c). */
int sh_needle_locked(const struct xz_shell_model *, const struct xz_shell_deck *);
/* BROWSE with its category rail holding the encoder (shell_browse.c). */
int sh_browse_at_root(const struct xz_shell *);
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
/* The four-deck view's status strip under the non-play screens (shell_wave.c): four compact quarters in
   band. Returns 0 and draws nothing for a two-deck model, whose screens draw their own halves.
   sh_strip_cell is a deck's touch target in the strip: a half of band for two decks (a NULL model), a
   quarter for four. */
int sh_deck_quarters(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect band);
struct xz_shell_rect sh_strip_cell(const struct xz_shell_model *m, struct xz_shell_rect band, int deck);
/* Traffic-light ink for a key against the master deck's key (shell_wave.c): green, amber or plain. With
   no master key read it falls back to the runtime's matches_master flag. */
enum { SH_KEY_GREEN=0x52d794, SH_KEY_AMBER=0xffbd59 };
uint32_t sh_key_ink(const struct xz_shell_model *m, const char *key, int matches_master, uint32_t plain);
/* The key in the chosen notation; key itself when it is NULL, already in it, or neither notation. */
const char *sh_key_text(const char *key, enum xz_shell_key_display mode, char *buffer, size_t cap);
/* DECK VIEW on PLAY (shell_play.c): a point the open picker owns although the header band covers it;
   BACK and other keys first close the picker or the encoder focus (1: BACK was used); the overlay token
   input must match is published after each complete frame. */
int sh_play_overlay_owns(const struct xz_shell *, const struct xz_shell_model *, int x, int y);
int sh_play_dismiss(struct xz_shell *, int back);
void sh_play_overlay_publish(struct xz_shell *, const struct xz_shell_model *);
/* Mod header buttons (shell.c): drawn over every screen; hit tests padded to XZ_SHELL_HEADER_HIT_H. */
void sh_header_buttons(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c);
enum xz_shell_action_kind sh_header_hit(int x, int y);
/* Runtime notice toast (shell_draw.c): shown, its box, and how many lines it wraps to (1 or 2). */
int sh_notice_shown(const struct xz_shell_model *m);
struct xz_shell_rect sh_notice_rect(const char *text, int *lines);
void sh_notice(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c);
#endif
