/* Source-only replay of the actual RAM panel dispatcher in both interfaces. */
#define main retained_controls_main
#include "test_runtime_controls_entry.c"
#undef main
int main(void){
 for(int interface=0;interface<2;interface++){
  memset(&model,0,sizeof(model));started=audio_available=requested_stems=1;
  model.enabled=XZ_UI_STEM;model.stems_overlay=1;model.ui_interface=interface;
  model.theme=interface?XZ_THEME_FLAGSHIP:0;
  xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
  memset(fixture_ram_calls,0,sizeof(fixture_ram_calls));software_ram_calls=0;fixture_ram_rc=0;
  refresh();ram_tracks();
 }
 puts("PASS CLASSIC/MODERN RAM dispatcher, four independent selectors, stale session refusal, native-index bounds and no settings persistence");
 return 0;
}
