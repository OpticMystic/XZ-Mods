/* SPDX-License-Identifier: MIT */
#ifndef XZ_KART_RUNTIME_H
#define XZ_KART_RUNTIME_H
#include <stdint.h>
struct xz_kart_launch { uint32_t epoch;unsigned mask;char epoch_env[64],mask_env[64],display_env[64]; };
int xz_kart_runtime_start(void);
void xz_kart_runtime_stop(void);
void xz_kart_runtime_display_probe(int (*probe)(void));
int xz_kart_runtime_input_pending(void);
int xz_kart_runtime_observe_key(unsigned channel,const void *event);
int xz_kart_runtime_physical(void *self,const void *event);
/* Only the existing SDK worker may prepare/attach after display arbitration. */
int xz_kart_runtime_prepare(const char *package_id,unsigned kind,struct xz_kart_launch *);
int xz_kart_runtime_attach(uint32_t pid,const struct xz_kart_launch *);
void xz_kart_runtime_release(uint32_t pid);
int xz_kart_runtime_signal_target(int launcher_pid);
void xz_kart_runtime_forget(uint32_t launcher_pid);
/* SDK worker only. Retain ownership and files if bounded group drain fails. */
int xz_kart_runtime_drain(uint32_t launcher_pid);
int xz_kart_runtime_group_pending(void);
void xz_kart_runtime_cleanup_profile(uint32_t launcher_pid,uint32_t epoch);
const char *xz_kart_runtime_sdk_test_root(void);
#endif
