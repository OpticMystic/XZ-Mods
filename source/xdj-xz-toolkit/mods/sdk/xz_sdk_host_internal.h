/* SPDX-License-Identifier: MIT */
#ifndef XZ_SDK_HOST_INTERNAL_H
#define XZ_SDK_HOST_INTERNAL_H

#include "xz_sdk_host.h"

#include <stdint.h>
#include <sys/types.h>

#include "xz_sdk_sha256.h"

#define XZ_SDK_MANIFEST_BYTES 4096u
#define XZ_SDK_FILE_NAME 256u
#define XZ_SDK_SCAN_MESSAGE 96u

#if defined(__GNUC__)
#define XZ_SDK_INTERNAL __attribute__((visibility("hidden")))
#else
#define XZ_SDK_INTERNAL
#endif

struct xz_sdk_file_record {
    char name[XZ_SDK_FILE_NAME];
    uint8_t digest[XZ_SHA256_BYTES];
};

struct xz_sdk_manifest {
    char id[XZ_SDK_ID];
    char name[XZ_SDK_NAME];
    char version[32];
    char entry[XZ_SDK_FILE_NAME];
    uint32_t kind;
    uint32_t autoload;
    uint32_t file_count;
    struct xz_sdk_file_record files[XZ_SDK_FILES];
};

struct xz_sdk_source {
    char root[XZ_SDK_PATH];
    char folder[XZ_SDK_PATH];
    char package[XZ_SDK_FILE_NAME];
    dev_t root_device;
    ino_t root_inode;
    dev_t folder_device;
    ino_t folder_inode;
    dev_t package_device;
    ino_t package_inode;
};

struct xz_sdk_package_record {
    struct xz_sdk_item view;
    struct xz_sdk_manifest manifest;
    struct xz_sdk_source source;
    uint8_t content_digest[XZ_SHA256_BYTES];
    int valid;
    int automatic;
    int root_autoload_enabled;
    int conflicted;
};

struct xz_sdk_scan_spec {
    const char *roots[XZ_SDK_ROOTS];
    unsigned root_count;
    const char *applications_folder;
    const char *autoload_folder;
    int (*cancelled)(void *context);
    void *cancel_context;
};

struct xz_sdk_scan_result {
    struct xz_sdk_package_record packages[XZ_SDK_PACKAGES];
    uint32_t count;
    uint32_t candidates_seen;
    uint32_t invalid_count;
    uint32_t warning_count;
    uint32_t limit_reached;
    uint32_t any_root_autoload_enabled;
    char message[XZ_SDK_SCAN_MESSAGE];
};

struct xz_sdk_stage {
    char base[XZ_SDK_PATH];
    char name[XZ_SDK_FILE_NAME];
    char path[XZ_SDK_PATH];
    char entry_path[XZ_SDK_PATH];
    dev_t base_device;
    ino_t base_inode;
    dev_t directory_device;
    ino_t directory_inode;
    uint32_t file_count;
    char files[XZ_SDK_FILES][XZ_SDK_FILE_NAME];
    int active;
};

XZ_SDK_INTERNAL int xz_sdk_internal_scan(
    const struct xz_sdk_scan_spec *spec,
    struct xz_sdk_scan_result *result);

/* Cheap bounded insertion/config probe. It never hashes payload bytes. */
XZ_SDK_INTERNAL int xz_sdk_internal_probe(
    const struct xz_sdk_scan_spec *spec,
    uint64_t *signature);

XZ_SDK_INTERNAL int xz_sdk_internal_stage(
    const struct xz_sdk_package_record *package,
    const char *stage_root,
    uint64_t nonce,
    const struct xz_sdk_scan_spec *spec,
    struct xz_sdk_stage *stage,
    char error[XZ_SDK_SCAN_MESSAGE]);

XZ_SDK_INTERNAL int xz_sdk_internal_source_present(
    const struct xz_sdk_source *source);

XZ_SDK_INTERNAL void xz_sdk_internal_stage_cleanup(
    struct xz_sdk_stage *stage);

/* Worker-only ownership journal and bounded recovery of dead-host RAM stages. */
XZ_SDK_INTERNAL int xz_sdk_internal_stage_child(
    const struct xz_sdk_stage *stage, pid_t child);
XZ_SDK_INTERNAL int xz_sdk_internal_recover(
    const char *stage_root, const struct xz_sdk_scan_spec *spec);

#endif
