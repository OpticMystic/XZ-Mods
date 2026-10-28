/* SPDX-License-Identifier: MIT
 * DESIGN 7.6, every row must work, in both interfaces (one menu): each setting control of the MODS sections and
 * their picture pages, touched as a finger does, reaches the real ui_runtime.c action, the published model changes,
 * and a choice shows lit afterwards. MODERN adds its own look rows (the stand-in shell applies them) and its six
 * stem layouts. Included by test_menu_runtime.c. */
static int fake_route_on,rows_mt_calls;
static int rows_mt_set(void *player,int on){(void)player;(void)on;rows_mt_calls++;return 0;}
struct xz_extra_route xz_extra_decks_route(void){return (struct xz_extra_route){fake_route_on,0};}
/* The connector is test_deck_view_runtime.h's fixture (fixture_route, fixture_rescans): with fake_route_on its
   players run and find no cable; without it NO_ENGINE. */
static void rows_route(int on){
    fake_route_on=on;fixture_route=(struct xz_extra_route_status){0};
    fixture_route.state=on?XZ_EXTRA_ROUTE_NOT_DETECTED:XZ_EXTRA_ROUTE_NO_ENGINE;
    fixture_route.fail_part=on?XZ_EXTRA_ROUTE_PART_CABLE:XZ_EXTRA_ROUTE_PART_NONE;
    snprintf(fixture_route.reason,sizeof fixture_route.reason,"%s",on?"The loopback audio route has not been detected.":
        "The software players are unavailable or have not been started.");
}

/* The sync link's master authority, as sync_link_runtime.c gives the full runtime: with the route available the
   runtime takes the master from it alone. During the walk it names deck 2, as the fixture's keys do; otherwise it
   has none. */
static int rows_master=-1;
int xz_sync_link_runtime_master_observe(struct xz_master_observation *m){
    if(rows_master<0)return 0;
    *m=(struct xz_master_observation){.deck=rows_master,.revision=1,.operator_epoch=1,.observed_ms=xz_game_milliseconds()};
    return 1;
}
/* KEYS has a tab for every deck: analysed tracks on decks 3 and 4 too (as test_key_four_deck.c), so its controls
   can act on the software decks; off again restores the fixture. */
static void rows_software_keys(int on){
    static struct xz_extra_decks saved;
    if(!on){fixture_extra=saved;return;}
    saved=fixture_extra;fixture_extra.route_up=1;
    for(int d=0;d<2;d++){
        struct xz_extra_deck *v=&fixture_extra.deck[d];v->loaded=1;v->known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY_STATE);
        v->key_state=(struct xz_deck_key){.owner={.route_epoch=1,.load_revision=(uint32_t)d+1,.media_generation=11},
            .original={d?7:4,1},.playing={.key={d?7:4,1},.cents=0},.metadata_valid=1,.engine_ready=1,
            .playing_valid=1,.audio_state=XZ_KEY_AUDIO_APPLIED};
    }
}

/* Everything a MODS setting control can change: the published model, the panel's own edit state, the key engine
   (decks 3 and 4 take key requests as player events), and the shell's own parts in MODERN. */
struct row_state {
    int deck_view,rb[2],stem_page,stem_bank,stem_order,pad_feedback,modern_stems,
        key_format,key_feedback,browse_traffic,browse_key_prefix,stem_colors[3],edit_page,edit_pair,adjusting,
        key_requests,player_events,wakes,deck,key_synced[2],mt_calls,stems_overlay,wave_mode,look_wave,look_usb;
    uint32_t enabled,scanning,look_hidden;
    struct xz_jump_settings jump;
};
static struct row_state row_state_now(void){
    struct row_state s;memset(&s,0,sizeof s);
    s.deck_view=model.deck_view;s.rb[0]=model.rekordbox_assign[0];s.rb[1]=model.rekordbox_assign[1];
    s.stem_page=model.stem_page;s.stem_bank=model.stem_bank;s.stem_order=model.stem_order;s.pad_feedback=model.pad_feedback;
    s.modern_stems=model.modern_stems;s.key_format=model.key_format;s.key_feedback=model.key_feedback;
    s.browse_traffic=model.browse_traffic;s.browse_key_prefix=model.browse_key_prefix;
    for(int i=0;i<3;i++)s.stem_colors[i]=model.stem_colors[i];
    s.edit_page=ui.jump_edit_page;s.edit_pair=ui.jump_edit_pair;s.adjusting=ui.adjusting;s.deck=ui.deck;
    s.key_synced[0]=model.deck[0].key_synced;s.key_synced[1]=model.deck[1].key_synced;s.mt_calls=rows_mt_calls;s.stems_overlay=model.stems_overlay;s.wave_mode=model.wave_mode;
    s.key_requests=fixture_key_requests;s.player_events=fixture_event_count;s.wakes=fixture_rescans;s.enabled=model.enabled;s.scanning=model.route.state==XZ_UI_ROUTE_SCANNING;s.jump=model.jump;
    s.look_wave=model.modern_look.wave_color;s.look_usb=model.modern_look.usb_colors;s.look_hidden=model.modern_look.hidden;
    return s;
}
/* The cable's engine present but no cable, analysed keys on both decks (deck 2 master): every control can act. */
static void rows_fixture(int interface){
    rekordbox_reset(interface,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);
    started=audio_available=requested_stems=1;model.enabled=XZ_UI_STEM;fixture_prepared=1;
    fake_look=(struct xz_ui_modern_look){0};
    rows_route(1);model.deck_view=0;key_available=1;native_master_tempo_on=rows_mt_set;
    memset(key_sync,0,sizeof key_sync);
    for(int d=0;d<2;d++){
        model.deck[d].ready|=XZ_UI_GATE|XZ_UI_SMART|XZ_UI_KEY|XZ_UI_KEYSYNC;model.deck[d].key_synced=0;
        fixture_key_valid[d]=1;fixture_key_cents[d]=0;
        fixture_keys[d]=(struct xz_key_snapshot){.original={d?9:0,1},.epoch=(uint32_t)(10+d),.loaded=1,.tempo_ratio=1,.master=d==1,.master_tempo=1};
    }
}
/* A control the DJ sets on its page: not a link or page (the walk in test_ui.c proves those), not an explanation,
   not the interface switch (its own suite), not UTILITY (it leaves MODS; utility_cell proves it). */
static int setting_control(const struct xz_ui_widget *w){
    switch(w->kind){
    case XZ_UI_PANEL:case XZ_UI_FEATURE_LINK:case XZ_UI_INFO:case XZ_UI_HOME_SETUP:case XZ_UI_FEATURE_SETUP_OPEN:
    case XZ_UI_LOCAL_STEM_SETUP_OPEN:case XZ_UI_SET_INTERFACE:case XZ_UI_CLOSE:case XZ_UI_BACK:case XZ_UI_PROFILE:
    case XZ_UI_OPEN_UTILITY:case XZ_UI_NONE:return 0;
    default:return 1;
    }
}
/* One of several: a touch on the lit one changes nothing, and the touched one shows lit afterwards. */
static int one_of_several(const struct xz_ui_widget *w){
    enum xz_ui_action_kind k=w->kind;
    if(k==XZ_UI_SHELL_LOOK)return w->index/8==XZ_UI_LOOK_WAVE_COLOR||w->index/8==XZ_UI_LOOK_USB_COLORS;
    return k==XZ_UI_DECK_VIEW||k==XZ_UI_KEY_FORMAT||k==XZ_UI_WAVE_MODE||k==XZ_UI_STEM_PAGE||k==XZ_UI_STEM_BANK||k==XZ_UI_PAD_FEEDBACK||
        k==XZ_UI_JUMP_ENABLE||k==XZ_UI_JUMP_EDIT_PAGE||k==XZ_UI_JUMP_SIZE||k==XZ_UI_REKORDBOX_ASSIGN||k==XZ_UI_STEM_COLOR||k==XZ_UI_DECK||
        k==XZ_UI_JUMP_PAIR||k==XZ_UI_STEMS_OVERLAY;
}
static int rows_checked[2];
static void every_control_on(int interface,int page){
    rows_fixture(interface);ui.page=(enum xz_ui_page)page;
    if(page==XZ_UI_KEY_SETUP)rows_software_keys(1);
    pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w);
    for(size_t i=0;i<n;i++){
        if(!setting_control(&w[i])||(w[i].kind==XZ_UI_PANEL&&xz_ui_page_is_top(w[i].index)))continue;
        if(xz_ui_page_is_top(page)&&w[i].kind==XZ_UI_DECK)continue;
        struct xz_ui_widget fresh[XZ_UI_WIDGETS];size_t k=xz_ui_layout(&ui,&model,fresh),at=k;
        for(size_t j=0;j<k;j++)if(fresh[j].kind==w[i].kind&&fresh[j].index==w[i].index&&fresh[j].deck==w[i].deck)at=j;
        if(at==k)continue;   /* an earlier control changed the page's controls */
        struct xz_ui_look look=xz_ui_widget_look(&ui,&model,&fresh[at]);
        if(!look.available)continue;   /* it says why in the dock (test_ui.c): STOCK page 1's pads, for one */
        if(one_of_several(&fresh[at])&&look.selected)continue;
        struct row_state before=row_state_now();
        struct xz_ui_action a[XZ_UI_ACTIONS];int cx=fresh[at].x+fresh[at].w/2,cy=fresh[at].y+fresh[at].h/2;
        size_t got=xz_ui_touch(&ui,&model,cx,cy,1,a);apply(NULL,a,got);
        got=xz_ui_touch(&ui,&model,cx,cy,0,a);apply(NULL,a,got);
        pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);
        struct row_state after=row_state_now();
        if(!memcmp(&before,&after,sizeof before)){fprintf(stderr,"%s control %d/%d on page %d changed nothing\n",interface?"MODERN":"CLASSIC",(int)fresh[at].kind,fresh[at].index,page);assert(0);}
        if(one_of_several(&fresh[at])&&fresh[at].kind!=XZ_UI_JUMP_PAIR&&(fresh[at].kind!=XZ_UI_STEMS_OVERLAY||interface)){
            struct xz_ui_widget again[XZ_UI_WIDGETS];size_t m2=xz_ui_layout(&ui,&model,again);int lit=-1;
            for(size_t j=0;j<m2;j++)if(again[j].kind==fresh[at].kind&&again[j].index==fresh[at].index&&again[j].deck==fresh[at].deck)lit=xz_ui_widget_look(&ui,&model,&again[j]).selected;
            if(lit==0){fprintf(stderr,"%s control %d/%d on page %d is not lit after the touch\n",interface?"MODERN":"CLASSIC",(int)fresh[at].kind,fresh[at].index,page);assert(0);}
        }
        ui.page=(enum xz_ui_page)page;ui.adjusting=0;
        assert(!touch.visible||ui.page==(enum xz_ui_page)page);
        rows_checked[interface!=0]++;
    }
    if(page==XZ_UI_KEY_SETUP)rows_software_keys(0);
}
static void every_row_works(void){
    rows_players=1;rows_master=1;
    static const int pages[]={XZ_UI_PLAY,XZ_UI_BEAT_JUMP,XZ_UI_KEY_SETUP,XZ_UI_BUTTONS,XZ_UI_STEM_SECTION,XZ_UI_STEM_PADS,
        XZ_UI_STEM_COLORS,XZ_UI_SCREEN,XZ_UI_SCREEN_LOOK,XZ_UI_FOUR_DECKS};
    for(int interface=0;interface<2;interface++)
        for(size_t i=0;i<sizeof pages/sizeof *pages;i++)every_control_on(interface?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC,pages[i]);
    rows_route(0);rows_players=0;rows_master=-1;
    for(int d=0;d<2;d++)fixture_key_valid[d]=0;
    native_master_tempo_on=NULL;fake_look=(struct xz_ui_modern_look){0};
    assert(rows_checked[0]>=60&&rows_checked[1]>rows_checked[0]);
    printf("PASS every control works in both interfaces (DESIGN 7.6): %d CLASSIC and %d MODERN touches reach the runtime, change the published model and light the confirmed choice\n",
        rows_checked[0],rows_checked[1]);
}
