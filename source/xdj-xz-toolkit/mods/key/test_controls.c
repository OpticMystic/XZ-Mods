/* SPDX-License-Identifier: MIT */
#define main existing_controls_test
#include "../ui/test_runtime_controls.c"
#undef main

void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){
    uint32_t value=0;
    switch(address){
    case 0x1c8a308:value=0x10000;break;
    case 0x10040:value=0x20000;break;case 0x10048:value=2;break;
    case 0x20000:value=0x30000;break;case 0x20004:value=0x40000;break;
    case 0x30138:value=0x50000;break;case 0x40138:value=0x60000;break;
    case 0x50026:value=1;break;case 0x60026:value=2;break;
    default:return -1;
    }
    memcpy(out,&value,n);return 0;
}
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
    uintptr_t p=(uintptr_t)self;int deck=p==0x50000?0:p==0x60000?1:((unsigned char*)self)[0x26]-1;
    fixture_mt_on_calls++;fixture_keys[deck].master_tempo=on;return 1;
}
static void toggle_test(void){
    unsigned char self[0x138]={0},input[16]={0};self[0x26]=2;
    native_master_tempo_on=fixture_master_tempo_on;stock_master_tempo_key=fixture_master_tempo_stock;
    for(int mt=0;mt<2;mt++)for(int master=0;master<2;master++){
        keys();fixture_keys[1].master_tempo=mt;fixture_keys[1].master=master;fixture_keys[0].master=!master;
        fixture_keys[1].tempo_ratio=1.08;
        fixture_keys[0].tempo_ratio=exp2(.23/12);fixture_key_applied[0]=100;
        assert(sync_key(1));assert(key_sync[1].valid&&fixture_keys[1].master_tempo==1);
        assert(key_sync[1].mt_before==mt&&fixture_key_cents[1]==323);
        fixture_key_apply(1);refresh();
        assert(key_strip[1].synced&&key_strip[1].cents==23);
        struct xz_shell_runtime_frame_input frame;shell_frame_input_locked(&frame);
        assert(frame.key[1].valid&&!strcmp(frame.key[1].original,"C")&&!strcmp(frame.key[1].text,"Eb"));
        assert(frame.key[1].cents==23&&frame.key[1].synced);
        assert(sync_key(1)&&!key_sync[1].valid&&fixture_key_cents[1]==0&&fixture_keys[1].master_tempo==mt);
        assert(sync_key(1));fixture_mt_stock_calls=0;
        input[11]=0;assert(master_tempo_key_hook(self,input));
        assert(!key_sync[1].valid&&fixture_key_cents[1]==0&&fixture_keys[1].master_tempo==mt&&!fixture_mt_stock_calls);
        assert(master_tempo_key_hook(self,input));input[11]=1;assert(master_tempo_key_hook(self,input));
        input[11]=2;assert(master_tempo_key_hook(self,input)&&!fixture_mt_stock_calls);
        input[11]=0;master_tempo_key_hook(self,input);assert(fixture_mt_stock_calls==1&&fixture_keys[1].master_tempo==!mt);
        input[11]=2;master_tempo_key_hook(self,input);
        fixture_keys[1].master_tempo=mt;assert(sync_key(1));
        fixture_keys[1].epoch++;fixture_key_cents[1]=0;fixture_key_applied[1]=0;refresh();
        assert(!key_sync[1].valid&&fixture_keys[1].master_tempo==mt&&fixture_key_cents[1]==0);
    }
    for(int deck=0;deck<2;deck++)for(int mt=0;mt<2;mt++){
        keys();self[0x26]=(unsigned char)(deck+1);fixture_keys[deck].master_tempo=mt;
        assert(sync_key(deck));fixture_key_apply(deck);int applied=fixture_key_applied[deck];
        self[0x34]=1;input[11]=0;assert(master_tempo_key_hook(self,input));
        assert(!key_sync[deck].valid&&!fixture_key_cents[deck]&&fixture_keys[deck].master_tempo==mt);
        assert(fixture_key_applied[deck]==applied);
        fixture_keys[deck].epoch++;fixture_key_cents[deck]=300;refresh();
        int requests=fixture_key_requests,stock=fixture_mt_stock_calls;
        assert(master_tempo_key_hook(self,input));input[11]=2;assert(master_tempo_key_hook(self,input));
        assert(fixture_key_requests==requests&&fixture_key_cents[deck]==300&&fixture_mt_stock_calls==stock);
        self[0x34]=0;
    }
    self[0x26]=2;
    keys();fixture_keys[1].original.minor=1;assert(sync_key(1)&&fixture_key_cents[1]==-100);
    assert(sync_key(1));fixture_keys[0].master=fixture_keys[1].master=0;assert(sync_key(0));
    keys();fixture_keys[1].tempo_ratio=exp2(1.23/12);refresh();
    struct xz_shell_runtime_frame_input frame;shell_frame_input_locked(&frame);
    assert(!strcmp(frame.key[1].text,"Db")&&frame.key[1].cents==23&&!frame.key[1].synced);
    fixture_keys[1].tempo_ratio=1;refresh();shell_frame_input_locked(&frame);
    assert(!strcmp(frame.key[1].text,"C")&&!frame.key[1].cents);
    /* Physical shortcut owns repeats/releases; second press turns sync off. */
    keys();struct xz_cue_event event={.deck=1,.sync=1,.shift=1,.operation=0};unsigned trace;
    assert(xz_ui_runtime_pad(&event,&trace)&&key_sync[1].valid);
    int requests=fixture_key_requests;assert(xz_ui_runtime_pad(&event,&trace)&&fixture_key_requests==requests);
    event.operation=2;assert(xz_ui_runtime_pad(&event,&trace));event.operation=0;
    assert(xz_ui_runtime_pad(&event,&trace)&&!key_sync[1].valid&&!fixture_key_cents[1]);
    event.operation=2;assert(xz_ui_runtime_pad(&event,&trace));event.shift=0;event.operation=0;
    assert(!xz_ui_runtime_pad(&event,&trace));
    keys();fixture_key_valid[0]=0;assert(!sync_key(1));
    puts("PASS two-deck toggle, relative target, MT restore both ways, consumed press, stock next press, load reset and live shell key fields");
}
static void badge_test(void){
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
}
int main(void){existing_controls_test();toggle_test();badge_test();return 0;}
