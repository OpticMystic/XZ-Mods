/* SPDX-License-Identifier: GPL-2.0-only */
/* The real connector-side snapshot, master projection and input entry
 * (extra_decks.c) on simulated time. The connector thread is not started; the
 * simulation calls xz_extra_decks_snapshot from its scheduled callers. */
#define _GNU_SOURCE
#include "sync_master_sim_clock.h"
#include "../extra_decks.c"
/* The players use the simulation's transport; the route sinks above are never called. */
xz_usb_result xz_usb_transport_submit(xz_usb_transport *t,xz_usb_deck d,uint32_t g,const float *s,uint32_t n,uint64_t a){
 (void)t;(void)d;(void)g;(void)s;(void)n;(void)a;return XZ_USB_STOPPED;}
xz_usb_result xz_usb_transport_flush_and_submit(xz_usb_transport *t,xz_usb_deck d,uint32_t e,const float *s,uint32_t n,uint64_t a,uint32_t *g){
 (void)t;(void)d;(void)e;(void)s;(void)n;(void)a;(void)g;return XZ_USB_STOPPED;}
xz_usb_result xz_usb_transport_flush(xz_usb_transport *t,xz_usb_deck d,uint32_t *g){(void)t;(void)d;(void)g;return XZ_USB_STOPPED;}
xz_usb_result xz_usb_transport_position(xz_usb_transport *t,xz_usb_position *o){(void)t;if(o)memset(o,0,sizeof *o);return XZ_USB_STOPPED;}
void sim_decks_attach(struct xz_xp_players *p,uint32_t epoch){
 __atomic_store_n(&players,p,__ATOMIC_RELEASE);
 control_update(CONTROL_AVAILABLE,p?CONTROL_AVAILABLE:0);
 sim_decks_route(p&&epoch,epoch);
}
void sim_decks_route(int up,uint32_t epoch){
 __atomic_store_n(&link_epoch,up?epoch:0,__ATOMIC_RELEASE);
 control_update(CONTROL_ROUTE,up?CONTROL_ROUTE:0);
}
