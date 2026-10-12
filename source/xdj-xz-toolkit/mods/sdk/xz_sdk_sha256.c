/* SPDX-License-Identifier: Unlicense */
/*
 * Derived from Brad Conte's public-domain crypto-algorithms SHA-256 source:
 * https://github.com/B-Con/crypto-algorithms/blob/master/sha256.c
 * Names and formatting were adapted for the XZ SDK runtime. The algorithm is
 * unchanged.
 */
#include "xz_sdk_sha256.h"

#include <string.h>

#define ROTATE_RIGHT(value, bits) (((value) >> (bits)) | ((value) << (32u - (bits))))
#define CHOICE(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJORITY(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTATE_RIGHT((x), 2) ^ ROTATE_RIGHT((x), 13) ^ ROTATE_RIGHT((x), 22))
#define EP1(x) (ROTATE_RIGHT((x), 6) ^ ROTATE_RIGHT((x), 11) ^ ROTATE_RIGHT((x), 25))
#define SIG0(x) (ROTATE_RIGHT((x), 7) ^ ROTATE_RIGHT((x), 18) ^ ((x) >> 3))
#define SIG1(x) (ROTATE_RIGHT((x), 17) ^ ROTATE_RIGHT((x), 19) ^ ((x) >> 10))

static const uint32_t constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void transform(struct xz_sha256_context *context,
                      const uint8_t data[64])
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t index;

    for (index = 0; index < 16; ++index) {
        size_t offset = (size_t)index * 4u;
        words[index] = ((uint32_t)data[offset] << 24) |
                       ((uint32_t)data[offset + 1u] << 16) |
                       ((uint32_t)data[offset + 2u] << 8) |
                       (uint32_t)data[offset + 3u];
    }
    for (; index < 64; ++index) {
        words[index] = SIG1(words[index - 2u]) + words[index - 7u] +
                       SIG0(words[index - 15u]) + words[index - 16u];
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0; index < 64; ++index) {
        uint32_t first = h + EP1(e) + CHOICE(e, f, g) +
                         constants[index] + words[index];
        uint32_t second = EP0(a) + MAJORITY(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void xz_sha256_init(struct xz_sha256_context *context)
{
    context->data_length = 0;
    context->bit_length = 0;
    context->state[0] = 0x6a09e667u;
    context->state[1] = 0xbb67ae85u;
    context->state[2] = 0x3c6ef372u;
    context->state[3] = 0xa54ff53au;
    context->state[4] = 0x510e527fu;
    context->state[5] = 0x9b05688cu;
    context->state[6] = 0x1f83d9abu;
    context->state[7] = 0x5be0cd19u;
}

void xz_sha256_update(struct xz_sha256_context *context,
                      const void *input,
                      size_t length)
{
    const uint8_t *data = (const uint8_t *)input;
    size_t index;
    for (index = 0; index < length; ++index) {
        context->data[context->data_length++] = data[index];
        if (context->data_length == sizeof(context->data)) {
            transform(context, context->data);
            context->bit_length += 512u;
            context->data_length = 0;
        }
    }
}

void xz_sha256_final(struct xz_sha256_context *context,
                     uint8_t digest[XZ_SHA256_BYTES])
{
    uint32_t index = context->data_length;
    uint32_t word;

    context->data[index++] = 0x80u;
    if (index > 56u) {
        while (index < 64u) {
            context->data[index++] = 0;
        }
        transform(context, context->data);
        memset(context->data, 0, 56u);
    } else {
        while (index < 56u) {
            context->data[index++] = 0;
        }
    }

    context->bit_length += (uint64_t)context->data_length * 8u;
    for (index = 0; index < 8u; ++index) {
        context->data[63u - index] =
            (uint8_t)(context->bit_length >> (index * 8u));
    }
    transform(context, context->data);

    for (word = 0; word < 8u; ++word) {
        digest[word * 4u] = (uint8_t)(context->state[word] >> 24);
        digest[word * 4u + 1u] = (uint8_t)(context->state[word] >> 16);
        digest[word * 4u + 2u] = (uint8_t)(context->state[word] >> 8);
        digest[word * 4u + 3u] = (uint8_t)context->state[word];
    }
}
