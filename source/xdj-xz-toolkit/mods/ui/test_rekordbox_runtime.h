/* SPDX-License-Identifier: MIT */
/* REKORDBOX button routing through the real ui_runtime.c hooks, with a stand-in shell that keeps the
   shell runtime's documented contract (shell_runtime.h xz_shrt_rekordbox_verdict, whose own table test is
   in test_shell_runtime.c): while it owns the screen it swallows MODS and STEMS, hands off visibly for the
   stock action and passes the rest untouched. Included once by test_runtime_controls.c. */
static int fake_shell_running, fake_owns, fake_action=-1, fake_hand_offs, fake_mods_keys, fake_mods_calls, fake_swallow;
static unsigned fake_requests;
static char fake_notice[64];
static int fake_shift, fake_stock_calls, fake_global_calls;
static const struct xz_shrt_key_map fake_rekordbox_map={0x202,0,XZ_SHRT_ASSIGNED,-1,-1,-1};
const struct xz_shrt_key_map *xz_shrt_key_lookup(uint16_t code, unsigned channel) { (void)channel;return code==0x202?&fake_rekordbox_map:NULL; }
int xz_shell_runtime_follow_interface(int modern) { fake_shell_running=modern;return 0; }
int xz_shell_runtime_owns(void) { return fake_shell_running&&fake_owns; }
void xz_shell_runtime_rekordbox_action(int action) { fake_action=action; }
int xz_shell_runtime_key(int eligible, int theme, uint16_t code, unsigned channel, unsigned op) {
    (void)theme;(void)channel;
    if(!fake_shell_running||code!=0x202)return 0;
    if((op&15)==0){
        fake_swallow=0;
        if(eligible&&fake_owns){
            if(fake_action==XZ_REKORDBOX_MODS){fake_swallow=1;fake_mods_keys++;}
            else if(fake_action==XZ_REKORDBOX_STEMS){fake_swallow=1;fake_requests|=XZ_SHELL_UI_TOGGLE_STEMS;}
            else if(fake_action==XZ_REKORDBOX_STOCK)fake_hand_offs++;
        }
    }
    return fake_swallow;
}
unsigned xz_shell_runtime_take_ui_requests(void) { unsigned r=fake_requests;fake_requests=0;return r; }
int xz_shell_runtime_mods_key(int eligible, int theme) { (void)theme;if(!fake_shell_running||!eligible)return 0;fake_mods_calls++;return 1; }
void xz_shell_runtime_notice(const char *text) { snprintf(fake_notice,sizeof(fake_notice),"%s",text); }
static int fake_shift_held(void) { return fake_shift; }
static int fake_stock_rekordbox(void *a,void *b,void *c,void *d) {
    const uint32_t *entry=a;(void)b;(void)c;(void)d;
    assert(!entry||entry[0]==1);   /* only ever a press-phase entry */
    fake_stock_calls++;return 0;
}
static void fake_global(void *self,const void *input) { (void)self;(void)input;fake_global_calls++; }
/* One physical REKORDBOX contact: press, then (when the onKey listener passed it) stock's dispatcher
   calls the handler, then the release. Returns what the handler returned, or -1 when it never ran. */
static int rekordbox_contact(int shift_at_press, int shift_at_handler) {
    unsigned char input[16]={0};input[8]=0x02;input[9]=0x02;
    fake_shift=shift_at_press;
    int before=fake_global_calls,result=-1;
    menu_back_key_hook(NULL,input);
    if(fake_global_calls>before){
        fake_shift=shift_at_handler;
        uint32_t entry[4]={1,0,2,0xe0a50u};
        result=uikey_rekordbox_hook(entry,NULL,NULL,NULL);
    }
    input[11]=2;menu_back_key_hook(NULL,input);
    return result;
}
static void rekordbox_reset(int interface, int press, int shift) {
    touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
    model.fb_takeover=0;__atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
    model.ui_interface=interface;model.theme=interface==XZ_UI_INTERFACE_MODERN?XZ_THEME_FLAGSHIP:0;
    fake_shell_running=interface==XZ_UI_INTERFACE_MODERN;fake_owns=fake_shell_running;
    model.rekordbox_assign[0]=press;model.rekordbox_assign[1]=shift;
    fake_action=-1;fake_hand_offs=fake_mods_keys=fake_mods_calls=fake_stock_calls=0;fake_notice[0]=0;fake_requests=0;
    memset(&model.connection,0,sizeof(model.connection));
}
static void rekordbox_runtime_check(void) {
    stock_uikey_rekordbox=fake_stock_rekordbox;stock_global_key=fake_global;rekordbox_shift_held=fake_shift_held;
    int saved_overlay=model.stems_overlay,saved_modern=model.modern_stems,stems;
    /* CLASSIC: the shell is stopped; only the handler hook acts. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_NOTHING);
    assert(rekordbox_contact(0,0)==1&&touch.visible&&!fake_stock_calls);
    assert(rekordbox_contact(0,0)==1&&!touch.visible&&!fake_stock_calls);
    /* SHIFT picks the second slot: NOTHING is consumed and changes nothing. */
    assert(rekordbox_contact(1,1)==1&&!touch.visible&&!fake_stock_calls);
    /* The stock action runs stock's handler once, with the press entry. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_STOCK,XZ_REKORDBOX_MODS);
    assert(rekordbox_contact(0,0)==0&&fake_stock_calls==1&&!touch.visible);
    assert(rekordbox_contact(1,1)==1&&fake_stock_calls==1&&touch.visible);
    /* STEMS steps the stem row layout once, as the STEMS button does. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_STEMS,XZ_REKORDBOX_STOCK);model.stems_overlay=XZ_STEM_LAYOUT_BELOW;
    assert(rekordbox_contact(0,0)==1&&model.stems_overlay!=XZ_STEM_LAYOUT_BELOW&&!fake_stock_calls);
    stems=model.stems_overlay;
    /* VJ.TOOLS SCREEN without a link: MODS opens on VJ.TOOLS with the reason; with one, the view switches. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_VJ_SCREEN,XZ_REKORDBOX_STOCK);
    assert(rekordbox_contact(0,0)==1&&touch.visible&&ui.page==XZ_UI_CONNECTION&&!strcmp(ui.notice,"VJ.TOOLS LINK IS OFF")&&!model.fb_takeover);
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_VJ_SCREEN,XZ_REKORDBOX_STOCK);
    model.connection.ready=model.connection.enabled=model.connection.connected=1;
    assert(rekordbox_contact(0,0)==1&&model.fb_takeover&&__atomic_load_n(&fb_takeover_active,__ATOMIC_ACQUIRE)&&!touch.visible);
    assert(rekordbox_contact(0,0)==1&&!model.fb_takeover);
    /* INTERFACE: CLASSIC -> MODERN starts the shell; the shell then sees the press first. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);
    assert(rekordbox_contact(0,0)==1&&model.ui_interface==XZ_UI_INTERFACE_MODERN&&model.theme==XZ_THEME_FLAGSHIP&&fake_shell_running);
    assert(!fake_stock_calls&&!strcmp(ui.notice,"INTERFACE: FLAGSHIP"));

    /* MODERN, the shell owning the screen. MODS and STEMS are the shell's own: swallowed at the key, the
       handler never runs and stock never sees the key. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STEMS);
    int global=fake_global_calls;
    assert(rekordbox_contact(0,0)==-1&&fake_action==XZ_REKORDBOX_MODS&&fake_mods_keys==1&&!fake_mods_calls&&!touch.visible);
    assert(fake_global_calls==global&&!rekordbox_latch.valid&&!fake_stock_calls);
    /* MODERN steps its own layout (modern_stems): PADS -> BELOW; CLASSIC's stems_overlay stays. */
    model.stems_overlay=XZ_STEM_LAYOUT_BELOW;model.modern_stems=XZ_MODERN_STEMS_PADS;
    assert(rekordbox_contact(1,1)==-1&&fake_action==XZ_REKORDBOX_STEMS&&model.modern_stems==XZ_STEM_LAYOUT_BELOW&&model.stems_overlay==XZ_STEM_LAYOUT_BELOW&&!fake_stock_calls);
    assert(fake_global_calls==global);
    /* The stock action hands off visibly and runs stock once. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_STOCK,XZ_REKORDBOX_NOTHING);
    assert(rekordbox_contact(0,0)==0&&fake_hand_offs==1&&fake_stock_calls==1);
    /* NOTHING passes without a hand-off and the handler consumes it. */
    assert(rekordbox_contact(1,1)==1&&fake_hand_offs==1&&fake_stock_calls==1);
    /* The press is decided once: SHIFT let go before stock's dispatcher runs still means SHIFT + PRESS. */
    assert(rekordbox_contact(1,0)==1&&fake_hand_offs==1&&fake_stock_calls==1);
    /* INTERFACE passes untouched, runs once in the handler and stops the shell. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);
    assert(rekordbox_contact(0,0)==1&&!fake_hand_offs&&!fake_stock_calls&&model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&!fake_shell_running);
    /* VJ.TOOLS SCREEN without a link: the shell's notice says why. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_VJ_SCREEN,XZ_REKORDBOX_STOCK);model.connection.ready=model.connection.enabled=1;
    assert(rekordbox_contact(0,0)==1&&!strcmp(fake_notice,"VJ.TOOLS IS NOT CONNECTED")&&!touch.visible&&!fake_hand_offs);
    /* MODS while the shell has handed the screen to stock: the handler asks the shell to come back with MODS. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);fake_owns=0;
    assert(rekordbox_contact(0,0)==1&&fake_mods_calls==1&&!fake_mods_keys&&!touch.visible&&!fake_stock_calls);
    /* MODS with the MODS panel open over the shell closes the panel. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);touch.visible=1;
    assert(rekordbox_contact(0,0)==1&&!touch.visible&&!fake_mods_calls&&!fake_mods_keys);
    /* Shell absent or not running in MODERN: MODS falls back to the MODS panel, still by hardware. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);fake_shell_running=0;
    assert(rekordbox_contact(0,0)==1&&touch.visible&&!fake_mods_calls);
    /* A stale latch (no onKey press seen for 2 s) is not used: the handler decides itself. */
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_STOCK,XZ_REKORDBOX_NOTHING);
    rekordbox_latch.valid=1;rekordbox_latch.action=XZ_REKORDBOX_NOTHING;rekordbox_latch.at=xz_game_milliseconds()-5000u;
    {uint32_t entry[4]={1,0,2,0};fake_shift=0;assert(uikey_rekordbox_hook(entry,NULL,NULL,NULL)==0&&fake_stock_calls==1&&!rekordbox_latch.valid);}

    /* MODS panel rows: the assignment saves, both slots, with the panel's notice. */
    settings_readonly=0;settings_running=0;settings_pending=0;
    struct xz_ui_action set={.kind=XZ_UI_REKORDBOX_ASSIGN,.phase=XZ_UI_PRESS,.index=1,.value=XZ_REKORDBOX_VJ_SCREEN};apply(NULL,&set,1);
    assert(model.rekordbox_assign[1]==XZ_REKORDBOX_VJ_SCREEN&&!strcmp(ui.notice,"SHIFT + REKORDBOX: VJ.TOOLS SCREEN"));
    set.index=0;set.value=XZ_REKORDBOX_MODS;apply(NULL,&set,1);
    assert(model.rekordbox_assign[0]==XZ_REKORDBOX_MODS&&!strcmp(ui.notice,"REKORDBOX BUTTON: MODS MENU"));
    set.value=XZ_REKORDBOX_ACTIONS;apply(NULL,&set,1);assert(model.rekordbox_assign[0]==XZ_REKORDBOX_MODS);
    set.index=2;set.value=0;apply(NULL,&set,1);assert(model.rekordbox_assign[0]==XZ_REKORDBOX_MODS&&model.rekordbox_assign[1]==XZ_REKORDBOX_VJ_SCREEN);
    struct xz_settings snap=settings_snapshot();
    assert(snap.rekordbox_press==XZ_REKORDBOX_MODS&&snap.rekordbox_shift==XZ_REKORDBOX_VJ_SCREEN&&snap.rekordbox_menu==1);
    /* The retired switch and takeover key map onto the new choices. */
    struct xz_ui_action old={.kind=XZ_UI_REKORDBOX_KEY,.value=0};apply(NULL,&old,1);assert(model.rekordbox_assign[0]==XZ_REKORDBOX_STOCK);
    struct xz_ui_action assign={.kind=XZ_UI_TAKEOVER_ASSIGN,.index=1};model.takeover_assign=XZ_TAKEOVER_ONSCREEN;apply(NULL,&assign,1);
    assert(model.takeover_assign==XZ_TAKEOVER_ONSCREEN);
    /* A profile brings its pair; an out-of-range one falls back to the defaults. */
    struct xz_settings profile=settings_snapshot();profile.rekordbox_press=XZ_REKORDBOX_STEMS;profile.rekordbox_shift=XZ_REKORDBOX_NOTHING;
    profile_apply_locked(&profile,"test");assert(model.rekordbox_assign[0]==XZ_REKORDBOX_STEMS&&model.rekordbox_assign[1]==XZ_REKORDBOX_NOTHING);
    profile.rekordbox_press=9;profile.rekordbox_shift=-1;profile_apply_locked(&profile,"test");
    assert(model.rekordbox_assign[0]==XZ_REKORDBOX_INTERFACE&&model.rekordbox_assign[1]==XZ_REKORDBOX_STOCK);
    (void)stems;
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);fake_shell_running=0;
    model.stems_overlay=saved_overlay;model.modern_stems=saved_modern;stock_uikey_rekordbox=NULL;stock_global_key=NULL;rekordbox_shift_held=rekordbox_shift_native;
    puts("PASS REKORDBOX button: CLASSIC handler only, MODERN shell swallows MODS/STEMS, stock hands off once, others run once, SHIFT slot latched at the press");
}
