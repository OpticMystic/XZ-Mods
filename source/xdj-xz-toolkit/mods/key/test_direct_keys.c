/* SPDX-License-Identifier: MIT */
#define main legacy_runtime_acceptance
#include "../ui/test_runtime_controls.c"
#undef main
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
int main(void){
    started=key_available=1;audio_available=0;sync_down=sync_owned=0;
    model=(struct xz_ui_model){0};model.shift_keysync=0;
    xz_ui_init(&ui);ui.page=XZ_UI_STEMS;touch.visible=touch.capture=0;
    fixture_key_valid[0]=fixture_key_valid[1]=1;
    fixture_keys[0]=(struct xz_key_snapshot){.original={2,0},.epoch=1,.tempo_ratio=1,.loaded=1,.master=1};
    fixture_keys[1]=(struct xz_key_snapshot){.original={0,0},.epoch=2,.tempo_ratio=1,.loaded=1};
    fixture_key_requests=0;fixture_key_cents[0]=fixture_key_cents[1]=0;
    struct xz_cue_event event={.deck=1,.sync=1,.shift=1,.operation=0};unsigned flags;
    assert(xz_ui_runtime_pad(&event,&flags));
    assert(fixture_key_requests==1&&fixture_key_cents[1]==200);
    assert(!touch.visible&&ui.page==XZ_UI_STEMS);
    assert(xz_ui_runtime_pad(&event,&flags)&&fixture_key_requests==1);
    event.operation=1;assert(xz_ui_runtime_pad(&event,&flags));
    event.shift=0;event.operation=2;assert(xz_ui_runtime_pad(&event,&flags));
    event.operation=0;assert(!xz_ui_runtime_pad(&event,&flags));
    event.operation=2;assert(!xz_ui_runtime_pad(&event,&flags));
    puts("PASS direct Shift+Sync with saved preference off, no setup window, repeats/releases owned and plain Sync preserved");
}
