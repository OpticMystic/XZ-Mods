/* Actual CLASSIC global-key hook. Firmware delivery is recorded, never called. */
#define XZ_MENU_RUNTIME_MAIN retained_about_menu_runtime
#include "test_menu_runtime.c"
#undef main
static unsigned delivered[4],deliveries;
static void stock_menu_record(void *self,const void *input){
 (void)self;assert(deliveries<4);delivered[deliveries++]=((const unsigned char *)input)[11];
}
int main(void){
 for(int p=0;p<XZ_UI_PAGE_COUNT;p++){
  rekordbox_reset(XZ_UI_INTERFACE_CLASSIC,XZ_REKORDBOX_MODS,XZ_REKORDBOX_STOCK);xz_ui_init(&ui);
  xz_native_touch_init(&touch,&ui,&model,apply,NULL);
  started=1;ui.page=(enum xz_ui_page)p;touch.visible=1;__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
  stock_global_key=stock_menu_record;deliveries=0;
  unsigned char input[16]={0};input[8]=0x0b;input[9]=0x02;
  menu_back_key_hook(NULL,input);assert(touch.visible&&deliveries==1&&delivered[0]==0);
  input[11]=1;menu_back_key_hook(NULL,input);assert(!touch.visible&&!__atomic_load_n(&visible,__ATOMIC_ACQUIRE));
  menu_back_key_hook(NULL,input);assert(!touch.visible);
  input[11]=2;menu_back_key_hook(NULL,input);assert(!touch.visible&&deliveries==4);
  assert(delivered[1]==1&&delivered[2]==1&&delivered[3]==2&&(int)ui.page==p);
 }
 puts("PASS actual CLASSIC global-key hook: MENU DOWN/HOLD/repeated HOLD/UP forwarded intact; panel yields on HOLD on every page");
 return 0;
}
