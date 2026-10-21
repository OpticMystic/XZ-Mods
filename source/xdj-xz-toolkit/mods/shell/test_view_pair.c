#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { STRIDE=804, GUARD=64, SIZE=GUARD+STRIDE*480+GUARD };
static struct fx_track track;
static uint16_t a[SIZE],b[SIZE],mask[800*480];
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static uint16_t *frame(uint16_t *p){return p+GUARD;}
static void render(const struct xz_shell *u,const struct xz_shell_model *m){
 struct xz_shell before=*u;struct xz_shell_model model=*m;
 for(int n=0;n<2;n++){
  uint16_t *p=n?b:a;
  for(size_t i=0;i<SIZE;i++)p[i]=0xa55a;
  for(int y=0;y<480;y++)for(int x=0;x<800;x++)frame(p)[y*STRIDE+x]=n?0xffff:0;
  assert(xz_shell_render(u,m,frame(p),STRIDE));
  for(int i=0;i<GUARD;i++)assert(p[i]==0xa55a&&p[SIZE-1-i]==0xa55a);
  for(int y=0;y<480;y++)for(int x=800;x<STRIDE;x++)assert(frame(p)[y*STRIDE+x]==0xa55a);
 }
 assert(!memcmp(a,b,sizeof a));
 assert(!memcmp(u,&before,sizeof before)&&!memcmp(m,&model,sizeof model));
}
static size_t tap(struct xz_shell *u,const struct xz_shell_model *m,int x,int y,struct xz_shell_action out[XZ_SHELL_ACTIONS]){
 struct xz_shell_action rest[XZ_SHELL_ACTIONS];
 size_t n=xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1},out);
 size_t k=xz_shell_input(u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1},rest);
 for(size_t i=0;i<k&&n<XZ_SHELL_ACTIONS;i++)out[n++]=rest[i];
 return n;
}
static void number_at(int x,int y,int deck,uint32_t background){
 for(int i=0;i<800*480;i++)mask[i]=rgb565(background);
 struct xz_canvas c={mask,800,800,480,XZ_THEME_FLAGSHIP};char text[8];snprintf(text,sizeof text,"%d",deck+1);
 sh_num(c,x,y,text,SH_NUM_MID,SH_WHITE);int ink=0;
 for(int yy=y;yy<y+sh_num_cap(SH_NUM_MID);yy++)for(int xx=x;xx<x+20;xx++)if(mask[yy*800+xx]!=rgb565(background)){
  ink++;assert(frame(a)[yy*STRIDE+xx]==mask[yy*800+xx]);
 }
 assert(ink>0&&sh_num_cap(SH_NUM_MID)>SH_CAP_TEXT);
}
static void title_at(int y,const char *title){
 memset(mask,0,sizeof mask);struct xz_canvas c={mask,800,800,480,XZ_THEME_FLAGSHIP};
 sh_text_fit(c,104,y+2,336,title,SH_TEXT,SH_WHITE);int ink=0;
 /* Beat markers now sit behind the title; its solid glyph cores still draw last. */
 for(int yy=y+2;yy<y+20;yy++)for(int x=104;x<780;x++)if(mask[yy*800+x]==rgb565(SH_WHITE)){
  ink++;assert(frame(a)[yy*STRIDE+x]==mask[yy*800+x]);
 }
 assert(ink>0);
}
static void test_views(void){
 static const int columns[4]={1,2,0,3};
 for(int order=0;order<2;order++)for(int sides=0;sides<4;sides++)for(int focus=0;focus<4;focus++)for(int count=2;count<=4;count+=2){
  struct xz_shell u;struct xz_shell_model m,view;struct xz_shell_action out[XZ_SHELL_ACTIONS];
  xz_shell_init(&u);fx_model_four_players(&m,&track);u.deck=focus;m.sides_extra=(uint8_t)sides;m.wave_order=order;
  struct xz_shell_model original=m;
  struct xz_shell_rect button=xz_shell_play_rect(&m,count==2?XZ_SHELL_R_VIEW_TWO:XZ_SHELL_R_VIEW_FOUR,0);
  assert(button.w>=44&&button.h>0);
  assert(!tap(&u,&m,button.x+button.w/2,43,out));
  assert(!tap(&u,&m,button.x+button.w/2,43,out));
  assert(u.play_decks==count&&u.deck==focus&&!memcmp(&m,&original,sizeof m));
  xz_shell_play_projection(&u,&m,&view);
  assert(view.deck_count==4&&view.play_view_count==count&&!memcmp(view.deck,m.deck,sizeof m.deck));
  render(&u,&m);
  for(int slot=0;slot<count;slot++){
   int d=count==2?slot+2*((sides>>slot)&1):slot;
   int selected=d==(d&1)+2*((sides>>(d&1))&1);
   int row=count==2?(d&1):order?columns[d]:d;
   struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d),strip=xz_shell_play_rect(&view,XZ_SHELL_R_STRIP,d);
   assert(tag.y==26+row*(count==2?119:78));
   assert(strip.x==(count==2?(d&1)*400:columns[d]*200)+2);
   uint32_t fill=selected?SH_BLUE:SH_RAIL;
   number_at(6,tag.y+1,d,fill);title_at(tag.y,m.deck[d].title);
   char text[8];snprintf(text,sizeof text,"%d",d+1);
   int width=count==2?64:56,pad=count==2?8:6,by=count==2?5:2;
   int nx=selected?5:(width-sh_num_width(SH_NUM_MID,text))/2;
   number_at(strip.x+pad+nx,strip.y+by+1,d,fill);
  }
  for(int slot=0;slot<count;slot++){
   int d=count==2?slot+2*((sides>>slot)&1):slot;
   struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d);u.deck=focus;
   size_t n=tap(&u,&m,12,tag.y+tag.h/2,out);
   assert(u.deck==d&&m.sides_extra==sides&&n==(size_t)(focus!=d));
   if(n)assert(out[0].kind==XZ_SHELL_ACT_ACTIVE_DECK&&out[0].deck==d);
   struct xz_shell_rect ov=xz_shell_play_rect(&view,XZ_SHELL_R_OVERVIEW,d);
   n=tap(&u,&m,ov.x+ov.w/2,ov.y+ov.h/2,out);
   assert(d>=2?n==1&&out[0].kind==XZ_SHELL_ACT_SEEK&&out[0].deck==d:n==0);
   if(count==2)assert(!xz_shell_play_rect(&view,XZ_SHELL_R_LANE,d^2).w);
  }
 }
 puts("PASS 2/4 view:44px idempotent hits, all pairs/orders/focuses, complete guarded frames, large logical numbers/titles, fixed3/1/2/4, correct seek targets");
}
static void test_grid_focus(void){
 for(int sides=0;sides<4;sides++)for(int focus=0;focus<4;focus++){
  struct xz_shell u;struct xz_shell_model m,view;struct xz_shell_action out[XZ_SHELL_ACTIONS];
  xz_shell_init(&u);fx_model_four_players(&m,&track);u.play_decks=2;u.deck=focus;u.grid_mode=1;m.sides_extra=(uint8_t)sides;
  m.capabilities|=XZ_SHELL_CAP_GRID|XZ_SHELL_CAP_GRID_EDIT;
  for(int d=0;d<4;d++)m.deck[d].caps_off&=~(XZ_SHELL_CAP_GRID|XZ_SHELL_CAP_GRID_EDIT);
  int expected=(focus&1)+2*((sides>>(focus&1))&1);
  size_t n=xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,1,0,-1},out);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_GRID_ADJUST&&out[0].deck==expected&&u.deck==focus);
  xz_shell_play_projection(&u,&m,&view);struct xz_shell_rect r=xz_shell_play_rect(&view,XZ_SHELL_R_GRID_SNAP,expected);
  n=tap(&u,&m,r.x+r.w/2,r.y+r.h/2,out);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_GRID_EDIT&&out[0].deck==expected&&u.deck==focus);
  m.deck[expected].caps_off|=XZ_SHELL_CAP_GRID|XZ_SHELL_CAP_GRID_EDIT;
  n=xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,1,0,-1},out);
  for(size_t i=0;i<n;i++)assert(out[i].kind!=XZ_SHELL_ACT_GRID_ADJUST&&out[i].kind!=XZ_SHELL_ACT_GRID_EDIT);
  assert(u.deck==focus&&m.deck_count==4&&m.sides_extra==sides);
 }
 puts("PASS pair GRID:visible same-side target, target capabilities, stored browse focus unchanged");
}
static void test_owner_and_availability(void){
 for(int side=0;side<2;side++){
  struct xz_shell u;struct xz_shell_model m,view;struct xz_shell_action out[XZ_SHELL_ACTIONS];
  xz_shell_init(&u);fx_model_four_players(&m,&track);u.play_decks=2;u.deck=1;m.sides_extra=3;
  int owner=side+2;xz_shell_play_projection(&u,&m,&view);struct xz_shell_rect ov=xz_shell_play_rect(&view,XZ_SHELL_R_OVERVIEW,owner);
  assert(xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,ov.x+30,ov.y+10,0,0,-1},out)==1&&out[0].deck==owner);
  m.sides_extra^=(uint8_t)(1u<<side);xz_shell_play_projection(&u,&m,&view);
  assert(sh_play_deck(&view,side)==side&&sh_play_deck(&view,side^1)==((side^1)+2)&&u.deck==1);
  assert(xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_MOVE,ov.x+60,ov.y+10,0,0,-1},out)==1&&out[0].deck==owner);
  assert(!xz_shell_input(&u,&m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,ov.x+60,ov.y+10,0,0,-1},out)&&!u.down&&u.deck==1);
  m.deck_count=2;u.play_decks=4;xz_shell_play_projection(&u,&m,&view);
  assert(view.deck_count==2&&view.play_view_count==2&&!xz_shell_play_rect(&view,XZ_SHELL_R_VIEW_FOUR,0).w);
  render(&u,&m);
 }
 puts("PASS pair lifetime:original logical touch owner, independent other side, native availability fallback");
}
int main(void){fx_track_build(&track,7);test_views();test_grid_focus();test_owner_and_availability();return 0;}
