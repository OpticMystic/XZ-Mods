/* SPDX-License-Identifier: MIT */
/* Report references (R-XXXX-XXXX) and loader IDs (L-XXXX-XXXX): one prefix letter and eight Crockford base32
   characters. Parsing is tolerant like reading a code aloud: any case, I and L read as 1, O as 0, and hyphens or
   spaces anywhere. U is not a Crockford character. Header-only so the runtime and the report helper share it. */
#ifndef XZ_CROCKFORD_H
#define XZ_CROCKFORD_H
#include <stddef.h>

enum { XZ_CROCKFORD_REF_CHARS = 11 }; /* "L-4F7K-2M9Q" */

static inline char xz_crockford_digit(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c == 'I' || c == 'L') return '1';
    if (c == 'O') return '0';
    if (c >= '0' && c <= '9') return c;
    if (c >= 'A' && c <= 'Z' && c != 'U') return c;
    return 0;
}

/* Canonical "<prefix>-XXXX-XXXX" into out (12 bytes) from len bytes of text; 0 on success, -1 otherwise.
   prefix is the upper-case letter the reference must start with. */
static inline int xz_crockford_ref(const char *text, size_t len, char prefix, char out[12]) {
    char digits[8];
    size_t got = 0, i = 0;
    if (!text || !out) return -1;
    while (i < len && (text[i] == ' ' || text[i] == '-')) i++;
    if (i >= len) return -1;
    char first = text[i] >= 'a' && text[i] <= 'z' ? (char)(text[i] - 32) : text[i];
    if (first != prefix) return -1;
    for (i++; i < len; i++) {
        char c = text[i];
        if (c == '-' || c == ' ') continue;
        char d = xz_crockford_digit(c);
        if (!d || got == sizeof digits) return -1;
        digits[got++] = d;
    }
    if (got != sizeof digits) return -1;
    out[0] = prefix; out[1] = '-';
    for (i = 0; i < 4; i++) out[2 + i] = digits[i];
    out[6] = '-';
    for (i = 0; i < 4; i++) out[7 + i] = digits[4 + i];
    out[11] = 0;
    return 0;
}
#endif
