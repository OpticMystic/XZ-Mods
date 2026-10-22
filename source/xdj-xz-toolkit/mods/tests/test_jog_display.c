#include "jog_display.h"
#include "jog_display_fixtures.h"
#include "jog_key_vectors.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void swap(uint8_t *d,const uint8_t *s) { unsigned i;for(i=0;i<64;i++)d[i]=s[i^3u]; }
static unsigned le16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static void put16(uint8_t *p,unsigned n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void packet(const uint8_t *frame,uint8_t *p) { swap(p,frame+12); }
static void seed(struct xz_jd *s) {
    unsigned i;uint8_t p[64];
    memcpy(p,native_info,64);p[0]=(uint8_t)(s->physical_side<<4);assert(xz_jd_capture(s,p)==XZ_JD_OK);
    for(i=1;i<6;i++) { xz_jd_clear_packet(s->physical_side,i,p);assert(xz_jd_capture(s,p)==XZ_JD_OK); }
    assert(xz_jd_ready(s));
}
static void check_crc(const uint8_t *p) {
    uint32_t got=(uint32_t)p[144]|((uint32_t)p[145]<<8)|((uint32_t)p[146]<<16)|((uint32_t)p[147]<<24);
    assert(got==xz_jd_crc(p));
}
int main(void) {
    struct xz_jd s;uint8_t native[148],out[148],p[64],wanted[64],paused_native[64];unsigned i;int result;
    uint8_t *storage=malloc(XZ_JD_STORAGE_BYTES);struct xz_jd_snapshot extra={.generation=1,.captured_ms=1000,.position_ms=123456,.length_ms=300789,.logical_deck=3,.paused=0,.tempo_range=6,.vinyl_mode=1,.tempo_known=1};
    assert(storage);assert(xz_jd_init(&s,1,storage,XZ_JD_STORAGE_BYTES,100)==XZ_JD_OK);
    memcpy(native,native_outer,148);swap(native+12,native_info);
    assert(xz_jd_step(&s,native,NULL,1000,out)==XZ_JD_OK);assert(!memcmp(out,native,148));
    assert(xz_jd_select(&s,3,&extra,1000)==XZ_JD_NOT_READY);
    xz_jd_native_epoch(&s,11);seed(&s);
    /* Retain a full multi-packet native artwork batch before ownership. */
    xz_jd_clear_packet(1,5,p);put16(p+4,3);
    for(i=1;i<=3;i++) { put16(p+2,i);p[6]=(uint8_t)(0xa0+i);assert(xz_jd_capture(&s,p)==XZ_JD_OK); }
    /* Paused native content must survive while no further native info arrives. */
    memcpy(paused_native,native_info,64);paused_native[2]=0x0a;paused_native[0xb]=1;paused_native[0xc]=9;
    assert(xz_jd_capture(&s,paused_native)==XZ_JD_OK);
    assert(xz_jd_select(&s,3,&extra,1000)==XZ_JD_OK);
    native[0]=0x34;native[1]=0x59;native[3]|=0x22;native[4]|=0x87;native[5]|=0xaf;native[7]|=0x20;
    native[0x50]=1;native[0x51]=0xe1;native[0x56]=0xff;native[0x57]=1;
    native[0x5d]=native[0x5e]=native[0x84]=native[0x8b]=0x7f;
    for(i=1;i<=6;i++) {
        memset(native+12,0xaa,64);assert(xz_jd_step(&s,native,&extra,1000,out)==XZ_JD_OK);packet(out,p);check_crc(out);
        assert((out[0x53]>>4)==3);assert((out[7]&0x30)==0);
        assert(!out[0]&&!out[0x50]&&!out[0x57]&&out[0x5d]==0x7f&&out[0x5e]==0x7f&&!out[0x84]&&!out[0x8b]);
        assert(out[1]==0x59&&(out[3]&0x20)==0x20&&(out[4]&0x80)==0x80&&(out[5]&0xf0)==0xa0);
        assert(out[0x51]==0xc0&&out[0x56]==0x6f);
        if(i<6) { xz_jd_clear_packet(1,i,wanted);assert(!memcmp(p,wanted,64)); }
        else { memcpy(wanted,native_info,64);wanted[0]=0x10;wanted[0x13]=wanted[0x14]=0xbb;assert(!memcmp(p,wanted,64)); }
    }
    extra.tempo_rate=-1234;extra.tempo_range=10;extra.vinyl_mode=0;extra.master_tempo=1;
    assert(xz_jd_step(&s,native,&extra,1000,out)==XZ_JD_OK);packet(out,p);memcpy(wanted,native_controls,64);wanted[0]=0x10;wanted[0x13]=wanted[0x14]=0xbb;assert(!memcmp(p,wanted,64));
    extra.tempo_rate=0;extra.tempo_range=6;extra.vinyl_mode=1;extra.master_tempo=0;
    extra.paused=1;assert(xz_jd_step(&s,native,&extra,1000,out)==XZ_JD_OK);packet(out,p);assert(p[2]==0x0a);
    /* Return must clear software artifacts, replay full paused native data,
     * finish artwork1..3, and then restore the native display configuration. */
    assert(xz_jd_select(&s,1,NULL,1000)==XZ_JD_OK);
    for(i=1;i<=5;i++) { assert(xz_jd_step(&s,native,NULL,1000,out)==XZ_JD_OK);packet(out,p);xz_jd_clear_packet(1,i,wanted);assert(!memcmp(p,wanted,64));assert((out[0x53]>>4)==1); }
    assert(xz_jd_step(&s,native,NULL,1000,out)==XZ_JD_OK);packet(out,p);assert(!memcmp(p,paused_native,64));
    { unsigned art=0,steps=0;while(s.phase!=XZ_JD_NATIVE&&steps++<20) { xz_jd_step(&s,native,NULL,1000,out);packet(out,p);if(p[1]==0x33) { art++;assert(le16(p+2)==art);assert(p[6]==0xa0+art); } }assert(art==3&&s.phase==XZ_JD_NATIVE); }
    assert(xz_jd_step(&s,native,NULL,1000,out)==XZ_JD_OK);assert((out[7]&0x30)==(native[7]&0x30));
    /* Expiry clears data but keeps logical3 while input still owns3. */
    assert(xz_jd_select(&s,3,&extra,1000)==XZ_JD_OK);
    result=xz_jd_step(&s,native,&extra,1101,out);assert(result==XZ_JD_STALE);assert(s.selected==3);packet(out,p);assert(p[0]==0x10&&p[1]==0x2d);
    for(i=0;i<5;i++)assert(xz_jd_step(&s,native,&extra,1101,out)==XZ_JD_STALE);
    packet(out,p);memcpy(wanted,native_empty,64);wanted[0]=0x10;assert(!memcmp(p,wanted,64));
    extra.captured_ms=1101;assert(xz_jd_step(&s,native,&extra,1101,out)==XZ_JD_OK);packet(out,p);assert(p[1]==0x2d);
    assert(xz_jd_select(&s,1,NULL,1101)==XZ_JD_OK);
    /* Unknown native generation and oversized/incomplete image transactions
     * prevent a fresh takeover. Wrong physical routing is rejected. */
    xz_jd_native_epoch(&s,12);assert(!xz_jd_ready(&s));assert(xz_jd_select(&s,3,&extra,1101)==XZ_JD_NOT_READY);
    xz_jd_clear_packet(1,5,p);put16(p+4,XZ_JD_ART_PACKETS+1);assert(xz_jd_capture(&s,p)==XZ_JD_NOT_READY);
    xz_jd_clear_packet(1,5,p);put16(p+2,2);put16(p+4,3);assert(xz_jd_capture(&s,p)==XZ_JD_NOT_READY);
    for(i=0;i<5;i++)xz_jd_step(&s,native,NULL,1101,out);
    assert(s.phase==XZ_JD_REPLAY);assert(xz_jd_step(&s,native,NULL,1101,out)==XZ_JD_RESTORING);
    packet(out,p);assert(!memcmp(p,native_empty,64));
    native[2]|=0x40;assert(xz_jd_step(&s,native,&extra,1000,out)==XZ_JD_UNSUPPORTED);assert(!memcmp(out,native,148));
    /* Every changing LED field found by the independent full ARM serializer
     * collapses to its native disabled-ring/LED-off value. */
    assert(xz_jd_init(&s,1,storage,XZ_JD_STORAGE_BYTES,100)==XZ_JD_OK);xz_jd_native_epoch(&s,13);seed(&s);
    assert(xz_jd_select(&s,3,&extra,1101)==XZ_JD_OK);
    for(i=0;i<sizeof(native_variants)/sizeof(native_variants[0]);i++) {
        unsigned j;assert(xz_jd_step(&s,native_variants[i],&extra,1101,out)==XZ_JD_OK);
        for(j=0;j<8;j++)assert(out[j]==native_disabled_ring[j]);
        for(j=0x50;j<0x90;j++)assert(out[j]==(j==0x53?0x30:native_disabled_ring[j]));
        check_crc(out);
    }
    assert(xz_jd_init(&s,2,storage,XZ_JD_STORAGE_BYTES,100)==XZ_JD_OK);xz_jd_native_epoch(&s,14);seed(&s);
    extra.logical_deck=4;assert(xz_jd_select(&s,4,&extra,1101)==XZ_JD_OK);
    for(i=0;i<6;i++) { assert(xz_jd_step(&s,native_right,&extra,1101,out)==XZ_JD_OK);packet(out,p);assert(p[0]==0x20&&(out[0x53]>>4)==4);check_crc(out); }
    assert(xz_jd_select(&s,3,&extra,1101)==XZ_JD_BAD_INPUT);
    assert(xz_jd_select(&s,2,NULL,1101)==XZ_JD_OK);xz_jd_step(&s,native_right,NULL,1101,out);packet(out,p);assert(p[0]==0x20&&(out[0x53]>>4)==2);
    /* Extended software deck status_known synthesis: cued pause, sync/master/slip,
     * BPM fields, loop window, and active JogCpuCom ring/button status bytes. */
    {
        struct xz_jd_snapshot rich={
            .generation=2,.captured_ms=1101,.position_ms=2000,.length_ms=200000,
            .logical_deck=4,.paused=1,.tempo_rate=-250,.tempo_range=10,
            .vinyl_mode=1,.master_tempo=1,.tempo_known=1,.status_known=1,
            .cued=1,.master=1,.sync_mode=XZ_JD_SYNC_LOCKED,.loop_active=1,.slip=1,
            .bpm_x100=12850,.track_bpm_x100=12500,.loop_in_ms=2000,.loop_out_ms=6000
        };
        assert(xz_jd_select(&s,4,&rich,1101)==XZ_JD_OK);
        for(i=0;i<6;i++)assert(xz_jd_step(&s,native_right,&rich,1101,out)==XZ_JD_OK);
        packet(out,p);check_crc(out);
        assert(p[0]==0x20&&p[2]==0x0c);
        assert((p[5]&0xb9u)==(0x01u|0x08u|0x10u|0x80u));
        assert(p[0x15]==128&&p[0x16]==0x50&&p[0x26]==128&&p[0x27]==0x05);
        assert(p[0x2f]==125&&p[0x30]==0x00);
        assert(!(p[9]&0x10u)); /* Known BPM must not carry unavailable. */
        assert(p[9]&0x20u); /* Native producer marks a valid displayed BPM separately. */
        assert(p[0x19]==0xff&&p[0x1a]==0xff&&p[0x1b]==0xff);
        assert(p[0x1c]==0xff&&p[0x1d]==0xff&&p[0x1e]==0xff);
        assert(p[0x1f]==0xff&&p[0x20]==0xff&&p[0x21]==0xff);
        assert(!(out[3]&0x02u)&&out[0x5d]==0x7f&&out[0x5e]==0x7f);
        assert((out[4]&0x07u)==0x06u&&out[0x50]==1&&(out[0x51]&0x21u)==0x01u);
        assert((out[5]&0x01u)==0x01u&&!(out[5]&0x0cu));
        assert((out[0x56]&0x90u)==0x90u&&out[0x57]==0);
        rich.bpm_x100=rich.track_bpm_x100=0;
        assert(xz_jd_step(&s,native_right,&rich,1101,out)==XZ_JD_OK);packet(out,p);
        assert((p[9]&0x30u)==0x10u);
        assert(p[0x15]==0&&p[0x16]==0&&p[0x26]==0x99&&p[0x27]==0x95);
        assert(p[0x2f]==0x99&&p[0x30]==0x59);
    }
    for(unsigned side=1;side<=2;side++) {
        assert(xz_jd_init(&s,side,storage,XZ_JD_STORAGE_BYTES,100)==XZ_JD_OK);xz_jd_native_epoch(&s,20);seed(&s);
        extra.logical_deck=(uint8_t)(side+2);extra.status_known=1;extra.captured_ms=1101;
        assert(xz_jd_select(&s,side+2,&extra,1101)==XZ_JD_OK);
        for(unsigned k=0;k<sizeof jog_key_vectors/sizeof jog_key_vectors[0];k++) {
            extra.key_code=xz_jd_key_code(jog_key_vectors[k].name);
            assert(extra.key_code==jog_key_vectors[k].code);
            for(unsigned clear=0;clear<6;clear++)assert(xz_jd_step(&s,native_right,&extra,1101,out)==XZ_JD_OK);
            packet(out,p);assert(p[0x3c]==jog_key_vectors[k].code);check_crc(out);
        }
        char unterminated[8]={'F','m','x','x','x','x','x','x'};
        assert(!xz_jd_key_code(unterminated)&&!xz_jd_key_code(NULL));
    }
    free(storage);puts("PASS: native ARM fixture parity, keys, ownership, quiet ring/status, full paused-native replay, freshness, media epoch, bounds, and unsupported PC mode");return 0;
}
