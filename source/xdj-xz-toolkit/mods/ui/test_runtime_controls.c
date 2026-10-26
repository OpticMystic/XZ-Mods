#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../ui_runtime.c"
#include "test_pc_runtime_stubs.h"
#ifndef XZ_RHYTHM_LAB_RUNTIME_TEST
#include "test_rhythm_lab_stubs.h"
#endif
#ifndef XZ_WAVE_RIDER_RUNTIME_TEST
#include "test_wave_rider_stubs.h"
#endif
#ifdef NDEBUG
#error Runtime UI acceptance requires active assertions
#endif
static int native_play=1,native_active=1,stock_calls,fixture_focus=-1,fixture_prepared=1;
static struct xz_touch_status last_stock;
static struct xz_stem_levels applied[2];
static float applied_bass[2];
static uint32_t fixture_generation=1;
static int fixture_loaded;
static unsigned fixture_stem_count[2]={3,3};
static unsigned waveform_calls[2],fixture_audio_status_calls[2],fixture_key_snapshot_calls[2];
int xz_native_skin_start(int theme){(void)theme;return 0;}
void xz_native_skin_request(int theme){(void)theme;}
static unsigned fixture_repaints;
void xz_native_skin_request_repaint(void){fixture_repaints++;fixture_pc_redraws++;}
const char *xz_native_skin_status(void){return "Native theme test fixture";}
void xz_native_skin_stop(void){}
int xz_native_focus_deck(void){return fixture_focus;}
int xz_native_play_visible(void){return native_play;}
int xz_native_inline_active(void){return native_active;}
static int native_four=-1;
int xz_native_four_order(void){return native_four;}
static int fixture_mixer_order;
int xz_native_four_mixer_order(void){return fixture_mixer_order;}
void xz_native_four_set_mixer_order(int mixer){fixture_mixer_order=!!mixer;}
int xz_native_inline_start(int(*on)(void),xz_native_wave_render draw,void *context){(void)on;(void)draw;(void)context;return 0;}
int xz_hook_arm(uint32_t a,const unsigned char g[8],void *r,void **o){(void)a;(void)g;(void)r;(void)o;return 0;}
int xz_native_led_start(void){return 0;}
void xz_native_led_stop(void){}
void xz_log(const char *s){(void)s;}
unsigned xz_runtime_cue_flags(void){return 0;}
static uint64_t fixture_retained_stems;
static int jump_calls,jump_deck,jump_beats,jump_accept=1;
int xz_runtime_beat_jump(int deck,int beats){jump_calls++;jump_deck=deck;jump_beats=beats;return jump_accept;}
int xz_runtime_set_cues(int a,int b){(void)a;(void)b;return 0;}
int xz_key_get_desired_semitones(int d,int *out){(void)d;*out=0;return 0;}
int xz_key_set_desired_semitones(int d,int n){(void)d;(void)n;return 0;}
static struct xz_key_snapshot fixture_keys[2];
static int fixture_key_valid[2],fixture_key_cents[2],fixture_key_requests;
int xz_key_native_snapshot(unsigned deck,struct xz_key_snapshot *out){
    if(deck<2)fixture_key_snapshot_calls[deck]++;
    if(deck>1||!fixture_key_valid[deck])return 0;*out=fixture_keys[deck];return 1;
}
int xz_key_get_desired_cents(int deck,uint32_t epoch,int *out){
    assert(deck>=0&&deck<2);*out=epoch==fixture_keys[deck].epoch?fixture_key_cents[deck]:0;return 0;
}
/* Requests reach the display only after the audio path applies them: tests call
   fixture_key_apply() to model the next processed block for that deck. */
static int fixture_key_applied[2];
static __attribute__((unused)) void fixture_key_apply(int deck){fixture_key_applied[deck]=fixture_key_cents[deck];}
int xz_key_get_applied_cents(int deck,uint32_t epoch,int *out){
    assert(deck>=0&&deck<2);*out=epoch==fixture_keys[deck].epoch?fixture_key_applied[deck]:0;return 0;
}
int xz_key_set_desired_cents(int deck,uint32_t epoch,int cents){
    assert(deck>=0&&deck<2&&epoch==fixture_keys[deck].epoch);
    if(cents < -1200||cents>1200)return -1;
    fixture_key_cents[deck]=cents;fixture_key_requests++;return 0;
}
int xz_key_clear_desired_cents(int deck,uint32_t epoch,int expected){
    if(deck<0||deck>1||epoch!=fixture_keys[deck].epoch||fixture_key_cents[deck]!=expected)return -1;
    fixture_key_cents[deck]=0;fixture_key_requests++;return 0;
}
void xz_audio_set_enabled(int on){(void)on;}
void xz_audio_set_levels(int deck,struct xz_stem_levels value){applied[deck]=value;}
void xz_audio_set_levels4(int deck,const float value[4]){
    applied[deck]=(struct xz_stem_levels){value[0],value[1],value[2]};applied_bass[deck]=value[3];
}
int xz_audio_set_levels4_for_generation(int deck,uint32_t generation,const float value[4]){
    if(generation!=fixture_generation)return -1;xz_audio_set_levels4(deck,value);return 0;
}
int xz_audio_get_status(int deck,struct xz_audio_status *out){assert(deck>=0&&deck<2);fixture_audio_status_calls[deck]++;memset(out,0,sizeof(*out));if(fixture_loaded&(1<<deck))snprintf(out->path,sizeof(out->path),"/media/usb2/test-track.mp3");out->retained_stems_bytes=fixture_retained_stems;out->generation=fixture_generation;out->prepared=fixture_prepared;out->stem_count=fixture_stem_count[deck];out->state=fixture_prepared?XZ_AUDIO_EXPERIMENTAL_READY:XZ_AUDIO_CACHE_MISSING;return 0;}
const char *xz_audio_state_name(enum xz_audio_state state){(void)state;return "ready";}
int xz_audio_waveform(int deck,unsigned role,unsigned char *bins,size_t count,float *progress){
    (void)role;if(deck<0||deck>1)return 0;
    waveform_calls[deck]++;memset(bins,deck?24:8,count);if(progress)*progress=0.5;return 1;
}
static struct xz_ram_status fixture_ram[2];
static int fixture_ram_rc,fixture_ram_calls[3];
void xz_ram_status(int deck,struct xz_ram_status *out){assert(deck>=0&&deck<2);*out=fixture_ram[deck];}
int xz_ram_request(int deck){fixture_ram_calls[0]++;if(fixture_ram_rc)return fixture_ram_rc;fixture_ram[deck].state=XZ_RAM_LOADING;return 0;}
int xz_ram_cancel(int deck){fixture_ram_calls[1]++;fixture_ram[deck].state=XZ_RAM_EMPTY;return 0;}
int xz_ram_release(int deck){fixture_ram_calls[2]++;fixture_ram[deck].state=XZ_RAM_READY;return 0;}
static void ram_tap(enum xz_ui_action_kind kind,int index){
    struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_action a[XZ_UI_ACTIONS];size_t n=xz_ui_layout(&ui,&model,w);
    xz_ui_cancel(&ui,a);
    for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index){
        assert(xz_ui_touch(&ui,&model,w[i].x+w[i].w/2,w[i].y+w[i].h/2,1,a)==1&&a[0].kind==kind);
        apply(NULL,a,1);xz_ui_touch(&ui,&model,0,0,0,a);return;
    }
    assert(!"missing RAM widget");
}
/* RAM TRACKS: the panel only starts, stops or frees the backend copy, and never saves settings. */
static void ram_tracks(void){
    const char *saved=model.settings_status;int pending=settings_pending;
    memset(fixture_ram,0,sizeof(fixture_ram));refresh();
    assert(model.deck[0].ram.state==XZ_UI_RAM_UNAVAILABLE&&model.deck[1].ram.state==XZ_UI_RAM_UNAVAILABLE);
    ui.page=XZ_UI_SETTINGS;ram_tap(XZ_UI_PANEL,XZ_UI_RAM_SETUP);assert(ui.page==XZ_UI_RAM_SETUP);
    ram_tap(XZ_UI_RAM_DECK,1);assert(ui.deck==1&&inline_ui.deck==1);
    fixture_ram_rc=-1;ram_tap(XZ_UI_RAM_REQUEST,0);
    assert(fixture_ram_calls[0]==1&&strstr(ui.notice,"RAM ENGINE NOT IN THIS LOADER"));
    fixture_ram_rc=0;fixture_ram[1]=(struct xz_ram_status){XZ_RAM_EMPTY,0,0,"SOURCE ON USB1"};
    refresh();assert(model.deck[1].ram.state==XZ_UI_RAM_EMPTY&&!strcmp(model.deck[1].ram.message,"SOURCE ON USB1"));
    ram_tap(XZ_UI_RAM_REQUEST,0);
    assert(fixture_ram_calls[0]==2&&model.deck[1].ram.state==XZ_UI_RAM_LOADING&&!strcmp(ui.notice,"DECK 2: COPYING ORIGINAL AUDIO TO RAM"));
    fixture_ram[1].copied=3u<<20;fixture_ram[1].bytes=9u<<20;refresh();
    assert(model.deck[1].ram.copied==3u<<20&&model.deck[1].ram.bytes==9u<<20);
    ram_tap(XZ_UI_RAM_CANCEL,0);assert(fixture_ram_calls[1]==1&&model.deck[1].ram.state==XZ_UI_RAM_EMPTY);
    /* A stale release after the USB has gone never reaches the backend. */
    fixture_ram[1].state=XZ_RAM_DETACHED;
    struct xz_ui_action release={.kind=XZ_UI_RAM_RELEASE,.deck=1};apply(NULL,&release,1);
    assert(!fixture_ram_calls[2]&&strstr(ui.notice,"RELEASE NEEDS THE USB SOURCE"));
    fixture_retained_stems=4096;fixture_ram[1].state=XZ_RAM_READY;refresh();
    assert(model.deck[1].ram.retained_stems_bytes==4096&&strstr(model.deck[1].ram.message,"PREPARED STEMS IN RAM"));
    fixture_retained_stems=0;refresh();assert(!model.deck[1].ram.retained_stems_bytes);
    ram_tap(XZ_UI_RAM_RELEASE,0);
    assert(fixture_ram_calls[2]==1&&model.deck[1].ram.state==XZ_UI_RAM_READY&&!strcmp(ui.notice,"DECK 2: RAM RELEASE REQUESTED"));
    fixture_ram[1].state=XZ_RAM_EMPTY;refresh();assert(model.deck[1].ram.state==XZ_UI_RAM_EMPTY);
    release.deck=2;apply(NULL,&release,1);assert(fixture_ram_calls[2]==1);
    memset(fixture_ram[0].message,'W',sizeof(fixture_ram[0].message));fixture_ram[0].state=99;refresh();
    assert(strlen(model.deck[0].ram.message)==sizeof(model.deck[0].ram.message)-1&&model.deck[0].ram.state==XZ_UI_RAM_UNAVAILABLE);
    assert(model.settings_status==saved&&settings_pending==pending);
    memset(fixture_ram,0,sizeof(fixture_ram));refresh();ui.page=XZ_UI_CONTROLS;ui.notice[0]=0;
}
static void stock(void *self,const struct xz_touch_status *s,const void *mode){(void)mode;stock_calls++;last_stock=*s;memcpy((char*)self+4,s,sizeof(*s));}
static void contact(void *self,unsigned x,unsigned y,int down){struct xz_touch_status s={(uint8_t)down,{0},x,y};touch_hook(self,&s,NULL);}
#include "test_pc_runtime_controls.h"
#include "test_pc_runtime_refresh.h"
#include "test_stem_layout_runtime.h"
#include "test_usb_stem_reason_runtime.h"
#include "test_rekordbox_runtime.h"
static void generation_pad_consistency(void){
    started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;fixture_prepared=1;
    model.jump.enabled=0;model.stem_page=0;model.shift_pages=0;model.stem_order=0;
    memset(&pads,0,sizeof(pads));pads.bank=0;
    for(int deck=0;deck<2;deck++){fixture_pc_source[deck]=(struct xz_pc_stem_source){.slot=-1,.epoch=100};fixture_pc_valid[deck]=1;fixture_stem_count[deck]=3;}
    refresh();
    for(int deck=0;deck<2;deck++){
        fixture_generation++;applied[deck]=(struct xz_stem_levels){1,1,1};
        struct xz_cue_event event={.deck=deck,.pad=0,.pad_page=0,.mode_button=-1,.hotcue_mode=1};unsigned flags;
        assert(xz_ui_runtime_pad(&event,&flags));event.operation=2;assert(xz_ui_runtime_pad(&event,&flags));
        refresh();
        int agrees=(model.deck[deck].muted&4)&&applied[deck].vocals==0;
        if(!agrees){fprintf(stderr,"FAIL deck %d first pad after load: UI mute=%u audio vocals=%f\n",deck+1,model.deck[deck].muted,applied[deck].vocals);assert(agrees);}
        fixture_stem_count[deck]=4;refresh();
        int survived=(model.deck[deck].muted&4)&&applied[deck].vocals==0;
        if(!survived){fprintf(stderr,"FAIL deck %d prepared stem-count publication erased first pad\n",deck+1);assert(survived);}
        fixture_stem_count[deck]=3;refresh();assert((model.deck[deck].muted&4)&&applied_bass[deck]==1);
    }
    puts("PASS first stem pad after track generation change stays visible and audible on both decks");
}

int main(void){
    unsigned char self[32]={0};started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;model.stems_overlay=1;
    xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
    struct xz_ui_action order_action={.kind=XZ_UI_FOUR_WAVE_ORDER,.value=1};
    apply(NULL,&order_action,1);assert(fixture_mixer_order==1&&model.four_wave_mixer==1);
    order_action.value=2;apply(NULL,&order_action,1);assert(fixture_mixer_order==1);
    order_action.value=0;apply(NULL,&order_action,1);assert(fixture_mixer_order==0&&model.four_wave_mixer==0);
    struct xz_ui_action color_action={.kind=XZ_UI_STEM_COLOR,.index=0,.value=4};apply(NULL,&color_action,1);
    assert(model.stem_colors[0]==4&&xz_ui_model_stem_color(&model,0)==0xff9f0a);
#ifdef XZ_STEM_COLOR_CAPTURE_TEST
    assert(captured_stem_colors[0]==0xff9f0a&&captured_stem_colors[1]==0xff3b30&&captured_stem_colors[2]==0x30d158);
#endif
    struct xz_settings color_snapshot=settings_snapshot();assert(color_snapshot.stem_colors[0]==4);
    color_action.kind=XZ_UI_STEM_COLORS_RESET;apply(NULL,&color_action,1);
    assert(!model.stem_colors[0]&&xz_ui_model_stem_color(&model,0)==0x2997ff);
#ifdef XZ_STEM_COLOR_CAPTURE_TEST
    assert(captured_stem_colors[0]==0x2997ff&&captured_stem_colors[1]==0xff3b30&&captured_stem_colors[2]==0x30d158);
#endif
    contact(self,200,130,1);contact(self,200,130,0);assert(model.deck[0].muted==4&&applied[0].vocals==0&&stock_calls==0);
    contact(self,200,260,1);contact(self,200,260,0);assert(model.deck[1].muted==4&&applied[1].vocals==0);
    contact(self,200,130,1);contact(self,200,130,0);assert(!model.deck[0].muted&&applied[0].vocals==1);
    contact(self,200,260,1);contact(self,200,260,0);assert(!model.deck[1].muted&&applied[1].vocals==1);
    contact(self,600,130,1);contact(self,600,130,0);assert(model.deck[0].bypass&&!model.deck[1].bypass);
    contact(self,600,260,1);contact(self,600,260,0);assert(model.deck[0].bypass&&model.deck[1].bypass);
    contact(self,600,130,1);contact(self,600,130,0);
    contact(self,600,260,1);contact(self,600,260,0);assert(!model.deck[0].bypass&&!model.deck[1].bypass);
    test_pc_runtime_controls(self);
    test_pc_runtime_refresh(self);
    struct xz_ui_action slider={.kind=XZ_UI_LEVEL,.deck=0,.index=2,.value=.5f};apply(NULL,&slider,1);
    contact(self,200,130,1);contact(self,160,260,1);
    assert(inline_contact_deck==0&&model.deck[0].levels[2]<.5f&&model.deck[1].levels[2]==1&&stock_calls==0);
    contact(self,160,260,0);assert(!inline_contact);
    slider.value=1;apply(NULL,&slider,1);
    contact(self,200,130,1);contact(self,200,130,0);assert(model.deck[0].muted==4);
    contact(self,200,130,1);contact(self,230,130,1);contact(self,230,130,0);
    assert(!model.deck[0].muted&&!pads.muted[0]);
    assert(applied[0].vocals>.2f&&applied[0].vocals<.3f&&applied[1].vocals==1);
    apply(NULL,&slider,1);
    settings_readonly=settings_running=1;settings_pending=0;
    struct xz_ui_action theme={.kind=XZ_UI_SET_THEME,.index=11};apply(NULL,&theme,1);
    assert(model.theme==11&&!settings_pending&&!strcmp(model.settings_status,"CHANGES LAST UNTIL REBOOT"));
    model.theme=0;settings_readonly=settings_running=0;
    contact(self,200,130,1);
    struct xz_ui_action hide={.kind=XZ_UI_STEMS_OVERLAY};apply(NULL,&hide,1);
    assert(model.stems_overlay==XZ_STEM_LAYOUT_BELOW_HALF&&inline_cancelled&&!inline_ui.down);
    contact(self,200,130,0);assert(!inline_contact);
    for(int step=0;step<4;step++)apply(NULL,&hide,1);assert(model.stems_overlay==XZ_STEM_LAYOUT_BELOW);
    int calls=stock_calls;native_active=0;contact(self,799,400,1);contact(self,799,400,0);assert(stock_calls==calls+2&&!inline_contact);
    native_active=1;contact(self,30,400,1);contact(self,200,130,1);contact(self,200,130,0);assert(stock_calls==calls+5);
    struct xz_wave_layout default_geometry;assert(xz_wave_layout_get(XZ_STEM_LAYOUT_BELOW,&default_geometry));
    contact(self,200,80,1);assert(last_stock.x==200&&last_stock.y==18+xz_wave_layout_source_row(&default_geometry,62));
    contact(self,200,180,1);assert(last_stock.y==18+xz_wave_layout_source_row(&default_geometry,83));contact(self,200,180,0);
    contact(self,200,180,1);assert(last_stock.y==18+xz_wave_layout_source_row(&default_geometry,162));
    contact(self,200,130,1);assert(last_stock.y==18+xz_wave_layout_source_row(&default_geometry,132));contact(self,200,130,0);
    {
        /* Four decks: native lanes map back to their own rows; DECK 3/4 lanes and gaps never reach stock. */
        struct xz_four_layout fg;assert(xz_four_layout_get(XZ_FOUR_INTERLEAVED,&fg));
        native_active=0;native_four=XZ_FOUR_INTERLEAVED;int before=stock_calls;
        contact(self,300,18+fg.lane_y[0]+30,1);
        assert(stock_calls==before+1&&last_stock.x==300&&last_stock.y==18+xz_four_layout_source_row(&fg,fg.lane_y[0]+30));
        contact(self,320,18+fg.lane_y[2]+5,1);
        assert(last_stock.x==320&&last_stock.y==18+xz_four_layout_source_row(&fg,fg.lane_y[0]+XZ_FOUR_LANE_HEIGHT-1));
        contact(self,320,18+fg.lane_y[2]+5,0);
        contact(self,300,18+fg.lane_y[1]+40,1);assert(last_stock.y==18+xz_four_layout_source_row(&fg,fg.lane_y[1]+40)&&last_stock.y>=18+136);
        contact(self,300,18+fg.lane_y[1]+40,0);
        before=stock_calls;
        contact(self,300,18+fg.lane_y[2]+20,1);contact(self,300,18+fg.lane_y[0]+20,1);contact(self,300,18+fg.lane_y[0]+20,0);
        contact(self,300,18+XZ_FOUR_LANE_HEIGHT+1,1);contact(self,300,18+XZ_FOUR_LANE_HEIGHT+1,0);
        contact(self,300,18+fg.lane_y[3]+10,1);native_four=-1;contact(self,300,18+fg.lane_y[3]+10,0);
        assert(stock_calls==before);
        native_four=XZ_FOUR_INTERLEAVED;contact(self,700,200,1);assert(stock_calls==before+1&&last_stock.y==200);contact(self,700,200,0);
        native_four=-1;native_active=1;
    }
    contact(self,200,260,1);fixture_generation++;contact(self,650,260,1);
    assert(model.deck[1].levels[2]==1&&inline_cancelled&&!inline_ui.down);
    contact(self,650,260,0);assert(!inline_contact);
    size_t count=536*XZ_WAVE_INLINE_HEIGHT;uint16_t *pixels=calloc(count+2,sizeof(*pixels));assert(pixels);pixels[0]=0x1234;pixels[count+1]=0xabcd;
    unsigned before_wave[2]={waveform_calls[0],waveform_calls[1]};
    assert(render_inline(NULL,pixels+1,count,536,536,XZ_WAVE_INLINE_HEIGHT));assert(pixels[0]==0x1234&&pixels[count+1]==0xabcd);
    assert(waveform_calls[0]-before_wave[0]==4&&waveform_calls[1]-before_wave[1]==4);
    uint16_t *saved=malloc(count*sizeof(*saved));assert(saved);memcpy(saved,pixels+1,count*sizeof(*saved));
    pthread_mutex_lock(&ui_mutex);memset(pixels+1,0,count*sizeof(*pixels));
    int cached=render_inline(NULL,pixels+1,count,536,536,XZ_WAVE_INLINE_HEIGHT);pthread_mutex_unlock(&ui_mutex);
    assert(cached&&!memcmp(saved,pixels+1,count*sizeof(*saved)));free(saved);
    model.stem_page=1;model.pad_feedback=1;
    struct xz_cue_event e={0};e.deck=0;e.pad=0;e.mode_button=-1;e.pad_page=0;e.hotcue_mode=1;
    unsigned flags,rgb;int lit;
    assert(!xz_ui_runtime_pad(&e,&flags));
    e.operation=2;assert(!xz_ui_runtime_pad(&e,&flags));
    assert(!xz_ui_runtime_pad_color(0,0,&rgb,&lit));
    e.operation=0;e.pad_page=1;xz_ui_runtime_pad_native_page(0,1);xz_ui_runtime_pad_native_page(1,1);
    assert(xz_ui_runtime_pad(&e,&flags));assert(applied[0].vocals==0);
    assert(xz_ui_runtime_pad_color(0,0,&rgb,&lit)&&rgb==xz_stem_hardware_color(2)&&!lit);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    struct xz_ui_action action={0};action.kind=XZ_UI_MUTE;action.deck=0;action.index=2;action.phase=XZ_UI_PRESS;
    apply(NULL,&action,1);assert(applied[0].vocals==1);
    apply(NULL,&action,1);assert(applied[0].vocals==0);
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags)&&applied[0].vocals==1);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    native_active=0;e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    e.deck=1;e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));
    assert(applied[0].vocals==0&&applied[1].vocals==0);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));assert(applied[0].vocals==0&&applied[1].vocals==1);
    e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));native_active=1;
    int before=stock_calls;contact(self,700,10,1);contact(self,700,10,1);contact(self,780,400,0);
    assert(model.stems_overlay==XZ_STEM_LAYOUT_BELOW_HALF&&inline_enabled()==XZ_STEM_LAYOUT_BELOW_HALF&&stock_calls==before);
    assert(!render_inline(NULL,pixels+1,count,536,536,XZ_WAVE_INLINE_HEIGHT));
    assert(render_inline(NULL,pixels+1,536*48,536,536,48));
    const int cycle_modes[4]={XZ_STEM_LAYOUT_OUTER,XZ_STEM_LAYOUT_OUTER_HALF,XZ_STEM_LAYOUT_HIDDEN,XZ_STEM_LAYOUT_BELOW};
    unsigned layout_redraws=fixture_pc_redraws;
    for(int step=0;step<4;step++){
        contact(self,700,10,1);assert(model.stems_overlay==cycle_modes[step]);
        contact(self,700,10,1);assert(model.stems_overlay==cycle_modes[step]);
        contact(self,700,10,0);assert(inline_enabled()==cycle_modes[step]);
    }
    assert(fixture_pc_redraws==layout_redraws+4&&stock_calls==before);
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    fixture_focus=0;refresh();assert(inline_ui.deck==0);
    fixture_prepared=0;fixture_focus=1;refresh();assert(inline_ui.deck==0);
    fixture_prepared=1;refresh();assert(inline_ui.deck==1&&ui.deck==1);
    inline_ui.deck=0;refresh();assert(inline_ui.deck==0);
    fixture_generation++;refresh();assert(inline_ui.deck==1);
    fixture_focus=3;refresh();assert(inline_ui.deck==1);
    model.enabled=0;e.operation=0;assert(!xz_ui_runtime_pad(&e,&flags));
    e.operation=2;assert(!xz_ui_runtime_pad(&e,&flags));model.enabled=XZ_UI_STEM;
    touch.visible=1;assert(!render_inline(NULL,pixels+1,count,536,536,XZ_WAVE_INLINE_HEIGHT));free(pixels);
    touch.visible=0;model.connection.enabled=model.connection.connected=1;model.fb_takeover=1;
    int native_before=stock_calls;
    contact(self,350,350,1);contact(self,350,350,0);assert(stock_calls==native_before);
    contact(self,40,10,1);contact(self,40,10,0);assert(!model.fb_takeover);
    contact(self,350,350,1);contact(self,350,350,0);assert(stock_calls>native_before);
    contact(self,40,10,1);contact(self,40,10,0);assert(model.fb_takeover);
    model.pad_feedback=1;model.enabled=XZ_UI_STEM;
    for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int bank=0;bank<2;bank++)for(int page=0;page<4;page++)for(int deck=0;deck<2;deck++){
        model.theme=theme;model.stem_bank=bank;model.stem_page=page;model.deck[deck].muted=0;model.deck[deck].bypass=0;
        for(int stem=0;stem<3;stem++)model.deck[deck].levels[stem]=1;
        xz_ui_runtime_pad_native_page(deck,page);
        for(int pad=0;pad<8;pad++){
            int slot=pad-bank*4;int active=xz_ui_runtime_pad_color(deck,pad,&rgb,&lit);
            assert(active==(slot>=0&&slot<4));
            if(active){assert(rgb==xz_stem_hardware_color(slot<3?2-slot:3));assert(lit==(slot<3));}
        }
        model.deck[deck].levels[2]=0;assert(xz_ui_runtime_pad_color(deck,bank*4,&rgb,&lit)&&!lit);
        model.deck[deck].bypass=1;assert(xz_ui_runtime_pad_color(deck,bank*4+3,&rgb,&lit)&&lit);
        assert(xz_ui_runtime_pad_color(deck,bank*4+1,&rgb,&lit)&&!lit);
        xz_ui_runtime_pad_native_page(deck,(page+1)%4);assert(!xz_ui_runtime_pad_color(deck,bank*4,&rgb,&lit));
    }
    /* Drive the actual picker and runtime apply path for every assignment. */
    ui.page=XZ_UI_BEAT_JUMP;model.jump=xz_jump_defaults();settings_readonly=1;
    for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int page=0;page<2;page++)for(int pair=0;pair<4;pair++){
        model.theme=theme;ui.jump_edit_page=page;ui.jump_edit_pair=pair;
        static const int ordered[]={0,1,2,3,4,5,6,8,9};
        for(int step=0;step<9;step++){
            int size=ordered[step];
            struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,widgets);
            model.jump.sizes[page*4+pair]=ordered[step?step-1:0];
            struct xz_jump_settings prior=model.jump;int found=0;
            for(size_t i=0;i<n;i++)if(widgets[i].kind==XZ_UI_JUMP_STEP&&widgets[i].index==(size?1:-1)){
                struct xz_ui_action actions[XZ_UI_ACTIONS];
                assert(xz_ui_touch(&ui,&model,widgets[i].x+10,widgets[i].y+10,1,actions)==1);
                apply(NULL,actions,1);xz_ui_touch(&ui,&model,0,0,0,actions);found=1;
            }
            assert(found&&model.jump.sizes[page*4+pair]==size);
            for(int other=0;other<8;other++)if(other!=page*4+pair)assert(model.jump.sizes[other]==prior.sizes[other]);
        }
    }
    /* Encoder: pushing a pair starts a size edit; turning steps it; a closed menu forwards. */
    touch.visible=1;ui.jump_edit_page=1;ui.jump_edit_pair=0;model.jump.sizes[7]=4;
    ui.focus_kind=XZ_UI_JUMP_PAIR;ui.focus_index=7;ui.focus_deck=-1;ui.adjusting=0;
    /* Only the press phase acts: a release, a hold or a later pass of the same click is swallowed and
       leaves focus alone (one click once activated the control twice). */
    {uint32_t release[4]={0,0,2,0},hold[4]={0,0,3,0},press[4]={1,0,2,0};
     assert(encoder_push_hook(release,NULL,NULL,NULL)==1&&!ui.adjusting&&ui.jump_edit_pair==0);
     assert(encoder_push_hook(hold,NULL,NULL,NULL)==1&&!ui.adjusting&&ui.jump_edit_pair==0);
     assert(encoder_push_hook(press,NULL,NULL,NULL)==1&&ui.jump_edit_pair==3&&ui.adjusting);
     assert(encoder_push_hook(release,NULL,NULL,NULL)==1&&ui.jump_edit_pair==3&&ui.adjusting);
     ui.jump_edit_pair=0;ui.adjusting=0;}
    assert(encoder_push_hook(NULL,NULL,NULL,NULL)==1&&ui.jump_edit_pair==3&&ui.adjusting);
    assert(encoder_rotate_hook(1)==1&&model.jump.sizes[7]==5);
    assert(encoder_rotate_hook(-1)==1&&model.jump.sizes[7]==4);
    assert(encoder_rotate_hook(100)==1&&model.jump.sizes[7]==9);
    assert(encoder_rotate_hook(-100)==1&&model.jump.sizes[7]==0);
    /* Not editing: turning moves focus and leaves the size alone. */
    ui.adjusting=0;assert(encoder_rotate_hook(1)==1&&!model.jump.sizes[7]&&!(ui.focus_kind==XZ_UI_JUMP_PAIR&&ui.focus_index==7));
    touch.visible=0;assert(!encoder_rotate_hook(1)&&!model.jump.sizes[7]);
    assert(!encoder_push_hook(NULL,NULL,NULL,NULL));
    /* REKORDBOX assigned to MODS toggles the panel; the stock action leaves it to stock (no hook armed here). */
    model.rekordbox_assign[0]=XZ_REKORDBOX_MODS;assert(uikey_rekordbox_hook(NULL,NULL,NULL,NULL)==1&&touch.visible);
    assert(uikey_rekordbox_hook(NULL,NULL,NULL,NULL)==1&&!touch.visible);
    model.rekordbox_assign[0]=XZ_REKORDBOX_STOCK;assert(uikey_rekordbox_hook(NULL,NULL,NULL,NULL)==0&&!touch.visible);
    model.rekordbox_assign[0]=XZ_REKORDBOX_MODS;
    settings_readonly=0;ui.page=XZ_UI_CONTROLS;model.theme=0;
    /* An explicit unused shortcut enables the same audio state, even when an
       old profile disabled both automatic stems and shift shortcuts. */
    memset(&pads,0,sizeof(pads));model.enabled=0;requested_stems=0;model.shift_pages=0;
    {struct xz_cue_event shortcut={.deck=0,.pad=-1,.mode_button=0,.shift=1};unsigned proof;
     assert(xz_ui_runtime_pad(&shortcut,&proof)&&requested_stems&&(model.enabled&XZ_UI_STEM));
     shortcut.operation=2;shortcut.shift=0;assert(xz_ui_runtime_pad(&shortcut,&proof));}
    /* Custom beat jump owns all four pairs even when stems use this page. */
    memset(&pads,0,sizeof(pads));memset(&jump_state,0,sizeof(jump_state));
    model.jump=xz_jump_defaults();model.jump.enabled=1;model.stem_page=3;model.shift_pages=1;
    struct xz_cue_event je={.deck=0,.pad=-1,.pad_page=0,.mode_button=3,.shift=1};unsigned jf;
    assert(!xz_ui_runtime_pad(&je,&jf)&&jump_state.page[0]==1);
    je.operation=2;assert(!xz_ui_runtime_pad(&je,&jf));
    je.mode_button=0;je.pad_page=3;je.operation=0;
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_state.page[0]==1);
    je.operation=2;assert(xz_ui_runtime_pad(&je,&jf)&&jump_state.page[0]==1);
    je=(struct xz_cue_event){.deck=0,.pad=7,.pad_page=3,.mode_button=-1};
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==1&&jump_deck==0&&jump_beats==128);
    {struct xz_ui_action off={.kind=XZ_UI_JUMP_ENABLE,.value=0};apply(NULL,&off,1);assert(jump_state.page[0]==1&&!model.jump.enabled);}
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==1);
    model.jump.enabled=0;je.operation=2;assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==1);
    jump_state.page[0]=0; /* Page 1 can still use stem pads when custom jumps are off. */
    je.pad=0;je.operation=0;pads.bank=0;assert(xz_ui_runtime_pad(&je,&jf));
    je.operation=2;assert(xz_ui_runtime_pad(&je,&jf));
    model.jump.enabled=1;je.operation=0;
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==2&&jump_beats==-1);
    je.operation=2;assert(xz_ui_runtime_pad(&je,&jf));
    /* Refusal is visible with MODS closed, and shell delivery cannot hold ui_mutex. */
    jump_accept=0;int calls_before=jump_calls;touch.visible=0;
    je.pad=1;je.operation=0;assert(xz_ui_runtime_pad(&je,&jf));
    assert(jump_calls==calls_before+1&&strstr(async_notice.text,"JUMP UNAVAILABLE"));
    assert(!strcmp(async_notice.text,fake_notice));
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==calls_before+1);
    je.operation=2;assert(xz_ui_runtime_pad(&je,&jf));jump_accept=1;
    struct xz_jump_snapshot jump_view;assert(xz_ui_runtime_jump_snapshot(0,&jump_view)&&jump_view.page==0&&jump_view.signed_beats[1]==1);
    assert(!xz_ui_runtime_jump_snapshot(4,&jump_view));
    model.jump.enabled=1;model.jump.shift_page=1;jump_state.page[0]=1;jump_state.page[1]=0;
    deck_page[0]=deck_page[1]=3;
    for(int pad=0;pad<8;pad++){
        assert(xz_ui_runtime_pad_color(0,pad,&rgb,&lit)==XZ_LED_HUE&&rgb==xz_jump_page2_color(pad));
        assert(!xz_ui_runtime_pad_color(1,pad,&rgb,&lit));
    }
    jump_state.page[0]=0;assert(!xz_ui_runtime_pad_color(0,0,&rgb,&lit));
    jump_state.page[0]=1;model.jump.shift_page=0;assert(xz_ui_runtime_pad_color(0,0,&rgb,&lit)==XZ_LED_HUE);
    model.jump.enabled=0;
    /* Utility/browser headings own the left title area. VJ remains reachable
       from its physical shortcut and MODS; hidden shortcut must not eat touch. */
    touch.visible=0;model.fb_takeover=0;native_play=0;
    int passed=stock_calls;contact(self,40,10,1);contact(self,40,10,0);
    assert(!model.fb_takeover&&stock_calls==passed+2);
    native_play=1;contact(self,40,10,1);contact(self,40,10,0);assert(model.fb_takeover);
    native_play=0;contact(self,40,10,1);contact(self,40,10,0);assert(!model.fb_takeover);
    /* Each settings pad chooses a bank without muting audio or editing jumps. */
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        ui.page=XZ_UI_CONTROLS;model.theme=theme;
        for(int pad=0;pad<8;pad++){
            struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,widgets);int found=0;
            unsigned muted=model.deck[0].muted;struct xz_jump_settings jump=model.jump;
            for(size_t i=0;i<n;i++)if(widgets[i].kind==XZ_UI_STEM_PAD_CONFIG&&widgets[i].index==pad){
                struct xz_ui_action action[XZ_UI_ACTIONS];assert(xz_ui_touch(&ui,&model,widgets[i].x+20,widgets[i].y+20,1,action)==1);
                apply(NULL,action,1);xz_ui_touch(&ui,&model,0,0,0,action);found=1;
            }
            assert(found&&model.stem_bank==pad/4&&pads.bank==pad/4&&model.deck[0].muted==muted&&!memcmp(&jump,&model.jump,sizeof(jump)));
        }
    }
    /* A different four-stem track starts neutral; count-only publication is tested separately. */
    fixture_generation++;fixture_stem_count[0]=4;refresh();
    assert(model.deck[0].stem_count==4&&pads.count[0]==4&&model.deck[1].stem_count==3);
    struct xz_ui_action bass_level={.kind=XZ_UI_LEVEL,.deck=0,.index=3,.value=.4f};apply(NULL,&bass_level,1);
    assert(applied_bass[0]==.4f);
    struct xz_ui_action bass_mute={.kind=XZ_UI_MUTE,.phase=XZ_UI_PRESS,.deck=0,.index=3};apply(NULL,&bass_mute,1);
    assert(applied_bass[0]==0&&(model.deck[0].muted&8u)&&!model.deck[0].bypass);
    fixture_stem_count[0]=3;refresh();assert(!(model.deck[0].muted&8u)&&model.deck[0].levels[3]==1);
    ram_tracks();
    /* Real settings worker writes UI choices; reloading restores them. */
    char persist_root[]="/tmp/xz-runtime-prefs-XXXXXX",persist_path[1200];
    assert(mkdtemp(persist_root));strcpy(settings_usb,persist_root);
    struct xz_settings profile_defaults;assert(xz_usb_profile_load(&settings_profile,persist_root,0,&profile_defaults,NULL)==1);
    settings_target_empty=1;settings_owner_valid=0;
    browse_settings(&profile_defaults);settings_slot=-1;
    __atomic_store_n(&profile_slot,~0u,__ATOMIC_RELAXED);
    settings_readonly=0;settings_running=1;settings_pending=0;
    assert(!pthread_create(&settings_thread,NULL,settings_writer,NULL));
    pthread_mutex_lock(&ui_mutex);
    struct xz_ui_action persist_actions[]={
        {.kind=XZ_UI_SET_THEME,.index=14},
        {.kind=XZ_UI_STEM_BANK,.index=1},
        {.kind=XZ_UI_STEMS_OVERLAY},
        {.kind=XZ_UI_JUMP_SIZE,.index=7,.value=9}
    };
    model.stems_overlay=1;apply(NULL,persist_actions,4);
    assert(settings_pending);settings_running=0;pthread_cond_signal(&settings_changed);pthread_mutex_unlock(&ui_mutex);
    assert(!pthread_join(settings_thread,NULL));
    struct xz_settings persisted;assert(!xz_settings_load(settings_usb,&persisted));
    assert(persisted.theme==14&&persisted.stem_bank==1&&persisted.stems_overlay==XZ_STEM_LAYOUT_BELOW_HALF&&persisted.jump.sizes[7]==9);
    snprintf(persist_path,sizeof(persist_path),"%s/VJ.Tools/XZ-Mods.cfg",persist_root);assert(!unlink(persist_path));
    snprintf(persist_path,sizeof(persist_path),"%s/VJ.Tools",persist_root);assert(!rmdir(persist_path));assert(!rmdir(persist_root));
    generation_pad_consistency();
    stem_layout_runtime_check();
    usb_stem_reason_runtime_check();
    rekordbox_runtime_check();
    /* Frame publication carries MODS order and reports the original key when feedback is OFF. */
    model.stem_order=5;model.key_feedback=0;model.key_format=1;key_valid[0]=1;
    key_snapshot[0]=(struct xz_key_snapshot){.loaded=1,.original={9,1},.format=XZ_KEY_CAMELOT,.tempo_ratio=1.06};
    struct xz_shell_runtime_frame_input policy;shell_frame_input_locked(&policy);
    assert(policy.stem_order==5&&policy.key[0].valid&&!strcmp(policy.key[0].text,"Am")&&!policy.key[0].cents);
    model.key_format=2;shell_frame_input_locked(&policy);assert(!strcmp(policy.key[0].text,"8A"));
    puts("PASS two-deck stem controls, waveform gestures, VJ view, all theme/page/bank LEDs and touch ownership");return 0;
}
