#ifdef NDEBUG
#error Notice acceptance requires assertions
#endif
#include "ui_skin.h"
#include "transient_notice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t normal[800*480],shown[800*480];
int main(void){
 struct xz_ui_transient_notice notice={0};xz_ui_notice_set(&notice,"LOAD REFUSED",100);
 struct xz_ui_notice_pixels saved={0};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  for(unsigned i=0;i<800*480;i++)normal[i]=0x4567;
  xz_ui_render_native_buttons(normal,800,theme,0,0,0);memcpy(shown,normal,sizeof normal);
  xz_ui_notice_badge(shown,800,theme,&notice,100,&saved);assert(memcmp(normal,shown,sizeof normal));
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<800;x++)
   if(normal[y*800+x]!=shown[y*800+x])assert(x>=744&&y<24);
  xz_ui_notice_restore(&saved,shown,800);
  xz_ui_render_native_buttons(normal,800,theme,0,0,0);
  xz_ui_render_native_buttons(shown,800,theme,0,0,0);
  xz_ui_notice_badge(shown,800,theme,&notice,3600,&saved);
  for(unsigned i=0;i<800*480;i++)if(normal[i]!=shown[i]){
   fprintf(stderr,"theme%d restoration x%u y%u %x != %x\n",theme,i%800,i/800,shown[i],normal[i]);assert(0);
  }
 }
 memcpy(shown,normal,sizeof shown);xz_ui_notice_badge(shown,800,0,&notice,100,&saved);
 shown[4*800+748]^=1;memcpy(normal,shown,sizeof normal);
 xz_ui_notice_restore(&saved,shown,800);assert(!memcmp(normal,shown,sizeof normal)&&!saved.valid);
 puts("PASS fresh native pixels supersede the last alert paint");
 puts("PASS alert button in all themes: only owned pixels change, expired alert restores reused buffer exactly");
}
