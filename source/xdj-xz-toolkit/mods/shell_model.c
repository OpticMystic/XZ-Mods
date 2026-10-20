/* SPDX-License-Identifier: MIT */
#include "shell_model.h"
#include <limits.h>
#include <string.h>

/* Storage must be zero-initialised and suitably aligned (malloc/calloc suffice).
 * Serialise refresh, setters and render on the model thread. A worker hands off
 * decoded arrays to that thread; these functions are deliberately lock-free.
 * Fixed caps: title 256 UTF-16 units / 769 UTF-8 bytes, key 31 bytes,
 * grid 4096 beats, scrolling wave 32768 columns, overview 1024 columns.
 * Oversize waves are peak-binned across the ENTIRE input, preserving span_ms.
 * Inputs above 1000000 columns (the existing PWV7 reader cap) are rejected.
 *
 * Header additions for the owning integrator (this worker owns only .c files):
 * void xz_shell_model_set_input(struct xz_shell_model_storage *, int deck,
 *     const struct xz_shell_deck *input, unsigned device_kind);
 * void xz_shell_model_diagnostics(const struct xz_shell_model_storage *,
 *     uint32_t out[4]);
 * int xz_shell_model_hot_cue_out(const struct xz_shell_model_storage *,
 *     int deck, int cue, int32_t *out_ms);
 * Input known bits accepted: LOADED, PLAYING, KEY (including match), KEY_SHIFT,
 * GRID, BAR_PHASE, SOURCE, ARTIST (copied, from the export by the loaded id), MEMORY_CUES (from the
 * track's analysis file). SOURCE maps device_kind 2/3 to USB1/USB2 (inferred).
 * The caller copies validated key/native-clock snapshots here before EVERY
 * refresh; inputs are consumed on refresh so a failed poll cannot reuse LIVE
 * bits. NULL clears input. All pointed-to data is copied, never borrowed.
 * Diagnostics are cumulative guard failures in R3, R4, R5, R6 order, saturating
 * at UINT32_MAX; a failed guard read counts too, once per family per refresh.
 */
enum { TITLE_BYTES=769, KEY_BYTES=32, GRID_CAP=4096, SCROLL_CAP=32768,
       OVERVIEW_CAP=1024, WAVE_INPUT_CAP=1000000, FRAME_BYTES=0x26c, TOTAL_BYTES=0x28,
       SETTINGS_CAP=512, RETRIES=3, SETTING_BYTES=0x8c, SETTINGS_CHUNK=29 /* entries per bulk read */ };
/* A deck whose native read fails for a moment (a torn frame or an unstable title record) keeps its
   last complete snapshot this long, so a single bad read never blanks the lanes. elapsed_at_ms stays
   at the time of that snapshot, so the renderer's extrapolation remains honest. */
#define HOLD_MS 250u
#define K(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
#define SET(f) (1u<<XZ_SHELL_SET_##f)
struct xz_shell_model_input {
    struct xz_shell_deck deck;
    char key[KEY_BYTES], key_original[KEY_BYTES], artist[TITLE_BYTES];
    struct xz_shell_beat grid[GRID_CAP];
};
struct deck_storage {
    char title[TITLE_BYTES], identity_title[TITLE_BYTES];
    int identity_valid, identity_loaded, identity_track_known, identity_track;
    int32_t identity_total;
    struct xz_shell_model_input input;
    struct xz_shell_column scroll[SCROLL_CAP], overview[OVERVIEW_CAP], stems[SCROLL_CAP];
    uint32_t scroll_count, scroll_span, overview_count, overview_span, stems_count, stems_span;
    int hot_out_known;int32_t hot_out[XZ_SHELL_HOT_CUES];
    /* Last complete native snapshot (before runtime inputs), for HOLD_MS. */
    struct xz_shell_deck held;char held_title[TITLE_BYTES];
    int held_valid;uint32_t held_at,held_id;
};
struct xz_shell_model_storage {
    struct deck_storage deck[XZ_SHELL_NATIVE_DECKS];
    uint32_t caution, guard_failures[4], fx_failures, holds;
    /* Code bytes never change once they matched, so a verified guard is not read again. */
    unsigned guard_ok, fx_guard_ok, slip_guard_ok;
    unsigned char fx_raw[12];int fx_raw_ok;
};
struct native_deck { unsigned char frame[FRAME_BYTES], total[TOTAL_BYTES]; };
struct setting_pair { uint32_t id, value; };

static uint16_t u16(const unsigned char *p){return (uint16_t)((unsigned)p[0]|(unsigned)p[1]<<8);}
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int pointer(uint32_t p,size_t n){return p>=0x10000u&&!(p&3u)&&(uint64_t)p+n<=UINT32_MAX;}
static int bytes(xz_shell_read_fn read,uint32_t p,void *out,size_t n){
    return read&&p>=0x10000u&&(uint64_t)p+n<=UINT32_MAX&&!read(p,out,n);
}
static int word(xz_shell_read_fn read,uint32_t p,uint32_t *out){
    unsigned char b[4];if(!bytes(read,p,b,4))return 0;*out=u32(b);return 1;
}
static int guard(xz_shell_read_fn read,uint32_t p,const unsigned char expected[16],uint32_t *failures){
    unsigned char b[16];if(bytes(read,p,b,16)&&!memcmp(b,expected,16))return 1;
    if(*failures!=UINT32_MAX)++*failures;
    return 0;
}
static int stable(xz_shell_read_fn read,uint32_t p,void *out,void *again,size_t n){
    for(unsigned attempt=0;attempt<RETRIES;attempt++){
        if(!bytes(read,p,out,n)||!bytes(read,p,again,n))return 0;
        if(!memcmp(out,again,n))return 1;
    }
    return 0;
}
static int chain(xz_shell_read_fn read,unsigned deck,uint32_t out[3]){
    return word(read,0x1c8981c,out)&&pointer(out[0],0x3c)&&
        word(read,out[0]+0x38,out+1)&&pointer(out[1],0x14)&&
        word(read,out[1]+0xc+deck*4,out+2)&&pointer(out[2],FRAME_BYTES);
}
static int native_deck(xz_shell_read_fn read,unsigned deck,struct native_deck *out){
    struct native_deck again;uint32_t a[3],b[3],c[3];
    for(unsigned attempt=0;attempt<RETRIES;attempt++){
        if(!chain(read,deck,a)||!bytes(read,a[2],out->frame,FRAME_BYTES)||
           !bytes(read,0x2247cac+0x1610+deck*0x60,out->total,TOTAL_BYTES)||
           !chain(read,deck,b)||!bytes(read,b[2],again.frame,FRAME_BYTES)||
           !bytes(read,0x2247cac+0x1610+deck*0x60,again.total,TOTAL_BYTES)||
           !chain(read,deck,c))return 0;
        if(!memcmp(a,b,sizeof(a))&&!memcmp(a,c,sizeof(a))&&
           !memcmp(out,&again,sizeof(again)))return 1;
    }
    return 0;
}
/* Reject malformed UTF-16, rather than display a partial or invented title. 1 = title, 0 = malformed or
   empty, -1 = the record could not be read stably (transient). */
static int title(xz_shell_read_fn read,unsigned deck,char out[TITLE_BYTES]){
    unsigned char record[0x224],again[0x224];size_t at=0;
    if(!stable(read,0x224dd4c+deck*0x224,record,again,sizeof(record)))return -1;
    unsigned length=u16(record+0x20);
    if(!u16(record+8)||!length||length>256)return 0;
    for(unsigned i=0;i<length;i++){
        uint32_t cp=u16(record+0x22+i*2);
        if(!cp)return 0;
        if(cp>=0xd800&&cp<=0xdbff){
            if(++i>=length)return 0;
            unsigned low=u16(record+0x22+i*2);
            if(low<0xdc00||low>0xdfff)return 0;
            cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
        }else if(cp>=0xdc00&&cp<=0xdfff)return 0;
        if(cp<0x80)out[at++]=(char)cp;
        else if(cp<0x800){out[at++]=(char)(0xc0|(cp>>6));out[at++]=(char)(0x80|(cp&63));}
        else if(cp<0x10000){out[at++]=(char)(0xe0|(cp>>12));out[at++]=(char)(0x80|((cp>>6)&63));out[at++]=(char)(0x80|(cp&63));}
        else {out[at++]=(char)(0xf0|(cp>>18));out[at++]=(char)(0x80|((cp>>12)&63));out[at++]=(char)(0x80|((cp>>6)&63));out[at++]=(char)(0x80|(cp&63));}
    }
    out[at]=0;return 1;
}
static int settings_head(xz_shell_read_fn read,uint32_t head[3]){
    return word(read,0x1c8a404,head)&&pointer(head[0],0x170)&&
        word(read,head[0]+0x164,head+1)&&word(read,head[0]+0x16c,head+2)&&
        head[2]<=SETTINGS_CAP&&(!head[2]||pointer(head[1],(size_t)head[2]*0x8c));
}
/* Reads the list in bulk, SETTINGS_CHUNK entries per read, each chunk twice: about 2 * n/29 reads
   instead of 2 * n word reads (each read is one pread of /proc/self/mem on the deck). */
static int settings_pass(xz_shell_read_fn read,uint32_t list,uint32_t n,struct setting_pair *out){
    unsigned char a[SETTINGS_CHUNK*SETTING_BYTES],b[SETTINGS_CHUNK*SETTING_BYTES];
    for(uint32_t first=0;first<n;first+=SETTINGS_CHUNK){
        uint32_t count=n-first<SETTINGS_CHUNK?n-first:SETTINGS_CHUNK;size_t length=(size_t)count*SETTING_BYTES;
        if(!stable(read,list+first*SETTING_BYTES,a,b,length))return 0;
        for(uint32_t i=0;i<count;i++){out[first+i].id=u32(a+i*SETTING_BYTES);out[first+i].value=u32(a+i*SETTING_BYTES+0x88);}
    }
    return 1;
}
static int settings(xz_shell_read_fn read,struct setting_pair *out,uint32_t *count){
    struct setting_pair again[SETTINGS_CAP];uint32_t a[3],b[3],c[3];
    for(unsigned attempt=0;attempt<RETRIES;attempt++){
        if(!settings_head(read,a)||!settings_pass(read,a[1],a[2],out)||
           !settings_head(read,b))return 0;
        if(memcmp(a,b,sizeof(a)))continue;
        if(!settings_pass(read,b[1],b[2],again)||!settings_head(read,c))return 0;
        if(!memcmp(a,c,sizeof(a))&&!memcmp(out,again,a[2]*sizeof(*out))){*count=a[2];return 1;}
    }
    return 0;
}
static int setting(const struct setting_pair *list,uint32_t n,uint32_t id,uint32_t *value){
    int found=0;
    for(uint32_t i=0;i<n;i++)if(list[i].id==id){if(found)return 0;*value=list[i].value;found=1;}
    return found;
}
/* SLIP and VINYL come from the same per-deck frame, at the bytes the stock getters read:
   StatWatcher::getPlaySlipOn 0x2498a0 (+0x3c bit 2), getPlaySlipping 0x2498ec (+0x3c bit 3), getPlaySlipTime
   0x249984 (+0x258, the ghost position; ComputePlayAddress 0xec614 scales it by UiGetPlayTotalTime, so it is in
   the units of total_ms) and getPlayModeVinyl 0x249e2c (+0x2d bit 0). The guard is each getter's load. */
static const struct { uint32_t at; unsigned char code[16]; } slip_loads[4]={
    {0x2498d4,{0x3c,0x00,0xd3,0xe5,0x50,0x01,0xe0,0xe7,0x1e,0xff,0x2f,0xe1,0x3c,0x00,0xd0,0xe5}},
    {0x249920,{0x3c,0x00,0xd3,0xe5,0xd0,0x01,0xe0,0xe7,0x1e,0xff,0x2f,0xe1,0x3c,0x00,0xd0,0xe5}},
    {0x2499a0,{0x00,0x00,0x51,0xe3,0x58,0x02,0x93,0x05,0x1e,0xff,0x2f,0x01,0x01,0x00,0x51,0xe3}},
    {0x249e60,{0x2d,0x00,0xd3,0xe5,0x01,0x00,0x00,0xe2,0x1e,0xff,0x2f,0xe1,0x2d,0x00,0xd0,0xe5}}};
static int slip_guards(xz_shell_read_fn read){
    unsigned char b[16];
    for(unsigned i=0;i<4;i++)if(!bytes(read,slip_loads[i].at,b,16)||memcmp(b,slip_loads[i].code,16))return 0;
    return 1;
}
static void slip_fields(struct xz_shell_deck *d,const struct native_deck *n,uint32_t now_ms){
    const unsigned char *f=n->frame;
    d->slip=!!(f[0x3c]&4);d->vinyl=f[0x2d]&1;d->known|=K(SLIP)|K(VINYL);
    uint32_t ghost=u32(f+0x258);
    if(d->slip&&(f[0x3c]&8)&&ghost<=INT32_MAX){d->slip_ms=(int32_t)ghost;d->slip_at_ms=now_ms?now_ms:1;d->known|=K(SLIP_POSITION);}
}
static void frame_fields(struct xz_shell_deck *d,struct deck_storage *storage,const struct native_deck *n,uint32_t now_ms){
    const unsigned char *f=n->frame;uint32_t value;
    value=u32(f+8);if(value<=INT32_MAX){d->total_ms=(int32_t)value;d->known|=K(TOTAL);}
    value=u32(f+0xc);if(value<=INT32_MAX){d->elapsed_ms=(int32_t)value;d->elapsed_at_ms=now_ms?now_ms:1;d->known|=K(ELAPSED);}
    value=u32(f+0x14);if(value<=INT32_MAX){d->bpm_x100=(int32_t)value;d->known|=K(BPM);}
    value=u32(f+0x18);if(value<=INT32_MAX){d->track_bpm_x100=(int32_t)value;d->known|=K(TRACK_BPM);}
    unsigned rate=u16(f+0x22);d->tempo_x100=rate&0x8000?(int32_t)rate-65536:(int32_t)rate;
    /* Tempo range, byte +0x20: ui::StatWatcher::getPlayTempoRange 0x249cc0 returns exactly this byte of
     * the same per-deck frame, and SetPlayInfo 0xefe00 / setControllerDisplayData 0xf0da0 read it as
     * 6, 10, 16 or 100 (WIDE): the range in percent. Anything else stays unknown. */
    if(f[0x20]==6||f[0x20]==10||f[0x20]==16||f[0x20]==100){d->tempo_range=f[0x20];d->known|=K(TEMPO_RANGE);}
    d->master_tempo=!!(f[0x2d]&4);d->master=!!(f[0x2f]&4);d->sync=!!(f[0x2f]&8);
    d->known|=K(TEMPO)|K(MASTER_TEMPO)|K(MASTER)|K(SYNC);
    d->loop.active=!!(f[0x2d]&0x80)&&!(f[0x2f]&2);
    d->loop.beats_num=u16(f+0x24);d->loop.beats_den=u16(f+0x26);
    uint32_t in=u32(f+0x44),out=u32(f+0x48);
    if(d->loop.beats_den&&in<=INT32_MAX&&out<=INT32_MAX){
        d->loop.in_ms=(int32_t)in;d->loop.out_ms=(int32_t)out;d->known|=K(LOOP);
    }
    if((f[0x2d]&0x40)&&in<=INT32_MAX){d->cue_ms=(int32_t)in;d->known|=K(CUE_POINT);}
    int valid=1;
    for(unsigned j=0;j<XZ_SHELL_HOT_CUES;j++){
        unsigned off=0x34*j;in=u32(f+0x78+off);out=u32(f+0x7c+off);
        if(in>INT32_MAX||out>INT32_MAX){valid=0;break;}
        /* No registered-flag contract is available. Presence inferred from
         * nonzero position/loop/color needs hardware confirmation. All-zero
         * cannot distinguish unset from a colorless cue at track time 0. */
        struct xz_shell_cue *cue=d->hot_cue+j;
        cue->rgb=(uint32_t)f[0xa4+off]<<16|(uint32_t)f[0xa5+off]<<8|f[0xa6+off];
        cue->set=!!(in||out||u16(f+0x76+off)||cue->rgb);
        cue->ms=(int32_t)in;cue->is_loop=out!=0;
        storage->hot_out[j]=(int32_t)out;
        /* shell_cue has no out_ms member; see the separate read-only getter. */
    }
    storage->hot_out_known=valid;
    if(valid)d->known|=K(HOT_CUES);else memset(d->hot_cue,0,sizeof(d->hot_cue));
    value=u32(f+0x268);
    d->beat_jump_num=value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);
    d->beat_jump_den=1;d->known|=K(BEAT_JUMP);
    value=u32(n->total+0x10);
    if(u32(n->total+0x24)&&u32(n->total)!=7&&value<=INT_MAX){d->track_no=(int)value;d->known|=K(TRACK_NO);}
    /* f+4 transport enum is unproven: never set PLAYING or CUE_STANDBY from it. */
}
size_t xz_shell_model_storage_size(void){return sizeof(struct xz_shell_model_storage);}
void xz_shell_model_diagnostics(const struct xz_shell_model_storage *s,uint32_t out[4]){
    if(out){if(s)memcpy(out,s->guard_failures,sizeof(s->guard_failures));else memset(out,0,4*sizeof(*out));}
}
void xz_shell_model_set_input(struct xz_shell_model_storage *s,int deck,const struct xz_shell_deck *input,unsigned device_kind){
    if(!s||deck<0||deck>=XZ_SHELL_NATIVE_DECKS)return;
    struct xz_shell_model_input *v=&s->deck[deck].input;
    if(!input){v->deck.known=0;return;}
    /* Stage before overwriting to permit a previous model deck as input. */
    struct xz_shell_deck d=*input;
    d.known&=K(LOADED)|K(PLAYING)|K(KEY)|K(KEY_SHIFT)|K(KEY_ORIGINAL)|K(KEY_SYNC)|K(GRID)|K(BAR_PHASE)|K(SOURCE)|K(ARTIST)|K(MEMORY_CUES)|K(SOURCE_LOOK);
    if(d.known&K(ARTIST)){
        size_t n=0;if(d.artist)while(n<TITLE_BYTES&&d.artist[n])++n;
        if(!n||n>=TITLE_BYTES)d.known&=~K(ARTIST);
        else {memmove(v->artist,d.artist,n+1);d.artist=v->artist;}
    }
    if((d.known&K(MEMORY_CUES))&&(d.memory_count<0||d.memory_count>XZ_SHELL_MEMORY_CUES))d.known&=~K(MEMORY_CUES);
    if(d.known&K(KEY)){
        size_t n=0;if(d.key)while(n<KEY_BYTES&&d.key[n])++n;
        if(!n||n>=KEY_BYTES)d.known&=~K(KEY);
        else {memmove(v->key,d.key,n+1);d.key=v->key;}
    }
    if(d.known&K(KEY_ORIGINAL)){
        size_t n=0;if(d.key_original)while(n<KEY_BYTES&&d.key_original[n])++n;
        if(!n||n>=KEY_BYTES||d.key_cents < -50||d.key_cents>50)d.known&=~K(KEY_ORIGINAL);
        else {memmove(v->key_original,d.key_original,n+1);d.key_original=v->key_original;}
    }
    if(d.known&K(GRID)){
        if(!d.grid||!d.grid_count||d.grid_count>GRID_CAP)d.known&=~K(GRID);
        else {
            for(uint32_t i=0;i<d.grid_count;i++)if(d.grid[i].beat_in_bar<1||d.grid[i].beat_in_bar>4||
                d.grid[i].ms<0||(i&&d.grid[i].ms<d.grid[i-1].ms)){d.known&=~K(GRID);break;}
            if(d.known&K(GRID)){memmove(v->grid,d.grid,d.grid_count*sizeof(*d.grid));d.grid=v->grid;}
        }
    }
    if(d.bar_phase<1||d.bar_phase>4)d.known&=~K(BAR_PHASE);
    if(d.known&K(SOURCE)){
        if(device_kind==2)d.source=XZ_SHELL_SRC_USB1;
        else if(device_kind==3)d.source=XZ_SHELL_SRC_USB2;
        else d.known&=~K(SOURCE);
    }
    v->deck=d;
}
static void input_fields(struct xz_shell_deck *d,struct xz_shell_model_input *v){
    const struct xz_shell_deck *i=&v->deck;
    if(i->known&K(LOADED))d->loaded=!!i->loaded;
    if(i->known&K(PLAYING))d->playing=!!i->playing;
    if(i->known&K(KEY)){d->key=i->key;d->key_matches_master=!!i->key_matches_master;}
    if(i->known&K(KEY_ORIGINAL)){d->key_original=i->key_original;d->key_cents=i->key_cents;}
    if(i->known&K(KEY_SYNC))d->key_synced=!!i->key_synced;
    if(i->known&K(KEY_SHIFT))d->key_shift=i->key_shift;
    if(i->known&K(GRID)){d->grid=i->grid;d->grid_count=i->grid_count;}
    if(i->known&K(BAR_PHASE))d->bar_phase=i->bar_phase;
    if(i->known&K(SOURCE))d->source=i->source;
    if(i->known&K(ARTIST))d->artist=i->artist;
    if(i->known&K(SOURCE_LOOK))d->look=i->look;
    if(i->known&K(MEMORY_CUES)){memcpy(d->memory_cue,i->memory_cue,sizeof(d->memory_cue));d->memory_count=i->memory_count;}
    d->known|=i->known;v->deck.known=0;
}
static int identity(struct xz_shell_deck *d,struct deck_storage *s){
    int loaded_known=!!(d->known&K(LOADED));
    if(!loaded_known)return 0;
    int track_known=!!(d->known&K(TRACK_NO));
    if(d->loaded&&((d->known&(K(TITLE)|K(TOTAL)))!=(K(TITLE)|K(TOTAL))))return 0;
    int changed=!s->identity_valid||s->identity_loaded!=d->loaded;
    if(d->loaded&&s->identity_valid&&s->identity_loaded){
        changed|=strcmp(s->identity_title,d->title)!=0||s->identity_total!=d->total_ms;
        if(track_known&&s->identity_track_known)changed|=s->identity_track!=d->track_no;
    }
    /* Initial confirmed empty deck is a baseline, not a new track. */
    if(!s->identity_valid&&!d->loaded)changed=0;
    if(d->loaded){
        memcpy(s->identity_title,d->title,strlen(d->title)+1);s->identity_total=d->total_ms;
        if(track_known){s->identity_track=d->track_no;s->identity_track_known=1;}
        else if(changed)s->identity_track_known=0;
    }else s->identity_track_known=0;
    s->identity_valid=1;s->identity_loaded=d->loaded;
    return changed;
}
/* BEAT FX state (E:/Claude/xz-fx-research-20261018/contracts.md section 1, read verified on the deck):
   mgr = [0x1c8981c], mixer = [mgr+0x50], 12-byte record at mixer+0x198, as MixerCpuCom::getBfxState
   0x25c914 copies it. Written by the mixer comm thread, so the record is read twice and both roots are
   read again. Channel labels and LEVEL/DEPTH are not proven: those bits stay clear. */
static const char *const fx_names[14]={"DELAY","ECHO","PING PONG","SPIRAL","REVERB","TRANS","FILTER",
    "FLANGER","PHASER","PITCH","SLIP ROLL","ROLL","VINYL BRAKE","HELIX"};
static const uint8_t fx_beats[12][2]={{1,16},{1,8},{1,4},{1,2},{3,4},{1,1},{2,1},{4,1},{8,1},{16,1},{32,1},{64,1}};
static int fx_guards(xz_shell_read_fn read){
    static const unsigned char entry[16]={0x66,0x1f,0x81,0xe2,0x00,0x30,0xa0,0xe1,0x07,0x00,0x91,0xe8,0x07,0x00,0x83,0xe8};
    /* Field loads in the stock getters: type +2, ON +3, beat +7, parameter +8, BPM +10. */
    static const struct { uint32_t at; unsigned char code[4]; } loads[5]={
        {0xc9044,{0x06,0x00,0xdd,0xe5}},{0xc91e4,{0x07,0x00,0xdd,0xe5}},{0xc915c,{0x0b,0x00,0xdd,0xe5}},
        {0xc91a0,{0xfc,0x00,0xdd,0xe1}},{0xc90cc,{0xbe,0x00,0xdd,0xe1}}};
    unsigned char b[16];
    if(!bytes(read,0x25c914,b,16)||memcmp(b,entry,16))return 0;
    for(unsigned i=0;i<5;i++)if(!bytes(read,loads[i].at,b,4)||memcmp(b,loads[i].code,4))return 0;
    return 1;
}
static void fx_fields(struct xz_shell_fx *f,struct xz_shell_model_storage *s,xz_shell_read_fn read){
    if(!s->fx_guard_ok){
        if(!fx_guards(read)){if(s->fx_failures!=UINT32_MAX)s->fx_failures++;return;}
        s->fx_guard_ok=1;
    }
    uint32_t mgr,mixer,again;unsigned char a[12],b[12];
    if(!word(read,0x1c8981c,&mgr)||!pointer(mgr,0x54)||!word(read,mgr+0x50,&mixer)||!pointer(mixer,0x1a4)||
       !bytes(read,mixer+0x198,a,12)||!bytes(read,mixer+0x198,b,12)||memcmp(a,b,12)||
       !word(read,0x1c8981c,&again)||again!=mgr||!word(read,mgr+0x50,&again)||again!=mixer)return;
    memcpy(s->fx_raw,a,12);s->fx_raw_ok=1;
    unsigned effect=a[2],beat=a[7];
    f->on=a[3]&1;f->known|=1u<<XZ_SHELL_FX_F_ON;
    /* BPM source: +6 zero AUTO, non-zero TAP. BPM x10 at +10 (102.2 BPM read live as 1022). The +5
       out-of-range byte was 1 in that live sample while the BPM matched the display, so it does not
       gate the BPM; only an implausible value does. */
    f->tap=a[6]!=0;f->known|=1u<<XZ_SHELL_FX_F_TAP;
    {unsigned bpm=u16(a+10);if(bpm>=200&&bpm<=9990){f->bpm_x10=(int32_t)bpm;f->known|=1u<<XZ_SHELL_FX_F_BPM;}}
    if(effect<14){f->name=fx_names[effect];f->known|=1u<<XZ_SHELL_FX_F_NAME;}
    /* REVERB and PITCH show a dashed beat and a percent parameter in stock: beat and time stay blank. */
    if(effect<14&&effect!=4&&effect!=9){
        if(beat>=1&&beat<=12){f->beat_num=fx_beats[beat-1][0];f->beat_den=fx_beats[beat-1][1];f->known|=1u<<XZ_SHELL_FX_F_BEAT;}
        f->time_ms=(int16_t)u16(a+8);f->known|=1u<<XZ_SHELL_FX_F_TIME;
    }
}
uint32_t xz_shell_model_fx_failures(const struct xz_shell_model_storage *s){return s?s->fx_failures:0;}
uint32_t xz_shell_model_holds(const struct xz_shell_model_storage *s){return s?s->holds:0;}
int xz_shell_model_fx_raw(const struct xz_shell_model_storage *s,unsigned char out[12]){
    if(!s||!s->fx_raw_ok||!out)return 0;memcpy(out,s->fx_raw,12);return 1;
}
unsigned xz_shell_model_refresh(struct xz_shell_model *m,struct xz_shell_model_storage *s,xz_shell_read_fn read,uint32_t now_ms){
    static const unsigned char guards[4][16]={
        {0x0c,0x30,0x90,0xe5,0x00,0x00,0x53,0xe3,0x03,0x00,0xa0,0x01,0x1e,0xff,0x2f,0x01},
        {0x08,0x31,0x9f,0xe5,0x01,0x00,0x51,0xe3,0xf0,0x05,0x2d,0xe9,0x41,0x12,0xa0,0x13},
        {0x4c,0x3d,0x0d,0xe3,0x24,0x32,0x40,0xe3,0x89,0x2f,0xa0,0xe3,0x92,0x30,0x20,0xe0},
        {0x7c,0x3f,0x0f,0xe3,0xea,0x33,0x40,0xe3,0x00,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1}};
    static const uint32_t addresses[4]={0x249820,0x2508dc,0xd9310,0x18ca44};
    int ok[4],auto_known[2]={0,0},auto_value[2]={0,0};unsigned changed=0;
    struct setting_pair list[SETTINGS_CAP];uint32_t count=0;
    if(!m||!s)return 0;
    memset(m->deck,0,sizeof(m->deck));memset(&m->settings,0,sizeof(m->settings));memset(&m->fx,0,sizeof(m->fx));m->clock_ms=now_ms;
    for(unsigned i=0;i<4;i++){
        ok[i]=read&&((s->guard_ok>>i&1u)||guard(read,addresses[i],guards[i],s->guard_failures+i));
        if(ok[i])s->guard_ok|=1u<<i;
    }
    if(read)fx_fields(&m->fx,s,read);
    unsigned char caution[4],again[4];
    /* Unknown caution must hand off too. UINT32_MAX is a fail-closed sentinel,
     * distinct from any 16-bit native browse caution id. */
    s->caution=UINT32_MAX;
    if(ok[3]&&stable(read,0x3eaff7c,caution,again,4))s->caution=u32(caution)&0xffff;
    if(ok[1])ok[1]=settings(read,list,&count);
    for(unsigned deck=0;deck<XZ_SHELL_NATIVE_DECKS;deck++){
        struct xz_shell_deck *d=m->deck+deck;struct deck_storage *v=s->deck+deck;struct native_deck n;
        int have_native=ok[0]&&native_deck(read,deck,&n),title_state=0;
        v->hot_out_known=0;
        v->title[0]=0;
        if(ok[2])title_state=title(read,deck,v->title);
        if(title_state>0&&(!have_native||(u32(n.total)!=7&&u32(n.total)!=9))){
            d->title=v->title;d->known|=K(TITLE);
        }
        /* Bracket the independently mutable title record with the same loaded-track record and total
         * time. A concurrent load must not mix two tracks. The rest of the frame (elapsed, BPM, phase)
         * moves on its own while a deck plays, so it is not part of the bracket: the fields come from
         * the first, self-consistent double read. */
        if(have_native){
            struct native_deck after;
            if(!native_deck(read,deck,&after)||memcmp(n.total,after.total,sizeof(n.total))||memcmp(n.frame+8,after.frame+8,4)){
                have_native=0;d->known&=~K(TITLE);d->title=NULL;
            }else {
                frame_fields(d,v,&n,now_ms);auto_known[deck]=1;auto_value[deck]=!!(n.frame[0x2e]&2);
                if(!s->slip_guard_ok)s->slip_guard_ok=slip_guards(read);
                if(s->slip_guard_ok)slip_fields(d,&n,now_ms);
            }
        }
        /* Identity must be settled, with a delivered title and sane total.
         * A valid cached id by itself may describe a pending load. */
        if(have_native){
            uint32_t id=u32(n.total+0xc),state=u32(n.total);
            if(state!=7&&state!=9&&id&&id!=UINT32_MAX&&n.total[9]<=5&&(d->known&K(TITLE))&&
               (d->known&K(TOTAL))&&d->total_ms>0){d->loaded=1;d->known|=K(LOADED);}
            else if(state!=7&&state!=9&&!id&&(d->known&K(TOTAL))&&!d->total_ms){d->known|=K(LOADED);}
        }
        if(ok[1]){
            uint32_t value;
            if(setting(list,count,0x10000003+deck,&value)&&(value==0x100||value==0x101)){
                d->time_mode=value==0x101?XZ_SHELL_TIME_REMAIN:XZ_SHELL_TIME_ELAPSED;d->known|=K(TIME_MODE);
            }
            if(have_native&&setting(list,count,0x62,&value)&&value>=0x30&&value<=0x33){
                d->quantize_on=!!(n.frame[0x2e]&0x80);d->quantize_num=1;d->quantize_den=1<<(0x33-value);d->known|=K(QUANTIZE);
            }
        }
        /* Hold: a complete snapshot is kept; a transient read failure (native frame unreadable or torn,
         * or the title record unstable while the same track stays loaded) republishes it for HOLD_MS.
         * A load in progress (state 7/9), a changed track id or malformed data never holds. */
        {
            uint32_t state=have_native?u32(n.total):0,id=have_native?u32(n.total+0xc):0;
            int complete=have_native&&(d->known&K(LOADED))&&(!d->loaded||(d->known&(K(TITLE)|K(TOTAL)))==(K(TITLE)|K(TOTAL)));
            int transient=read&&ok[0]&&(!have_native||(title_state<0&&state!=7&&state!=9&&id==v->held_id));
            if(complete){
                v->held=*d;v->held_valid=1;v->held_at=now_ms;v->held_id=id;
                if(d->known&K(TITLE))memcpy(v->held_title,d->title,strlen(d->title)+1);else v->held_title[0]=0;
            }else if(transient&&v->held_valid&&(uint32_t)(now_ms-v->held_at)<HOLD_MS){
                *d=v->held;d->title=(d->known&K(TITLE))?v->held_title:NULL;
                if(s->holds!=UINT32_MAX)s->holds++;
            }else if(!transient||(uint32_t)(now_ms-v->held_at)>=HOLD_MS)v->held_valid=0;
        }
        input_fields(d,&v->input);
        if(identity(d,v)){changed|=1u<<deck;v->scroll_count=v->overview_count=v->stems_count=0;}
        /* Hide old data while identity is unreadable, without declaring a new
         * track on a transient failed/torn read. */
        int current=v->identity_valid&&v->identity_loaded&&(d->known&(K(LOADED)|K(TITLE)|K(TOTAL)))==(K(LOADED)|K(TITLE)|K(TOTAL))&&d->loaded;
        if(current&&v->scroll_count){d->wave=(struct xz_shell_wave){v->scroll,v->scroll_count,v->scroll_span};d->known|=K(WAVE);}
        if(current&&v->overview_count){d->overview=(struct xz_shell_wave){v->overview,v->overview_count,v->overview_span};d->known|=K(OVERVIEW);}
#ifdef XZ_SHELL_HAS_STEM_WAVE
        if(current&&v->stems_count){d->stems=(struct xz_shell_wave){v->stems,v->stems_count,v->stems_span};d->known|=K(STEM_WAVE);}
#endif
    }
    if((m->deck[0].known&K(TIME_MODE))&&(m->deck[1].known&K(TIME_MODE))&&m->deck[0].time_mode==m->deck[1].time_mode){
        m->settings.time_mode=m->deck[0].time_mode;m->settings.known|=SET(TIME_MODE);
    }
    /* Auto cue is per deck. Only publish a device-wide value when both agree. */
    if(auto_known[0]&&auto_known[1]&&auto_value[0]==auto_value[1]){
        m->settings.auto_cue=auto_value[0];m->settings.known|=SET(AUTO_CUE);
    }
    return changed;
}
static uint32_t copy_wave(struct xz_shell_column *out,size_t cap,const struct xz_shell_column *in,size_t count,uint32_t span){
    if(!in||!count||!span||count>WAVE_INPUT_CAP)return 0;
    size_t n=count<cap?count:cap;
    if(count<=cap)memmove(out,in,n*sizeof(*out));
    else for(size_t i=0;i<n;i++){
        size_t first=(size_t)((uint64_t)i*count/n),end=(size_t)((uint64_t)(i+1)*count/n);
        struct xz_shell_column peak={0,0,0};
        for(size_t j=first;j<end;j++){
            if(in[j].low>peak.low)peak.low=in[j].low;
            if(in[j].mid>peak.mid)peak.mid=in[j].mid;
            if(in[j].high>peak.high)peak.high=in[j].high;
        }
        out[i]=peak;
    }
    return (uint32_t)n;
}
void xz_shell_model_set_wave(struct xz_shell_model_storage *s,int deck,const struct xz_shell_column *scroll,size_t scroll_count,uint32_t scroll_span_ms,const struct xz_shell_column *overview,size_t overview_count,uint32_t overview_span_ms){
    if(!s||deck<0||deck>=XZ_SHELL_NATIVE_DECKS)return;
    struct deck_storage *d=s->deck+deck;
    d->scroll_count=copy_wave(d->scroll,SCROLL_CAP,scroll,scroll_count,scroll_span_ms);d->scroll_span=scroll_span_ms;
    d->overview_count=copy_wave(d->overview,OVERVIEW_CAP,overview,overview_count,overview_span_ms);d->overview_span=overview_span_ms;
}
void xz_shell_model_set_stems(struct xz_shell_model_storage *s,int deck,const struct xz_shell_column *stems,size_t count,uint32_t span_ms){
    if(!s||deck<0||deck>=XZ_SHELL_NATIVE_DECKS)return;
    struct deck_storage *d=s->deck+deck;
    d->stems_count=copy_wave(d->stems,SCROLL_CAP,stems,count,span_ms);d->stems_span=span_ms;
}
int xz_shell_model_has_stems(const struct xz_shell_model_storage *s,int deck){
    return s&&deck>=0&&deck<XZ_SHELL_NATIVE_DECKS&&s->deck[deck].stems_count;
}
uint32_t xz_shell_model_caution(const struct xz_shell_model_storage *s){return s?s->caution:UINT32_MAX;}

int xz_shell_model_hot_cue_out(const struct xz_shell_model_storage *s,int deck,int cue,int32_t *out_ms){
    if(!s||!out_ms||deck<0||deck>=XZ_SHELL_NATIVE_DECKS||cue<0||cue>=XZ_SHELL_HOT_CUES||!s->deck[deck].hot_out_known)return 0;
    *out_ms=s->deck[deck].hot_out[cue];return 1;
}
