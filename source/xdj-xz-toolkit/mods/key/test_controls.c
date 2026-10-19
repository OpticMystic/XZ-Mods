/* SPDX-License-Identifier: MIT */
#define main existing_controls_test
#include "../ui/test_runtime_controls.c"
#undef main

void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
static uint16_t key_screen[800*480+2];
static void keys(void){
    fixture_key_valid[0]=fixture_key_valid[1]=1;
    fixture_keys[0]=(struct xz_key_snapshot){.original={2,0},.epoch=1,.tempo_ratio=1,.loaded=1,.master=1};
    fixture_keys[1]=(struct xz_key_snapshot){.original={0,0},.epoch=2,.tempo_ratio=1,.loaded=1};
    fixture_key_cents[0]=fixture_key_cents[1]=fixture_key_requests=0;fixture_key_applied[0]=fixture_key_applied[1]=0;
    started=key_available=1;model.shift_keysync=1;sync_down=sync_owned=0;
    master_tempo_down=master_tempo_owned=0;memset(key_sync,0,sizeof(key_sync));
}
static int fixture_mt_stock_calls,fixture_mt_on_calls;
static int fixture_master_tempo_stock(void *self,const void *input){
    const unsigned char *p=input,*s=self;int deck=s[0x26]-1;
    fixture_mt_stock_calls++;
    if(!(p[11]&15))fixture_keys[deck].master_tempo=!fixture_keys[deck].master_tempo;
    return 1;
}
static int fixture_master_tempo_on(void *self,int on){
    const unsigned char *s=self;fixture_mt_on_calls++;fixture_keys[s[0x26]-1].master_tempo=on;return 1;
}
static void master_tempo_exit_test(void){
    unsigned char self[0x138]={0},input[16]={0};self[0x26]=2;
    stock_master_tempo_key=fixture_master_tempo_stock;
    native_master_tempo_on=fixture_master_tempo_on;
    for(int mt=0;mt<2;mt++){
        keys();fixture_keys[1].master_tempo=mt;fixture_keys[1].tempo_ratio=1.08;
        assert(sync_key(1));fixture_key_apply(1);refresh();
        int applied=fixture_key_applied[1];fixture_mt_stock_calls=fixture_mt_on_calls=0;
        input[11]=0;assert(master_tempo_key_hook(self,input));
        assert(!key_sync[1].valid&&!model.deck[1].key_synced&&fixture_key_cents[1]==0);
        assert(fixture_keys[1].master_tempo==1&&fixture_mt_stock_calls==0&&fixture_mt_on_calls==1);
        assert(fixture_keys[0].master&&fixture_key_cents[0]==0&&!fixture_keys[0].master_tempo);
        assert(fixture_key_applied[1]==applied); /* Never report a request as applied audio. */
        int requests=fixture_key_requests;assert(master_tempo_key_hook(self,input));input[11]=1;assert(master_tempo_key_hook(self,input));
        input[11]=2;assert(master_tempo_key_hook(self,input)&&fixture_key_requests==requests&&fixture_mt_on_calls==1);
        fixture_key_apply(1);refresh();assert(!strcmp(model.deck[1].key_readout,"C > C")&&!key_strip[1].synced);
        input[11]=0;master_tempo_key_hook(self,input);assert(!fixture_keys[1].master_tempo&&fixture_mt_stock_calls==1);
        input[11]=2;master_tempo_key_hook(self,input);assert(sync_key(1));
    }
    keys();fixture_keys[0].original=fixture_keys[1].original;assert(sync_key(1)&&fixture_key_cents[1]==0);
    input[11]=0;assert(master_tempo_key_hook(self,input)&&!key_sync[1].valid);input[11]=3;master_tempo_key_hook(self,input);
    keys();fixture_key_cents[1]=300;input[11]=0;master_tempo_key_hook(self,input);assert(fixture_key_cents[1]==300);
    input[11]=2;master_tempo_key_hook(self,input);
    keys();assert(sync_key(1));self[0x34]=1;input[11]=0;master_tempo_key_hook(self,input);assert(key_sync[1].valid&&fixture_key_cents[1]!=0);
    self[0x34]=0;input[11]=2;master_tempo_key_hook(self,input);
    /* A new load cannot receive the previous track's reset. */
    keys();assert(sync_key(1));fixture_keys[1].epoch++;fixture_key_cents[1]=300;
    int requests=fixture_key_requests,stock=fixture_mt_stock_calls;
    input[11]=0;master_tempo_key_hook(self,input);
    assert(fixture_key_requests==requests&&fixture_key_cents[1]==300&&fixture_mt_stock_calls==stock+1);
    input[11]=2;master_tempo_key_hook(self,input);
    /* Claimed repeats/releases stay consumed even when loading or Shift changes mid-press. */
    keys();assert(sync_key(1));input[11]=0;assert(master_tempo_key_hook(self,input));
    requests=fixture_key_requests;stock=fixture_mt_stock_calls;
    fixture_keys[1].epoch++;fixture_key_cents[1]=300;refresh();self[0x34]=1;
    assert(master_tempo_key_hook(self,input));input[11]=2;assert(master_tempo_key_hook(self,input));
    assert(fixture_key_requests==requests&&fixture_key_cents[1]==300&&fixture_mt_stock_calls==stock);
    self[0x34]=0;
    /* Deck 1 uses the same native channel mapping and leaves Deck 2's intent/shift intact. */
    keys();self[0x26]=1;fixture_keys[0].master=0;fixture_keys[1].master=1;
    fixture_key_cents[1]=fixture_key_applied[1]=100;key_sync[1].valid=1;
    key_sync[1].epoch=fixture_keys[1].epoch;key_sync[1].cents=100;model.deck[1].key_synced=1;
    assert(sync_key(0));input[11]=0;assert(master_tempo_key_hook(self,input));
    assert(!key_sync[0].valid&&fixture_key_cents[0]==0&&fixture_keys[0].master_tempo);
    assert(key_sync[1].valid&&model.deck[1].key_synced&&fixture_key_cents[1]==100&&!fixture_keys[1].master_tempo);
    input[11]=2;assert(master_tempo_key_hook(self,input));
    puts("PASS Master Tempo exits Key Sync once, forces MT on, retains applied-key truth and stock/manual/Shift behavior");
}
int main(void){
    existing_controls_test();master_tempo_exit_test();keys();refresh();
    assert(model.deck[1].ready&XZ_UI_KEYSYNC);assert(!(model.deck[0].ready&XZ_UI_KEYSYNC));
    struct xz_cue_event event={.deck=1,.sync=1,.shift=1,.operation=0};unsigned trace;
    assert(xz_ui_runtime_pad(&event,&trace)==1);
    assert(fixture_key_requests==1&&fixture_key_cents[1]==200);
    assert(xz_ui_runtime_pad(&event,&trace)==1&&fixture_key_requests==1);
    event.shift=0;event.operation=2;assert(xz_ui_runtime_pad(&event,&trace)==1);
    assert(!(sync_owned&2));
    event.operation=0;assert(!xz_ui_runtime_pad(&event,&trace)&&fixture_key_requests==1);
    event.operation=2;assert(!xz_ui_runtime_pad(&event,&trace));
    event.shift=1;event.operation=0;assert(xz_ui_runtime_pad(&event,&trace));
    assert(fixture_key_cents[1]==200);event.operation=3;assert(xz_ui_runtime_pad(&event,&trace));
    fixture_keys[1].tempo_ratio=exp2(1.23/12);refresh();
    assert(strstr(model.deck[1].key_readout,"+23c"));
    event.operation=0;assert(xz_ui_runtime_pad(&event,&trace));
    assert(fixture_key_cents[1]==77);event.operation=2;assert(xz_ui_runtime_pad(&event,&trace));
    /* A pending request never changes the shown key; the applied shift does. */
    refresh();assert(strcmp(model.deck[1].key_readout,"C > D")&&model.deck[1].key_synced);
    fixture_key_apply(1);refresh();assert(!strcmp(model.deck[1].key_readout,"C > D")&&model.deck[1].key_synced);
    assert(key_strip[0].sync_master&&!key_strip[0].synced&&key_strip[1].synced&&!key_strip[1].sync_master);
    {uint16_t stock[3]={'C',0},line[96];size_t n=xz_key_line_adjacent(1,stock,1,line);char text[96];
     for(size_t i=0;i<=n;i++)text[i]=(char)line[i];assert(n&&!strcmp(text,"C > D  KEY SYNC"));}
    fixture_keys[1].master_tempo=1;refresh();assert(strstr(model.deck[1].key_readout,"-23c"));
    /* A manual shift ends the KEY SYNC indication. */
    {struct xz_ui_action shift={.kind=XZ_UI_KEY_SHIFT,.deck=1,.index=1};apply(NULL,&shift,1);refresh();
     assert(!model.deck[1].key_synced&&!key_strip[0].sync_master);}
    event.operation=0;assert(xz_ui_runtime_pad(&event,&trace));assert(fixture_key_cents[1]==200);
    event.operation=2;assert(xz_ui_runtime_pad(&event,&trace));
    fixture_keys[1].original.minor=1;event.operation=0;assert(xz_ui_runtime_pad(&event,&trace));
    assert(fixture_key_cents[1]==-100);event.operation=2;assert(xz_ui_runtime_pad(&event,&trace));
    int requests=fixture_key_requests;fixture_key_valid[0]=0;event.operation=0;
    {enum xz_ui_page page=ui.page;assert(xz_ui_runtime_pad(&event,&trace));assert(fixture_key_requests==requests&&ui.page==page&&!touch.visible);}
    /* No master key: the error is inline on deck 2's strip, nothing pops up. */
    refresh();assert(!strcmp(key_strip[1].error,"LOAD A KEYED TRACK ON DECK 1")&&xz_mods_key_strip_proof_v1.errors[1]==1);
    event.operation=2;model.shift_keysync=0;assert(xz_ui_runtime_pad(&event,&trace));
    /* Retired preference off: the always-available gesture syncs directly, is consumed
       with its repeat and release, and never reaches the firmware beat-sync toggle. */
    keys();model.shift_keysync=0;ui.page=XZ_UI_STEMS;event=(struct xz_cue_event){.deck=1,.sync=1,.shift=1,.operation=0};
    assert(xz_ui_runtime_pad(&event,&trace)&&ui.page==XZ_UI_STEMS&&!touch.visible&&fixture_key_requests==1&&model.shift_keysync);
    refresh();assert(key_strip[1].synced&&!key_strip[1].error[0]);
    assert(xz_ui_runtime_pad(&event,&trace));event.operation=1;assert(xz_ui_runtime_pad(&event,&trace));
    event.shift=0;event.operation=2;assert(xz_ui_runtime_pad(&event,&trace));
    event.operation=0;assert(!xz_ui_runtime_pad(&event,&trace));event.operation=2;assert(!xz_ui_runtime_pad(&event,&trace));
    keys();fixture_keys[0].master=fixture_keys[1].master=0;assert(!sync_key(1)&&fixture_key_requests==0);
    refresh();assert(!strcmp(key_strip[1].error,"MAKE DECK 1 MASTER FIRST"));
    fixture_keys[1].master=1;assert(!sync_key(1)&&fixture_key_requests==0);refresh();assert(!strcmp(key_strip[1].error,"MAKE DECK 1 MASTER FIRST"));
    fixture_keys[1].master=0;
    fixture_keys[0].master=1;struct xz_ui_action check={.kind=XZ_UI_KEY_CHECK};
    settings_running=1;settings_readonly=0;settings_pending=0;strcpy(settings_usb,"/tmp/key-settings-fixture");apply(NULL,&check,1);
    assert(model.shift_keysync&&settings_pending);settings_running=0;settings_usb[0]=0;
    refresh();ui.page=XZ_UI_KEY_SETUP;
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        model.theme=theme;key_screen[0]=0x1234;key_screen[800*480+1]=0xabcd;
        assert(xz_ui_render(&ui,&model,key_screen+1,800*480,800));
        assert(key_screen[0]==0x1234&&key_screen[800*480+1]==0xabcd);
    }
    /* The MODS badge keeps its own page on browse screens too: columns are chosen from the
       table header, never by repurposing the badge. */
    {
        unsigned char self[32]={0};ui.page=XZ_UI_CONTROLS;touch.visible=0;touch.capture=0;model.fb_takeover=0;
        native_play=1;refresh();xz_key_browser_list_drawn();refresh();
        contact(self,770,10,1);contact(self,770,10,0);assert(touch.visible&&ui.page==XZ_UI_CONTROLS);
        touch.visible=0;native_play=0;xz_key_browser_list_drawn();
        contact(self,770,10,1);contact(self,770,10,0);assert(touch.visible&&ui.page==XZ_UI_CONTROLS);
        touch.visible=0;ui.page=XZ_UI_CONTROLS;native_play=1;refresh();native_play=0;
        contact(self,770,10,1);contact(self,770,10,0);assert(touch.visible&&ui.page==XZ_UI_CONTROLS);
        touch.visible=0;native_play=1;
    }
    puts("PASS MODS badge unchanged on browse; Shift+Sync failures inline, never a popup");
    printf("PASS physical Shift+Sync: repeat/release/ordinary Sync, both modes, fractional tempo, setup and %d themes\n",XZ_THEME_COUNT);
    return 0;
}
