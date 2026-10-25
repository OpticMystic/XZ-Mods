/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_SUPPORT_H
#define XZ_NATIVE_SUPPORT_H
#include <stdint.h>

enum xz_support_phase {
    XZ_SUPPORT_IDLE = 0,
    XZ_SUPPORT_WORKING,
    XZ_SUPPORT_PREPARED,
    XZ_SUPPORT_STORED,
    XZ_SUPPORT_EXPORTED,
    XZ_SUPPORT_OUTCOME_UNKNOWN,
    XZ_SUPPORT_NEEDS_CLOCK,
    XZ_SUPPORT_ERROR
};

#define XZ_SUPPORT_PREVIEW_LINES 6
#define XZ_SUPPORT_LINE_CHARS 76

struct xz_support_model {
    enum xz_support_phase phase;
    int ready;
    int usb_writable;
    int clock_ready;
    uint32_t draft_revision;
    uint32_t payload_bytes;
    uint32_t redactions;
    uint32_t page_index;
    uint32_t page_count;
    char report_id[33];
    char payload_sha256[65];
    char github_url[160];
    char export_path[160];
    char status[128];
    char preview[XZ_SUPPORT_PREVIEW_LINES][XZ_SUPPORT_LINE_CHARS];
};

enum xz_support_op {
    XZ_SUPPORT_OP_PREPARE = 0,
    XZ_SUPPORT_OP_SET_CLOCK = 1,
    XZ_SUPPORT_OP_SEND = 2,
    XZ_SUPPORT_OP_CHECK = 3,
    XZ_SUPPORT_OP_EXPORT = 4
};

struct xz_support_request_args {
    enum xz_support_op op;
    int category;
    int summary;
    uint32_t draft_revision;
    uint64_t report_epoch;
    char expected_sha256[65];
};

void xz_support_read(struct xz_support_model *out, int requested_page) __attribute__((weak));
int xz_support_request(const struct xz_support_request_args *req) __attribute__((weak));
#endif
