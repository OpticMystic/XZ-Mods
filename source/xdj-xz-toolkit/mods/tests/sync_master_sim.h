/* SPDX-License-Identifier: MIT */
/* Seams between the translation units of the SYNC/MASTER simulation. The
 * real modules keep clashing static names (now_ms, put, ...), so each one is
 * included into its own unit and exports only these plain functions. */
#ifndef SYNC_MASTER_SIM_H
#define SYNC_MASTER_SIM_H
#include <stdint.h>
#include "../extra_decks.h"

/* One simulated monotonic clock, in microseconds, for every module. */
extern uint64_t sim_us;

/* sync_master_sim_decks.c: the real extra_decks.c (snapshot, projection, input). */
struct xz_xp_players;
void sim_decks_attach(struct xz_xp_players *p,uint32_t epoch);
void sim_decks_route(int up,uint32_t epoch);

/* sync_master_sim_bridge.c: forget lifetime counters so each seed starts as a fresh process. */
void sim_bridge_reset(void);
void sim_bridge_debug(char *out,unsigned n);

/* sync_master_sim_led.c: the real ui/native_led.c hook and the stock LED packet. */
enum sim_packet { SIM_PACKET_ONE, SIM_PACKET_MISSING, SIM_PACKET_DUPLICATE, SIM_PACKET_FORCED, SIM_PACKETS };
struct sim_led_row { int present,forced_by_stock; uint32_t state; };
/* Stock state written for channel 1/2 by the stock player callback. */
struct sim_led_stock { uint32_t sync[2],master[2]; };
void sim_led_init(int rebuild_each_callback);
/* Runs one stock LED callback for channel 1 or 2, with our hook installed.
 * late != 0 lets an ordinary stock writer run again after our hook. */
void sim_led_callback(unsigned channel,enum sim_packet variant,int late,const struct sim_led_stock *stock);
/* What the packet carries after the callback (what the hardware is sent). */
void sim_led_rows(unsigned channel,struct sim_led_row *sync,struct sim_led_row *master);
void sim_led_stop(void);
void sim_led_start(void);
#endif
