/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Four-stem acceptance requires assertions
#endif
#include "ui.h"
#include "ui_skin.h"
#include "stem_pads.h"
#include "native_led.h"
#include "wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 536
static uint16_t pixels[W*XZ_WAVE_INLINE_HEIGHT];
static uint16_t screen[800*480];

static struct xz_ui_model local_model(int count0,int count1){
    struct xz_ui_model m={0};m.enabled=XZ_UI_STEM;
    int counts[2]={count0,count1};
    for(int deck=0;deck<2;deck++){
        m.deck[deck].ready=XZ_UI_STEM;m.deck[deck].stem_count=counts[deck];
        for(int i=0;i<4;i++)m.deck[deck].levels[i]=1;
    }
    return m;
}
static size_t row(const struct xz_ui *u,const struct xz_ui_model *m,int deck,struct xz_ui_widget out[XZ_UI_WIDGETS]){
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_inline_layout(u,m,W,XZ_WAVE_INLINE_HEIGHT,w),k=0;
    for(size_t i=0;i<n;i++)if(w[i].deck==deck)out[k++]=w[i];
    return k;
}
static size_t tap(struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a,struct xz_ui_action out[XZ_UI_ACTIONS]){
    int x=a->x+a->w/2,y=a->y+a->h/2;
    size_t n=xz_ui_inline_touch(u,m,W,XZ_WAVE_INLINE_HEIGHT,x,y,1,out);
    if(!n)n=xz_ui_inline_touch(u,m,W,XZ_WAVE_INLINE_HEIGHT,x,y,0,out);
    return n;
}

int main(void){
    struct xz_ui u;xz_ui_init(&u);
    struct xz_ui_widget w[XZ_UI_WIDGETS];struct xz_ui_action a[XZ_UI_ACTIONS];

    /* Three-stem rows are unchanged: three mutes then BYPASS in four equal slots. */
    struct xz_ui_model m=local_model(0,3);
    for(int deck=0;deck<2;deck++){
        size_t n=row(&u,&m,deck,w);assert(n==4);
        for(int slot=0;slot<3;slot++){assert(w[slot].kind==XZ_UI_MUTE&&w[slot].index==xz_stem_at(0,slot));}
        assert(w[3].kind==XZ_UI_BYPASS&&!strcmp(w[3].label,"BYPASS"));
        for(size_t i=0;i<n;i++)assert(w[i].x==W*(int)i/4+1);
        for(int slot=0;slot<3;slot++)if(w[slot].index==1)assert(!strcmp(w[slot].label,"HARMONICS"));
    }

    /* Four-stem: A-C in saved order, BASS at slot 3 like pad D/H, BYPASS last; HARM reads INST. */
    for(int order=0;order<XZ_STEM_ORDER_COUNT;order++){
        m=local_model(4,4);m.stem_order=order;
        for(int deck=0;deck<2;deck++){
            size_t n=row(&u,&m,deck,w);assert(n==5);
            for(int slot=0;slot<4;slot++){
                assert(w[slot].kind==XZ_UI_MUTE&&w[slot].index==xz_stem_pad_role(order,4,slot));
                assert(w[slot].x==W*slot/5+1&&w[slot].w==W*(slot+1)/5-W*slot/5-2);
            }
            assert(w[3].index==XZ_STEM_BASS&&!strcmp(w[3].label,"BASS"));
            for(int slot=0;slot<3;slot++)if(w[slot].index==1)assert(!strcmp(w[slot].label,"INST"));
            assert(w[4].kind==XZ_UI_BYPASS);
            /* The screen slot and the physical pad act on the same role. */
            for(int pad=0;pad<4;pad++){
                struct xz_stem_pads state={0};state.order=order;state.count[deck]=4;
                struct xz_cue_event e={deck,pad,0,1,0,0,0,-1,0};int toggle=-1;
                assert(xz_stem_pad_event(&state,&e,1,&toggle));
                struct xz_ui panel;xz_ui_init(&panel);
                assert(tap(&panel,&m,&w[pad],a)==1&&a[0].kind==XZ_UI_MUTE&&a[0].index==toggle&&a[0].deck==deck);
            }
            struct xz_ui panel;xz_ui_init(&panel);
            assert(tap(&panel,&m,&w[4],a)==1&&a[0].kind==XZ_UI_BYPASS&&a[0].deck==deck);
        }
    }

    /* Mixed decks lay out independently. */
    m=local_model(4,3);
    assert(row(&u,&m,0,w)==5&&w[3].index==XZ_STEM_BASS);
    assert(row(&u,&m,1,w)==4&&w[3].kind==XZ_UI_BYPASS);
    m=local_model(3,4);
    assert(row(&u,&m,0,w)==4&&row(&u,&m,1,w)==5);

    /* Dragging BASS sets levels[3]; a tap without drag mutes it. */
    m=local_model(4,3);m.deck[0].levels[3]=.5f;
    row(&u,&m,0,w);
    {
        struct xz_ui panel;xz_ui_init(&panel);struct xz_ui_widget b=w[3];int y=b.y+b.h/2;
        assert(!xz_ui_inline_touch(&panel,&m,W,XZ_WAVE_INLINE_HEIGHT,b.x+10,y,1,a));
        assert(xz_ui_inline_touch(&panel,&m,W,XZ_WAVE_INLINE_HEIGHT,b.x+40,y,1,a)==1);
        assert(a[0].kind==XZ_UI_LEVEL&&a[0].index==XZ_STEM_BASS&&a[0].deck==0&&a[0].value>.5f);
        xz_ui_inline_touch(&panel,&m,W,XZ_WAVE_INLINE_HEIGHT,b.x+40,y,0,a);
    }

    /* Setup states keep the five-slot shape and end in the one SET UP button. */
    m=local_model(4,3);m.enabled=0;
    assert(row(&u,&m,0,w)==5&&w[4].kind==XZ_UI_LOCAL_STEM_SETUP_OPEN&&!strcmp(w[4].label,"SET UP"));
    m=local_model(3,3);m.deck[1].stem_error="4-STEM SET INCOMPLETE: BASS FILE MISSING";
    assert(row(&u,&m,1,w)==4&&w[3].kind==XZ_UI_LOCAL_STEM_SETUP_OPEN);
    assert(row(&u,&m,0,w)==4&&w[3].kind==XZ_UI_BYPASS);

    /* One bass colour everywhere: the PC BASS colour, following user stem colours. */
    for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int custom=0;custom<2;custom++){
        m=local_model(4,4);m.theme=theme;
        if(custom)for(int stem=0;stem<3;stem++)m.stem_colors[stem]=stem+4;
        assert(xz_ui_model_local_stem_color(&m,XZ_STEM_BASS)==xz_ui_model_pc_role_color(&m,XZ_UI_PC_BASS));
        for(int stem=0;stem<3;stem++)assert(xz_ui_model_local_stem_color(&m,stem)==xz_ui_model_stem_color(&m,stem));
        struct xz_ui_look l;row(&u,&m,0,w);l=xz_ui_widget_look(&u,&m,&w[3]);
        assert(l.color==xz_ui_model_local_stem_color(&m,XZ_STEM_BASS));
        /* Every skin draws the BASS slot border in the bass colour, not drum colour. */
        memset(pixels,0,sizeof(pixels));
        assert(xz_ui_inline_render(&u,&m,pixels,sizeof(pixels)/sizeof(*pixels),W,W,XZ_WAVE_INLINE_HEIGHT));
        assert(pixels[(size_t)(w[3].y+w[3].h/2)*W+(size_t)w[3].x]==xz_theme_rgb565(l.color));
        m.deck[0].muted=8u;
        assert(xz_ui_widget_look(&u,&m,&w[3]).selected);
        assert(xz_ui_inline_render(&u,&m,pixels,sizeof(pixels)/sizeof(*pixels),W,W,XZ_WAVE_INLINE_HEIGHT));
        /* Rendering the colour editor and the setup page with four-stem state is safe on every theme. */
        u.page=XZ_UI_STEM_COLORS;assert(xz_ui_render(&u,&m,screen,800*480,800));
        u.page=XZ_UI_LOCAL_STEM_SETUP;u.deck=0;m.deck[0].stem_format="FOUR OGG";
        assert(xz_ui_render(&u,&m,screen,800*480,800));
        m.deck[0].stem_error="4-STEM FORMAT NOT SUPPORTED YET: RX3";
        assert(xz_ui_render(&u,&m,screen,800*480,800));
        u.page=XZ_UI_CONTROLS;assert(xz_ui_render(&u,&m,screen,800*480,800));
        xz_ui_init(&u);
    }

    /* Pad LEDs: three-stem slot 3 stays white BYPASS; four-stem slot 3 is lit bass while audible. */
    {
        int colors[3]={0,0,0};float levels[4]={1,1,1,1};uint32_t rgb;int lit;
        for(int role=0;role<3;role++){
            assert(xz_stem_pad_led(role,colors,0,levels,0,&rgb,&lit)&&rgb==xz_stem_hardware_color(role)&&lit);
            assert(xz_stem_pad_led(role,colors,1u<<role,levels,0,&rgb,&lit)&&!lit);
            assert(xz_stem_pad_led(role,colors,0,levels,1,&rgb,&lit)&&!lit);
        }
        colors[1]=2;assert(xz_stem_pad_led(1,colors,0,levels,0,&rgb,&lit)&&rgb==xz_stem_palette_color(2,0));
        assert(xz_stem_pad_led(xz_stem_pad_role(0,3,3),colors,0,levels,0,&rgb,&lit)&&rgb==0xffffff&&!lit);
        assert(xz_stem_pad_led(XZ_STEM_PAD_BYPASS,colors,0,levels,1,&rgb,&lit)&&lit);
        int bass=xz_stem_pad_role(0,4,3);
        assert(xz_stem_pad_led(bass,colors,0,levels,0,&rgb,&lit)&&rgb==XZ_STEM_HARDWARE_BASS&&lit);
        for(int stem=0;stem<4;stem++)assert(XZ_STEM_HARDWARE_BASS!=xz_stem_hardware_color(stem));
        assert(xz_stem_hardware_color(4)==XZ_STEM_HARDWARE_BASS&&xz_stem_hardware_color(5)==0xffffff);
        assert(xz_stem_pad_led(bass,colors,8u,levels,0,&rgb,&lit)&&!lit);
        assert(xz_stem_pad_led(bass,colors,7u,levels,0,&rgb,&lit)&&lit);
        assert(xz_stem_pad_led(bass,colors,0,levels,1,&rgb,&lit)&&!lit);
        levels[3]=0;assert(xz_stem_pad_led(bass,colors,0,levels,0,&rgb,&lit)&&!lit);
        assert(!xz_stem_pad_led(-1,colors,0,levels,0,&rgb,&lit));
    }

    /* STEM ORDER readout names the fixed bass pad only while a four-stem deck is loaded. */
    for(int count=3;count<=4;count++){
        m=local_model(3,count);
        struct xz_ui c;xz_ui_init(&c);c.page=XZ_UI_CONTROLS;
        struct xz_ui_widget all[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&c,&m,all);int found=0;
        for(size_t i=0;i<n;i++)if(all[i].kind==XZ_UI_STEM_ORDER){c.focus_kind=all[i].kind;c.focus_index=all[i].index;c.focus_deck=all[i].deck;found=1;}
        assert(found);
        struct xz_ui_info info;assert(xz_ui_describe(&c,&m,&info));
        char expected[48];snprintf(expected,sizeof(expected),"%s%s",xz_stem_order_name(0),count==4?" +BASS":"");
        assert(!strcmp(info.state,expected));
    }
    puts("PASS four-stem rows, pad/screen agreement, mixed decks, bass level, setup slot, bass colour, LEDs and order readout");
    return 0;
}
