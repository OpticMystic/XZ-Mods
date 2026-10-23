#include "jog_display.h"
#include <string.h>

static unsigned get16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static void set16(uint8_t *p,unsigned n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static void set32(uint8_t *p,uint32_t n) { unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i)); }
static void swap64(uint8_t *out,const uint8_t *in) { unsigned i;for(i=0;i<64;i++)out[i]=in[i^3u]; }
static int family_id(unsigned command) {
    switch(command) { case 0x21:return 0;case 0x2d:return 1;case 0x30:return 2;case 0x3f:return 3;case 0x2c:return 4;case 0x33:return 5;default:return -1; }
}
uint32_t xz_jd_crc(const uint8_t p[144]) {
    unsigned i,j;uint32_t crc=0xffff;
    for(i=0;i<144;i++) { crc^=p[i^3u];for(j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0x8408u:0u); }
    return ((~crc)&0xffffu)<<16;
}
int xz_jd_init(struct xz_jd *s,unsigned side,void *memory,size_t bytes,uint32_t fresh) {
    unsigned i;uint8_t *p=(uint8_t *)memory;
    if(!s||!memory||bytes<XZ_JD_STORAGE_BYTES||(side!=1&&side!=2)||!fresh)return XZ_JD_BAD_INPUT;
    memset(s,0,sizeof(*s));s->physical_side=(uint8_t)side;s->selected=(uint8_t)side;s->freshness_ms=fresh;
    for(i=0;i<6;i++) { struct xz_jd_family *f=&s->family[i];f->capacity=(uint16_t)(i<4?1:i==4?XZ_JD_WAVE_PACKETS:XZ_JD_ART_PACKETS);f->bank[0]=p;p+=f->capacity*64u;f->bank[1]=p;p+=f->capacity*64u; }
    return XZ_JD_OK;
}
void xz_jd_native_epoch(struct xz_jd *s,uint64_t epoch) {
    unsigned i;if(s->native_epoch==epoch)return;s->native_epoch=epoch;
    for(i=0;i<6;i++) { s->family[i].complete=0;s->family[i].next=0;s->family[i].replay=0; }
    if(s->phase==XZ_JD_REPLAY)s->replay_family=0;
}
int xz_jd_capture(struct xz_jd *s,const uint8_t p[64]) {
    int kind=family_id(p[1]);unsigned number,count;struct xz_jd_family *f;
    if(kind<0||(p[0]>>4)!=s->physical_side)return XZ_JD_BAD_INPUT;
    f=&s->family[kind];number=kind?get16(p+2):1;count=kind?get16(p+4):1;
    if(!number||!count||number>count||count>f->capacity) { f->complete=0;f->next=0;return XZ_JD_NOT_READY; }
    if(number==1) { f->building_count=(uint16_t)count;f->next=1; }
    if(number!=f->next||count!=f->building_count) { f->next=0;return XZ_JD_NOT_READY; }
    memcpy(f->bank[f->active^1u]+(number-1u)*64u,p,64);f->next++;
    if(number==count) { f->active^=1u;f->count=(uint16_t)count;f->complete=1;f->version++;f->next=0; }
    return XZ_JD_OK;
}
int xz_jd_ready(const struct xz_jd *s) { unsigned i;if(!s->native_epoch)return 0;for(i=0;i<6;i++)if(!s->family[i].complete)return 0;return 1; }
static int fresh(const struct xz_jd *s,const struct xz_jd_snapshot *p,uint64_t now) {
    return p&&p->logical_deck==s->physical_side+2u&&p->paused<=1&&p->remain<=1&&p->tempo_known==1&&p->vinyl_mode<=1&&p->master_tempo<=1&&
        (p->tempo_range==0||p->tempo_range==6||p->tempo_range==10||p->tempo_range==16||p->tempo_range==100)&&
        p->status_known<=1&&(!p->status_known||(p->cued<=1&&p->master<=1&&p->sync_mode<=XZ_JD_SYNC_BLINK&&
        p->loop_active<=1&&p->slip<=1&&p->slip_active<=1&&p->loop_edit<=2&&p->held_playing<=1&&(!p->loop_active||p->loop_out_ms>p->loop_in_ms)))&&
        p->generation&&p->captured_ms<=now&&now-p->captured_ms<=s->freshness_ms&&p->position_ms<=p->length_ms&&p->length_ms<128u*60000u;
}
int xz_jd_select_unavailable(struct xz_jd *s,unsigned deck) {
    if(!s||deck!=s->physical_side+2u)return XZ_JD_BAD_INPUT;
    if(!xz_jd_ready(s))return XZ_JD_NOT_READY;
    if(s->selected!=deck||!s->unavailable) {
        s->phase=XZ_JD_EXTRA_CLEAR;s->clear_next=1;
    }
    s->selected=(uint8_t)deck;s->software_generation=0;s->unavailable=1;
    return XZ_JD_STALE;
}
int xz_jd_select(struct xz_jd *s,unsigned deck,const struct xz_jd_snapshot *p,uint64_t now) {
    if(deck!=s->physical_side&&deck!=s->physical_side+2u)return XZ_JD_BAD_INPUT;
    if(deck==s->physical_side) {
        s->selected=(uint8_t)deck;s->unavailable=0;
        if(s->phase!=XZ_JD_NATIVE&&s->phase!=XZ_JD_NATIVE_CLEAR&&s->phase!=XZ_JD_REPLAY) { s->phase=XZ_JD_NATIVE_CLEAR;s->clear_next=1; }
        return XZ_JD_OK;
    }
    if(!fresh(s,p,now))return XZ_JD_BAD_INPUT;
    if(!xz_jd_ready(s))return XZ_JD_NOT_READY;
    if(s->selected!=deck||s->software_generation!=p->generation||s->unavailable) { s->phase=XZ_JD_EXTRA_CLEAR;s->clear_next=1;s->software_generation=p->generation;s->unavailable=0; }
    s->selected=(uint8_t)deck;return XZ_JD_OK;
}
void xz_jd_clear_packet(unsigned deck,unsigned family,uint8_t p[64]) {
    static const uint8_t command[6]={0,0x2d,0x30,0x3f,0x2c,0x33};
    memset(p,0,64);if(family<1||family>5)return;p[0]=(uint8_t)(deck<<4);p[1]=command[family];p[2]=p[4]=1;if(family==4)p[6]=0x80;
}
static void time_ms(uint8_t *p,uint32_t value) { p[0]=(uint8_t)(value/60000u);p[1]=(uint8_t)((value/1000u)%60u);set16(p+2,value%1000u); }
static void encode_bpm_pair(uint8_t *p,unsigned bpm_x100,int high_first) {
    unsigned tenths=bpm_x100/10u,whole=tenths/10u,frac=tenths%10u;
    p[0]=(uint8_t)(whole&0xffu);
    p[1]=high_first?(uint8_t)((((whole>>8)&0x0fu)<<4)|(frac&0x0fu))
                   :(uint8_t)(((frac&0x0fu)<<4)|((whole>>8)&0x0fu));
}
static void info(const struct xz_jd_snapshot *s,uint8_t p[64]) {
    /* Neutral native producer output: BPM invalid, no cue/loop/slip/sync,
     * quantize disabled, native track number unknown, explicit elapsed time. */
    static const uint8_t native_neutral[64]={
        0x10,0x21,0x08,0x00,0x10,0x41,0x01,0x00,0x00,0xde,0x80,0x02,0x03,0xc8,0x01,0x05,
        0x00,0x15,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xff,0xff,0x00,0x00,0x00,0x00,0x99,0x95,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x99,
        0x59,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0d,0x00,0x00};
    memcpy(p,native_neutral,64);p[0]=(uint8_t)(s->logical_deck<<4);p[2]=(uint8_t)(s->paused?0x0a:0x08);
    time_ms(p+0xb,s->position_ms);time_ms(p+0xf,s->length_ms);p[0x13]=p[0x14]=0xbb;set16(p+0x17,(uint16_t)s->tempo_rate);
    p[4]=(uint8_t)((s->tempo_range==6?1:s->tempo_range==10?2:s->tempo_range==16?3:s->tempo_range==100?4:0)<<4);
    p[4]|=(uint8_t)(s->master_tempo<<2);p[5]=(uint8_t)((p[5]&0xfcu)|s->vinyl_mode);
    if(s->status_known) {
        p[3]=(uint8_t)((p[3]&~0x08u)|(s->remain?0x08u:0u));
        p[0x3c]=s->key_code;
        if(s->paused&&s->cued)p[2]=0x0cu;
        p[5]=(uint8_t)((p[5]&~0xb8u)|(s->slip?0x08u:0u)|
            (s->sync_mode==XZ_JD_SYNC_LOCKED?0x10u:s->sync_mode==XZ_JD_SYNC_BLINK?0x20u:0u)|
            (s->master?0x80u:0u));
        if(s->bpm_x100>0) {
            encode_bpm_pair(p+0x15,s->bpm_x100,0);
            encode_bpm_pair(p+0x26,s->bpm_x100,1);
            /* Native producer: bit 4 unavailable, bit 5 valid BPM. */
            p[9]=(uint8_t)((p[9]&~0x10u)|0x20u);
        }
        if(s->track_bpm_x100>0)encode_bpm_pair(p+0x2f,s->track_bpm_x100,0);
        /* Loop and cue time units are unresolved. Keep the neutral inactive
         * sentinels instead of serializing software milliseconds. */
    }
}
uint8_t xz_jd_key_code(const char key[8]) {
    static const char names[][4]={
        "C","Am","Db","Bbm","D","Bm","Eb","Cm","E","Dbm","F","Dm",
        "F#","Ebm","G","Em","Ab","Fm","A","F#m","Bb","Gm","B","Abm",
        "C#","A#m","D#","C#m","Gb","D#m","G#","Gbm","A#","G#m"
    };
    static const uint8_t wheel_b[12]={23,13,3,17,7,21,11,1,15,5,19,9};
    if(!key||!memchr(key,0,8))return 0;
    for(unsigned i=0;i<sizeof names/sizeof names[0];i++)if(!strcmp(key,names[i]))return (uint8_t)(i+1u);
    unsigned n=0,i=0;
    while(i<2&&key[i]>='0'&&key[i]<='9'){n=n*10u+(unsigned)(key[i]-'0');i++;}
    if(n>=1&&n<=12&&(key[i]=='A'||key[i]=='B')&&!key[i+1])
        return (uint8_t)(0x80u|(wheel_b[n-1]+(key[i]=='A')));
    return 0;
}
static void quiet_status(uint8_t *p) {
    /* Exact native LED differential proof identifies these status fields.
     * Ring positions are disabled, not borrowed from native deck1/2. */
    p[0]=0;p[3]&=(uint8_t)~2u;p[4]&=(uint8_t)~7u;p[5]&=(uint8_t)~15u;
    p[0x50]=0;p[0x51]&=(uint8_t)~0x21u;p[0x56]&=(uint8_t)~0x90u;p[0x57]=0;
    p[0x5d]=p[0x5e]=0x7f;p[0x84]=p[0x8b]=0;
}
static void extra_status(uint8_t *p,const struct xz_jd_snapshot *s,uint64_t now) {
    /* Match the existing software lamp model using the qualified on/off bits.
     * Flashing is sampled here; no native timer or blink-record fields are written. */
    uint8_t blink=(uint8_t)(((uint32_t)now/250u)&1u);
    uint8_t vinyl=s->vinyl_mode?1u:0u;
    uint8_t slip=s->slip?(s->slip_active?blink:1u):0u;
    uint8_t playing=!s->paused||s->held_playing;
    uint8_t cue_led=playing?1u:(s->cued?1u:blink);
    uint8_t play_led=playing?1u:blink;
    uint8_t sync_led=s->sync_mode!=XZ_JD_SYNC_OFF?1u:0u;
    uint8_t loop_led=(playing||s->loop_active)?1u:0u;
    uint8_t in_led=s->loop_edit?(s->loop_edit==1?blink:0u):loop_led;
    uint8_t out_led=s->loop_edit?(s->loop_edit==2?blink:0u):loop_led;
    uint8_t master_led=s->master?1u:0u;
    p[4]=(uint8_t)((p[4]&~7u)|(vinyl<<1)|(slip<<2));
    p[5]=(uint8_t)((p[5]&~15u)|cue_led|(play_led<<1)|(in_led<<2)|(out_led<<3));
    p[0x50]=vinyl;
    p[0x51]=(uint8_t)((p[0x51]&~0x21u)|slip);
    p[0x56]=(uint8_t)((p[0x56]&~0x90u)|(sync_led?0x10u:0u)|(master_led?0x80u:0u));
}
static void unavailable(unsigned deck,uint8_t packet[64]) {
    struct xz_jd_snapshot empty={0};
    empty.logical_deck=(uint8_t)deck;info(&empty,packet);
    packet[9]&=0x7f;packet[0x13]=packet[0x14]=0;
}
static int replay(struct xz_jd *s,uint8_t packet[64]) {
    while(s->replay_family<6) {
        struct xz_jd_family *f=&s->family[s->replay_family];
        if(!f->complete)return 0;
        if(f->replay_version!=f->version) { f->replay=0;f->replay_version=f->version; }
        if(f->replay<f->count) { memcpy(packet,f->bank[f->active]+f->replay*64u,64);f->replay++;if(f->replay==f->count)s->replay_family++;return 1; }
        s->replay_family++;
    }
    s->phase=XZ_JD_NATIVE;return 0;
}
static void observe_native(struct xz_jd *s,const uint8_t native[148]){
    unsigned sequence=get16(native+8);
    if(!s->observed||sequence!=s->observed_sequence){
        uint8_t packet[64];swap64(packet,native+12);(void)xz_jd_capture(s,packet);
        s->observed_sequence=(uint16_t)sequence;s->observed=1;s->native_pending=1;
    }
}
static int info_changed(const struct xz_jd_snapshot *a,const struct xz_jd_snapshot *b){
    return a->paused!=b->paused||a->remain!=b->remain||a->tempo_rate!=b->tempo_rate||a->tempo_range!=b->tempo_range||
        a->master_tempo!=b->master_tempo||a->vinyl_mode!=b->vinyl_mode||a->status_known!=b->status_known||
        a->cued!=b->cued||a->master!=b->master||a->sync_mode!=b->sync_mode||a->slip!=b->slip||
        a->bpm_x100!=b->bpm_x100||a->track_bpm_x100!=b->track_bpm_x100||a->key_code!=b->key_code;
}
static void software_packet(struct xz_jd *s,const struct xz_jd_snapshot *snapshot,uint64_t now,uint8_t packet[64]){
    int wave=s->software_wave&&s->wave_next<XZ_JD_RGB_WAVE_PACKETS;
    int due=!s->have_info||now<s->last_info_ms||now-s->last_info_ms>=20u||s->wave_since_info>=7u||
        info_changed(snapshot,&s->last_info);
    if(wave&&s->have_info&&(s->after_info||!due)){
        (void)xz_jd_wave_packet(s->software_wave,s->physical_side,s->wave_next++,packet);
        s->wave_since_info++;s->after_info=0;
    }else{
        info(snapshot,packet);s->last_info=*snapshot;s->last_info_ms=now;s->have_info=1;
        s->wave_since_info=0;s->after_info=1;
    }
}
static int local_display(const uint8_t frame[148]){
    /* LINK selection alone retains native local commands (captured 0xa0/0xb0,
     * secondary kind 0xc0). A certified host or USB-owned secondary block does
     * not belong to this adapter. */
    return !(frame[2]&0x40u)&&(frame[0x4e]&0x7fu)==0x40u&&
        (!(frame[2]&0x80u)||frame[0x4e]==0xc0u);
}
int xz_jd_step(struct xz_jd *s,const uint8_t native[148],const struct xz_jd_snapshot *snapshot,uint64_t now,uint8_t out[148]) {
    uint8_t packet[64];unsigned sequence,new_packet;int emitted=0,result=XZ_JD_OK;
    memcpy(out,native,148);
    if(!local_display(native))return XZ_JD_UNSUPPORTED;
    observe_native(s,native);new_packet=s->native_pending;s->native_pending=0;
    if(s->phase==XZ_JD_EXTRA||s->phase==XZ_JD_EXTRA_CLEAR) {
        int valid=fresh(s,snapshot,now);
        if(!valid) {
            if(!s->unavailable) { s->unavailable=1;s->phase=XZ_JD_EXTRA_CLEAR;s->clear_next=1; }
            result=XZ_JD_STALE;
        } else if(s->unavailable||snapshot->generation!=s->software_generation) {
            s->unavailable=0;s->software_generation=snapshot->generation;s->phase=XZ_JD_EXTRA_CLEAR;s->clear_next=1;
        }
    }
    if(s->phase==XZ_JD_EXTRA_CLEAR||s->phase==XZ_JD_NATIVE_CLEAR) {
        s->have_info=0;s->after_info=0;s->wave_since_info=0;s->wave_next=0;
        unsigned deck=s->phase==XZ_JD_EXTRA_CLEAR?s->physical_side+2u:s->physical_side;
        xz_jd_clear_packet(deck,s->clear_next++,packet);emitted=1;
        if(s->clear_next==6) { s->phase=s->phase==XZ_JD_EXTRA_CLEAR?XZ_JD_EXTRA:XZ_JD_REPLAY;s->replay_family=0;for(sequence=0;sequence<6;sequence++){s->family[sequence].replay=0;s->family[sequence].replay_version=s->family[sequence].version;} }
    } else if(s->phase==XZ_JD_EXTRA) { if(s->unavailable)unavailable(s->selected,packet);else software_packet(s,snapshot,now,packet);emitted=1; }
    else if(s->phase==XZ_JD_REPLAY) {
        if((++s->replay_ticks&7u)==0&&s->family[0].complete) { memcpy(packet,s->family[0].bank[s->family[0].active],64);emitted=1; }
        else emitted=replay(s,packet);
        if(!emitted&&s->phase!=XZ_JD_NATIVE) {
            /* Remain in truthful native INFO while an invalidated media epoch
             * is being recaptured. Never replay partial old artwork. */
            if(s->family[0].complete)memcpy(packet,s->family[0].bank[s->family[0].active],64);
            else unavailable(s->physical_side,packet);
            emitted=1;
            result=XZ_JD_RESTORING;
        }
    }
    if(emitted) {
        if(!s->ever_owned)s->wire_sequence=(uint16_t)get16(native+8);
        s->ever_owned=1;
        /* Local commands address the physical receiver; frame 0x53 carries the selected deck. */
        packet[0]=(uint8_t)((packet[0]&15u)|(s->physical_side<<4));
        swap64(out+12,packet);out[7]&=0xcf;
        out[0x53]=(uint8_t)((out[0x53]&15u)|((s->selected)<<4));
        if(s->selected==s->physical_side+2u) {
            quiet_status(out);
            if(!s->unavailable&&snapshot&&snapshot->status_known)extra_status(out,snapshot,now);
        }
    }
    if(s->ever_owned) {
        if(emitted||new_packet)s->wire_sequence++;
        set16(out+8,s->wire_sequence);set32(out+144,xz_jd_crc(out));
    }
    return result;
}
int xz_jd_prepare(struct xz_jd *s,const uint8_t native[148],unsigned selected,const struct xz_jd_snapshot *snapshot,
 uint64_t now,const struct xz_jd_wave *wave,uint32_t wave_revision,struct xz_jd_transaction *tx){
    if(!local_display(native))return XZ_JD_UNSUPPORTED;
    observe_native(s,native);tx->next=*s;
    tx->selected=selected==s->physical_side?xz_jd_select(&tx->next,selected,NULL,now):
        snapshot?xz_jd_select(&tx->next,selected,snapshot,now):xz_jd_select_unavailable(&tx->next,selected);
    if(tx->next.software_wave!=wave||tx->next.wave_revision!=wave_revision){
        if(tx->next.software_wave&&!wave&&tx->next.phase==XZ_JD_EXTRA){tx->next.phase=XZ_JD_EXTRA_CLEAR;tx->next.clear_next=1;}
        tx->next.software_wave=wave;tx->next.wave_revision=wave_revision;tx->next.wave_next=0;tx->next.have_info=0;
    }
    tx->phase_before=tx->next.phase;
    tx->result=xz_jd_step(&tx->next,native,snapshot,now,tx->frame);return tx->result;
}
void xz_jd_commit(struct xz_jd *s,const struct xz_jd_transaction *tx){*s=tx->next;}
