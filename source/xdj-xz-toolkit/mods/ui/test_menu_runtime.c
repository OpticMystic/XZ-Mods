/* SPDX-License-Identifier: MIT
 * One MODS menu in both interfaces, through the real ui_runtime.c: in MODERN every way in (the shell's MODS key,
 * MENU and header button, a MODS section on UTILITY's rail, the button next to LINK, setups from the play screen,
 * HOME's SET UP) shows the panel over the shell on the right page; its UTILITY cell opens the player's own settings;
 * its MODERN look rows reach the shell; a short MENU follows ui/menu_key.h in both interfaces (stock first); the
 * keys the shell owns leave MODS in one press; a touch that closes MODS never lands on the shell. Then every
 * control of every page works in both interfaces. The stand-in shell keeps shell_runtime.h's contract.
 * First the existing runtime control suite runs unchanged. Built by verify_menu_runtime.py. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#define main existing_runtime_controls
#include "test_runtime_controls.c"
#undef main
#include <stdio.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
/* The two local players' memory map (as test_play_key_runtime_r1.c), only while every_row_works runs: KEY SYNC
   needs a player to act on. Otherwise nothing is readable. */
static int rows_players;
int xz_read_memory(uint32_t address,void *out,size_t n){
    uint32_t value=0;
    if(!rows_players)return -1;
    if(n==4){
        if(address==0x1c8a308)value=0x200000;else if(address==0x200040)value=0x210000;else if(address==0x200048)value=2;
        else if(address==0x210000)value=0x220000;else if(address==0x210004)value=0x221000;
        else if(address==0x220138)value=0x230000;else if(address==0x221138)value=0x231000;else return -1;
        memcpy(out,&value,4);return 0;
    }
    if(n==1&&(address==0x230026||address==0x231026)){*(unsigned char *)out=address==0x230026?1:2;return 0;}
    return -1;
}

/* The stand-in shell. Off while the existing suite runs: it proves the runtime without these hooks. */
static int fake_menu_on, fake_utility, fake_closes, fake_look_sets, fake_mods_page=-1, fake_stock_play=-1, fake_touches;
static struct xz_ui_modern_look fake_look;
int xz_shell_runtime_show_utility(void){if(!fake_menu_on||!fake_shell_running)return 0;fake_utility++;return 1;}
void xz_shell_runtime_close_mods(void){if(fake_menu_on)fake_closes++;}
int xz_shell_runtime_take_mods_page(void){int page=fake_mods_page;fake_mods_page=-1;return page;}
int xz_shell_runtime_stock_play(void){return fake_menu_on?fake_stock_play:-1;}
int xz_shell_runtime_look(struct xz_ui_modern_look *out){
    memset(out,0,sizeof *out);if(!fake_menu_on||!fake_shell_running)return 0;
    *out=fake_look;out->known=1;return 1;
}
/* As shell_runtime.c look_apply_locked: the choice range per item, SHOW 0 / HIDE 1 for the parts. */
int xz_shell_runtime_look_set(int item,int choice){
    if(!fake_menu_on||!fake_shell_running)return 0;
    if(item==XZ_UI_LOOK_WAVE_COLOR){if(choice<0||choice>=XZ_UI_WAVE_COLORS)return 0;fake_look.wave_color=choice;fake_look.usb_colors=1;}
    else if(item==XZ_UI_LOOK_USB_COLORS){if(choice<0||choice>2)return 0;fake_look.usb_colors=choice;}
    else if(item>=XZ_UI_LOOK_MODE_LAMPS&&item<XZ_UI_LOOK_ITEMS){if(choice!=0&&choice!=1)return 0;
        fake_look.hidden=choice?fake_look.hidden|1u<<item:fake_look.hidden&~(1u<<item);}
    else return 0;
    fake_look_sets++;return 1;
}
/* The shell's touch owner: a contact it is offered while not eligible goes to stock; while eligible it takes it. */
int xz_shell_runtime_touch(int eligible,int theme,int x,int y,int down,int stock_down){
    (void)theme;(void)x;(void)y;(void)stock_down;
    if(!fake_menu_on||!fake_shell_running||!eligible)return XZ_SHELL_TOUCH_TO_STOCK;
    if(down)fake_touches++;
    return XZ_SHELL_TOUCH_TAKEN;
}

static void modern(void){
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);
    xz_ui_init(&ui);fake_utility=fake_closes=fake_look_sets=fake_touches=0;fake_mods_page=-1;fake_stock_play=-1;fake_requests=0;
    started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;fixture_prepared=1;
    pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);
}
static int shown(void){return touch.visible&&__atomic_load_n(&visible,__ATOMIC_ACQUIRE);}
static int shut(void){return !touch.visible&&!__atomic_load_n(&visible,__ATOMIC_ACQUIRE);}
/* The shell asks for MODS (the MODS key, MENU, the MODS button: page -1; a UTILITY rail tab: its section). */
static void shell_opens(int page){
    fake_mods_page=page;fake_requests|=XZ_SHELL_UI_OPEN_MODS;
    pthread_mutex_lock(&ui_mutex);shell_ui_requests_locked();pthread_mutex_unlock(&ui_mutex);
}
static void run(enum xz_ui_action_kind kind,int index,float value){
    struct xz_ui_action a={.kind=kind,.phase=XZ_UI_PRESS,.deck=-1,.index=index,.value=value};apply(NULL,&a,1);
}
static void tap_widget(enum xz_ui_action_kind kind,int index){
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w),at=n;
    for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)at=i;
    assert(at<n);
    struct xz_ui_action a[XZ_UI_ACTIONS];int x=w[at].x+w[at].w/2,y=w[at].y+w[at].h/2;
    size_t got=xz_ui_touch(&ui,&model,x,y,1,a);apply(NULL,a,got);got=xz_ui_touch(&ui,&model,x,y,0,a);apply(NULL,a,got);
}
static struct xz_ui_widget focused(void){struct xz_ui_widget w;assert(xz_ui_focused(&ui,&model,&w));return w;}

static void modern_entries(void){
    /* the MODS key, MENU or the MODS button: the menu over the shell, which is no longer eligible */
    modern();shell_opens(-1);
    pthread_mutex_lock(&ui_mutex);int eligible=shell_eligible_locked();pthread_mutex_unlock(&ui_mutex);
    assert(shown()&&ui.page==XZ_UI_HOME&&!eligible);
    /* it reopens where it was: CLOSE returns to the shell, which also closes UTILITY when it was under it */
    xz_ui_navigate(&ui,XZ_UI_SCREEN);run(XZ_UI_CLOSE,0,0);assert(shut()&&fake_closes==1);
    shell_opens(-1);assert(shown()&&ui.page==XZ_UI_SCREEN);
    run(XZ_UI_CLOSE,0,0);
    /* a section on UTILITY's rail: that section, focus on its first control */
    shell_opens(XZ_UI_PLAY);assert(shown()&&ui.page==XZ_UI_PLAY);
    {struct xz_ui_widget f=focused();assert(f.kind==XZ_UI_FEATURE_LINK&&f.index==XZ_UI_BEAT_JUMP);}
    run(XZ_UI_CLOSE,0,0);
    /* the button next to LINK set to MODS MENU, through its handler (the shell stepped aside): opens, then closes */
    pthread_mutex_lock(&ui_mutex);rekordbox_run_locked(XZ_REKORDBOX_MODS);pthread_mutex_unlock(&ui_mutex);assert(shown());
    pthread_mutex_lock(&ui_mutex);rekordbox_run_locked(XZ_REKORDBOX_MODS);pthread_mutex_unlock(&ui_mutex);assert(shut());
    /* setups from the play screen open the menu on their page */
    modern();run(XZ_UI_LOCAL_STEM_SETUP_OPEN,0,0);assert(shown()&&ui.page==XZ_UI_LOCAL_STEM_SETUP);
    modern();{struct xz_ui_action a={.kind=XZ_UI_PC_SETUP,.phase=XZ_UI_PRESS,.deck=0,.index=XZ_UI_PC_WHY_SOURCE};apply(NULL,&a,1);}
    assert(shown()&&ui.page==XZ_UI_PC_STEMS);
    modern();run(XZ_UI_METRICS_SETUP_OPEN,0,0);assert(shown()&&ui.page==XZ_UI_METRICS_SETUP);
    /* HOME's SET UP FOUR DECKS: 4 DECKS, focus on RESCAN */
    modern();fixture_route=(struct xz_extra_route_status){0};fixture_route.state=XZ_EXTRA_ROUTE_NOT_DETECTED;
    pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);
    shell_opens(XZ_UI_HOME);tap_widget(XZ_UI_HOME_SETUP,XZ_UI_FOUR_DECKS);
    assert(shown()&&ui.page==XZ_UI_FOUR_DECKS);{struct xz_ui_widget f=focused();assert(f.kind==XZ_UI_ROUTE_RESCAN);}
    run(XZ_UI_CLOSE,0,0);fixture_route=(struct xz_extra_route_status){0};
    /* over the VJ takeover too */
    modern();model.fb_takeover=1;__atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);shell_opens(-1);assert(shown());
    run(XZ_UI_CLOSE,0,0);model.fb_takeover=0;__atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
    puts("PASS MODERN: the MODS key, MENU, the MODS button, UTILITY's rail, the button next to LINK, play-screen setups and HOME's SET UP open the one menu on the right page");
}
static void utility_cell(void){
    modern();shell_opens(XZ_UI_STEM_SECTION);
    {struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w),cells=0;
     for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_OPEN_UTILITY){cells++;assert(w[i].x<XZ_UI_RAIL_W&&w[i].y<XZ_UI_RAIL_DIVIDER_Y&&w[i].h>=40);}
     assert(cells==1);}
    tap_widget(XZ_UI_OPEN_UTILITY,0);
    assert(shut()&&fake_utility==1&&ui.page==XZ_UI_STEM_SECTION);
    shell_opens(-1);assert(shown()&&ui.page==XZ_UI_STEM_SECTION);run(XZ_UI_CLOSE,0,0);
    /* CLASSIC has no UTILITY cell: held MENU still opens the player's own UTILITY there */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);
    {struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w);for(size_t i=0;i<n;i++)assert(w[i].kind!=XZ_UI_OPEN_UTILITY);}
    puts("PASS UTILITY: MODERN's rail cell closes MODS and opens the player's own settings; MODS reopens where it was");
}
static void look_rows(void){
    modern();fake_look=(struct xz_ui_modern_look){0};
    pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);assert(model.modern_look.known);
    run(XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_COLOR,XZ_UI_WAVE_RGB);
    assert(fake_look.wave_color==XZ_UI_WAVE_RGB&&model.modern_look.wave_color==XZ_UI_WAVE_RGB&&model.wave_mode==0&&!strcmp(ui.notice,"WAVEFORM: RGB"));
    run(XZ_UI_SHELL_LOOK,XZ_UI_LOOK_WAVE_COLOR,XZ_UI_WAVE_STEMS);assert(model.wave_mode==2&&model.modern_look.wave_color==XZ_UI_WAVE_STEMS);
    run(XZ_UI_SHELL_LOOK,XZ_UI_LOOK_MODE_LAMPS,1);assert((model.modern_look.hidden>>XZ_UI_LOOK_MODE_LAMPS)&1u);
    int sets=fake_look_sets;run(XZ_UI_SHELL_LOOK,XZ_UI_LOOK_USB_COLORS,7);assert(fake_look_sets==sets&&!strcmp(ui.notice,"THAT PART DID NOT CHANGE"));
    /* MODERN's own stem layouts: the picture sets its layout */
    run(XZ_UI_STEMS_OVERLAY,0,(float)(1+XZ_MODERN_STEMS_PADS));assert(model.modern_stems==XZ_MODERN_STEMS_PADS);
    run(XZ_UI_STEMS_OVERLAY,0,(float)(1+XZ_STEM_LAYOUT_OUTER));assert(model.modern_stems==XZ_STEM_LAYOUT_OUTER);
    fake_look=(struct xz_ui_modern_look){0};
    puts("PASS MODERN look rows: WAVEFORM COLOURS with RGB, lamps and titles reach the shell and show its values; stem layout pictures set MODERN's layout");
}
/* One physical key contact through the onKey hook: op 0 press, 1 hold, 2 release. */
static void key(uint16_t code,unsigned op){
    unsigned char input[16]={0};input[8]=(unsigned char)(code&255);input[9]=(unsigned char)(code>>8);input[11]=(unsigned char)op;
    menu_back_key_hook(NULL,input);
}
static void menu_table(void){
    stock_global_key=fake_global;fake_keys_all=1;
    /* the table itself: every place has one job; MODS opens only where MENU has no stock job */
    for(int p=0;p<XZ_MENU_PLACES;p++){
        const struct xz_menu_rule *r=xz_menu_rule(p);assert(r->what&&r->what[0]&&r->basis&&r->basis[0]);
        int stock_job=p==XZ_MENU_AT_STOCK_MENU||p==XZ_MENU_AT_TRACK_LIST||p==XZ_MENU_AT_TAG_LIST||p==XZ_MENU_AT_GRID_ADJUST||
            p==XZ_MENU_AT_UTILITY||p==XZ_MENU_AT_OTHER||p==XZ_MENU_AT_GAME||p==XZ_MENU_AT_TAKEOVER;
        assert(r->job==(p==XZ_MENU_AT_MODS?XZ_MENU_JOB_CLOSE_MODS:stock_job?XZ_MENU_JOB_STOCK:XZ_MENU_JOB_MODS));
    }
    assert(xz_menu_rule(XZ_MENU_AT_PLAY)->unknown&&!xz_menu_rule(XZ_MENU_AT_TRACK_LIST)->unknown&&xz_menu_rule(-1)==xz_menu_rule(XZ_MENU_AT_OTHER));
    /* CLASSIC play screen, no stock menu: a short MENU opens MODS and another closes it; stock sees every event */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);
    native_play=1;fake_stock_play=1;int calls=fake_global_calls;
    key(0x20b,0);key(0x20b,2);assert(shown()&&fake_global_calls==calls+2);
    key(0x20b,0);key(0x20b,2);assert(shut()&&fake_global_calls==calls+4);
    /* held: stock's UTILITY, no MODS (the hold reaches stock with no extra press) */
    key(0x20b,0);key(0x20b,1);key(0x20b,2);assert(shut()&&fake_global_calls==calls+7);
    /* held with MODS open: MODS closes and stock opens UTILITY */
    key(0x20b,0);key(0x20b,2);assert(shown());key(0x20b,0);key(0x20b,1);assert(shut());key(0x20b,2);assert(shut());
    /* a stock menu open over the play screen (the probe says not play): MENU is that menu's */
    fake_stock_play=0;key(0x20b,0);key(0x20b,2);assert(shut());
    /* not the play screen (browse, TAG LIST, UTILITY...): stock's */
    fake_stock_play=1;native_play=0;key(0x20b,0);key(0x20b,2);assert(shut());native_play=1;
    /* the probe cannot read: the play scene counts as the play screen */
    fake_stock_play=-1;key(0x20b,0);key(0x20b,2);assert(shown());key(0x20b,0);key(0x20b,2);assert(shut());
    /* the VJ takeover and a game keep MENU as before */
    model.fb_takeover=1;key(0x20b,0);key(0x20b,2);assert(shut());model.fb_takeover=0;
    /* other stock keys are untouched in CLASSIC, MODS open or not */
    calls=fake_global_calls;key(0x209,0);key(0x209,2);assert(shut()&&fake_global_calls==calls+2);
    key(0x20b,0);key(0x20b,2);assert(shown());calls=fake_global_calls;key(0x209,0);key(0x209,2);assert(shown()&&fake_global_calls==calls+2);
    run(XZ_UI_CLOSE,0,0);
    puts("PASS MENU in CLASSIC (ui/menu_key.h): short opens and closes MODS on the play screen, stock keeps it elsewhere, held reaches stock UTILITY, other keys untouched");
    /* MODERN with MODS open: short MENU closes it, held opens UTILITY, the shell's screen keys leave MODS and act */
    modern();shell_opens(-1);calls=fake_global_calls;
    key(0x20b,0);key(0x20b,2);assert(shut()&&fake_global_calls==calls);
    shell_opens(-1);key(0x20b,0);key(0x20b,1);assert(shut()&&fake_utility==1);key(0x20b,2);assert(shut()&&fake_global_calls==calls);
    shell_opens(-1);int keys=fake_shell_keys;key(0x209,0);assert(shut()&&fake_shell_keys==keys+1);key(0x209,2);
    /* BACK stays the menu's: one page up */
    shell_opens(XZ_UI_PLAY);xz_ui_navigate(&ui,XZ_UI_BEAT_JUMP);key(0x4208,0);key(0x4208,2);assert(shown()&&ui.page==XZ_UI_PLAY);
    run(XZ_UI_CLOSE,0,0);fake_keys_all=0;stock_global_key=NULL;
    puts("PASS MENU in MODERN with MODS open: short closes, held opens UTILITY, BROWSE leaves MODS and browses in one press, BACK goes one page up");
}
static void touch_hand_back(void){
    /* a tap on CLOSE closes MODS; the rest of that contact never reaches the shell (its MODS button is there) */
    modern();shell_opens(-1);unsigned char self[32]={0};stock_touch=stock;
    contact(self,760,20,1);assert(shut());contact(self,760,20,1);contact(self,760,20,0);
    assert(!fake_touches);
    /* the next contact is the shell's again */
    contact(self,400,200,1);contact(self,400,200,0);assert(fake_touches==1);
    puts("PASS a touch that closes MODS keeps its contact: the shell under it never sees a tap on its MODS button");
}
#include "test_section_rows_runtime.h"
#ifndef XZ_MENU_RUNTIME_MAIN
#define XZ_MENU_RUNTIME_MAIN main
#endif
int XZ_MENU_RUNTIME_MAIN(void){
    existing_runtime_controls();
    fake_menu_on=1;
    modern_entries();
    utility_cell();
    look_rows();
    menu_table();
    touch_hand_back();
    every_row_works();
    puts("PASS menu runtime");
    return 0;
}
