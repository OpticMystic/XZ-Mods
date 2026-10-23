/* SPDX-License-Identifier: MIT */
#ifndef XZ_NETWORK_LINUX_H
#define XZ_NETWORK_LINUX_H
#include "network.h"
#define XZ_NET_LEASE_BYTES 64
#define XZ_NET_EVENT_FD 9
#define XZ_NET_LIFETIME_FD 4
#define XZ_NET_STRING_INNER(x) #x
#define XZ_NET_STRING(x) XZ_NET_STRING_INNER(x)
#define XZ_NET_EVENT_ENV "XZ_NETWORK_EVENT_FD=" XZ_NET_STRING(XZ_NET_EVENT_FD)
enum xz_net_lease_kind { XZ_NET_LEASE_BOUND=1, XZ_NET_LEASE_RENEW, XZ_NET_LEASE_DECONFIG, XZ_NET_LEASE_FAILURE };
struct xz_net_lease {
    enum xz_net_lease_kind kind;
    char interface[XZ_NET_IFNAME];
    struct xz_net_address address;
    struct xz_net_ipv4 gateway;
    uint32_t seconds;
    uint8_t has_gateway;
};
enum xz_net_result xz_net_lease_encode(const struct xz_net_lease *,unsigned char out[XZ_NET_LEASE_BYTES]);
enum xz_net_result xz_net_lease_decode(const unsigned char in[XZ_NET_LEASE_BYTES],struct xz_net_lease *);
/* OS boundary, injected in fixtures. Only production linux_os.c invokes OS networking.
 * add=1 must use exclusive creation; add=0 deletes only an exact matching object.
 * inventory is zero-initialized by the caller. now/poll obey monotonic deadlines. */
struct xz_net_linux_io {
    void *context;
    uint64_t (*now_ms)(void *);
    enum xz_net_result (*inventory)(void *,const struct xz_net_control *,struct xz_net_snapshot *,struct xz_net_error *);
    enum xz_net_result (*address)(void *,int add,const struct xz_net_identity *,const struct xz_net_address *,const struct xz_net_control *);
    enum xz_net_result (*route)(void *,int add,const struct xz_net_identity *,struct xz_net_ipv4,unsigned metric,const struct xz_net_control *);
    int (*route_present)(void *,const struct xz_net_identity *,struct xz_net_ipv4,unsigned metric);
    enum xz_net_result (*client_start)(void *,const struct xz_net_identity *,uint64_t *token,const struct xz_net_control *);
    enum xz_net_result (*client_poll)(void *,uint64_t token,struct xz_net_lease *,unsigned wait_ms,const struct xz_net_control *);
    enum xz_net_result (*client_stop)(void *,uint64_t token,const struct xz_net_control *);
};
struct xz_net_linux;
struct xz_net_linux_config {
    const char *client_path,*event_path;
    int client_contract_verified; /* Root verifies pinned udhcpc flags and FD inheritance. */
};
enum xz_net_result xz_net_linux_create(struct xz_net_linux **,struct xz_net_backend *,const struct xz_net_linux_io *);
void xz_net_linux_destroy(struct xz_net_linux *);
struct xz_net_linux_os;
enum xz_net_result xz_net_linux_os_create(struct xz_net_linux_os **,struct xz_net_linux_io *,const struct xz_net_linux_config *);
void xz_net_linux_os_destroy(struct xz_net_linux_os *);
#endif
