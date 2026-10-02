#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../ui_runtime.c"
#ifndef XZ_WAVE_RIDER_RUNTIME_TEST
#include "test_wave_rider_stubs.h"
#endif
#ifdef NDEBUG
#error Runtime UI acceptance requires active assertions
#endif
static int native_play=1,native_active=1,stock_calls,fixture_focus=-1,fixture_prepared=1;
static struct xz_touch_status last_stock;
static struct xz_stem_levels applied[2];
static uint32_t fixture_generation=1;
static unsigned waveform_calls[2];
int xz_native_skin_start(int theme){(void)theme;return 0;}
void xz_native_skin_request(int theme){(void)theme;}
const char *xz_native_skin_status(void){return "Native theme test fixture";}
void xz_native_skin_stop(void){}
int xz_native_focus_deck(void){return fixture_focus;}
int xz_native_play_visible(void){return native_play;}
int xz_native_inline_active(void){return native_active;}
int xz_native_inline_start(int(*on)(void),xz_native_wave_render draw,void *context){(void)on;(void)draw;(void)context;return 0;}
int xz_hook_arm(uint32_t a,const unsigned char g[8],void *r,void **o){(void)a;(void)g;(void)r;(void)o;return 0;}
int xz_native_led_start(void){return 0;}
void xz_native_led_stop(void){}
void xz_log(const char *s){(void)s;}
unsigned xz_runtime_cue_flags(void){return 0;}
static int jump_calls,jump_deck,jump_beats;
int xz_runtime_beat_jump(int deck,int beats){jump_calls++;jump_deck=deck;jump_beats=beats;return 1;}
int xz_runtime_set_cues(int a,int b){(void)a;(void)b;return 0;}
int xz_key_get_desired_semitones(int d,int *out){(void)d;*out=0;return 0;}
int xz_key_set_desired_semitones(int d,int n){(void)d;(void)n;return 0;}
void xz_audio_set_enabled(int on){(void)on;}
void xz_audio_set_levels(int deck,struct xz_stem_levels value){applied[deck]=value;}
int xz_audio_get_status(int deck,struct xz_audio_status *out){(void)deck;memset(out,0,sizeof(*out));out->generation=fixture_generation;out->prepared=fixture_prepared;out->state=XZ_AUDIO_EXPERIMENTAL_READY;return 0;}
const char *xz_audio_state_name(enum xz_audio_state state){(void)state;return "ready";}
int xz_audio_waveform(int deck,unsigned role,unsigned char *bins,size_t count,float *progress){
    (void)role;if(deck<0||deck>1)return 0;
    waveform_calls[deck]++;memset(bins,deck?24:8,count);if(progress)*progress=0.5;return 1;
}
static void stock(void *self,const struct xz_touch_status *s,const void *mode){(void)mode;stock_calls++;last_stock=*s;memcpy((char*)self+4,s,sizeof(*s));}
static void contact(void *self,unsigned x,unsigned y,int down){struct xz_touch_status s={(uint8_t)down,{0},x,y};touch_hook(self,&s,NULL);}
int main(void){
    unsigned char self[32]={0};started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;model.stems_overlay=1;
    xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
    contact(self,200,130,1);contact(self,200,130,0);assert(model.deck[0].muted==4&&applied[0].vocals==0&&stock_calls==0);
    contact(self,200,260,1);contact(self,200,260,0);assert(model.deck[1].muted==4&&applied[1].vocals==0);
    contact(self,200,130,1);contact(self,200,130,0);assert(!model.deck[0].muted&&applied[0].vocals==1);
    contact(self,200,260,1);contact(self,200,260,0);assert(!model.deck[1].muted&&applied[1].vocals==1);
    contact(self,600,130,1);contact(self,600,130,0);assert(model.deck[0].bypass&&!model.deck[1].bypass);
    contact(self,600,260,1);contact(self,600,260,0);assert(model.deck[0].bypass&&model.deck[1].bypass);
    contact(self,600,130,1);contact(self,600,130,0);
    contact(self,600,260,1);contact(self,600,260,0);assert(!model.deck[0].bypass&&!model.deck[1].bypass);
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
    assert(!model.stems_overlay&&inline_cancelled&&!inline_ui.down);
    contact(self,200,130,0);assert(!inline_contact);
    apply(NULL,&hide,1);assert(model.stems_overlay);
    int calls=stock_calls;native_active=0;contact(self,799,400,1);contact(self,799,400,0);assert(stock_calls==calls+2&&!inline_contact);
    native_active=1;contact(self,30,400,1);contact(self,200,130,1);contact(self,200,130,0);assert(stock_calls==calls+5);
    contact(self,200,80,1);assert(last_stock.x==200&&last_stock.y==18+xz_wave_source_row(62,84));
    contact(self,200,180,1);assert(last_stock.y==18+xz_wave_source_row(83,84));contact(self,200,180,0);
    contact(self,200,180,1);assert(last_stock.y==18+xz_wave_source_row(162,84));
    contact(self,200,130,1);assert(last_stock.y==18+xz_wave_source_row(132,84));contact(self,200,130,0);
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
    assert(!model.stems_overlay&&!inline_enabled()&&stock_calls==before);
    assert(!render_inline(NULL,pixels+1,count,536,536,XZ_WAVE_INLINE_HEIGHT));
    e.operation=0;assert(xz_ui_runtime_pad(&e,&flags));e.operation=2;assert(xz_ui_runtime_pad(&e,&flags));
    contact(self,700,10,1);contact(self,700,10,0);assert(model.stems_overlay&&inline_enabled());
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
    touch.visible=1;ui.jump_edit_page=1;ui.jump_edit_pair=3;model.jump.sizes[7]=4;
    assert(encoder_rotate_hook(1)==1&&model.jump.sizes[7]==5);
    assert(encoder_rotate_hook(-1)==1&&model.jump.sizes[7]==4);
    assert(encoder_rotate_hook(100)==1&&model.jump.sizes[7]==9);
    assert(encoder_rotate_hook(-100)==1&&model.jump.sizes[7]==0);
    touch.visible=0;assert(!encoder_rotate_hook(1)&&!model.jump.sizes[7]);
    settings_readonly=0;ui.page=XZ_UI_CONTROLS;model.theme=0;
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
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==1);
    model.jump.enabled=0;je.operation=2;assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==1);
    /* A stem press/release must not leave a stale held bit in the jump state. */
    je.pad=0;je.operation=0;pads.bank=0;assert(xz_ui_runtime_pad(&je,&jf));
    je.operation=2;assert(xz_ui_runtime_pad(&je,&jf));
    model.jump.enabled=1;je.operation=0;
    assert(xz_ui_runtime_pad(&je,&jf)&&jump_calls==2&&jump_beats==-16);
    je.operation=2;assert(xz_ui_runtime_pad(&je,&jf));
    model.jump.enabled=1;model.jump.shift_page=1;jump_state.page[0]=1;jump_state.page[1]=0;
    deck_page[0]=deck_page[1]=3;
    for(int pad=0;pad<8;pad++){
        assert(xz_ui_runtime_pad_color(0,pad,&rgb,&lit)==XZ_LED_HUE&&rgb==xz_jump_page2_color(pad));
        assert(!xz_ui_runtime_pad_color(1,pad,&rgb,&lit));
    }
    jump_state.page[0]=0;assert(!xz_ui_runtime_pad_color(0,0,&rgb,&lit));
    jump_state.page[0]=1;model.jump.shift_page=0;assert(!xz_ui_runtime_pad_color(0,0,&rgb,&lit));
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
    /* Real settings worker writes UI choices; reloading restores them. */
    char persist_root[]="/tmp/xz-runtime-prefs-XXXXXX",persist_path[1200];
    assert(mkdtemp(persist_root));strcpy(settings_usb,persist_root);assert(!stat(settings_usb,&settings_volume));
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
    assert(persisted.theme==14&&persisted.stem_bank==1&&!persisted.stems_overlay&&persisted.jump.sizes[7]==9);
    snprintf(persist_path,sizeof(persist_path),"%s/VJ.Tools/XZ-Mods.cfg",persist_root);assert(!unlink(persist_path));
    snprintf(persist_path,sizeof(persist_path),"%s/VJ.Tools",persist_root);assert(!rmdir(persist_path));assert(!rmdir(persist_root));
    puts("PASS two-deck stem controls, waveform gestures, VJ view, all theme/page/bank LEDs and touch ownership");return 0;
}
