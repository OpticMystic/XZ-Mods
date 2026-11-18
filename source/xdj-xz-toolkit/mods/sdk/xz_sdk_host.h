/* SPDX-License-Identifier: MIT */
#ifndef XZ_SDK_HOST_H
#define XZ_SDK_HOST_H
#include <stdint.h>
#define XZ_SDK_PACKAGES 32
#define XZ_SDK_ROOTS 8
#define XZ_SDK_ID 64
#define XZ_SDK_NAME 64
#define XZ_SDK_PATH 1024
#define XZ_SDK_PROFILE_BYTES 2048
#define XZ_SDK_PACKAGE_BYTES (16u * 1024u * 1024u)
#define XZ_SDK_FILES 32
#define XZ_SDK_DEFAULT_APPLICATIONS_FOLDER "VJ.Tools/Applications"
#define XZ_SDK_DEFAULT_AUTOLOAD_FOLDER "VJ.Tools/Autoload"

enum xz_sdk_kind { XZ_SDK_APPLICATION=1, XZ_SDK_EXTENSION, XZ_SDK_PROFILE };
enum xz_sdk_state { XZ_SDK_READY=1, XZ_SDK_RUNNING, XZ_SDK_FAILED, XZ_SDK_BLOCKED };
enum xz_sdk_command_kind { XZ_SDK_RUN=1, XZ_SDK_STOP, XZ_SDK_SCAN, XZ_SDK_AUTOLOAD };
struct xz_sdk_item {
    char id[XZ_SDK_ID], name[XZ_SDK_NAME], version[32], detail[96];
    uint32_t kind, state, autoload;
};
struct xz_sdk_snapshot {
    uint32_t generation, count, scanning, autoload_enabled, foreground;
    char status[96];
    struct xz_sdk_item items[XZ_SDK_PACKAGES];
};
/* RUN value=1 explicitly requests saving a profile; automatic runs use 0. */
struct xz_sdk_command { uint32_t kind, generation, index, value; };
struct xz_sdk_host_config {
    /* Absolute mount roots. Relative application/autoload folders are configurable. */
    const char *roots[XZ_SDK_ROOTS];
    unsigned root_count;
    const char *applications_folder, *autoload_folder, *stage_root;
    int autoload_enabled;
    /* Called ONLY from manager worker with no manager mutex held. */
    int (*apply_profile)(void *context, const char *record, int persist);
    int (*display_available)(void *context);
    void *context;
};
/* One bounded worker owns discovery, hashing, staging, spawn, reaping and removal.
 * start/stop are lifecycle-only. read/submit use trylock and never perform IO.
 * Package code always runs in a separate process with LD_PRELOAD removed. */
int xz_sdk_host_start(const struct xz_sdk_host_config *);
void xz_sdk_host_stop(void);
int xz_sdk_host_read(struct xz_sdk_snapshot *);
int xz_sdk_host_submit(const struct xz_sdk_command *);
/* Includes the brief display reservation, for atomic game/admission exclusion.
 * UI takeover follows snapshot.foreground, which means a child is running. */
int xz_sdk_host_foreground(void);
#endif
