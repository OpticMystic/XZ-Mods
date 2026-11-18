/* SPDX-License-Identifier: MIT */
#ifndef XZ_NETWORK_UI_TYPES_H
#define XZ_NETWORK_UI_TYPES_H
#include "network.h"
enum xz_network_step { XZ_NETWORK_DETECT, XZ_NETWORK_CHOOSE, XZ_NETWORK_REVIEW, XZ_NETWORK_VERIFY };
enum xz_network_field { XZ_NETWORK_IP, XZ_NETWORK_MASK, XZ_NETWORK_GATEWAY };
struct xz_network_ui_draft {
    struct xz_net_trial trial;
    enum xz_network_step step;
    enum xz_network_field field;
    int selected,adapter_offset,initialized,setup_reason;
    uint64_t revision;
    char ip[16],mask[16],gateway[16],edit[16];
};
#endif
