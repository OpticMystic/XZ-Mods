/* SPDX-License-Identifier: MIT */
/* DECK VIEW through the real ui_runtime.c: the MODS action, the button next to LINK, a play-screen change
   through ui/layout_cycle.h, saved settings and their restore, the DECKS button switch, RESCAN and the pad
   page per deck. Included once by test_runtime_controls.c after test_rekordbox_runtime.h. */
static void deck_view_runtime_check(void) {
    struct xz_ui_action a={.kind=XZ_UI_DECK_VIEW,.phase=XZ_UI_PRESS,.deck=-1};
    uint64_t revision=settings_revision;
    for(int view=XZ_DECK_VIEW_TWO;view<XZ_DECK_VIEW_COUNT;view++){
        a.value=(float)view;apply(NULL,&a,1);
        assert(model.deck_view==view&&xz_deck_layout_get()==view&&settings_snapshot().deck_view==view);
        assert(model.four_wave_mixer==(view==XZ_DECK_VIEW_MIXER)&&strstr(ui.notice,"DECK VIEW: "));
    }
    assert(settings_revision==revision+3);
    /* Out of range or between values: nothing changes, nothing saves. */
    a.value=3;apply(NULL,&a,1);a.value=1.5f;apply(NULL,&a,1);a.value=-1;apply(NULL,&a,1);
    assert(model.deck_view==XZ_DECK_VIEW_MIXER&&xz_deck_layout_get()==XZ_LAYOUT_MIXER);
    /* A change made on the play screen (xz_deck_layout_set) is saved once, on the next refresh. */
    xz_deck_layout_set(XZ_LAYOUT_TWO);revision=settings_revision;
    refresh_local();assert(model.deck_view==XZ_DECK_VIEW_TWO&&settings_revision==revision+1&&settings_snapshot().deck_view==XZ_DECK_VIEW_TWO);
    refresh_local();assert(settings_revision==revision+1);
    /* Restoring saved settings applies the view without saving it back. */
    struct xz_settings saved=settings_snapshot();saved.deck_view=XZ_DECK_VIEW_FOUR;saved.deck_view_button=0;
    profile_apply_locked(&saved,"deck-view-test");revision=settings_revision;
    assert(model.deck_view==XZ_DECK_VIEW_FOUR&&xz_deck_layout_get()==XZ_LAYOUT_FOUR&&!model.deck_view_button&&!xz_deck_view_button_shown());
    refresh_local();assert(settings_revision==revision&&model.deck_view==XZ_DECK_VIEW_FOUR);
    /* The DECKS button on the play screen. */
    struct xz_ui_action button={.kind=XZ_UI_DECK_VIEW_BUTTON,.phase=XZ_UI_PRESS,.deck=-1,.value=1};
    apply(NULL,&button,1);assert(model.deck_view_button&&xz_deck_view_button_shown()&&settings_snapshot().deck_view_button==1&&settings_revision==revision+1);
    button.value=0;apply(NULL,&button,1);assert(!model.deck_view_button&&!xz_deck_view_button_shown()&&!settings_snapshot().deck_view_button);
    refresh_local();assert(!model.deck_view_button);
    button.value=1;apply(NULL,&button,1);
    /* The retired FOUR-DECK WAVE ORDER action still lands on DECK VIEW. */
    struct xz_ui_action order={.kind=XZ_UI_FOUR_WAVE_ORDER,.phase=XZ_UI_PRESS,.deck=-1,.value=1};
    apply(NULL,&order,1);assert(model.deck_view==XZ_DECK_VIEW_MIXER);
    order.value=0;apply(NULL,&order,1);assert(model.deck_view==XZ_DECK_VIEW_FOUR);
    /* The button next to LINK set to DECK VIEW steps 2 -> 4 (1 2 3 4) -> 4 (3 1 2 4) -> 2 and says so. */
    stock_uikey_rekordbox=fake_stock_rekordbox;stock_global_key=fake_global;rekordbox_shift_held=fake_shift_held;
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_DECK_VIEW,XZ_REKORDBOX_STOCK);
    a.value=XZ_DECK_VIEW_TWO;apply(NULL,&a,1);ui.notice[0]=0;
    static const int steps[3]={XZ_DECK_VIEW_FOUR,XZ_DECK_VIEW_MIXER,XZ_DECK_VIEW_TWO};
    static const char *const said[3]={"4 DECKS, NUMBER ORDER","4 DECKS, MIXER ORDER","2 DECKS"};
    for(int k=0;k<3;k++){
        int handled=rekordbox_contact(0,0);
        if(handled!=1||model.deck_view!=steps[k]||fake_stock_calls||touch.visible)
            fprintf(stderr,"FAIL step %d: handled=%d view=%d stock=%d visible=%d notice=%s\n",k,handled,model.deck_view,fake_stock_calls,touch.visible,ui.notice);
        assert(handled==1&&model.deck_view==steps[k]&&!fake_stock_calls&&!touch.visible);
        assert(strstr(ui.notice,said[k])&&settings_snapshot().deck_view==steps[k]);
    }
    /* MODERN: the shell shows the notice; SHIFT reaches the second slot. */
    rekordbox_reset(XZ_UI_INTERFACE_MODERN,XZ_REKORDBOX_STOCK,XZ_REKORDBOX_DECK_VIEW);
    assert(rekordbox_contact(1,1)==1&&model.deck_view==XZ_DECK_VIEW_FOUR&&!fake_stock_calls&&strstr(fake_notice,"DECK VIEW: 4 DECKS"));
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_INTERFACE,XZ_REKORDBOX_STOCK);fake_shell_running=0;
    stock_uikey_rekordbox=NULL;stock_global_key=NULL;rekordbox_shift_held=rekordbox_shift_native;
    /* RESCAN: without the four-deck connector (this build) nothing scans and the card says why. */
    struct xz_ui_action rescan={.kind=XZ_UI_ROUTE_RESCAN,.phase=XZ_UI_PRESS,.deck=-1};
    apply(NULL,&rescan,1);
    assert(model.route.scanning_since&&model.route.state==XZ_UI_ROUTE_NO_ENGINE&&strstr(model.route.reason,"newer USB loader"));
    assert(xz_deck_caps(&model,2)==0&&xz_deck_cap_reason(&model,3,XZ_DECK_CAN_DISPLAY));
    /* The pad page each deck is on: native decks from the jump state, decks 3 and 4 unknown until reported. */
    jump_state.page[0]=0;jump_state.page[1]=1;refresh_local();
    assert(model.jump_page[0]==0&&model.jump_page[1]==1&&model.jump_page[2]==-1&&model.jump_page[3]==-1);
    jump_state.page[1]=0;
    puts("PASS DECK VIEW: MODS action, play-screen change saved once, restore without save, DECKS button, retired wave order, button next to LINK in both interfaces, RESCAN, pad page per deck");
}
