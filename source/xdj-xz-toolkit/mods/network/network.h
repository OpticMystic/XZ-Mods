/* SPDX-License-Identifier: MIT */
#ifndef XZ_NETWORK_SERVICE_H
#define XZ_NETWORK_SERVICE_H
#include <stddef.h>
#include <stdint.h>

#define XZ_NET_ADAPTERS 8
#define XZ_NET_ADDRESSES 4
#define XZ_NET_IFNAME 16
#define XZ_NET_DETAIL 128

enum xz_net_known { XZ_NET_UNKNOWN, XZ_NET_NO, XZ_NET_YES };
enum xz_net_owner { XZ_NET_OWNER_UNKNOWN, XZ_NET_OWNER_NATIVE,
                   XZ_NET_OWNER_MODS, XZ_NET_OWNER_OTHER };
enum xz_net_ip_origin { XZ_NET_ORIGIN_UNKNOWN, XZ_NET_ORIGIN_STATIC_CONFIRMED,
                       XZ_NET_ORIGIN_DHCP_LEASE, XZ_NET_ORIGIN_AUTOIP_CONFIRMED };
enum xz_net_mode { XZ_NET_KEEP, XZ_NET_DHCP, XZ_NET_STATIC };
enum xz_net_static_dhcp { XZ_NET_STATIC_KEEP_DHCP, XZ_NET_STATIC_DISABLE_DHCP };
enum xz_net_gateway_mode { XZ_NET_GATEWAY_KEEP, XZ_NET_GATEWAY_NONE,
                          XZ_NET_GATEWAY_SET };
enum xz_net_result {
    XZ_NET_OK, XZ_NET_PENDING, XZ_NET_BUSY, XZ_NET_INVALID, XZ_NET_STALE,
    XZ_NET_NEEDS_DETECT, XZ_NET_NO_ADAPTER, XZ_NET_LINK_DOWN,
    XZ_NET_INCOMPLETE_SNAPSHOT, XZ_NET_ERR_OWNER_UNKNOWN, XZ_NET_NATIVE_OWNS,
    XZ_NET_NEEDS_BACKEND, XZ_NET_NEEDS_DHCP_CLIENT, XZ_NET_UNSAFE_CLIENT,
    XZ_NET_NEEDS_BINDING, XZ_NET_TIMEOUT, XZ_NET_CONFLICT,
    XZ_NET_VERIFY_FAILED, XZ_NET_ROLLED_BACK, XZ_NET_ROLLBACK_FAILED,
    XZ_NET_CANCELLED
};
enum xz_net_cap {
    XZ_NET_CAP_STATIC_ADD = 1u << 0,    /* Keeps every pre-existing valid address. */
    XZ_NET_CAP_DHCP_START = 1u << 1,    /* Existing client, old address retained. */
    XZ_NET_CAP_DHCP_STOP = 1u << 2,     /* Verified control of actual DHCP owner. */
    XZ_NET_CAP_GATEWAY = 1u << 3,       /* Changes MODS-owned route only. */
    XZ_NET_CAP_BINDING = 1u << 4,       /* VJ.Tools path can select this adapter. */
    XZ_NET_CAP_RESTORE = 1u << 5        /* Tested rollback for requested operation. */
};

struct xz_net_ipv4 { uint8_t octet[4]; };
struct xz_net_address {
    struct xz_net_ipv4 ip;
    uint8_t prefix;
    enum xz_net_ip_origin origin;
};
struct xz_net_identity {
    char name[XZ_NET_IFNAME];
    uint32_t ifindex;
    uint8_t mac[6];
    enum xz_net_known mac_known;
    uint64_t identity_epoch; /* Detects deletion/reuse of ifindex/name. */
};
struct xz_net_adapter {
    struct xz_net_identity id;
    enum xz_net_known admin_up, link_up, addresses_known, gateway_known;
    enum xz_net_known dhcp_enabled, owned_dhcp_enabled;
    uint8_t competing_client;
    enum xz_net_owner dhcp_owner, network_owner;
    unsigned caps;
    uint8_t address_count, addresses_truncated;
    struct xz_net_address address[XZ_NET_ADDRESSES];
    uint8_t gateway_present, gateway_ambiguous;
    struct xz_net_ipv4 gateway;
    uint64_t state_revision; /* Address/route/owner/capability changes. */
    enum xz_net_result setup_reason;
    char detail[XZ_NET_DETAIL];
};
struct xz_net_snapshot {
    uint64_t generation, observed_ms;
    uint8_t count, inventory_truncated;
    struct xz_net_adapter adapter[XZ_NET_ADAPTERS];
};
struct xz_net_static {
    struct xz_net_address address;
    enum xz_net_static_dhcp dhcp; /* Static addition alone never proves DHCP OFF. */
    enum xz_net_gateway_mode gateway_mode;
    struct xz_net_ipv4 gateway;
};
struct xz_net_preference {
    struct xz_net_identity adapter;
    enum xz_net_mode mode;
    struct xz_net_static static_ip;
};
struct xz_net_policy {
    uint32_t schema; /* 1. Separate policy, never appended to settings CSV. */
    uint8_t count;
    uint8_t request_route; /* Explicit additive MODS default-route request. */
    struct xz_net_preference order[XZ_NET_ADAPTERS];
};
struct xz_net_trial {
    struct xz_net_policy policy;
    struct xz_net_identity selected; /* Exact adapter named on Apply review. */
    uint64_t expected_generation, expected_state_revision;
};
enum xz_net_command_kind { XZ_NET_CMD_DETECT, XZ_NET_CMD_APPLY_TRIAL,
                          XZ_NET_CMD_VERIFY, XZ_NET_CMD_REVERT, XZ_NET_CMD_CANCEL };
struct xz_net_command {
    enum xz_net_command_kind kind;
    uint64_t request_id;
    union {
        struct xz_net_trial apply;
        uint64_t trial_id; /* VERIFY, REVERT, CANCEL. Never cancels another job. */
    } body;
};
enum xz_net_phase { XZ_NET_IDLE, XZ_NET_DETECTING, XZ_NET_PREFLIGHT,
                    XZ_NET_APPLYING, XZ_NET_VERIFYING, XZ_NET_RESTORING,
                    XZ_NET_TRIAL_READY, XZ_NET_NEEDS_SETUP, XZ_NET_FAILED };
struct xz_net_view {
    struct xz_net_snapshot actual;
    struct xz_net_policy applied; /* Set only after complete verification. */
    enum xz_net_phase phase;
    enum xz_net_result result;
    uint64_t request_id, trial_id;
    uint8_t applied_known, advertised_known, rollback_available;
    struct xz_net_identity advertised_adapter;
    struct xz_net_address advertised;
    char detail[XZ_NET_DETAIL];
};
struct xz_net_error { enum xz_net_result code; char detail[XZ_NET_DETAIL]; };

/* Text and OS outputs are untrusted boundaries. Validation returns specific errors.
 * Canonical IPv4 only. Netmask must be contiguous. No shell text reaches a backend. */
enum xz_net_result xz_net_parse_ipv4(const char *, struct xz_net_ipv4 *);
enum xz_net_result xz_net_parse_mask(const char *, uint8_t *prefix);
enum xz_net_result xz_net_validate_identity(const struct xz_net_identity *);
enum xz_net_result xz_net_validate_trial(const struct xz_net_trial *,
                                      const struct xz_net_snapshot *, struct xz_net_error *);

/* prepare captures a complete rollback journal, checks actual native ownership and
 * runtime admission, and does no mutation. A ticket has an operation deadline.
 * apply/restore revalidate affected state immediately before each mutation.
 * All callbacks run only on the service worker and honor monotonic deadlines.
 * A real backend uses documented OS APIs and fixed argv for a detected client.
 * Private backend tickets retain all addresses/routes/owner evidence, beyond UI limits. */
struct xz_net_control {
    void *context;
    uint64_t deadline_ms;
    uint64_t (*now_ms)(void *);
    int (*cancelled)(void *);
};
int xz_net_control_expired(const struct xz_net_control *);
const char *xz_net_result_text(enum xz_net_result);
int xz_net_identity_equal(const struct xz_net_identity *, const struct xz_net_identity *);
void xz_net_ipv4_text(struct xz_net_ipv4, char out[16]);
struct xz_net_backend_ticket;
struct xz_net_backend {
    void *context;
    uint64_t (*now_ms)(void *);
    enum xz_net_result (*detect)(void *, const struct xz_net_control *,
                                 struct xz_net_snapshot *, struct xz_net_error *);
    enum xz_net_result (*prepare)(void *, const struct xz_net_trial *, const struct xz_net_control *,
                                  struct xz_net_backend_ticket **, struct xz_net_error *);
    enum xz_net_result (*apply)(void *, struct xz_net_backend_ticket *, const struct xz_net_control *,
                                struct xz_net_error *);
    enum xz_net_result (*verify)(void *, struct xz_net_backend_ticket *, const struct xz_net_control *,
                                 struct xz_net_snapshot *, struct xz_net_error *);
    enum xz_net_result (*restore)(void *, struct xz_net_backend_ticket *, const struct xz_net_control *,
                                  struct xz_net_snapshot *, struct xz_net_error *);
    enum xz_net_result (*maintain)(void *, struct xz_net_backend_ticket *, const struct xz_net_control *, struct xz_net_view *);
    void (*describe)(void *, struct xz_net_backend_ticket *, struct xz_net_view *);
    void (*release)(void *, struct xz_net_backend_ticket *);
};
struct xz_net_service;
/* start owns one worker. submit copies one immutable command; never performs I/O.
 * try_read copies a completed view without blocking render or input.
 * stop must not abandon a mutation: cancel/restore with its separate reserved budget.
 * No save or boot-auto-apply entry point exists in the trial service. */
enum xz_net_result xz_net_service_start(struct xz_net_service **, const struct xz_net_backend *);
enum xz_net_result xz_net_service_submit(struct xz_net_service *, const struct xz_net_command *);
enum xz_net_result xz_net_service_try_read(struct xz_net_service *, struct xz_net_view *);
enum xz_net_result xz_net_service_stop(struct xz_net_service *, uint64_t deadline_ms);
#endif
