/* SPDX-License-Identifier: Unlicense */
#ifndef XZ_SDK_SHA256_H
#define XZ_SDK_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define XZ_SHA256_BYTES 32u

#if defined(__GNUC__)
#define XZ_SDK_SHA_HIDDEN __attribute__((visibility("hidden")))
#else
#define XZ_SDK_SHA_HIDDEN
#endif

struct xz_sha256_context {
    uint8_t data[64];
    uint32_t data_length;
    uint64_t bit_length;
    uint32_t state[8];
};

XZ_SDK_SHA_HIDDEN void xz_sha256_init(struct xz_sha256_context *context);
XZ_SDK_SHA_HIDDEN void xz_sha256_update(struct xz_sha256_context *context,
                                        const void *data,
                                        size_t length);
XZ_SDK_SHA_HIDDEN void xz_sha256_final(
    struct xz_sha256_context *context,
    uint8_t digest[XZ_SHA256_BYTES]);

#endif
