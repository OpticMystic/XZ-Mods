/* SPDX-License-Identifier: MIT
 * MODS in the MODERN interface through the real ui_runtime.c: the shell's MENU tab commands run on the
 * panel's own state and actions, every panel page opens in the shell's MODS screen, and the panel itself
 * never becomes visible. CLASSIC keeps the panel. The stand-in shell keeps shell_runtime.h's contract.
 * First the existing runtime control suite runs unchanged. Built by verify_menu_runtime.py. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#define main existing_runtime_controls
#include "test_runtime_controls.c"
#undef main
#include <stdio.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}

/* The stand-in shell's MODS screen: which MENU tab root it shows, the page it was given, its commands. */
/* Off while the existing suite runs: it proves the runtime without these hooks (an older shell). */
static int fake_menu_on, fake_mods_open, fake_menu_tab_root=-1, fake_shows, fake_closes, fake_publishes;
static struct xz_shell_menu fake_menu;
static struct xz_shell_runtime_menu_command fake_queue[16];
static unsigned fake_queued;
int xz_shell_runtime_menu_wanted(void){return fake_menu_on&&fake_shell_running&&fake_mods_open&&fake_menu_tab_root>=0;}
void xz_shell_runtime_menu(const struct xz_shell_menu *m){fake_menu=*m;fake_publishes++;}
int xz_shell_runtime_take_menu_command(struct xz_shell_runtime_menu_command *c){
    if(!fake_queued)return 0;
    *c=fake_queue[0];memmove(fake_queue,fake_queue+1,(--fake_queued)*sizeof(*fake_queue));return 1;
}
int xz_shell_runtime_show_mods(int root){
    if(!fake_menu_on||!fake_shell_running)return 0;
    if(root!=XZ_UI_GENERAL&&root!=XZ_UI_CONTROLS&&root!=XZ_UI_SETTINGS&&root!=XZ_UI_EXTRAS)return 0;
    fake_shows++;fake_mods_open=1;fake_menu_tab_root=root;return 1;
}
void xz_shell_runtime_close_mods(void){fake_closes++;fake_mods_open=0;}
int xz_shell_runtime_mods_open(void){return fake_menu_on&&fake_shell_running&&fake_mods_open;}

/* One push in the shell's MENU tab: queued as shell_runtime.c queues it, run as ui_runtime.c runs it. */
static void command(struct xz_shell_runtime_menu_command c){
    assert(fake_queued<16);fake_queue[fake_queued++]=c;
    fake_requests|=XZ_SHELL_UI_MENU;
    pthread_mutex_lock(&ui_mutex);shell_ui_requests_locked();pthread_mutex_unlock(&ui_mutex);
    assert(!fake_queued);
}
static void open_tab(int root){
    fake_mods_open=1;fake_menu_tab_root=root;
    command((struct xz_shell_runtime_menu_command){.op=XZ_SHELL_MENU_OPEN,.root=root});
    assert((int)ui.page==root&&!ui.history_count&&fake_menu.page==root&&fake_menu.root==root);
}
static void activate(int row,int choice){
    assert(row>=0&&row<fake_menu.row_count);
    const struct xz_shell_menu_row *r=&fake_menu.row[row];
    struct xz_shell_runtime_menu_command c={.op=XZ_SHELL_MENU_ACTIVATE};
    c.target=choice>=0?fake_menu.choice[r->first_choice+choice].target:r->target;
    command(c);
}
static int row_named(const char *label){
    for(int r=0;r<fake_menu.row_count;r++)if(!strcmp(fake_menu.row[r].label,label))return r;
    fprintf(stderr,"no row %s on %s\n",label,fake_menu.title);assert(0);return -1;
}
static void back(void){command((struct xz_shell_runtime_menu_command){.op=XZ_SHELL_MENU_BACK});}
static void modern(void){
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);
    xz_ui_init(&ui);fake_mods_open=0;fake_menu_tab_root=-1;fake_shows=fake_closes=fake_publishes=0;fake_queued=0;
    started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;fixture_prepared=1;
}
static int panel_shut(void){return !touch.visible&&!__atomic_load_n(&visible,__ATOMIC_ACQUIRE);}

/* Every row of every page the four MENU tabs reach runs on the panel; the panel never shows. */
static void menu_walk(void){
    static const int roots[4]={XZ_UI_GENERAL,XZ_UI_CONTROLS,XZ_UI_SETTINGS,XZ_UI_EXTRAS};
    int seen[XZ_UI_PAGE_COUNT]={0},rows=0;
    for(int t=0;t<4;t++){
        modern();open_tab(roots[t]);
        /* breadth first over links, one row run per visit, from a fresh copy of the page each time */
        static struct xz_ui queue[96];int head=0,tail=0;queue[tail++]=ui;
        while(head<tail){
            struct xz_ui at=queue[head++];
            if(seen[at.page]++>2)continue;
            ui=at;pthread_mutex_lock(&ui_mutex);menu_publish_locked();pthread_mutex_unlock(&ui_mutex);
            struct xz_shell_menu page=fake_menu;
            for(int r=0;r<page.row_count;r++){
                const struct xz_shell_menu_row *row=&page.row[r];
                /* games and Doom take the screen: they are proven below, not walked */
                if(row->target.kind==XZ_UI_ARCADE_OPEN||row->target.kind==XZ_UI_WAVE_RIDER_OPEN||row->target.kind==XZ_UI_RHYTHM_LAB_OPEN||
                   row->target.kind==XZ_UI_GLYPH_RUN_OPEN||row->target.kind==XZ_UI_DOOM_START||row->target.kind==XZ_UI_TAKEOVER_TOGGLE||
                   row->target.kind==XZ_UI_SET_INTERFACE||row->target.kind==XZ_UI_PROFILE_SAVE)continue;
                ui=at;fake_menu=page;
                activate(r,row->kind==XZ_SHELL_MENU_TOGGLE||row->kind==XZ_SHELL_MENU_CHOICE?(row->current==0?1:0):-1);
                rows++;
                assert(panel_shut());
                if(ui.page!=at.page&&tail<96)queue[tail++]=ui;
            }
        }
    }
    int pages=0;for(int p=0;p<XZ_UI_PAGE_COUNT;p++)pages+=seen[p]>0;
    assert(pages>=20);
    printf("PASS MODERN MODS: %d rows over %d pages run on the panel's own state; the panel never shows\n",rows,pages);
}

/* A panel control that opens a setup page opens it in the shell's MODS screen, on the tab it lives under. */
static void setups_open_in_shell(void){
    modern();
    struct xz_ui_action a={.kind=XZ_UI_LOCAL_STEM_SETUP_OPEN,.phase=XZ_UI_PRESS,.deck=1};
    apply(NULL,&a,1);
    /* opened from outside MODS: under its own parent, CONTROLS */
    assert(panel_shut()&&fake_shows==1&&fake_menu_tab_root==XZ_UI_CONTROLS&&ui.page==XZ_UI_LOCAL_STEM_SETUP&&ui.deck==1);
    assert(fake_menu.page==XZ_UI_LOCAL_STEM_SETUP&&fake_menu.root==XZ_UI_CONTROLS&&fake_menu.depth==1);
    /* opened from GENERAL's own row inside MODS: under GENERAL, BACK returns there */
    modern();for(int d=0;d<2;d++){model.deck[d].ready&=~(uint32_t)XZ_UI_STEM;model.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;}
    audio_available=0;model.enabled=0;open_tab(XZ_UI_GENERAL);
    activate(row_named("SET UP STANDALONE STEMS"),-1);
    assert(panel_shut()&&ui.page==XZ_UI_LOCAL_STEM_SETUP&&fake_menu.root==XZ_UI_GENERAL&&fake_menu.depth==1);
    back();assert(ui.page==XZ_UI_GENERAL);
    audio_available=1;
    modern();
    a=(struct xz_ui_action){.kind=XZ_UI_UNAVAILABLE,.phase=XZ_UI_PRESS,.index=XZ_UI_STEM};apply(NULL,&a,1);
    assert(panel_shut()&&fake_shows==1&&ui.page==XZ_UI_LOCAL_STEM_SETUP);
    modern();
    a=(struct xz_ui_action){.kind=XZ_UI_PC_SETUP,.phase=XZ_UI_PRESS,.deck=0,.index=XZ_UI_PC_WHY_SOURCE};apply(NULL,&a,1);
    assert(panel_shut()&&fake_shows==1&&ui.page==XZ_UI_PC_STEMS&&fake_menu_tab_root==XZ_UI_CONTROLS);
    modern();
    a=(struct xz_ui_action){.kind=XZ_UI_METRICS_SETUP_OPEN,.phase=XZ_UI_PRESS};apply(NULL,&a,1);
    assert(panel_shut()&&fake_shows==1&&ui.page==XZ_UI_METRICS_SETUP);
    /* every step of the stem setup, inside MODS: never the panel */
    modern();open_tab(XZ_UI_GENERAL);xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);
    for(int step=0;step<3;step++){a=(struct xz_ui_action){.kind=XZ_UI_LOCAL_STEM_SETUP_STEP,.phase=XZ_UI_PRESS,.index=step};apply(NULL,&a,1);assert(panel_shut());}
    /* a REKORDBOX key set to MODS while the VJ takeover holds the screen: the shell's MODS, over it */
    modern();model.fb_takeover=1;__atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);
    pthread_mutex_lock(&ui_mutex);mods_panel_toggle_locked();int eligible=shell_eligible_locked();pthread_mutex_unlock(&ui_mutex);
    assert(panel_shut()&&fake_shows==1&&fake_mods_open&&eligible);
    fake_mods_open=0;
    pthread_mutex_lock(&ui_mutex);eligible=shell_eligible_locked();pthread_mutex_unlock(&ui_mutex);
    assert(!eligible);   /* MODS closed: the takeover shows again */
    model.fb_takeover=0;__atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
    puts("PASS MODERN: stem, PC and metrics setups and MODS over the VJ view open in the shell's MODS, under the tab they belong to");
}

/* A game started from MODS takes the screen and gives it back to the same MODS page. */
static void games_return(void){
    modern();open_tab(XZ_UI_EXTRAS);
    activate(row_named("GAMES"),-1);assert(ui.page==XZ_UI_GAMES&&fake_menu.depth==1);
    activate(row_named("BEAT ARCADE"),-1);
    assert(arcade_present&&touch.visible);   /* the game's own full screen */
    pthread_mutex_lock(&ui_mutex);int eligible=shell_eligible_locked();pthread_mutex_unlock(&ui_mutex);
    assert(!eligible);
    /* the MODS key leaves the game for MODS: the shell's, on the page the game was started from */
    fake_shows=0;
    pthread_mutex_lock(&ui_mutex);mods_panel_toggle_locked();pthread_mutex_unlock(&ui_mutex);
    assert(!arcade_present&&panel_shut()&&fake_shows==1&&fake_menu_tab_root==XZ_UI_EXTRAS);
    puts("PASS a game started from MODS returns to the shell's MODS, never to the panel");
}

/* BACK runs one level up, then closes top-level MODS to its prior screen. */
static void back_one_level(void){
    modern();open_tab(XZ_UI_SETTINGS);
    for(int row=0;row<fake_menu.row_count;row++)assert(strcmp(fake_menu.row[row].label,"KEY SETUP"));
    open_tab(XZ_UI_EXTRAS);
    activate(row_named("NETWORK UPDATES"),-1);assert(ui.page==XZ_UI_UPDATES);
    activate(row_named("NETWORK SETUP"),-1);assert(ui.page==XZ_UI_UPDATE_SETUP&&fake_menu.depth==2);
    back();assert(ui.page==XZ_UI_UPDATES&&panel_shut());
    back();assert(ui.page==XZ_UI_EXTRAS&&panel_shut());
    int closed=fake_closes;
    back();assert(panel_shut()&&!fake_mods_open&&fake_closes==closed+1);
    puts("PASS menu BACK: one page up; top-level MODS closes to the prior screen");
}

/* REKORDBOX set to MODS, reaching the native handler: the shell opens MODS and its tab's page is read at
   once, not on the next input. */
static void rekordbox_mods_reads_page(void){
    modern();xz_ui_navigate(&ui,XZ_UI_DOOM_FILES);
    fake_queue[fake_queued++]=(struct xz_shell_runtime_menu_command){.op=XZ_SHELL_MENU_OPEN,.root=XZ_UI_GENERAL};
    fake_requests|=XZ_SHELL_UI_MENU;
    pthread_mutex_lock(&ui_mutex);int stock=rekordbox_run_locked(XZ_REKORDBOX_MODS);pthread_mutex_unlock(&ui_mutex);
    assert(!stock&&fake_mods_calls==1&&!fake_queued&&ui.page==XZ_UI_GENERAL&&panel_shut());
    puts("PASS REKORDBOX = MODS: the shell's MODS opens with its page read in the same press");
}
/* CLASSIC keeps its panel exactly: the same setup opens the panel, and nothing goes to the shell. */
static void classic_keeps_panel(void){
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);
    xz_ui_init(&ui);fake_shows=0;
    struct xz_ui_action a={.kind=XZ_UI_LOCAL_STEM_SETUP_OPEN,.phase=XZ_UI_PRESS};apply(NULL,&a,1);
    assert(touch.visible&&!fake_shows&&ui.page==XZ_UI_LOCAL_STEM_SETUP);
    touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    puts("PASS CLASSIC: setups open the panel as before");
}

#ifndef XZ_MENU_RUNTIME_MAIN
#define XZ_MENU_RUNTIME_MAIN main
#endif
int XZ_MENU_RUNTIME_MAIN(void){
    existing_runtime_controls();
    fake_menu_on=1;
    menu_walk();
    setups_open_in_shell();
    games_return();
    back_one_level();
    rekordbox_mods_reads_page();
    classic_keeps_panel();
    puts("PASS menu runtime");
    return 0;
}
