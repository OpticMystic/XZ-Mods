/* SPDX-License-Identifier: MIT */
#ifndef XZ_TEST_WAVE_RIDER_STUBS_H
#define XZ_TEST_WAVE_RIDER_STUBS_H
/* Existing fixtures do not open Wave Rider. Its real integration fixture
   defines XZ_WAVE_RIDER_RUNTIME_TEST and links the production runtime. */
void xz_wave_rider_open(uint32_t now){(void)now;}
void xz_wave_rider_close(void){}
int xz_wave_rider_present(void){return 0;}
int xz_wave_rider_leave_requested(void){return 0;}
int xz_wave_rider_input(int deck,const void *event,uint32_t now){(void)deck;(void)event;(void)now;return 0;}
int xz_wave_rider_touch(int x,int y,int down,uint32_t now){(void)x;(void)y;(void)down;(void)now;return 0;}
int xz_wave_rider_draw(uint16_t *p,size_t count,size_t stride,uint32_t now){(void)p;(void)count;(void)stride;(void)now;return 0;}
int xz_wave_rider_pad(int deck,int pad,unsigned *rgb,int *enabled){(void)deck;(void)pad;(void)rgb;(void)enabled;return 0;}
#endif
