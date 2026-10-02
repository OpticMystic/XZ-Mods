/* SPDX-License-Identifier: MIT */
#define XZ_WAVE_RIDER_RUNTIME_TEST
#define main stock_runtime_test
#include "../ui/test_runtime_controls.c"
#undef main
#include "wave_rider.h"
#include <stdio.h>

void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
static struct xz_wave_rider_snapshot source_wave;
static int source_result=1,source_start_result,source_selected=-1;
static unsigned source_starts,source_selects,source_reads;
int xz_wave_rider_source_start(void){source_starts++;return source_start_result;}
void xz_wave_rider_source_select(int deck){source_selected=deck;source_selects++;}
int xz_wave_rider_source_read(struct xz_wave_rider_snapshot *out){
    source_reads++;*out=source_wave;out->deck=source_selected;out->observed_ms=xz_game_milliseconds();return source_result;
}
/* Read the runtime's published acceptance counters without changing game state. */
struct rider_proof {
    uint32_t version,frames,inputs,phase,deck,kind,valid,generation;
    uint32_t first,total,spikes,gems,score,judged,render_us,worst_render_us;
    float position,altitude;
};
extern struct rider_proof xz_wave_rider_proof_v1;
static uint16_t screen[800*480+2];
static unsigned char touch_fixture[64],decks[2][64];
static void draw(void){
    screen[0]=0x1234;screen[800*480+1]=0xabcd;
    assert(xz_mods_render_v1(screen+1,800,480,800,NULL));
    assert(screen[0]==0x1234&&screen[800*480+1]==0xabcd);
}
static int key(int deck,uint16_t code,unsigned operation,int32_t pulses){
    uint8_t event[24]={0};memcpy(event+8,&code,2);event[11]=(uint8_t)operation;memcpy(event+12,&pulses,4);
    return xz_ui_runtime_arcade_key(decks[deck],event);
}
static void tap(unsigned x,unsigned y){contact(touch_fixture,x,y,1);contact(touch_fixture,x,y,0);}
static void open_game(int takeover){
    model.fb_takeover=takeover;model.connection.connected=0;touch.visible=1;ui.page=XZ_UI_GAMES;
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t count=xz_ui_layout(&ui,&model,widgets);
    const struct xz_ui_widget *game=NULL;
    for(size_t i=0;i<count;i++)if(widgets[i].kind==XZ_UI_WAVE_RIDER_OPEN){game=&widgets[i];break;}
    assert(game);
    contact(touch_fixture,(unsigned)(game->x+game->w/2),(unsigned)(game->y+game->h/2),1);
    assert(xz_wave_rider_present()&&touch.visible&&!model.fb_takeover&&source_selected>=0);
    int before=stock_calls;
    contact(touch_fixture,750,24,1);assert(xz_wave_rider_present());
    contact(touch_fixture,750,24,0);assert(xz_wave_rider_present()&&stock_calls==before);
    assert(!touch.capture&&!touch.opening_contact);
    draw();assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_SETUP);
}
static void no_analysis(void){
    memset(&source_wave,0,sizeof(source_wave));source_wave.error=XZ_WAVE_RIDER_NO_ANALYSIS;
}
static void valid_analysis(unsigned generation){
    memset(&source_wave,0,sizeof(source_wave));source_wave.valid=source_wave.playing=1;
    source_wave.count=XZ_WAVE_RIDER_SAMPLES;source_wave.total=10000;
    source_wave.position=500;source_wave.generation=generation;source_wave.track_hash=100+generation;
    source_wave.kind=1;source_wave.normalization=255;
    strcpy(source_wave.title,"ROUTING FIXTURE");
    for(unsigned i=0;i<XZ_WAVE_RIDER_SAMPLES;i++){
        source_wave.bands[i*3]=(uint8_t)(i%90==0?230:8);
        source_wave.bands[i*3+1]=(uint8_t)(40+i/40%60);
        source_wave.bands[i*3+2]=(uint8_t)(i%120<30?140:8);
    }
}
int main(void){
    stock_runtime_test();decks[0][0x26]=1;decks[1][0x26]=2;
    settings_readonly=1;native_active=0;
    no_analysis();open_game(1);assert(source_starts==1&&source_selected==0);
    assert(!xz_wave_rider_proof_v1.valid);
    tap(400,366); /* Detect -> choose. */
    tap(580,180);assert(source_selected==1);draw();assert(xz_wave_rider_proof_v1.deck==1);
    tap(400,366); /* Choose -> verify analysis. */
    assert(key(1,0x4102,0,0));assert(key(1,0x4102,2,0));draw();
    assert(xz_wave_rider_present()&&xz_wave_rider_proof_v1.phase==XZ_RIDER_SETUP);
    int before=stock_calls;tap(400,366); /* Missing analysis offers return to native track loading. */
    assert(!xz_wave_rider_present()&&!touch.visible&&model.fb_takeover&&source_selected== -1);
    assert(stock_calls==before); /* Exit's release is still owned. */
    puts("PASS Games action, opening touch ownership, deck choice, missing-analysis setup and takeover restore");

    valid_analysis(1);open_game(0);assert(source_selected==1);draw();
    assert(xz_wave_rider_proof_v1.valid);
    tap(80,452);tap(200,180);assert(source_selected==0);tap(400,366);draw();
    assert(xz_wave_rider_proof_v1.deck==0);
    tap(400,366); /* Controls verification. */
    unsigned inputs=xz_wave_rider_proof_v1.inputs;
    assert(!key(1,0x4305,4,10));assert(!key(1,0x4102,0,0));assert(!key(1,0x4102,2,0));
    assert(!key(1,0x4306,0,0)&&!key(1,0x4306,2,0));
    assert(!key(0,0x4101,0,0));assert(!key(0,0x4101,2,0));
    assert(!key(0,0x4103,0,0));assert(!key(0,0x4103,2,0));
    assert(key(0,0x4305,4,20));assert(key(0,0x4305,4,-5));
    assert(key(0,0x4102,0,0));assert(key(0,0x4102,0,0));assert(key(0,0x4102,2,0));
    assert(!key(0,0x4102,2,0));assert(xz_wave_rider_proof_v1.inputs==inputs+3);
    tap(400,380);draw();assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_RIDE);
    unsigned rgb;int enabled;assert(xz_ui_runtime_pad_color(0,0,&rgb,&enabled)&&enabled);
    assert(rgb==0x53e5d9);
    assert(!xz_wave_rider_pad(1,0,&rgb,&enabled));
    float before_hop=xz_wave_rider_proof_v1.altitude;
    assert(key(0,0x4305,4,30));assert(key(0,0x4102,0,0));assert(key(0,0x4102,3,0));draw();
    assert(xz_wave_rider_proof_v1.altitude>before_hop+50);
    source_wave.position=520;draw();assert(xz_wave_rider_proof_v1.position==520);
    source_wave.playing=0;draw();assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_HOLD);
    source_wave.playing=1;draw();assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_RIDE);
    assert(xz_wave_rider_draw(screen+1,800*480,800,xz_game_milliseconds()-5));
    assert(xz_wave_rider_proof_v1.valid&&xz_wave_rider_proof_v1.phase==XZ_RIDER_RIDE);
    assert(screen[0]==0x1234&&screen[800*480+1]==0xabcd);
    puts("PASS source publication newer than the caller frame timestamp remains fresh and riding");
    puts("PASS selected jog and CUE routing, paired releases, PLAY/other deck pass-through, source pause/resume and live pixels");

    assert(key(0,0x4306,0,0)&&key(0,0x4306,0,0));
    assert(key(0,0x4120,0,0));assert(!xz_wave_rider_present()&&!touch.visible&&!model.fb_takeover);
    assert(source_selected== -1&&key(0,0x4120,2,0)&&!key(0,0x4120,2,0));
    assert(key(0,0x4306,2,0)&&!key(0,0x4306,2,0));
    assert(!key(0,0x4306,0,0));
    assert(!key(0,0x4102,0,0)&&!key(0,0x4305,4,10));
    before=stock_calls;tap(700,380);assert(stock_calls==before+2);
    puts("PASS Pad H exit, vinyl touch/release capture, disabled input handoff and native touch restoration");

    valid_analysis(1);open_game(1);tap(400,366);tap(400,452);draw();
    assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_RIDE);
    source_wave.generation=3;source_wave.track_hash=103;source_wave.position=600;draw();
    assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_TITLE&&xz_wave_rider_proof_v1.generation==3);
    assert(key(0,0x4102,0,0)&&key(0,0x4102,2,0));draw();
    assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_RIDE);
    source_result=0;struct timespec stale_delay={0,300000000};nanosleep(&stale_delay,NULL);draw();
    assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_SETUP&&!xz_wave_rider_proof_v1.valid);
    source_result=1;
    no_analysis();draw();assert(xz_wave_rider_proof_v1.phase==XZ_RIDER_SETUP&&!xz_wave_rider_proof_v1.valid);
    assert(key(0,0x4306,0,0));
    contact(touch_fixture,740,24,1);assert(!xz_wave_rider_present()&&model.fb_takeover);
    before=stock_calls;contact(touch_fixture,740,24,0);assert(stock_calls==before);
    assert(key(0,0x4306,3,0)&&!key(0,0x4306,3,0));
    assert(source_reads>0&&source_selects>=6);
    puts("PASS source generation reset, stalled/missing source returns to setup and touch-exit release ownership");
    printf("ROUTING_JSON {\"source_reads\":%u,\"source_selects\":%u,\"real_runtime_frames\":%u,\"real_runtime_inputs\":%u}\n",
        source_reads,source_selects,xz_wave_rider_proof_v1.frames,xz_wave_rider_proof_v1.inputs);
    return 0;
}
