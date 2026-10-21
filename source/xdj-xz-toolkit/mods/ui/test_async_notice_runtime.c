/* Real runtime/frame entry fixture; no firmware or controller access. */
#define xz_game_milliseconds fixture_notice_clock
#define main existing_runtime_controls
#include "test_runtime_controls.c"
#undef main
#undef xz_game_milliseconds
static uint32_t notice_clock=100,test_scene=6;
uint32_t fixture_notice_clock(void){return notice_clock;}
unsigned xz_native_browse_mode(void){return 2;}
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t bytes){
 if(address==0x3c6fc7c&&bytes==4){memcpy(out,&test_scene,4);return 0;}
 return -1;
}
static uint16_t base[800*480+8],normal[800*480+8],shown[800*480+8];
static void browse_frame(uint16_t *p){
 for(unsigned i=0;i<800*480+8;i++)p[i]=0x4567;
 for(int x=296;x<=670;x++){
  p[2*800+x]=p[33*800+x]=0;
  p[31*800+x]=0x118b;p[32*800+x]=0x198c;
 }
 p[16*800+296]=0x198c;
}
static void *deliver(void *text){xz_ui_runtime_notice(text);return NULL;}
int main(void){
 started=1;native_play=native_active=0;model.theme=0;settings_running=0;settings_readonly=1;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);
 for(int d=0;d<2;d++){fixture_pc_source[d]=(struct xz_pc_stem_source){.slot=-1,.epoch=1};fixture_pc_valid[d]=1;}
 browse_frame(base);memcpy(normal,base,sizeof base);
 assert(xz_mods_render_v1(normal,800,480,800,NULL)==1);
 char message[90];memset(message,'X',sizeof message);message[sizeof message-1]=0;
 xz_ui_runtime_notice(message);memset(message,'Z',sizeof message);
 assert(strlen(async_notice.text)==63&&async_notice.text[0]=='X'&&!touch.visible);
 xz_ui_runtime_notice("USB SOURCE CHANGED / LOAD REFUSED");
 memcpy(shown,base,sizeof base);assert(xz_mods_render_v1(shown,800,480,800,NULL)==1);
 assert(!touch.visible&&memcmp(shown,normal,800*480*2));
 for(int y=0;y<480;y++)for(int x=0;x<800;x++)
  if(shown[y*800+x]!=normal[y*800+x])assert(y<24&&x>=744);
 for(int i=800*480;i<800*480+8;i++)assert(shown[i]==0x4567);
 /* Cached, contended rendering uses the bounded value copy, not shared state. */
 uint16_t busy[800*480+8];memcpy(busy,base,sizeof base);
 pthread_mutex_lock(&ui_mutex);
 assert(xz_mods_render_v1(busy,800,480,800,NULL)==1);
 pthread_mutex_unlock(&ui_mutex);
 for(int y=0;y<24;y++)assert(!memcmp(busy+y*800+744,shown+y*800+744,56*2));
 /* Expiry redraws the same owned header, even on the same painted backbuffer. */
 notice_clock+=XZ_UI_NOTICE_MS;
 pthread_mutex_lock(&ui_mutex);
 assert(xz_mods_render_v1(busy,800,480,800,NULL)==1);
 pthread_mutex_unlock(&ui_mutex);
 for(int y=0;y<24;y++)assert(!memcmp(busy+y*800+744,normal+y*800+744,56*2));
 /* A worker copy waits for the UI lock; no torn cached payload is exposed. */
 pthread_t thread;pthread_mutex_lock(&ui_mutex);
 struct xz_ui_transient_notice before=async_notice;
 assert(!pthread_create(&thread,NULL,deliver,"SECOND ASYNC REFUSAL"));
 assert(!memcmp(&before,&async_notice,sizeof before));
 pthread_mutex_unlock(&ui_mutex);assert(!pthread_join(thread,NULL));
 assert(!strcmp(async_notice.text,"SECOND ASYNC REFUSAL"));
 /* Every native theme clears the alert on its own reused button pixels. */
 struct xz_ui_notice_pixels theme_notice={0};
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  browse_frame(base);memcpy(normal,base,sizeof base);
  xz_ui_render_native_buttons(normal,800,theme,0,0,0);memcpy(shown,normal,sizeof shown);
  xz_ui_notice_badge(shown,800,theme,&async_notice,notice_clock,&theme_notice);
  xz_ui_notice_restore(&theme_notice,shown,800);
  xz_ui_render_native_buttons(normal,800,theme,0,0,0);
  xz_ui_render_native_buttons(shown,800,theme,0,0,0);
  if(memcmp(normal,shown,sizeof normal)){fprintf(stderr,"notice restoration theme %d\n",theme);assert(0);}
 }
 /* The existing MODS target opens the full refusal detail; no automatic takeover. */
 unsigned char touch_owner[32]={0};contact(touch_owner,772,12,1);contact(touch_owner,772,12,0);
 assert(touch.visible&&!strcmp(ui.notice,"SECOND ASYNC REFUSAL"));
 /* Clock wrap remains finite; unrelated native surfaces are never overwritten. */
 xz_ui_notice_set(&before,"WRAPPED",UINT32_MAX-100);
 assert(xz_ui_notice_text(&before,10)&&!xz_ui_notice_text(&before,4000));
 uint16_t absent[800*480];for(unsigned i=0;i<800*480;i++)absent[i]=0x1234;
 struct xz_browse_head head={1,-1,async_notice.text};assert(!xz_browse_head_render(absent,800,0,&head));
 for(unsigned i=0;i<800*480;i++)assert(absent[i]==0x1234);
 puts("PASS async Classic browse notice: bounded copy, no takeover, cached expiry restores MODS button and tap opens full detail, mutex isolation, wrap and absent-surface guards");
}
