/* SPDX-License-Identifier: MIT */
#ifndef XZ_KART_INPUT_H
#define XZ_KART_INPUT_H
#include <stddef.h>
#include <stdint.h>
#ifndef XZ_KART_INPUT_PATH
#define XZ_KART_INPUT_PATH "/dev/shm/xz-kart-input-v2"
#endif
#define XZ_KART_INPUT_VERSION 2u
#define XZ_KART_INPUT_MAGIC UINT32_C(0x584b4931)
#define XZ_KART_LEASE_MS 750u
#define XZ_KART_STARTUP_MS 30000u
#define XZ_KART_JOG_MS 100u
#define XZ_KART_EXIT_MS 1000u
#define XZ_KART_HISTORY 32u
enum xz_kart_pad { XZ_KART_ACCEL=1u, XZ_KART_DRIFT=2u, XZ_KART_ITEM=4u,
 XZ_KART_BRAKE=8u, XZ_KART_MODE=32u, XZ_KART_MENU=64u, XZ_KART_EXIT=128u, XZ_KART_TOUCH=256u };
#define XZ_KART_FADER_BUTTON 128u
/* Native little-endian ARM/x86 ABI, aligned 32-bit atomic words only.
 * Host writes grant/payload. The granted child writes only client_ms/epoch. */
struct xz_kart_transition { uint32_t sequence,buttons,when; };
struct xz_kart_wire {
 uint32_t magic,version,bytes,grant_seq,owner,epoch,deck_mask,host_ms;
 uint32_t client_ms,client_epoch,payload_seq,payload_epoch;
 uint32_t buttons[2],jog_delta[2],jog_ms[2];
 uint32_t event_seq[2];
 struct xz_kart_transition events[2][XZ_KART_HISTORY];
 uint32_t requested_owner,requested_epoch;
 uint32_t armed_mask;
 /* Client requests a mode; host acknowledges it and requires a fresh center. */
 uint32_t requested_fader_mask,fader_mask,fader_value[2],fader_ready_mask;
 uint32_t requested_mode_seq[2],acknowledged_mode_seq[2];
};
struct xz_kart_host {
 struct xz_kart_wire *wire;
 uint32_t owned[2],blocked[2];
 uint32_t physical_down[2];
 unsigned display_ready;
};
struct xz_kart_sample { uint32_t buttons; int16_t turn; int8_t throttle; unsigned fader_mode,fader_ready; };
struct xz_kart_reader {
 uint32_t owner,epoch,mask,previous[2],exit_started[2];
 unsigned exit_armed[2];
 uint32_t cursor[2],delivered[2];
 unsigned overflow[2];
};
struct xz_kart_command {
 int16_t turn;
 int8_t forward;
 uint16_t buttons;
 unsigned menu_pressed,exit_requested,mode_pressed;
};
void xz_kart_host_init(struct xz_kart_host *,struct xz_kart_wire *);
/* Host calls are serialized by its physical-input/lease mutex. No IO here. */
int xz_kart_host_grant(struct xz_kart_host *,uint32_t owner,uint32_t epoch,unsigned mask,uint32_t now);
/* Runtime must verify this is the granted launcher's actual child first. */
int xz_kart_host_adopt(struct xz_kart_host *,uint32_t launcher,uint32_t child,uint32_t epoch,uint32_t now);
void xz_kart_host_revoke(struct xz_kart_host *);
void xz_kart_host_tick(struct xz_kart_host *,uint32_t now);
void xz_kart_host_display(struct xz_kart_host *,int ready);
int xz_kart_host_event(struct xz_kart_host *,unsigned deck,const void *,size_t,uint32_t now);
int xz_kart_host_player_event(struct xz_kart_host *,const void *self,size_t self_bytes,
 const void *event,size_t event_bytes,uint32_t now);
int xz_kart_read(struct xz_kart_reader *,struct xz_kart_wire *,unsigned deck,uint32_t now,struct xz_kart_sample *);
void xz_kart_command(struct xz_kart_reader *,unsigned seat,const struct xz_kart_sample *,
 int active,uint32_t now,struct xz_kart_command *);
#endif
