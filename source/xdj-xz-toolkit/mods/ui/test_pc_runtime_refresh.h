/* SPDX-License-Identifier: MIT */
static void test_pc_runtime_refresh(void *self){
    struct xz_ui_model saved_model=model;struct xz_ui saved_ui=ui,saved_inline=inline_ui;
    uint32_t saved_generation=fixture_generation;int saved_focus=fixture_focus;
    struct xz_stem_levels before_audio[2]={applied[0],applied[1]};
    xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_visible(&touch,0);
    fixture_pc_notes=0;fixture_pc_roles=11;pc_observing=1;fixture_pc_ready=1;
    memset(pc_map_confirmed,0,sizeof(pc_map_confirmed));
    xz_pc_stems_init(&pc_stems,xz_pc_stems_native_sink());stock_pc_midi=fixture_pc_stock_callback;
    for(unsigned d=0;d<2;d++){
        fixture_pc_source[d]=(struct xz_pc_stem_source){.pc=1,.slot=(int)d,.linked=1,.rekordbox=1,.loaded=1,.epoch=21+d};
        pc_map_confirmed[d]=1;
    }
    pc_refresh_locked();fixture_pc_reply(0,XZ_PC_DRUMS,1);pc_refresh_locked();
    unsigned redraws=fixture_pc_redraws,audio_calls=fixture_audio_status_calls[0]+fixture_audio_status_calls[1];
    unsigned key_calls=fixture_key_snapshot_calls[0]+fixture_key_snapshot_calls[1];
    fixture_pc_contact(self,0,XZ_PC_DRUMS,1,0);
    assert(!fixture_pc_notes&&inline_ui.held_kind==XZ_UI_PC_TOGGLE&&inline_ui.down&&fixture_pc_redraws==redraws+1);
    /* A refresh for another surface cannot cancel a PC gesture using local generations. */
    fixture_generation++;fixture_focus=1;refresh();
    assert(inline_ui.held_kind==XZ_UI_PC_TOGGLE&&inline_ui.down&&!inline_cancelled);
    assert(fixture_audio_status_calls[0]+fixture_audio_status_calls[1]==audio_calls);
    assert(fixture_key_snapshot_calls[0]+fixture_key_snapshot_calls[1]==key_calls);
    fixture_pc_contact(self,0,XZ_PC_DRUMS,0,0);assert(fixture_pc_notes==2);
    assert((model.deck[0].pc_stems.pending&1)&&(model.deck[0].pc_stems.known&1)&&(model.deck[0].pc_stems.active&1));
    /* Pending retains the last confirmed LED fill; feedback is the state transition. */
    uint16_t pixels[536*XZ_WAVE_INLINE_HEIGHT];
    assert(render_inline(NULL,pixels,sizeof(pixels)/sizeof(*pixels),536,536,XZ_WAVE_INLINE_HEIGHT));
    struct xz_ui_widget drums=fixture_pc_widget(0,XZ_PC_DRUMS);
    uint16_t lit=pixels[(drums.y+8)*536+drums.x+2];
    fixture_pc_reply(0,XZ_PC_DRUMS,1);pc_refresh_locked();
    assert(render_inline(NULL,pixels,sizeof(pixels)/sizeof(*pixels),536,536,XZ_WAVE_INLINE_HEIGHT));
    assert(pixels[(drums.y+8)*536+drums.x+2]==lit);
    redraws=fixture_pc_redraws;fixture_pc_reply(0,XZ_PC_DRUMS,0);assert(fixture_pc_redraws==redraws+1);
    pc_refresh_locked();assert(!(model.deck[0].pc_stems.pending&1)&&!(model.deck[0].pc_stems.active&1));
    redraws=fixture_pc_redraws;int stock_feedback=fixture_pc_stock;
    fixture_pc_reply(0,XZ_PC_DRUMS,0);
    assert(fixture_pc_stock==stock_feedback+1&&fixture_pc_redraws==redraws);
    /* The provider's current mode, rather than the cached mask, decides sendability. */
    fixture_pc_roles=15;pc_refresh_locked();
    struct xz_ui_action bass={.kind=XZ_UI_PC_TOGGLE,.deck=0,.index=XZ_PC_BASS,.pc_target=0,.pc_epoch=21};
    unsigned notes=fixture_pc_notes;fixture_pc_roles=11;apply(NULL,&bass,1);
    assert(fixture_pc_notes==notes&&model.deck[0].pc_stems.available==11);
    fixture_pc_roles=15;pc_refresh_locked();
    fixture_pc_contact(self,0,XZ_PC_INST,1,0);fixture_pc_roles=11;
    fixture_pc_contact(self,0,XZ_PC_INST,0,0);assert(fixture_pc_notes==notes);
    fixture_pc_roles=15;pc_refresh_locked();
    fixture_pc_contact(self,0,XZ_PC_BASS,1,0);fixture_pc_contact(self,0,XZ_PC_BASS,0,0);
    assert(fixture_pc_notes==notes+2&&fixture_pc_note[notes]==0x72);
    fixture_pc_reply(0,XZ_PC_BASS,0);
    /* A fresh LOCAL -> PC transition selects the light path before heavy reads. */
    model.deck[0].pc_stems.source=XZ_UI_SOURCE_LOCAL;
    audio_calls=fixture_audio_status_calls[0]+fixture_audio_status_calls[1];
    key_calls=fixture_key_snapshot_calls[0]+fixture_key_snapshot_calls[1];
    assert(render_inline(NULL,pixels,sizeof(pixels)/sizeof(*pixels),536,536,XZ_WAVE_INLINE_HEIGHT));
    assert(fixture_audio_status_calls[0]+fixture_audio_status_calls[1]==audio_calls);
    assert(fixture_key_snapshot_calls[0]+fixture_key_snapshot_calls[1]==key_calls);
    assert(!memcmp(before_audio,applied,sizeof(before_audio)));
    /* A local row still refreshes normally in a mixed-source composition. */
    fixture_pc_source[1]=(struct xz_pc_stem_source){.slot=-1,.epoch=1};fixture_focus=-1;
    audio_calls=fixture_audio_status_calls[1];key_calls=fixture_key_snapshot_calls[1];
    refresh();assert(fixture_audio_status_calls[1]>audio_calls&&fixture_key_snapshot_calls[1]>key_calls);
    notes=fixture_pc_notes;fixture_pc_contact(self,0,XZ_PC_DRUMS,1,0);
    fixture_generation++;fixture_focus=1;refresh();
    assert(inline_ui.held_kind==XZ_UI_PC_TOGGLE&&inline_ui.down&&!inline_cancelled);
    fixture_pc_contact(self,0,XZ_PC_DRUMS,0,0);assert(fixture_pc_notes==notes+2);
    fixture_pc_reply(0,XZ_PC_DRUMS,0);
    fixture_pc_source[0]=fixture_pc_source[1];fixture_pc_roles=11;pc_refresh_locked();
    pc_observing=0;stock_pc_midi=NULL;fixture_generation=saved_generation;fixture_focus=saved_focus;
    model=saved_model;ui=saved_ui;inline_ui=saved_inline;xz_native_touch_visible(&touch,0);
    puts("PASS PC no-action press redraw outside locks, pending known LED preservation, dynamic 3/4 mode routing/cancellation, fresh source fast path, and local-generation isolation with mixed USB refresh");
}
