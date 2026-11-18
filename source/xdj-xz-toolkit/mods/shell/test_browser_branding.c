#ifdef NDEBUG
#error Browser acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include "shell_source.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct fx_track track;
static struct bx_library lib;
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT], baseline[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT], logo[160*160];
static uint64_t digest(void){uint64_t h=14695981039346656037ull;for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)h=(h^pixels[i])*1099511628211ull;return h;}
static void save(const char *name){FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(int i=0;i<800*480;i++){unsigned v=pixels[i];unsigned char rgb[3]={(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}fclose(f);}
int main(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action actions[XZ_SHELL_ACTIONS];
 fx_track_build(&track,7);bx_build(&lib);fx_model_full(&m,&track);xz_shell_init(&u);xz_shell_browse_reset();
 m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SOURCE;m.browse=&b;bx_tracks(&b,&lib,XZ_SHELL_BROWSE);
 struct xz_shell_event event={.kind=XZ_SHELL_KEY,.key=XZ_SHELL_KEY_BROWSE};xz_shell_input(&u,&m,&event,actions);
 assert(xz_shell_render(&u,&m,pixels,800));uint64_t plain=digest();
 struct xz_shell_browse_row rows[XZ_SHELL_BROWSE_WINDOW];memcpy(rows,b.rows,(size_t)b.window_count*sizeof *rows);b.rows=rows;
 rows[0].known&=~(1u<<XZ_SHELL_RF_STEMS);rows[0].stems_available=1;assert(xz_shell_render(&u,&m,pixels,800)&&digest()==plain);
 rows[0].known|=1u<<XZ_SHELL_RF_STEMS;assert(xz_shell_render(&u,&m,pixels,800)&&digest()!=plain);save("browse-stems.ppm");
 rows[0].stems_available=0;assert(xz_shell_render(&u,&m,pixels,800)&&digest()==plain);
 bx_folders(&b,&lib,XZ_SHELL_BROWSE);assert(xz_shell_render(&u,&m,pixels,800));plain=digest();
 memcpy(rows,b.rows,(size_t)b.window_count*sizeof *rows);b.rows=rows;rows[0].known|=1u<<XZ_SHELL_RF_STEMS;rows[0].stems_available=1;
 assert(xz_shell_render(&u,&m,pixels,800)&&digest()==plain);
 for(size_t i=0;i<sizeof logo/sizeof *logo;i++)logo[i]=0xf81f;
 struct xz_shell_source_model source={0};source.device[1]=(struct xz_shell_device){.known=1u<<XZ_SHELL_D_STATUS,.status=XZ_SHELL_DEVICE_READY,.logo=logo,.profile_name="TEST DJ"};
 m.source=&source;xz_shell_init(&u);u.stack[0].screen=XZ_SHELL_SOURCE;
 assert(xz_shell_render(&u,&m,pixels,800));struct xz_shell_rect tile=xz_shell_source_rect(1);
 assert(pixels[(tile.y+70)*800+tile.x+tile.w/2]==0xf81f);save("source-dj-logo.ppm");
 xz_shell_init(&u);for(int d=0;d<2;d++){m.deck[d].known|=1u<<XZ_SHELL_F_LOADED;m.deck[d].loaded=0;}
 m.idle_logo=NULL;m.idle_dj_name=NULL;assert(xz_shell_render(&u,&m,pixels,800));memcpy(baseline,pixels,sizeof pixels);save("idle-xz-logo.ppm");
 /* The built-in XZ Mods mark, small and quiet: solid grey (#8c8c8c, the shell's grey text) with only black and
    darker grey edges in the 160 px square, on the screen's own black. */
 {int grey=0,other=0;for(int y=132;y<292;y++)for(int x=320;x<480;x++){uint16_t v=pixels[y*800+x];unsigned r=v>>11,g=(v>>5)&63,b=v&31;
   grey+=v==0x8c71;other+=!(r==b&&g==(r*63+15)/31&&r<=17);}
  assert(grey>2500&&other==0);
  for(int x=316;x<484;x++)assert(pixels[132*800+x]==0&&pixels[291*800+x]==0);
  for(int y=132;y<292;y++)assert(pixels[y*800+318]==0&&pixels[y*800+320]==0&&pixels[y*800+479]==0&&pixels[y*800+481]==0);}
 m.idle_logo=logo;m.idle_dj_name="TEST DJ";assert(xz_shell_render(&u,&m,pixels,800));
 for(int y=132;y<292;y++)for(int x=320;x<480;x++)assert(pixels[y*800+x]==0xf81f);
 assert(memcmp(pixels,baseline,sizeof pixels));save("idle-dj-logo.ppm");
 m.deck[0].loaded=1;assert(xz_shell_render(&u,&m,pixels,800));assert(pixels[180*800+400]!=0xf81f);
 m.deck[0].loaded=0;m.deck[0].known&=~(1u<<XZ_SHELL_F_LOADED);m.deck[1].known&=~(1u<<XZ_SHELL_F_LOADED);
 m.startup_idle=1;assert(xz_shell_render(&u,&m,pixels,800));assert(pixels[180*800+400]==0xf81f);
 m.startup_idle=0;assert(xz_shell_render(&u,&m,pixels,800));assert(pixels[180*800+400]!=0xf81f);
 m.startup_idle=1;m.deck[0].known|=1u<<XZ_SHELL_F_LOADED;m.deck[0].loaded=1;
 assert(xz_shell_render(&u,&m,pixels,800));assert(pixels[180*800+400]!=0xf81f);
 puts("PASS available-only stem badge; USB profile logo; XZ logo fallback; empty-deck DJ logo; loaded deck restores playback UI");return 0;
}
