/* SPDX-License-Identifier: MIT */
#define XZ_RHYTHM_LAB_RUNTIME_TEST
#define main stock_runtime_test
#include "../ui/test_runtime_controls.c"
#undef main
#include "rhythm_lab.h"
#include <stdio.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t a,void *o,size_t n){(void)a;(void)o;(void)n;return -1;}
static struct xz_wave_rider_snapshot source_wave;
static int source_selected=-1;static unsigned source_reads;
int xz_wave_rider_source_start(void){return 0;}
void xz_wave_rider_source_select(int deck){source_selected=deck;}
int xz_wave_rider_source_read(struct xz_wave_rider_snapshot *w){source_reads++;*w=source_wave;w->deck=source_selected;w->observed_ms=xz_game_milliseconds();return 1;}
#define proof xz_rhythm_lab_proof_v1
static uint16_t screen[800*480+2];
static unsigned char touch_fixture[64],decks[2][64];
static void draw(void){screen[0]=0x1234;screen[800*480+1]=0xabcd;assert(xz_mods_render_v1(screen+1,800,480,800,NULL));assert(screen[0]==0x1234&&screen[800*480+1]==0xabcd);}
static int key(int deck,uint16_t code,unsigned op,int32_t pulses){uint8_t e[24]={0};memcpy(e+8,&code,2);e[11]=(uint8_t)op;memcpy(e+12,&pulses,4);return xz_ui_runtime_arcade_key(decks[deck],e);}
static void tap(unsigned x,unsigned y){contact(touch_fixture,x,y,1);contact(touch_fixture,x,y,0);}
static void open_lab(void){
    model.fb_takeover=1;model.connection.connected=0;touch.visible=1;ui.page=XZ_UI_GAMES;
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w);const struct xz_ui_widget *entry=NULL;
    for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_RHYTHM_LAB_OPEN)entry=&w[i];assert(entry);
    contact(touch_fixture,(unsigned)(entry->x+entry->w/2),(unsigned)(entry->y+entry->h/2),1);
    assert(xz_rhythm_lab_present()&&!model.fb_takeover);
    contact(touch_fixture,750,24,0);assert(xz_rhythm_lab_present());draw();assert(proof.phase==LAB_MENU);
}
static void valid_source(void){
    memset(&source_wave,0,sizeof(source_wave));source_wave.valid=source_wave.playing=1;
    source_wave.count=XZ_WAVE_RIDER_SAMPLES;source_wave.total=40000;source_wave.position=500;
    source_wave.generation=1;source_wave.track_hash=111;source_wave.kind=1;source_wave.normalization=400;
    source_wave.pulse_period=75;source_wave.pulse_confidence=1;strcpy(source_wave.title,"NATIVE INPUT STUDY");
    for(unsigned i=0;i<XZ_WAVE_RIDER_SAMPLES;i++){source_wave.bands[i*3]=(uint8_t)(i%75<10?240-i%75*21:4);source_wave.bands[i*3+1]=(uint8_t)(50+i%90);source_wave.bands[i*3+2]=(uint8_t)(25+i%130);}
}
int main(void){
    stock_runtime_test();decks[0][0x26]=1;decks[1][0x26]=2;settings_readonly=1;native_active=0;
    /* Games open from MODS in MODERN too: a touch that leaves the game must not leave it owning the contact. */
    model.ui_interface=XZ_UI_INTERFACE_MODERN;
    open_lab();tap(150,110);tap(400,365);draw();assert(proof.phase==LAB_SETUP&&!proof.valid);
    tap(400,365);assert(!xz_rhythm_lab_present()&&model.fb_takeover);
    valid_source();
    for(unsigned mode=0;mode<10;mode++){
        open_lab();tap(mode<5?180:560,96+(mode%5)*60+25);draw();assert(proof.mode==mode&&proof.phase==LAB_BRIEF);
        if(mode==0){assert(key(0,0x4102,0,0));draw();assert(proof.held[0]);assert(key(0,0x4102,2,0));}
        else tap(400,365);
        draw();assert(proof.phase==LAB_RUN&&proof.valid);
        assert(!key(0,0x4101,0,0)&&!key(0,0x4101,2,0)&&!key(0,0x4103,0,0));
        float before=proof.angle[0];assert(key(0,0x4305,4,80));draw();assert(fabsf(proof.angle[0]-before)<.07f);
        assert(key(0,0x4306,0,0));assert(key(0,0x4102,0,0));draw();assert(proof.held[0]);
        assert(key(0,0x4102,2,0));draw();assert(!proof.held[0]);assert(key(0,0x4306,2,0));
        assert(!key(1,0x4305,4,20)&&!key(1,0x4102,0,0));
        source_wave.playing=0;draw();assert(proof.phase==LAB_PAUSE);source_wave.playing=1;draw();assert(proof.phase==LAB_RUN);
        assert(key(0,0x4102,0,0));tap(750,24);assert(!xz_rhythm_lab_present()&&model.fb_takeover);
        assert(!xz_rhythm_lab_touch(400,300,1,xz_game_milliseconds())); /* the release reached the lab */
        assert(key(0,0x4102,2,0)&&!key(0,0x4102,2,0));
    }
    open_lab();tap(500,450);tap(180,120);tap(400,365);draw();
    assert(key(0,0x4102,0,0)&&key(1,0x4102,0,0));draw();assert(proof.held[0]&&proof.held[1]);
    assert(key(0,0x4102,2,0));draw();assert(!proof.held[0]&&proof.held[1]);
    assert(key(1,0x4102,2,0));draw();assert(!proof.held[1]);
    assert(key(0,0x411f,0,0));draw();assert(proof.phase==LAB_RUN&&proof.elapsed==0);assert(key(0,0x411f,2,0));
    assert(key(0,0x411e,0,0));draw();assert(proof.phase==LAB_RESULTS);assert(key(0,0x411e,2,0));
    tap(750,24);assert(!xz_rhythm_lab_present());
    open_lab();tap(240,450);draw();assert(proof.phase==LAB_CONTROLS);
    tap(180,260);draw();assert(proof.action_binding[0]==2&&proof.action_binding[1]==0);
    tap(400,455);draw();assert(proof.phase==LAB_MENU);
    tap(180,120);tap(400,365);draw();assert(proof.phase==LAB_RUN);
    assert(key(0,0x4102,0,0));draw();assert(!proof.held[0]);
    assert(key(0,0x4119,0,0));draw();assert(!proof.held[0]);assert(key(0,0x4119,2,0));
    assert(key(0,0x411b,0,0)&&key(1,0x4102,0,0));draw();assert(proof.held[0]&&proof.held[1]);
    assert(key(0,0x4102,2,0));draw();assert(proof.held[0]);
    assert(key(0,0x411b,2,0)&&key(1,0x4102,2,0));draw();assert(!proof.held[0]&&!proof.held[1]);
    unsigned rgb;int lit;assert(xz_rhythm_lab_pad(0,2,&rgb,&lit)&&lit);assert(xz_rhythm_lab_pad(0,0,&rgb,&lit)&&!lit);
    for(unsigned deck=0;deck<2;deck++)for(unsigned choice=1;choice<5;choice++){
        tap(240,455);draw();assert(proof.phase==LAB_CONTROLS);
        tap(deck?580:180,120+choice*58+20);tap(400,455);draw();assert(proof.action_binding[deck]==choice&&proof.phase==LAB_RUN);
        uint16_t code=(uint16_t)(choice==1?0x4119:0x4119+choice);
        assert(key(deck,0x4102,0,0));draw();assert(!proof.held[deck]);assert(key(deck,0x4102,2,0));
        assert(key(deck,code,0,0));draw();assert(proof.held[deck]);assert(key(deck,code,2,0));draw();assert(!proof.held[deck]);
        assert(!key(deck,0x4101,0,0));
    }
    assert(key(0,0x411d,0,0));draw();assert(proof.held[0]);
    tap(240,455);tap(180,140);tap(400,455);draw();assert(proof.action_binding[0]==0&&!proof.held[0]);
    assert(key(0,0x4102,0,0));draw();assert(proof.held[0]);
    assert(key(0,0x411d,2,0));draw();assert(proof.held[0]);assert(key(0,0x4102,2,0));
    tap(750,24);open_lab();draw();assert(proof.action_binding[0]==0&&proof.action_binding[1]==4);
    puts("PASS per-deck pad-only remapping ignores CUE, preserves other deck and PLAY, lights selected pad, retains release ownership across changes and survives reopening");
    tap(750,24);touch.visible=1;ui.page=XZ_UI_GAMES;
    struct xz_ui_widget glyph_widgets[XZ_UI_WIDGETS];size_t glyph_count=xz_ui_layout(&ui,&model,glyph_widgets);const struct xz_ui_widget *glyph_entry=NULL;
    for(size_t i=0;i<glyph_count;i++)if(glyph_widgets[i].kind==XZ_UI_GLYPH_RUN_OPEN)glyph_entry=&glyph_widgets[i];assert(glyph_entry);
    contact(touch_fixture,(unsigned)(glyph_entry->x+glyph_entry->w/2),(unsigned)(glyph_entry->y+glyph_entry->h/2),1);contact(touch_fixture,750,24,0);
    draw();assert(proof.mode==LAB_GLYPH&&proof.phase==LAB_BRIEF);
    tap(700,455);draw();assert(!key(1,0x411d,0,0));tap(700,455);
    tap(240,455);draw();assert(proof.phase==LAB_CONTROLS);tap(400,455);draw();assert(proof.phase==LAB_BRIEF);
    tap(400,365);draw();assert(proof.phase==LAB_RUN);
    assert(key(0,0x4119,0,0));draw();assert(proof.glyph_height[0]>0);assert(key(0,0x4119,2,0));
    assert(key(1,0x411d,0,0));draw();assert(proof.glyph_height[1]>0);assert(key(1,0x411d,2,0));
    assert(!key(0,0x4101,0,0));assert(key(0,0x4305,4,300));draw();assert(proof.angle[0]>.1f&&proof.angle[0]<.4f);
    source_wave.position=source_wave.total-1;draw();assert(proof.phase==LAB_PAUSE);
    tap(400,225);assert(!xz_rhythm_lab_present());
    puts("PASS Static Surfer native menu, independent remapped jumps during practice, bounded jog, duo toggle, Buttons and song-end return to deck");
    unsigned char back[24]={0};uint16_t back_key=0x4208;memcpy(back+8,&back_key,2);
    model.rekordbox_assign[0]=XZ_REKORDBOX_MODS;open_lab();
    assert(encoder_rotate_hook(1)==1&&encoder_rotate_hook(1)==1);draw();
    assert(game_nav.visible&&game_nav.focus>=0&&game_nav.shown.at[game_nav.focus].y==156);
    assert(encoder_push_hook(NULL,NULL,NULL,NULL)==1);draw();assert(proof.phase==LAB_BRIEF&&proof.mode==1);
    menu_back_key_hook(NULL,back);back[11]=2;menu_back_key_hook(NULL,back);back[11]=0;draw();assert(proof.phase==LAB_MENU);
    menu_back_key_hook(NULL,back);back[11]=2;menu_back_key_hook(NULL,back);back[11]=0;
    assert(!xz_rhythm_lab_present()&&!touch.visible&&model.fb_takeover);
    open_lab();assert(uikey_rekordbox_hook(NULL,NULL,NULL,NULL)==1);
    assert(!xz_rhythm_lab_present()&&touch.visible&&ui.page==XZ_UI_GAMES);
    assert(uikey_rekordbox_hook(NULL,NULL,NULL,NULL)==1&&!touch.visible);
    puts("PASS encoder picks a study, BACK returns to the menu then the deck, REKORDBOX leaves the lab for MODS on GAMES");
    printf("ROUTING_JSON {\"frames\":%u,\"inputs\":%u,\"source_reads\":%u,\"ten_cases\":true,\"duo_independent\":true,\"play_passthrough\":true,\"release_ownership\":true}\n",proof.frames,proof.inputs,source_reads);
    return 0;
}
