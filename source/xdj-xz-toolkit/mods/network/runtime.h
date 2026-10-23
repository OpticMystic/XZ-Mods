/* SPDX-License-Identifier: MIT. Root calls start/stop in the existing lifecycle. */
#ifndef XZ_NETWORK_RUNTIME_H
#define XZ_NETWORK_RUNTIME_H
#include "linux.h"
enum xz_net_result xz_net_runtime_start(const struct xz_net_linux_config *);
enum xz_net_result xz_net_runtime_stop(unsigned budget_ms);
enum xz_net_result xz_net_runtime_submit(const struct xz_net_command *) __attribute__((weak));
enum xz_net_result xz_net_runtime_read(struct xz_net_view *) __attribute__((weak));
enum xz_net_result xz_net_runtime_advertised(char ip[16],char adapter[16]);
/* Receiver ABI: 1 verified canonical address, 0 unavailable and all 16 bytes cleared.
 * Snapshot only: no I/O and no blocking lock. NULL returns 0. */
int xz_mods_network_address_v1(char out[16]) __attribute__((visibility("default")));
#endif
