#ifdef NDEBUG
#error Menu history acceptance requires active assertions
#endif
#include "ui.h"
#include "ui_menu.h"
#include <assert.h>
#include <stdio.h>

int main(void){
 unsigned checks=0;
 for(int interface=0;interface<2;interface++)for(int repeat=0;repeat<32;repeat++){
  struct xz_ui u;struct xz_ui_action out[XZ_UI_ACTIONS];xz_ui_init(&u);
  xz_ui_navigate(&u,XZ_UI_NETWORK);xz_ui_navigate(&u,XZ_UI_NETWORK_SETUP);
  for(int field=0;field<3;field++){
   xz_ui_navigate(&u,XZ_UI_NETWORK_EDIT);assert(u.history_count==3);checks++;
   /* DONE and CANCEL both return through this shared navigation call. */
   xz_ui_navigate(&u,XZ_UI_NETWORK_SETUP);assert(u.history_count==2&&u.page==XZ_UI_NETWORK_SETUP);checks++;
  }
  for(int step=0;step<3;step++){
   static const enum xz_ui_page want[]={XZ_UI_NETWORK,XZ_UI_GENERAL,XZ_UI_GENERAL};
   size_t n=interface?xz_ui_menu_back(&u,out):xz_ui_back(&u,out);
   assert(n==1&&u.page==want[step]&&out[0].kind==(step==2?XZ_UI_CLOSE:XZ_UI_PANEL));checks++;
  }
  assert(!u.history_count);checks++;
 }
 puts("PASS menu history: CLASSIC and FLAGSHIP BACK, 64 repeated IP/MASK/GATEWAY ancestor returns, 640 assertions; root closes in one BACK");
 assert(checks==640);return 0;
}
