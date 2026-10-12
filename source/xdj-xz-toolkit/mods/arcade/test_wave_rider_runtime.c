/* SPDX-License-Identifier: MIT */
#define XZ_WAVE_RIDER_RUNTIME_TEST
#define main stock_runtime_test
#include "../ui/test_runtime_controls.c"
#undef main
#include "wave_rider.h"
#include <stdio.h>

void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
const char *xz_kart_runtime_sdk_test_root(void){return NULL;}
static struct xz_wave_rider_snapshot source_wave;
static int source_result=1,source_selected=-1;
static unsigned source_selects,source_reads;
int xz_wave_rider_source_start(void){return 0;}
void xz_wave_rider_source_select(int deck){source_selected=deck;source_selects++;}
int xz_wave_rider_source_read(struct xz_wave_rider_snapshot *out){
    source_reads++;*out=source_wave;out->deck=source_selected;out->observed_ms=xz_game_milliseconds();return source_result;
}
#define proof xz_wave_rider_proof_v2
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
    const struct xz_ui_widget *entry=NULL;
    for(size_t i=0;i<count;i++)if(widgets[i].kind==XZ_UI_WAVE_RIDER_OPEN){entry=&widgets[i];break;}
    assert(entry);
    contact(touch_fixture,(unsigned)(entry->x+entry->w/2),(unsigned)(entry->y+entry->h/2),1);
    assert(xz_wave_rider_present()&&touch.visible&&!model.fb_takeover);
    int before=stock_calls;contact(touch_fixture,750,24,1);assert(xz_wave_rider_present());
    contact(touch_fixture,750,24,0);assert(xz_wave_rider_present()&&stock_calls==before);
    assert(!touch.capture&&!touch.opening_contact);draw();assert(proof.phase==XZ_RIDER_SETUP);
}
static void valid_analysis(unsigned generation){
    memset(&source_wave,0,sizeof(source_wave));source_wave.valid=source_wave.playing=1;
    source_wave.count=XZ_WAVE_RIDER_SAMPLES;source_wave.total=10000;source_wave.position=500;
    source_wave.generation=generation;source_wave.track_hash=100+generation;source_wave.kind=1;
    source_wave.normalization=300;source_wave.pulse_period=75;source_wave.pulse_confidence=1;
    strcpy(source_wave.title,"ROUTING FIXTURE");
    for(unsigned i=0;i<XZ_WAVE_RIDER_SAMPLES;i++){
        source_wave.bands[i*3]=(uint8_t)(i%75<10?240-i%75*21:4);
        source_wave.bands[i*3+1]=65;source_wave.bands[i*3+2]=(uint8_t)(i%300<25?100-i%25*3:4);
    }
}
static void enter_controls(unsigned mode,unsigned music){
    tap(mode==0?150:mode==1?400:650,250);
    tap(400,366);tap(music?580:200,180);tap(400,366);draw();tap(400,366);draw();
    assert(proof.mode==mode&&proof.deck==music&&source_selected==(int)music);
}
static void jog_and_cue_regression(void){
    valid_analysis(90);open_game(0);enter_controls(XZ_RIDER_SOLO,0);
    assert(proof.difficulty==XZ_RIDER_EASY);tap(560,220);draw();assert(proof.difficulty==XZ_RIDER_HARD);
    tap(400,220);draw();assert(proof.difficulty==XZ_RIDER_NORMAL);
    float before=proof.pitch[0];
    assert(key(0,0x4305,4,80));draw();
    float turn=fabsf(proof.pitch[0]-before);
    assert(key(0,0x4306,0,0));draw();unsigned resting=(unsigned)proof.tuck[0];
    assert(key(0,0x4102,0,0));draw();unsigned pressed=(unsigned)proof.tuck[0];
    assert(key(0,0x4102,2,0));draw();unsigned released=(unsigned)proof.tuck[0];
    assert(key(0,0x4306,2,0));
    printf("CONTROL_REGRESSION jog_80_pulses_radians=%.3f platter_rest=%u cue_down=%u cue_up=%u\n",(double)turn,resting,pressed,released);
    fflush(stdout);
    assert(turn<=.12f);
    assert(!resting&&pressed&&!released);
    assert(key(0,0x4120,0,0));assert(!xz_wave_rider_present());assert(key(0,0x4120,2,0));
    open_game(0);enter_controls(XZ_RIDER_SOLO,0);assert(proof.difficulty==XZ_RIDER_NORMAL);
    tap(400,452);draw();assert(proof.phase==XZ_RIDER_RIDE&&proof.difficulty==XZ_RIDER_EASY);
    assert(key(0,0x4120,0,0));assert(!xz_wave_rider_present());assert(key(0,0x4120,2,0));
}
int main(void){
    stock_runtime_test();decks[0][0x26]=1;decks[1][0x26]=2;settings_readonly=1;native_active=0;
    jog_and_cue_regression();
    memset(&source_wave,0,sizeof(source_wave));source_wave.error=XZ_WAVE_RIDER_NO_ANALYSIS;
    open_game(1);tap(400,366);tap(580,180);draw();assert(source_selected==1&&proof.deck==1);
    tap(400,366);tap(400,366);
    assert(!xz_wave_rider_present()&&!touch.visible&&model.fb_takeover&&source_selected==-1);
    puts("PASS native Games action, opening/release ownership, missing-data setup and takeover restore");

    valid_analysis(1);open_game(0);enter_controls(XZ_RIDER_SOLO,0);
    assert(!key(1,0x4305,4,10)&&!key(1,0x4102,0,0)&&!key(1,0x4306,0,0));
    assert(!key(0,0x4101,0,0)&&!key(0,0x4101,2,0)&&!key(0,0x4103,0,0));
    float slope_pitch=proof.pitch[0];
    assert(key(0,0x4305,4,400));draw();assert(proof.pitch[0]<slope_pitch-.2f);
    assert(key(0,0x4102,0,0));draw();assert(proof.tuck[0]==1);
    assert(key(0,0x4102,2,0));draw();assert(proof.tuck[0]==0);
    tap(400,380);draw();assert(proof.phase==XZ_RIDER_RIDE);
    assert(key(0,0x4306,0,0));draw();assert(!proof.tuck[0]);
    assert(key(0,0x4102,0,0));draw();assert(proof.tuck[0]==1);
    assert(key(0,0x4119,0,0));assert(key(0,0x4102,2,0));draw();assert(proof.tuck[0]==1);
    assert(key(0,0x4119,2,0));draw();assert(!proof.tuck[0]);
    assert(key(0,0x4306,2,0));draw();assert(proof.tuck[0]==0);
    source_wave.position=520;draw();source_wave.playing=0;draw();assert(proof.phase==XZ_RIDER_HOLD);
    source_wave.playing=1;draw();assert(proof.phase==XZ_RIDER_RIDE);
    assert(xz_wave_rider_draw(screen+1,800*480,800,xz_game_milliseconds()-5,NULL));assert(proof.valid&&proof.phase==XZ_RIDER_RIDE);
    assert(key(0,0x4306,0,0));assert(key(0,0x4120,0,0));assert(!xz_wave_rider_present());
    assert(key(0,0x4120,2,0)&&key(0,0x4306,2,0));assert(!key(0,0x4306,2,0));
    puts("PASS solo jog pitch, held dive/chord releases, music and other-deck pass-through, pause and freshness");

    for(unsigned mode=XZ_RIDER_COOP;mode<=XZ_RIDER_DUEL;mode++){
        valid_analysis(mode+2);open_game(1);enter_controls(mode,1);
        float p1=proof.pitch[1],p0_start=proof.pitch[0];
        assert(key(0,0x4305,4,700));draw();assert(proof.pitch[0]<p0_start-.35f&&proof.pitch[1]==p1);
        float p0=proof.pitch[0];
        assert(key(1,0x4305,4,-800));draw();assert(proof.pitch[1]>p1+.35f&&proof.pitch[0]==p0);
        assert(key(0,0x4102,0,0));assert(key(1,0x4306,0,0));draw();assert(proof.tuck[0]==1&&!proof.tuck[1]);
        assert(key(1,0x4119,0,0));draw();assert(proof.tuck[1]==1);
        assert(key(0,0x4102,2,0));draw();assert(proof.tuck[0]==0&&proof.tuck[1]==1);
        assert(key(1,0x4119,3,0));draw();assert(proof.tuck[1]==0);
        assert(key(1,0x4306,3,0));
        for(unsigned deck=0;deck<2;deck++){
            assert(!key((int)deck,0x4101,0,0)&&!key((int)deck,0x4101,2,0));
            unsigned rgb;int enabled;assert(xz_wave_rider_pad((int)deck,0,&rgb,&enabled)&&enabled);
            assert(rgb==(deck?0xff8b80:0x53e5d9));
            assert(xz_wave_rider_pad((int)deck,4,&rgb,&enabled)&&!enabled);
        }
        tap(400,380);draw();assert(proof.phase==XZ_RIDER_RIDE);
        contact(touch_fixture,600,200,1);draw();assert(!proof.tuck[0]&&proof.tuck[1]);
        contact(touch_fixture,100,200,0);draw();assert(!proof.tuck[1]);
        assert(key(0,0x4102,0,0)&&key(1,0x4306,0,0));
        contact(touch_fixture,740,24,1);assert(!xz_wave_rider_present()&&model.fb_takeover);
        int before=stock_calls;contact(touch_fixture,740,24,0);assert(stock_calls==before);
        assert(key(0,0x4102,2,0)&&key(1,0x4306,2,0));assert(!key(1,0x4306,2,0));
    }
    puts("PASS reopened mode selection, independent two-jog/two-deck controls, shared-song source, power LEDs and touch-owner release");
    valid_analysis(7);open_game(0);enter_controls(XZ_RIDER_DUEL,0);tap(400,452);draw();
    assert(proof.phase==XZ_RIDER_RIDE);source_wave.generation=8;source_wave.track_hash=808;draw();assert(proof.phase==XZ_RIDER_TITLE);
    assert(key(0,0x4102,0,0));draw();assert(proof.phase==XZ_RIDER_RIDE&&proof.tuck[0]);assert(key(0,0x4102,2,0));
    source_result=0;struct timespec wait={0,300000000};nanosleep(&wait,NULL);draw();
    assert(proof.phase==XZ_RIDER_SETUP&&!proof.valid);
    printf("ROUTING_JSON {\"frames\":%u,\"inputs\":%u,\"source_reads\":%u,\"source_selects\":%u,\"both_jogs\":true,\"press_release\":true}\n",
        proof.frames,proof.inputs,source_reads,source_selects);
    return 0;
}
