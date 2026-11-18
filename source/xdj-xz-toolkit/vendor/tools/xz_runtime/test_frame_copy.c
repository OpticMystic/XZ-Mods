/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include "xz_frame_copy.h"
static uint16_t source_words[800*480+32] __attribute__((aligned(64)));
static uint8_t output[64+1624*480+64] __attribute__((aligned(64)));
static void verify(size_t pitch,const uint8_t *source) {
    const uint8_t *destination=output+64;
    for(size_t i=0;i<64;i++)assert(output[i]==0xa5);
    for(unsigned row=0;row<480;row++) {
        assert(!memcmp(destination+row*pitch,source+row*1600u,1600));
        for(size_t x=1600;x<pitch;x++)assert(destination[row*pitch+x]==0xa5);
    }
    for(size_t i=64+pitch*480;i<sizeof output;i++)assert(output[i]==0xa5);
}
int main(void) {
    (void)xz_frame_copy_control_tick;
    assert(xz_frame_copy_initialize() && xz_frame_copy_v1[4]==1);
    for(size_t i=0;i<sizeof source_words/sizeof *source_words;i++)source_words[i]=(uint16_t)(i*811u+(i>>4));
    const size_t pitches[]={1600,1602,1624};
    unsigned frames=0;
    for(unsigned mode=0;mode<4;mode++) {
        char command[]={(char)('0'+mode),'\n'};
        xz_frame_copy_accept_control(command,2);assert(xz_frame_copy_v1[1]==mode);
        unsigned offset=xz_frame_copy_render_offset();assert(offset==((mode&2)?0u:12u));
        const uint8_t *source=(const uint8_t *)(source_words+offset);
        for(unsigned p=0;p<3;p++)for(unsigned pass=0;pass<2;pass++) {
            memset(output,0xa5,sizeof output);
            if(pass)for(unsigned y=0;y<480;y++)memset(output+64+y*pitches[p],0x3c,1600);
            xz_frame_copy_present(output+64,pitches[p],source);frames++;verify(pitches[p],source);
            assert(xz_frame_copy_v1[2]==mode && xz_frame_copy_v1[3]==((mode&2)?0u:24u));
        }
    }
    assert(xz_frame_copy_v1[5]==frames && xz_frame_copy_v1[6]==12 && xz_frame_copy_v1[7]==12);
    xz_frame_copy_accept_control("4",1);xz_frame_copy_accept_control("1x",2);xz_frame_copy_accept_control("3\nx",3);xz_frame_copy_accept_control(NULL,-1);
    assert(xz_frame_copy_v1[1]==3);
    xz_frame_libc_copy=NULL;__atomic_store_n(&xz_frame_copy_v1[4],0u,__ATOMIC_RELEASE);
    memset(output,0xa5,sizeof output);xz_frame_copy_present(output+64,1600,(const uint8_t *)source_words);verify(1600,(const uint8_t *)source_words);
    assert(xz_frame_copy_v1[2]==2);
    puts("PASS all four copy/alignment modes, contiguous and padded rows, complete overwrite, guards, invalid control and unavailable-libc fallback.");
    return 0;
}
