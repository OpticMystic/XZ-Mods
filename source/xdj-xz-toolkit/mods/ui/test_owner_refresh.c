#define xz_game_milliseconds fixture_refresh_clock
#define main controls_original_main
#include "test_runtime_controls.c"
#undef main
#undef xz_game_milliseconds
static uint32_t test_now=1000;
uint32_t fixture_refresh_clock(void){return test_now;}
extern void xz_ui_runtime_owner_tick(void) __attribute__((weak));
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){
 uint32_t value;
 if(n==4&&address==0x3c6fc7c)value=2;
 else if(n==4&&address==0x1c888d0)value=0x10000;
 else return -1;
 memcpy(out,&value,4);return 0;
}
static uint16_t frame[800*480];
static struct xz_shell_runtime_frame_input shown;
int xz_shell_runtime_frame(uint16_t *pixels,size_t stride,const struct xz_shell_runtime_frame_input *in){
 (void)pixels;(void)stride;shown=*in;return fake_shell_running&&in->eligible;
}
static unsigned calls(void){return fixture_audio_status_calls[0]+fixture_audio_status_calls[1]+fixture_key_snapshot_calls[0]+fixture_key_snapshot_calls[1];}
static void tick(void){void (*volatile fn)(void)=xz_ui_runtime_owner_tick;if(fn)fn();}
static void present(void){assert(xz_mods_render_v1(frame,800,480,800,NULL));}
int main(void){
 started=audio_available=key_available=requested_stems=1;settings_readonly=1;model.enabled=XZ_UI_STEM;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
 model.ui_interface=XZ_UI_INTERFACE_MODERN;model.theme=XZ_THEME_FLAGSHIP;
 fake_shell_running=fake_owns=1;fixture_key_valid[0]=1;
 fixture_keys[0]=(struct xz_key_snapshot){.loaded=1,.original={9,1},.format=XZ_KEY_CLASSIC,.tempo_ratio=1};
 model.key_feedback=0;model.key_format=1;
 /* No event hook yet: preserve initialization refresh. */
 unsigned before=calls();present();assert(calls()>before);assert(!strcmp(shown.key[0].text,"Am"));
 shell_ui_refreshed_ms=fixture_refresh_clock()-120u;before=calls();present();assert(calls()>before); /* absent hook */
 tick();shell_ui_refreshed_ms=fixture_refresh_clock()-120u;
 before=calls();fixture_keys[0].original=(struct xz_music_key){0,1};present();
 assert(calls()==before); /* Baseline fails here: broad providers run in display. */
 assert(!strcmp(shown.key[0].text,"Am"));
 pthread_mutex_lock(&ui_mutex);tick();present();pthread_mutex_unlock(&ui_mutex);assert(calls()==before);
 tick();assert(calls()>before);before=calls();present();assert(calls()==before);
 assert(strcmp(shown.key[0].text,"Am")); /* owner result is visible next frame */
 play_key=(struct xz_play_key){.open=1,.deck=0,.classic_side=-1};
 before=calls();present();assert(calls()>before); /* open PLAY still verifies live native data */
 play_key.open=0;before=calls();present();assert(calls()==before);
 /* A stopped owner cannot leave stale UI state forever. */
 shell_ui_refreshed_ms=fixture_refresh_clock()-201u;before=calls();present();assert(calls()>before);
 test_now+=201u;shell_ui_refreshed_ms=test_now-120u;before=calls();present();assert(calls()>before); /* stale heartbeat */
 /* CLASSIC and hand-back keep their synchronous provider refresh. */
 fake_shell_running=fake_owns=0;model.ui_interface=XZ_UI_INTERFACE_CLASSIC;before=calls();present();assert(calls()>before);
 started=0;before=calls();tick();assert(calls()==before);assert(!xz_mods_render_v1(frame,800,480,800,NULL));
 puts("PASS broad provider display budget, owner refresh, next-frame data, busy owner lock, absent/stale fallback, CLASSIC and stop");
 return 0;
}

