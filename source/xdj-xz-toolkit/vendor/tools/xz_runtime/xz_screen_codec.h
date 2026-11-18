/* SPDX-License-Identifier: MIT
 * Lossless little-endian RGB565 RLE. Each command is a LE16 word: bit 15
 * selects a repeated word, low 15 bits encode pixel count minus one. Literal
 * commands carry count words; repeated commands carry one word. A previous
 * frame selects XOR deltas. Zero return means the caller must send raw pixels.
 */
#ifndef XZ_SCREEN_CODEC_H
#define XZ_SCREEN_CODEC_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint16_t xz_screen_delta_word(const unsigned char *pixels,
                                           const unsigned char *previous, size_t i) {
    uint16_t value = (uint16_t)(pixels[i*2] | (uint16_t)pixels[i*2+1] << 8);
    if (previous) value ^= (uint16_t)(previous[i*2] | (uint16_t)previous[i*2+1] << 8);
    return value;
}
/* Fixed 256-color stream palette. No dithering or RLE work on the controller. */
static inline unsigned char xz_screen_rgb332_pixel(uint16_t pixel) {
    return (unsigned char)((pixel>>8&0xe0u)|(pixel>>6&0x1cu)|(pixel>>3&3u));
}
static inline void xz_screen_rgb332(unsigned char *out,const unsigned char *source,size_t count) {
    for(size_t i=0;i<count;i++)out[i]=xz_screen_rgb332_pixel((uint16_t)(source[i*2]|(uint16_t)source[i*2+1]<<8));
}
static inline void xz_screen_put16(unsigned char *out, uint16_t value) {
    out[0]=(unsigned char)value;out[1]=(unsigned char)(value >> 8);
}
static size_t xz_screen_rle16(unsigned char *out, size_t capacity,
                            const unsigned char *pixels, const unsigned char *previous,
                            size_t count) {
    size_t i=0, used=0;
    while (i<count) {
        uint16_t value=xz_screen_delta_word(pixels,previous,i);
        size_t run=1;
        while (run<32768 && i+run<count && xz_screen_delta_word(pixels,previous,i+run)==value) run++;
        if (run>=2) {
            if (capacity-used<4) return 0;
            xz_screen_put16(out+used,(uint16_t)(0x8000u | (run-1)));
            xz_screen_put16(out+used+2,value);used+=4;i+=run;
        } else {
            size_t start=i++;
            while (i<count && i-start<32768) {
                if (i+1<count && xz_screen_delta_word(pixels,previous,i)==xz_screen_delta_word(pixels,previous,i+1)) break;
                i++;
            }
            size_t length=i-start;
            if (capacity-used<2+length*2) return 0;
            xz_screen_put16(out+used,(uint16_t)(length-1));used+=2;
            if (!previous) memcpy(out+used,pixels+start*2,length*2);
            else for (size_t n=0;n<length;n++) xz_screen_put16(out+used+n*2,xz_screen_delta_word(pixels,previous,start+n));
            used+=length*2;
        }
    }
    return used;
}
#endif
