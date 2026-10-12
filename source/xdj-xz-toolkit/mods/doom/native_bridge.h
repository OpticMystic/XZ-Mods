/* SPDX-License-Identifier: MIT
 * Process-neutral input protocol; no Doom engine code links into the DJ app. */
#ifndef XZ_GAME_NATIVE_BRIDGE_H
#define XZ_GAME_NATIVE_BRIDGE_H
#include <stdint.h>
#define XZ_GAME_INPUT_PATH "/dev/shm/xz-doom-input-v2"
#define XZ_GAME_INPUT_MAGIC UINT32_C(0x58444731)
#define XZ_GAME_INPUT_VERSION 2u
/* Browse encoder, BACK and the menu key arrive as discrete taps. The DJ app
 * writes nav[head % XZ_GAME_NAV] then publishes head; the game keeps its own
 * read position, so a burst of detents is never merged into one press. */
#define XZ_GAME_NAV 16u
enum xz_game_nav_key { XZ_GAME_NAV_UP=1, XZ_GAME_NAV_DOWN, XZ_GAME_NAV_ENTER, XZ_GAME_NAV_BACK, XZ_GAME_NAV_QUIT };
struct xz_game_input {
    uint32_t magic,version,owner,heartbeat;
    uint32_t buttons[2],jog[2],jog_time[2];
    uint32_t nav_head,nav[XZ_GAME_NAV];
};
struct xz_game_native { struct xz_game_input *input; uint32_t owned[2]; };
uint32_t xz_game_milliseconds(void);
int xz_game_active(const struct xz_game_input *,uint32_t now);
int xz_game_decode(struct xz_game_native *,int deck,const void *input,uint32_t now);
int xz_doom_bridge_start(void);
int xz_doom_physical_key(void *self,const void *input);
int xz_doom_ready(void);
int xz_doom_launch(void);
int xz_doom_running(void);
/* Queues one navigation tap for a running game; 0 when none is running. */
int xz_game_nav_send(struct xz_game_input *,enum xz_game_nav_key,uint32_t now);
int xz_doom_nav(enum xz_game_nav_key);
/* Another local display owner, such as the XZ SDK foreground app. NULL clears it. */
void xz_game_display_owner(int (*active)(void));
struct xz_doom_browser {
    char labels[6][96],details[6][64],selection[96],status[96];
    int count,offset,total,base,map,scanning,can_play,chex;
};
void xz_doom_browser_scan(void);
void xz_doom_browser_read(int offset,struct xz_doom_browser *);
int xz_doom_browser_select(int index,char reason[96]);
void xz_doom_browser_clear_map(void);
int xz_doom_choose_game(int chex);
#endif
