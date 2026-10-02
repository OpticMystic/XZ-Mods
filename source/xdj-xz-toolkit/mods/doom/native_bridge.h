/* SPDX-License-Identifier: MIT
 * Process-neutral input protocol; no Doom engine code links into the DJ app. */
#ifndef XZ_GAME_NATIVE_BRIDGE_H
#define XZ_GAME_NATIVE_BRIDGE_H
#include <stdint.h>
#define XZ_GAME_INPUT_PATH "/dev/shm/xz-doom-input-v1"
#define XZ_GAME_INPUT_MAGIC UINT32_C(0x58444731)
struct xz_game_input {
    uint32_t magic,version,owner,heartbeat;
    uint32_t buttons[2],jog[2],jog_time[2];
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
