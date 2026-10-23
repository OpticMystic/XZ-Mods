#ifdef NDEBUG
#error Search acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include "shell_runtime_core.c"

static struct xz_shell u;
static struct xz_shell_model m;
static struct fx_track track;
static struct bx_library lib;
static struct xz_shell_browse_model b;
static struct xz_shell_action out[XZ_SHELL_ACTIONS];
static unsigned checks,failures;
#define CHECK(e) do{checks++;if(!(e)){failures++;printf("FAIL line %d: %s\n",__LINE__,#e);}}while(0)
static size_t event(struct xz_shell_event e){return xz_shell_input(&u,&m,&e,out);}
static size_t turn(int delta){return event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_TURN,.delta=delta,.deck=-1});}
static size_t push(void){return event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_PUSH,.deck=-1});}
static size_t key(enum xz_shell_key k){return event((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=k,.deck=-1});}
static size_t physical_push(struct xz_shrt_push *phase,uint32_t flags,uint32_t state){int k=xz_shrt_push_event(phase,flags,state);return k<0?0:event((struct xz_shell_event){.kind=(enum xz_shell_event_kind)k,.deck=-1});}
static int index_of(const char *label){for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(!strcmp(label,xz_shell_browse_key_label(i)))return i;assert(0);return 0;}
static void select_key(const char *label){turn(INT_MIN);turn(index_of(label));}
static size_t tap(enum xz_shell_browse_region region,int i){struct xz_shell_rect r=xz_shell_browse_rect(&u,&m,region,i);assert(r.w&&r.h);size_t n=event((struct xz_shell_event){.kind=XZ_SHELL_TOUCH_DOWN,.x=r.x+r.w/2,.y=r.y+r.h/2,.deck=-1});event((struct xz_shell_event){.kind=XZ_SHELL_TOUCH_UP,.x=r.x+r.w/2,.y=r.y+r.h/2,.deck=-1});return n;}
static void start(void){fx_model_full(&m,&track);bx_tracks(&b,&lib,XZ_SHELL_SEARCH);b.media_generation=17;m.browse=&b;m.capabilities|=XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD;m.deck_count=4;xz_shell_init(&u);xz_shell_browse_reset();key(XZ_SHELL_KEY_SEARCH);}
static void query(const char *want){CHECK(!strcmp(xz_shell_browse_query(),want));}
static void picture(const char *name){static uint16_t p[804*482+128];for(unsigned i=0;i<sizeof p/sizeof *p;i++)p[i]=0xa55a;CHECK(xz_shell_render_frame(&u,&m,p+64+804,804));for(int i=0;i<64+804;i++)CHECK(p[i]==0xa55a);for(int y=0;y<480;y++)for(int x=800;x<804;x++)CHECK(p[64+804+y*804+x]==0xa55a);for(unsigned i=64+804*481;i<sizeof p/sizeof *p;i++)CHECK(p[i]==0xa55a);if(name){FILE *f=fopen(name,"wb");assert(f);fputs("P6\n800 480\n255\n",f);for(int y=0;y<480;y++)for(int x=0;x<800;x++){unsigned v=p[64+804+y*804+x];unsigned char rgb[3]={(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));}}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);fx_track_build(&track,7);bx_build(&lib);(void)bx_folders;start();
 CHECK(!turn(11));CHECK(push()==1&&out[0].kind==XZ_SHELL_ACT_BROWSE&&out[0].id==XZ_SHELL_BROWSE_SEARCH_QUERY);query("Q");
 CHECK(u.stack[u.depth-1].cursor==0&&!u.load_choice.mode);CHECK(!event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_HOLD,.deck=-1}));query("Q");
 select_key("A");CHECK(push()==1);query("QA");select_key("DELETE");CHECK(push()==1);query("Q");select_key("SPACE");CHECK(push()==1);query("Q ");
 select_key("CLEAR");CHECK(push()==1);query("");CHECK(!push());
 select_key("DELETE");CHECK(!push());select_key("A");for(int i=0;i<XZ_SHELL_BROWSE_QUERY+4;i++)push();CHECK(strlen(xz_shell_browse_query())==XZ_SHELL_BROWSE_QUERY-1);CHECK(!push());
 select_key("CLEAR");push();turn(INT_MIN);push();query("1");turn(INT_MAX);push();query("1 ");
 select_key("A");if(argc>1){char path[1024];snprintf(path,sizeof path,"%s/search-focus.ppm",argv[1]);picture(path);}else picture(NULL);
 select_key("SEARCH");CHECK(!push());CHECK(xz_shell_browse_rows_visible(&u)==9);turn(18);CHECK(u.stack[u.depth-1].cursor==18);
 struct xz_shrt_push phase={0};CHECK(xz_shrt_push_event(&phase,1,2)==XZ_SHELL_ENCODER_PUSH);CHECK(xz_shrt_push_event(&phase,2,3)==XZ_SHELL_ENCODER_HOLD);
 CHECK(!turn(-19)&&u.stack[u.depth-1].cursor==0&&!u.load_choice.mode);CHECK(xz_shell_browse_rows_visible(&u)==9);
 turn(INT_MIN);CHECK(u.stack[u.depth-1].cursor==0&&!u.load_choice.mode);turn(1);CHECK(u.stack[u.depth-1].cursor==0&&!u.load_choice.mode);turn(-1);
 memset(&phase,0,sizeof phase);CHECK(!physical_push(&phase,1,2)&&!u.load_choice.mode);CHECK(xz_shell_browse_rows_visible(&u)==4);query("1 ");
 /* No preceding PUSH from a long gesture can run a result action here. */
 CHECK(!physical_push(&phase,2,3)&&!u.load_choice.mode);query("1 ");CHECK(!physical_push(&phase,0,0)&&!u.load_choice.mode);
 CHECK(!key(XZ_SHELL_KEY_BACK));CHECK(xz_shell_screen(&u)==XZ_SHELL_SEARCH&&xz_shell_browse_rows_visible(&u)==9);
 turn(18);CHECK(!push()&&u.load_choice.mode==2);key(XZ_SHELL_KEY_BACK);CHECK(!u.load_choice.mode&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);
 CHECK(event((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=XZ_SHELL_KEY_LOAD,.deck=2})==1&&out[0].kind==XZ_SHELL_ACT_LOAD&&out[0].deck==2&&out[0].id==1018);CHECK(xz_shell_screen(&u)==XZ_SHELL_PLAY);key(XZ_SHELL_KEY_BACK);CHECK(xz_shell_screen(&u)==XZ_SHELL_SEARCH&&u.stack[u.depth-1].cursor==18);query("1 ");
 key(XZ_SHELL_KEY_BACK);CHECK(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 start();CHECK(tap(XZ_SHELL_BR_KEY,index_of("Z"))==1);query("Z");tap(XZ_SHELL_BR_FIELD,0);CHECK(xz_shell_browse_rows_visible(&u)==9);turn(2);CHECK(u.stack[u.depth-1].cursor==2);tap(XZ_SHELL_BR_FIELD,0);select_key("DELETE");CHECK(push()==1);query("");
 tap(XZ_SHELL_BR_ROW,1);turn(1);CHECK(u.stack[u.depth-1].cursor==2);CHECK(!push()&&u.load_choice.mode==2);key(XZ_SHELL_KEY_BACK);CHECK(!u.load_choice.mode);
 start();turn(0);CHECK(!key(XZ_SHELL_KEY_BACK)&&xz_shell_screen(&u)==XZ_SHELL_SEARCH);key(XZ_SHELL_KEY_BACK);CHECK(xz_shell_screen(&u)==XZ_SHELL_PLAY);
 printf("SEARCH encoder: %u checks / %u failures\n",checks,failures);return !!failures;
}
