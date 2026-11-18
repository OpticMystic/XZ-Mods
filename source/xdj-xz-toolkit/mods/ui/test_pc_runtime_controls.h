/* SPDX-License-Identifier: MIT */
static struct xz_ui_widget fixture_pc_widget(int deck,int role){
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];
    size_t n=xz_ui_inline_layout(&inline_ui,&model,536,XZ_WAVE_INLINE_HEIGHT,widgets);
    for(size_t i=0;i<n;i++)if(widgets[i].deck==deck&&widgets[i].kind==XZ_UI_PC_TOGGLE&&widgets[i].index==role)return widgets[i];
    assert(!"PC role missing");return widgets[0];
}
static void fixture_pc_contact(void *self,int deck,int role,int down,int drag){
    struct xz_ui_widget w=fixture_pc_widget(deck,role);
    unsigned y=(unsigned)(deck?XZ_WAVE_SECOND_CONTROL_Y:XZ_WAVE_FIRST_CONTROL_Y)+(unsigned)w.h/2;
    contact(self,(unsigned)(131+w.x+w.w/2+drag),y,down);
}
static void fixture_pc_page_tap(enum xz_ui_action_kind kind,int index){
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];struct xz_ui_action actions[XZ_UI_ACTIONS];
    size_t n=xz_ui_layout(&ui,&model,widgets);
    for(size_t i=0;i<n;i++)if(widgets[i].kind==kind&&widgets[i].index==index){
        assert(xz_ui_touch(&ui,&model,widgets[i].x+10,widgets[i].y+10,1,actions)==1);
        apply(NULL,actions,1);xz_ui_touch(&ui,&model,0,0,0,actions);return;
    }
    assert(!"PC setup action missing");
}
static void fixture_pc_reply(unsigned target,unsigned role,int on){
    uint8_t message[3]={(uint8_t)(0x90+12+target),(uint8_t)(0x70+role),(uint8_t)(on?127:0)};
    pc_midi_hook(NULL,NULL,message);
}
static void test_pc_runtime_controls(void *self){
    struct xz_ui_model saved_model=model;struct xz_ui saved_ui=ui,saved_inline=inline_ui;
    struct xz_stem_levels local_audio[2]={applied[0],applied[1]};
    unsigned local_muted[2]={model.deck[0].muted,model.deck[1].muted};
    float local_levels[2][3];memcpy(local_levels[0],model.deck[0].levels,sizeof(local_levels[0]));memcpy(local_levels[1],model.deck[1].levels,sizeof(local_levels[1]));
    int local_bypass[2]={model.deck[0].bypass,model.deck[1].bypass};
    int stock_before=stock_calls;
    fixture_pc_notes=fixture_pc_stock=0;pc_observing=1;fixture_pc_roles=11;
    memset(pc_map_confirmed,0,sizeof(pc_map_confirmed));memset(pc_test,0,sizeof(pc_test));
    xz_pc_stems_init(&pc_stems,xz_pc_stems_native_sink());stock_pc_midi=fixture_pc_stock_callback;
    fixture_pc_source[0]=(struct xz_pc_stem_source){.pc=1,.slot=0,.linked=1,.rekordbox=1,.loaded=1,.epoch=0x1000001};
    pc_refresh_locked();assert(model.deck[0].pc_stems.source==XZ_UI_SOURCE_PC&&model.deck[0].pc_stems.available==11&&!model.deck[0].pc_stems.verified);
    /* The real touch hook opens the wizard and sends nothing before its live test. */
    fixture_pc_contact(self,0,XZ_PC_DRUMS,1,0);fixture_pc_contact(self,0,XZ_PC_DRUMS,0,0);
    assert(ui.page==XZ_UI_PC_STEMS&&touch.visible&&!fixture_pc_notes);
    fixture_pc_page_tap(XZ_UI_PC_CHECK,XZ_UI_PC_STEP_DETECT);
    fixture_pc_page_tap(XZ_UI_PC_CHECK,XZ_UI_PC_STEP_MAPPING);
    assert(model.deck[0].pc_stems.setup_step==XZ_UI_PC_STEP_TEST&&!fixture_pc_notes);
    fixture_pc_page_tap(XZ_UI_PC_TEST,XZ_UI_PC_DRUMS);
    assert(fixture_pc_notes==2&&fixture_pc_channel[0]==12&&fixture_pc_note[0]==0x70&&fixture_pc_pressed[0]&&!fixture_pc_pressed[1]);
    assert(model.deck[0].pc_stems.setup_step==XZ_UI_PC_STEP_CONFIRM&&!pc_stems.deck[0].verified);
    fixture_pc_reply(0,XZ_PC_DRUMS,0);assert(fixture_pc_stock==1&&!pc_stems.deck[0].pending);
    fixture_pc_page_tap(XZ_UI_PC_CONFIRM,1);
    assert(pc_stems.deck[0].verified&&fixture_pc_notes==2&&model.deck[0].pc_stems.setup_step==XZ_UI_PC_STEP_DONE);
    xz_native_touch_visible(&touch,0);
    /* Current 3-stem mode has exactly three buttons and cannot send BASS. */
    {
        struct xz_ui_widget widgets[XZ_UI_WIDGETS];unsigned count=0;
        size_t n=xz_ui_inline_layout(&inline_ui,&model,536,XZ_WAVE_INLINE_HEIGHT,widgets);
        for(size_t i=0;i<n;i++)if(widgets[i].deck==0&&widgets[i].kind==XZ_UI_PC_TOGGLE){assert(widgets[i].index!=XZ_PC_BASS);count++;}
        assert(count==3);
    }
    struct xz_ui_action bass={.kind=XZ_UI_PC_TOGGLE,.deck=0,.index=XZ_PC_BASS,.pc_target=0,.pc_epoch=fixture_pc_source[0].epoch};
    apply(NULL,&bass,1);assert(fixture_pc_notes==2);
    /* Every software target routes through channels 12-15, with no local state mutation. */
    for(unsigned target=0;target<XZ_PC_DECKS;target++){
        unsigned physical=target%2;fixture_pc_source[physical]=(struct xz_pc_stem_source){.pc=1,.slot=(int)target,.linked=1,.rekordbox=1,.loaded=1,.epoch=0x1000001+target};
        pc_map_confirmed[target]=1;pc_refresh_locked();assert(model.deck[physical].pc_stems.target==(int)target&&model.deck[physical].pc_stems.verified);
        for(unsigned role=0;role<XZ_PC_ROLES;role++)if(role!=XZ_PC_BASS){
            unsigned before=fixture_pc_notes;
            fixture_pc_contact(self,(int)physical,(int)role,1,0);assert(fixture_pc_notes==before);
            fixture_pc_contact(self,(int)physical,(int)role,0,0);
            assert(fixture_pc_notes==before+2&&fixture_pc_channel[before]==12+target&&fixture_pc_note[before]==0x70+role);
            assert(fixture_pc_pressed[before]&&!fixture_pc_pressed[before+1]&&pc_stems.deck[target].pending&(1u<<role));
            fixture_pc_contact(self,(int)physical,(int)role,1,0);fixture_pc_contact(self,(int)physical,(int)role,0,0);assert(fixture_pc_notes==before+2);
            fixture_pc_reply(target,role,1);pc_refresh_locked();assert(!(pc_stems.deck[target].pending&(1u<<role))&&pc_stems.deck[target].known&(1u<<role));
        }
        unsigned before=fixture_pc_notes;
        fixture_pc_contact(self,(int)physical,XZ_PC_INST,1,0);fixture_pc_contact(self,(int)physical,XZ_PC_INST,1,30);fixture_pc_contact(self,(int)physical,XZ_PC_INST,0,30);
        assert(fixture_pc_notes==before);
        struct xz_cue_event event={.deck=(int)physical,.pad=0,.pad_page=1,.mode_button=-1};unsigned flags,rgb;int lit;
        assert(!xz_ui_runtime_pad(&event,&flags));event.operation=2;assert(!xz_ui_runtime_pad(&event,&flags));
        model.jump.enabled=model.jump.shift_page=1;jump_state.page[physical]=1;deck_page[physical]=3;
        assert(!xz_ui_runtime_pad_color((int)physical,0,&rgb,&lit));
        model.jump.enabled=model.jump.shift_page=0;jump_state.page[physical]=0;deck_page[physical]=-1;
    }
    /* Full-width epochs reject an old gesture even when its low 24 bits match. */
    unsigned target=2,before=fixture_pc_notes;fixture_pc_source[0].slot=(int)target;pc_map_confirmed[target]=1;pc_refresh_locked();
    struct xz_ui_action stale={.kind=XZ_UI_PC_TOGGLE,.deck=0,.index=XZ_PC_DRUMS,.pc_target=2,.pc_epoch=fixture_pc_source[0].epoch};
    fixture_pc_source[0].epoch+=0x1000000;apply(NULL,&stale,1);assert(fixture_pc_notes==before);
    fixture_pc_contact(self,0,XZ_PC_DRUMS,1,0);fixture_pc_source[0].epoch++;fixture_pc_contact(self,0,XZ_PC_DRUMS,0,0);assert(fixture_pc_notes==before);
    fixture_pc_contact(self,0,XZ_PC_DRUMS,1,0);fixture_pc_source[0].slot=0;fixture_pc_contact(self,0,XZ_PC_DRUMS,0,0);assert(fixture_pc_notes==before);
    fixture_pc_contact(self,0,XZ_PC_DRUMS,1,0);fixture_pc_source[0].pc=0;fixture_pc_contact(self,0,XZ_PC_DRUMS,0,0);assert(fixture_pc_notes==before);
    fixture_pc_source[0].pc=1;fixture_pc_source[0].slot=0;pc_refresh_locked();
    /* Stale local actions cannot change the local engine after a PC source change. */
    struct xz_ui_action local_actions[]={{.kind=XZ_UI_MUTE,.deck=0,.index=0},{.kind=XZ_UI_LEVEL,.deck=0,.index=1,.value=.25f},{.kind=XZ_UI_BYPASS,.deck=0,.value=1}};
    apply(NULL,local_actions,3);
    assert(!memcmp(local_audio,applied,sizeof(local_audio))&&model.deck[0].muted==local_muted[0]&&model.deck[1].muted==local_muted[1]);
    assert(!memcmp(local_levels[0],model.deck[0].levels,sizeof(local_levels[0]))&&!memcmp(local_levels[1],model.deck[1].levels,sizeof(local_levels[1])));
    assert(model.deck[0].bypass==local_bypass[0]&&model.deck[1].bypass==local_bypass[1]);
    /* Disconnection, invalid source, missing track and wrong app are silent. */
    struct xz_ui_action current=stale;current.pc_target=0;current.pc_epoch=fixture_pc_source[0].epoch;
    for(int lock=0;lock<6;lock++){
        fixture_pc_valid[0]=1;fixture_pc_ready=pc_observing=1;
        fixture_pc_source[0].linked=fixture_pc_source[0].loaded=fixture_pc_source[0].rekordbox=1;
        if(lock==0)fixture_pc_source[0].linked=0;else if(lock==1)fixture_pc_source[0].loaded=0;
        else if(lock==2)fixture_pc_source[0].rekordbox=0;else if(lock==3)fixture_pc_valid[0]=0;
        else if(lock==4)pc_observing=0;else fixture_pc_ready=0;
        apply(NULL,&current,1);assert(fixture_pc_notes==before);
    }
    fixture_pc_valid[0]=fixture_pc_ready=pc_observing=1;fixture_pc_source[0].linked=fixture_pc_source[0].loaded=fixture_pc_source[0].rekordbox=1;
    pc_refresh_locked();assert(!pc_stems.deck[0].verified);
    /* A qualified future 4-stem producer can enable BASS explicitly. No replay on refresh. */
    fixture_pc_roles=15;pc_map_confirmed[0]=1;pc_refresh_locked();assert(fixture_pc_notes==before);
    fixture_pc_contact(self,0,XZ_PC_BASS,1,0);fixture_pc_contact(self,0,XZ_PC_BASS,0,0);assert(fixture_pc_notes==before+2&&fixture_pc_note[before]==0x72);
    xz_pc_stems_tick(&pc_stems,pc_stems.deck[0].sent_at[XZ_PC_BASS]+600);assert(!pc_stems.deck[0].pending&&!(pc_stems.deck[0].known&(1u<<XZ_PC_BASS)));
    pc_refresh_locked();assert(fixture_pc_notes==before+2);
    fixture_pc_reply(0,XZ_PC_BASS,0);assert(pc_stems.deck[0].known&(1u<<XZ_PC_BASS));
    assert(stock_calls==stock_before&&!memcmp(local_audio,applied,sizeof(local_audio)));
    fixture_pc_source[0]=(struct xz_pc_stem_source){.slot=-1,.epoch=1};fixture_pc_source[1]=fixture_pc_source[0];
    fixture_pc_roles=11;pc_refresh_locked();pc_observing=0;stock_pc_midi=NULL;
    model=saved_model;ui=saved_ui;inline_ui=saved_inline;xz_native_touch_visible(&touch,0);
    puts("PASS actual touch -> runtime -> captured MIDI on PC1-PC4; 3-stem BASS lock, explicit test qualification, full epoch/target/source cancellation, stock physical pads, feedback and timeout, no local audio change or replay");
}
