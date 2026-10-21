#ifdef NDEBUG
#error Browse return acceptance requires assertions
#endif
#include "browse_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include "shell_source.h"
#include <string.h>
static struct bx_library lib;static struct fx_track track;
static struct xz_shell_source_model source;
static size_t key(struct xz_shell *u,struct xz_shell_model *m,int key,int deck,struct xz_shell_action *a){
 struct xz_shell_event e={XZ_SHELL_KEY,0,0,0,key,deck};return xz_shell_input(u,m,&e,a);
}
static void setup(struct xz_shell *u,struct xz_shell_model *m,struct xz_shell_browse_model *b,int count,enum xz_shell_screen s){
 fx_model_full(m,&track);bx_tracks(b,&lib,s);b->media_generation=17;m->browse=b;m->deck_count=count;
 m->capabilities|=XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_TAG_LIST;
 m->source=&source;m->capabilities|=XZ_SHELL_CAP_SOURCE;
 xz_shell_init(u);xz_shell_browse_reset();
 int k=s==XZ_SHELL_PLAYLIST?XZ_SHELL_KEY_PLAYLIST:s==XZ_SHELL_TAG_LIST?XZ_SHELL_KEY_TAG_LIST:s==XZ_SHELL_SEARCH?XZ_SHELL_KEY_SEARCH:XZ_SHELL_KEY_BROWSE;
 struct xz_shell_action a[XZ_SHELL_ACTIONS];key(u,m,k,-1,a);
 u->stack[u->depth-1].focus=1;u->stack[u->depth-1].cursor=12;u->stack[u->depth-1].scroll=9;
}
int main(void){
 (void)&bx_folders;fx_track_build(&track,1);bx_build(&lib);
 for(int count=2;count<=4;count+=2)for(int screen=XZ_SHELL_BROWSE;screen<=XZ_SHELL_SEARCH;screen++)for(int back=0;back<2;back++){
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action a[XZ_SHELL_ACTIONS];
  setup(&u,&m,&b,count,(enum xz_shell_screen)screen);int depth=u.depth;
  size_t n=key(&u,&m,XZ_SHELL_KEY_LOAD,0,a);if(count==4){assert(!n);n=key(&u,&m,XZ_SHELL_KEY_LOAD,1,a);}
  assert(n==1&&a[0].kind==XZ_SHELL_ACT_LOAD&&xz_shell_screen(&u)==XZ_SHELL_PLAY);
  key(&u,&m,back?XZ_SHELL_KEY_BACK:XZ_SHELL_KEY_BROWSE,-1,a);
  assert((int)xz_shell_screen(&u)==screen&&u.depth==depth);
  assert(u.stack[depth-1].cursor==12&&u.stack[depth-1].scroll==9&&u.stack[depth-1].focus==1);
  struct xz_shell_event turn={XZ_SHELL_ENCODER_TURN,0,0,1,0,-1};xz_shell_input(&u,&m,&turn,a);
  assert(u.stack[depth-1].cursor==13&&u.stack[depth-1].focus==1);
 }
 for(int why=0;why<4;why++){
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action a[XZ_SHELL_ACTIONS];
  setup(&u,&m,&b,2,XZ_SHELL_BROWSE);assert(key(&u,&m,XZ_SHELL_KEY_LOAD,0,a)==1&&a[0].value==13);
  if(why==0)b.media_generation++;else if(why==1)b.list_id++;else if(why==2)b.source_name="USB2";else m.browse=NULL;
  key(&u,&m,XZ_SHELL_KEY_BACK,-1,a);assert(xz_shell_screen(&u)==XZ_SHELL_PLAY&&u.browse_return.depth==0);
 }
 /* Key probes only alter their own copied return state. */
 {
  struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shell_action a[XZ_SHELL_ACTIONS];
  setup(&u,&m,&b,2,XZ_SHELL_BROWSE);struct xz_shell copy=u;
  key(&copy,&m,XZ_SHELL_KEY_LOAD,0,a);assert(copy.browse_return.depth>1&&!u.browse_return.depth);
  key(&copy,&m,XZ_SHELL_KEY_BACK,-1,a);assert(copy.stack[copy.depth-1].cursor==12&&u.stack[u.depth-1].cursor==12);
  xz_shell_init(&u);key(&u,&m,XZ_SHELL_KEY_BACK,-1,a);assert(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 }
 puts("PASS media/list replacement invalidates saved view; fresh BACK is inert; copy probes preserve original shell");
 puts("PASS LOAD -> PLAY -> BACK/BROWSE restores list, row, scroll and encoder track focus for two/four decks");
}
