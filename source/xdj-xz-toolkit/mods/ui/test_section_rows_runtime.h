/* SPDX-License-Identifier: MIT
 * DESIGN 7.6, every row must work: each setting row of the MODS sections and their picture pages, pushed
 * or picked through the shell's MENU command path, reaches the real ui_runtime.c action, the published
 * model changes, and the page published next shows the confirmed value. Included by test_menu_runtime.c. */
static int fake_route_on,fake_wakes,rows_mt_calls;
static int rows_mt_set(void *player,int on){(void)player;(void)on;rows_mt_calls++;return 0;}
struct xz_extra_route xz_extra_decks_route(void){return (struct xz_extra_route){fake_route_on,0};}
void xz_extra_decks_wake(void){fake_wakes++;}

/* Everything a MODS setting row can change: the published model, the panel's own edit state, the key engine. */
struct row_state {
    int deck_view,deck_view_button,rb[2],stem_page,stem_bank,stem_order,pad_feedback,modern_stems,
        key_format,key_feedback,browse_traffic,browse_key_prefix,stem_colors[3],edit_page,edit_pair,adjusting,
        key_requests,wakes,deck,key_synced[2],mt_calls,stems_overlay,wave_mode;
    uint32_t enabled,scanning;
    struct xz_jump_settings jump;
};
static struct row_state row_state_now(void){
    struct row_state s;memset(&s,0,sizeof s);
    s.deck_view=model.deck_view;s.deck_view_button=model.deck_view_button;s.rb[0]=model.rekordbox_assign[0];s.rb[1]=model.rekordbox_assign[1];
    s.stem_page=model.stem_page;s.stem_bank=model.stem_bank;s.stem_order=model.stem_order;s.pad_feedback=model.pad_feedback;
    s.modern_stems=model.modern_stems;s.key_format=model.key_format;s.key_feedback=model.key_feedback;
    s.browse_traffic=model.browse_traffic;s.browse_key_prefix=model.browse_key_prefix;
    for(int i=0;i<3;i++)s.stem_colors[i]=model.stem_colors[i];
    s.edit_page=ui.jump_edit_page;s.edit_pair=ui.jump_edit_pair;s.adjusting=ui.adjusting;s.deck=ui.deck;
    s.key_synced[0]=model.deck[0].key_synced;s.key_synced[1]=model.deck[1].key_synced;s.mt_calls=rows_mt_calls;s.stems_overlay=model.stems_overlay;s.wave_mode=model.wave_mode;
    s.key_requests=fixture_key_requests;s.wakes=fake_wakes;s.enabled=model.enabled;s.scanning=model.route.scanning_since;s.jump=model.jump;
    return s;
}
static void publish_now(void){pthread_mutex_lock(&ui_mutex);refresh_local();menu_publish_locked();pthread_mutex_unlock(&ui_mutex);}
/* A row the DJ sets on this page: not a link (menu_walk proves links), not an explanation, not the shell's own
   look rows (shell_runtime.c applies those), not the interface switch or the DJ profile pick (their own suites). */
static int setting_row(const struct xz_shell_menu *p,const struct xz_shell_menu_row *r){
    if(r->kind==XZ_SHELL_MENU_LINK||r->kind==XZ_SHELL_MENU_INFO)return 0;
    /* a row of choices runs its choices' targets */
    const struct xz_shell_menu_target *t=r->choice_count?&p->choice[r->first_choice].target:&r->target;
    switch(t->kind){
    case XZ_UI_SHELL_LOOK:case XZ_UI_SET_INTERFACE:case XZ_UI_SET_MODERN_THEME:case XZ_UI_PROFILE_PICK:case XZ_UI_INFO:
    case XZ_UI_PANEL:case XZ_UI_FEATURE_LINK:case XZ_UI_FEATURE_SETUP_OPEN:case XZ_UI_LOCAL_STEM_SETUP_OPEN:case XZ_UI_DECK:return 0;
    default:return 1;
    }
}
static int rows_checked_74,rows_unavailable_74;
/* MODERN, the cable's engine present but no cable, analysed keys on both decks (deck 2 master): every row can act. */
static void rows_fixture(void){
    modern();fake_route_on=1;model.deck_view=0;model.route.scanning_since=0;key_available=1;native_master_tempo_on=rows_mt_set;
    memset(key_sync,0,sizeof key_sync);
    for(int d=0;d<2;d++){
        model.deck[d].ready|=XZ_UI_GATE|XZ_UI_SMART|XZ_UI_KEY|XZ_UI_KEYSYNC;model.deck[d].key_synced=0;
        fixture_key_valid[d]=1;fixture_key_cents[d]=0;
        fixture_keys[d]=(struct xz_key_snapshot){.original={d?9:0,1},.epoch=(uint32_t)(10+d),.loaded=1,.tempo_ratio=1,.master=d==1,.master_tempo=1};
    }
}
static void every_row_on(int root,int page){
    rows_fixture();
    open_tab(root);
    if(page!=root){pthread_mutex_lock(&ui_mutex);xz_ui_navigate(&ui,(enum xz_ui_page)page);pthread_mutex_unlock(&ui_mutex);}
    publish_now();
    struct xz_ui at=ui;struct xz_shell_menu shown=fake_menu;
    assert(shown.page==page);
    for(int r=0;r<shown.row_count;r++){
        const struct xz_shell_menu_row *row=&shown.row[r];
        if(!setting_row(&shown,row))continue;
        if(!row->available){assert(row->reason[0]);rows_unavailable_74++;printf("  unavailable here, says why: %s (page %d): %s\n",row->label,page,row->reason);continue;}
        /* a choice not lit now; a part whose own control is not already the selected one */
        int choice=-1;
        if(row->kind==XZ_SHELL_MENU_CHOICE||row->kind==XZ_SHELL_MENU_TOGGLE){
            for(int c=0;c<row->choice_count&&choice<0;c++)if(c!=row->current&&shown.choice[row->first_choice+c].available)choice=c;
            if(choice<0)continue;
        }else if(row->kind==XZ_SHELL_MENU_PART&&row->target.kind!=XZ_UI_JUMP_PAIR&&row->target.kind!=XZ_UI_ROUTE_RESCAN){
            struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&at,&model,w);int selected=0;
            for(size_t i=0;i<n;i++)if((int)w[i].kind==row->target.kind&&w[i].index==row->target.index)selected=xz_ui_widget_look(&at,&model,&w[i]).selected;
            if(selected)continue;
        }
        ui=at;fake_menu=shown;
        struct row_state before=row_state_now();
        activate(r,choice);
        publish_now();
        struct row_state after=row_state_now();
        if(!memcmp(&before,&after,sizeof before)){fprintf(stderr,"row %s on page %d changed nothing\n",row->label,page);assert(0);}
        /* the page published next shows the confirmed value */
        if(choice>=0){
            int found=0;
            for(int k=0;k<fake_menu.row_count;k++)if(fake_menu.row[k].key==row->key){
                found=1;
                if(fake_menu.row[k].current!=choice){fprintf(stderr,"row %s shows %d after picking %d\n",row->label,fake_menu.row[k].current,choice);assert(0);}
            }
            assert(found);
        }
        assert(panel_shut());
        rows_checked_74++;
    }
}
/* CLASSIC: the panel's own touch on each setting control, through the same apply(). */
static int classic_setting(const struct xz_ui_widget *w){
    switch(w->kind){
    case XZ_UI_PANEL:case XZ_UI_FEATURE_LINK:case XZ_UI_INFO:case XZ_UI_HOME_SETUP:case XZ_UI_FEATURE_SETUP_OPEN:
    case XZ_UI_LOCAL_STEM_SETUP_OPEN:case XZ_UI_SET_INTERFACE:case XZ_UI_CLOSE:case XZ_UI_BACK:case XZ_UI_PROFILE:case XZ_UI_NONE:return 0;
    default:return 1;
    }
}
static int classic_choice(enum xz_ui_action_kind k){
    return k==XZ_UI_DECK_VIEW||k==XZ_UI_KEY_FORMAT||k==XZ_UI_WAVE_MODE||k==XZ_UI_STEM_PAGE||k==XZ_UI_STEM_BANK||k==XZ_UI_PAD_FEEDBACK||
        k==XZ_UI_JUMP_ENABLE||k==XZ_UI_JUMP_EDIT_PAGE||k==XZ_UI_JUMP_SIZE||k==XZ_UI_REKORDBOX_ASSIGN||k==XZ_UI_STEM_COLOR||k==XZ_UI_DECK||
        k==XZ_UI_JUMP_PAIR||k==XZ_UI_STEMS_OVERLAY;
}
static int classic_checked;
static void every_control_on_classic(int page){
    rows_fixture();
    rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);
    xz_ui_init(&ui);ui.page=(enum xz_ui_page)page;
    pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&model,w);
    for(size_t i=0;i<n;i++){
        if(!classic_setting(&w[i])||(w[i].kind==XZ_UI_PANEL&&xz_ui_page_is_top(w[i].index)))continue;
        if(xz_ui_page_is_top(page)&&w[i].kind==XZ_UI_DECK)continue;
        struct xz_ui_widget fresh[XZ_UI_WIDGETS];size_t k=xz_ui_layout(&ui,&model,fresh),at=k;
        for(size_t j=0;j<k;j++)if(fresh[j].kind==w[i].kind&&fresh[j].index==w[i].index&&fresh[j].deck==w[i].deck)at=j;
        if(at==k)continue;   /* an earlier control changed the page's controls */
        struct xz_ui_look look=xz_ui_widget_look(&ui,&model,&fresh[at]);
        if(!look.available)continue;
        if(classic_choice(fresh[at].kind)&&look.selected)continue;
        struct row_state before=row_state_now();
        struct xz_ui_action a[XZ_UI_ACTIONS];int cx=fresh[at].x+fresh[at].w/2,cy=fresh[at].y+fresh[at].h/2;
        size_t got=xz_ui_touch(&ui,&model,cx,cy,1,a);apply(NULL,a,got);
        got=xz_ui_touch(&ui,&model,cx,cy,0,a);apply(NULL,a,got);
        pthread_mutex_lock(&ui_mutex);refresh_local();pthread_mutex_unlock(&ui_mutex);
        struct row_state after=row_state_now();
        if(!memcmp(&before,&after,sizeof before)){fprintf(stderr,"CLASSIC control %d/%d on page %d changed nothing\n",(int)fresh[at].kind,fresh[at].index,page);assert(0);}
        if(classic_choice(fresh[at].kind)&&fresh[at].kind!=XZ_UI_STEMS_OVERLAY&&fresh[at].kind!=XZ_UI_JUMP_PAIR){
            struct xz_ui_widget again[XZ_UI_WIDGETS];size_t m2=xz_ui_layout(&ui,&model,again);int lit=-1;
            for(size_t j=0;j<m2;j++)if(again[j].kind==fresh[at].kind&&again[j].index==fresh[at].index&&again[j].deck==fresh[at].deck)lit=xz_ui_widget_look(&ui,&model,&again[j]).selected;
            if(lit==0){fprintf(stderr,"CLASSIC control %d/%d on page %d is not lit after the touch\n",(int)fresh[at].kind,fresh[at].index,page);assert(0);}
        }
        ui.page=(enum xz_ui_page)page;ui.adjusting=0;
        assert(!touch.visible||ui.page==(enum xz_ui_page)page);
        classic_checked++;
    }
}
static void every_row_works(void){
    rows_players=1;
    static const int classic_pages[]={XZ_UI_PLAY,XZ_UI_BEAT_JUMP,XZ_UI_KEY_SETUP,XZ_UI_BUTTONS,XZ_UI_STEM_SECTION,XZ_UI_STEM_PADS,
        XZ_UI_STEM_COLORS,XZ_UI_SCREEN,XZ_UI_FOUR_DECKS};
    for(size_t i=0;i<sizeof classic_pages/sizeof *classic_pages;i++)every_control_on_classic(classic_pages[i]);
    assert(classic_checked>=60);
    printf("PASS every CLASSIC control works (DESIGN 7.6): %d touches reach the runtime, change the published model and light the confirmed choice\n",classic_checked);
    every_row_on(XZ_UI_PLAY,XZ_UI_PLAY);
    every_row_on(XZ_UI_PLAY,XZ_UI_BEAT_JUMP);
    every_row_on(XZ_UI_PLAY,XZ_UI_KEY_SETUP);
    every_row_on(XZ_UI_STEM_SECTION,XZ_UI_STEM_SECTION);
    every_row_on(XZ_UI_STEM_SECTION,XZ_UI_STEM_PADS);
    every_row_on(XZ_UI_STEM_SECTION,XZ_UI_STEM_COLORS);
    every_row_on(XZ_UI_SCREEN,XZ_UI_SCREEN);
    every_row_on(XZ_UI_FOUR_DECKS,XZ_UI_FOUR_DECKS);
    fake_route_on=0;model.route.scanning_since=0;rows_players=0;
    for(int d=0;d<2;d++)fixture_key_valid[d]=0;
    native_master_tempo_on=NULL;
    assert(rows_checked_74>=40);
    printf("PASS every row works (DESIGN 7.6): %d setting rows reach the runtime, change the published model and show the confirmed value; %d unavailable rows say why\n",
        rows_checked_74,rows_unavailable_74);
}
