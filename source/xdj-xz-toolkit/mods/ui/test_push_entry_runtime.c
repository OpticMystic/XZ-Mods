/* SPDX-License-Identifier: MIT
 * The rotary selector's push through the real ui_runtime.c hook, driven the way stock drives it: a model of
 * UiKey_KeyPush (0xe14c0) keeps the selector's entry of the browse key table {flags, 0, state, handler} and
 * BrowseKeyProcessing (0xe15e8) calls the handler for an entry whose state is not 0 (encoder_push.h).
 * The owner's report: BROWSE, then a push of the selector, did nothing. A push the mods took left the entry's
 * state set, so stock started no pass for the next press. Built by verify_push_entry_runtime.py; with
 * -DOLD_PUSH_API it builds against the runtime before the fix, where the first check fails. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#define XZ_MENU_RUNTIME_MAIN retained_menu_runtime_main
#include "test_menu_runtime.c"
#undef main

static uint32_t table[74][4];                 /* stock's browse key table: kind 8 is the selector */
static int seen[64],seen_count,shell_owns=1,stock_calls;
#ifdef OLD_PUSH_API
int xz_shell_runtime_encoder_push(int eligible,int theme,uint32_t flags,uint32_t state){
    (void)eligible;(void)theme;
    if(shell_owns&&seen_count<64)seen[seen_count++]=flags==1&&state==2?1:flags==1&&state==3?2:3;
    return shell_owns;
}
#else
int xz_shell_runtime_selector_push(int eligible,int theme,int phase){
    (void)eligible;(void)theme;
    if(shell_owns&&seen_count<64)seen[seen_count++]=phase==XZ_PUSH_DOWN?1:phase==XZ_PUSH_HOLD?2:phase==XZ_PUSH_NONE?0:3;
    return shell_owns;
}
#endif
static int stock_push(void *a,void *b,void *c,void *d){(void)a;(void)b;(void)c;(void)d;stock_calls++;table[8][2]=0;return 1;}
/* UiKey_KeyPush for kind 8: flags = press|hold; a press sets 2 from 0, a hold sets 3 when set; 1 = a pass. */
static int key_push(int press,int hold){
    uint32_t *e=table[8];e[1]=0;
    if(press){int pass=!e[2];if(pass)e[2]=2;e[0]=1;return pass;}
    if(hold){int pass=!!e[2];if(pass)e[2]=3;e[0]=1;return pass;}
    e[0]=0;return e[2]!=0;
}
static void key_pass(void){if(table[8][2])encoder_push_hook(table[8],NULL,NULL,NULL);}
static void pe_click(void){if(key_push(1,0))key_pass();if(key_push(0,0))key_pass();}
static __attribute__((unused)) void pe_held(void){if(key_push(1,0))key_pass();if(key_push(0,1))key_pass();if(key_push(0,0))key_pass();}
static int pe_count(int kind){int n=0;for(int i=0;i<seen_count;i++)n+=seen[i]==kind;return n;}
static void pe_modern(void){
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);
    started=1;play_key.open=0;touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    memset(table,0,sizeof table);seen_count=0;shell_owns=1;stock_calls=0;
    stock_encoder_push=stock_push;
#ifndef OLD_PUSH_API
    push_entry_address=(uintptr_t)table[8];
#endif
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    /* The reported flow at the hook: two clicks with no turn between reach the shell as two presses, and
       the entry is left as stock's handler leaves it. */
    pe_modern();
    pe_click();
    printf("first click: shell saw %d phases, entry state %u\n",seen_count,(unsigned)table[8][2]);
    pe_click();
    printf("second click: shell saw %d phases (%d presses), entry state %u\n",seen_count,pe_count(1),(unsigned)table[8][2]);
    assert(pe_count(1)==2&&pe_count(3)==2&&table[8][2]==0&&stock_calls==0);
    for(int i=0;i<10;i++)pe_click();
    assert(pe_count(1)==12&&pe_count(3)==12&&table[8][2]==0);
#ifndef OLD_PUSH_API
    /* A held push: press, one hold, its release; nothing repeated, the entry cleared. */
    pe_modern();pe_held();
    assert(seen_count==3&&seen[0]==1&&seen[1]==2&&seen[2]==3&&table[8][2]==0);
    /* A pass of the same contact again (another key's pass while the selector is down) reads nothing. */
    pe_modern();
    if(key_push(1,0))key_pass();
    key_pass();key_pass();
    assert(seen_count==3&&seen[0]==1&&seen[1]==0&&seen[2]==0&&table[8][2]==XZ_PUSH_MARK_DOWN);
    if(key_push(0,0))key_pass();
    assert(table[8][2]==0);
    /* The screen changed owner between the press and the release: the release is still the mods',
       never a half contact for stock's hidden browser, and the entry is cleared. */
    pe_modern();
    if(key_push(1,0))key_pass();
    shell_owns=0;
    if(key_push(0,0))key_pass();
    assert(stock_calls==0&&table[8][2]==0);
    /* A press the mods did not take goes to stock, untouched. */
    pe_modern();shell_owns=0;pe_click();
    assert(stock_calls==1&&seen_count==0);
    /* The CLASSIC MODS panel acts on the press only: a held push opens one child, the hold and release
       never activate again. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);
    memset(table,0,sizeof table);stock_calls=0;push_entry_address=(uintptr_t)table[8];
    touch.visible=1;ui.page=XZ_UI_EXTRAS;ui.focus_kind=XZ_UI_PANEL;ui.focus_index=XZ_UI_GAMES;ui.focus_deck=-1;
    pe_held();
    assert(ui.page==XZ_UI_GAMES&&ui.history_count==1&&table[8][2]==0&&stock_calls==0);
    /* Only stock's own entry is ever written. */
    uint32_t other[4]={1,0,2,0};push_entry_address=(uintptr_t)table[8];touch.visible=0;
    model.ui_interface=XZ_UI_INTERFACE_MODERN;shell_owns=1;
    encoder_push_hook(other,NULL,NULL,NULL);
    assert(other[2]==2);
#endif
    puts("PASS selector push through the runtime hook: every click reaches the shell, holds once, the entry left as stock leaves it");
    return 0;
}
