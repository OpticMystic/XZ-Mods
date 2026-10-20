#ifndef XZ_MOD_RUNTIME_H
#define XZ_MOD_RUNTIME_H
#include <stddef.h>
#include <stdint.h>

/* Constructor/loader thread only. Known ARM prologues, never a generic patch API. */
int xz_hook_arm(uint32_t address, const unsigned char expected[8],
                void *replacement, void **original);
int xz_hook_slot(uint32_t address, uint32_t expected, void *replacement, void **original);
/* Guarded reads outside audio callbacks; zero means the complete range was copied. */
int xz_read_memory(uint32_t address, void *out, size_t length);
void xz_log(const char *message);
int xz_runtime_set_cues(int gate, int smart);
unsigned xz_runtime_cue_flags(void);
int xz_runtime_beat_jump(int deck, int beats);
/* IKeyManager::onKey hook, before the shell: SHIFT edges and the LOAD / TIME MODE / QUANTIZE keys of a
   side driving DECK 3 / 4 (deck_select.h). 0: the shell and stock see the key as usual; 1: the key is taken,
   neither sees it; XZ_RUNTIME_KEY_SHELL_LOAD + side: the press of LOAD on that side while the FLAGSHIP
   shell owns the screen, which the caller hands to xz_shell_runtime_extra_load_key (the key is taken). */
#define XZ_RUNTIME_KEY_SHELL_LOAD 2
int xz_runtime_deck_global_key(uint16_t code, unsigned channel, unsigned operation);
/* SHORTCUT DECK SELECT: switches a side (0 left, 1 right) as SHIFT pressed twice does. 1 switched, 0 the CH3/CH4
   route is down (the caller shows XZ_DS_NO_ROUTE_TEXT), -1 no players or no adapter. Takes only deck select's
   own lock, so the shell may call it under its lock. */
int xz_runtime_deck_select_toggle(int side);
#endif
