#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The side mark (sh_side_mark): which physical side a deck belongs to, drawn as the owner asked: a black box for
   the unit with its two jog wheels, the other side's dotted. This deck's own wheel also says whether its side is
   on it now: a white disc with a black stroke while the side drives the deck (it is under the hand), a mid grey
   disc (SH_WHEEL_DIM) while the side is on its other deck. The tile's fill says the same.
   Pinned here:
   - the three wheels apart on every tile colour (blue, grey, white, light and deep USB stick colours): all white,
     all mid grey, twelve white dots on black; the box and wheel places the same in both states;
   - the state of every deck in every pair, numeric and mixer order, on the four lanes, the four strips and the
     four-deck status strip under BROWSE and the other screens;
   - no mark, arrow or word on 2 DECKS (its two decks are always the ones the sides drive), with the number
     centred in the strip badge;
   - 4 DECKS waiting for decks 3 and 4: decks 1 and 2 are driven, the placeholders carry no mark;
   - decks 3 and 4 up without the deck select seam: no mark (the sides are unknown);
   - a side switch changes only the tiles and badges of that side's two decks, one own wheel going white as the
     other dims, and no touch answers differently;
   - the status strip's track titles: a deck's title draws only in its own quarter's title row (left of the BPM
     box, cut there), nowhere else on BROWSE, in every pair and both orders and at INFO's lower strip height; an
     empty deck draws none, and a loaded deck whose title is not known yet shows "---" as the play tiles do. */
#include "four_deck_fixtures.h"
#include "browse_fixtures.h"
#include "shell_internal.h"
#include "../ui/layout_cycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=XZ_SHELL_WIDTH, H=XZ_SHELL_HEIGHT };
static struct fx_track track;
static uint16_t a[W*H],b[W*H];
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static uint16_t px(const uint16_t *p,int x,int y){return p[y*W+x];}
static void render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *p){
 memset(p,0,sizeof a);assert(xz_shell_render(u,m,p,W));
}
static void model(struct xz_shell_model *m,int layout,int sides){
 fx_model_four_players(m,&track);m->stems_known=1;m->layout_preference=layout+1;m->sides_extra=(uint8_t)sides;
}
/* The own wheel's centre (left for 1 and 3, right for 2 and 4) in a w x h mark centred on cx with wheels of r. */
static int own_wheel_x(int cx,int w,int h,int r,int deck){
 int x=cx-w/2,inset=(h-2*r-1)/2;
 return deck&1?x+w-1-inset-r:x+inset+r;
}
/* The mark (the owner's drawing): a black w x h unit box centred on cx, cy with a grey edge, the tile just left
   of it; this deck's wheel (left for 1 and 3, right for 2 and 4) a disc with a black stroke, white while the
   side drives the deck and mid grey while the side is on its other deck; the other wheel a dotted white circle:
   white dots on its rim, black between them and inside. */
static void check_mark(const uint16_t *p,int cx,int cy,int w,int h,int r,int deck,enum sh_side side,uint32_t tile){
 int x=cx-w/2,y=cy-h/2,inset=(h-2*r-1)/2,left=x+inset+r,right=x+w-1-inset-r;
 int own=deck&1?right:left,far=deck&1?left:right;
 uint16_t white=rgb565(SH_WHITE),black=rgb565(SH_BLACK),edge=rgb565(SH_UNIT_EDGE);
 assert(side==SH_SIDE_DRIVES||side==SH_SIDE_OTHER);
 uint16_t wheel=rgb565(side==SH_SIDE_DRIVES?SH_WHITE:SH_WHEEL_DIM);
 assert(own==own_wheel_x(cx,w,h,r,deck));
 assert(px(p,x,y)==edge&&px(p,x+w-1,y)==edge&&px(p,x,y+h-1)==edge&&px(p,x+w-1,y+h-1)==edge&&px(p,cx,y)==edge);
 assert(px(p,x-1,cy)==rgb565(tile));
 assert(px(p,x+1,y+1)==black&&px(p,cx,cy)==black&&px(p,x+w-2,y+h-2)==black);
 assert(px(p,own,cy)==wheel&&px(p,own-r+2,cy)==wheel&&px(p,own+r-2,cy)==wheel&&px(p,own,cy-r+2)==wheel);
 assert(px(p,own+r+1,cy)==black&&px(p,own-r-1,cy)==black);
 assert(px(p,far,cy)==black&&px(p,far+1,cy+1)==black&&px(p,far-2,cy)==black);
 assert(px(p,far+r,cy)==white&&px(p,far-r,cy)==white&&px(p,far,cy-r)==white&&px(p,far,cy+r)==white);
 assert(px(p,far+r,cy+1)==black&&px(p,far-r,cy-1)==black);
}
/* Nothing but the tile's colour in x0..x1, rows y0..y1. */
static void check_empty(const uint16_t *p,int x0,int x1,int y0,int y1,uint32_t tile){
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)assert(px(p,x,y)==rgb565(tile));
}
/* The three wheels on every tile colour the brief names, both mark sizes, both sides: the driven wheel all white,
   the dimmed one all mid grey in the same pixels, the other side twelve white dots on black; the box and both
   wheels in the same places in both states, so a side switch changes only the own wheel's colour. */
static void test_wheels(void){
 static const uint32_t tiles[]={SH_BLUE,SH_RAIL,0xf0f0f0,0xffe020,0xff8cc8,0x7cf0a0,0x9b3cff,0xe02020,0x1a7a3a,0x3c3cff};
 static const int widths[2]={32,28};
 static uint16_t s[2][48*24];
 enum { SW=48, SH=24, R=5 };
 /* Mid grey: well apart in lightness from the black box behind it and from the white wheel (a contrast of about
    four to one each way), and not the box edge's grey. */
 uint16_t dim=rgb565(SH_WHEEL_DIM),white=rgb565(SH_WHITE),black=rgb565(SH_BLACK);
 assert((SH_WHEEL_DIM&0xff)>=0x60&&(SH_WHEEL_DIM&0xff)<=0x80&&SH_WHEEL_DIM==(SH_WHEEL_DIM&0xff)*0x010101u);
 assert(dim!=rgb565(SH_UNIT_EDGE)&&dim!=white&&dim!=black);
 for(unsigned t=0;t<sizeof tiles/sizeof *tiles;t++)for(int k=0;k<2;k++)for(int deck=0;deck<2;deck++){
  int w=widths[k],cx=SW/2,cy=SH/2,own=own_wheel_x(cx,w,16,R,deck),far=own_wheel_x(cx,w,16,R,deck^1);
  for(int state=0;state<2;state++){
   struct xz_canvas c={s[state],SW,SW,SH,XZ_THEME_FLAGSHIP};
   xz_rect(c,0,0,SW,SH,tiles[t]);
   sh_side_mark(c,cx,cy,w,16,R,deck,state?SH_SIDE_OTHER:SH_SIDE_DRIVES);
  }
  int solid=0,dots=0;
  for(int y=0;y<SH;y++)for(int x=0;x<SW;x++){
   uint16_t on=s[0][y*SW+x],off=s[1][y*SW+x];
   int dx=x-own,dy=y-cy,in_own=dx*dx+dy*dy<=(R+1)*(R+1);
   /* Only the own wheel differs; its solid pixels are white against mid grey, pixel for pixel, and its
      smoothed rim is darker when dimmed. */
   if(on!=off)assert(in_own&&((on>>5)&63)>((off>>5)&63));
   if(on==white&&in_own){assert(off==dim);solid++;}
   if(in_own&&dx*dx+dy*dy<=(R-2)*(R-2))assert(on==white&&off==dim);
   int fx=x-far,fy=y-cy;
   if(fx>=-R&&fx<=R&&fy>=-R&&fy<=R){assert(on==white||on==black);dots+=on==white;}
  }
  /* A full disc, not a ring: the solid white (or grey) fills the wheel inside its stroke. */
  assert(solid>=37);
  assert(dots==12&&s[0][cy*SW+far]==black);
 }
 puts("PASS three wheels apart on every tile colour, both sizes, both sides; nothing moves between states");
}
enum { MARK_X=46 }; /* shell_play.c TILE_MARK_X */
static struct xz_shell_rect strip_badge(const struct xz_shell_model *view,int deck){
 struct xz_shell_rect s=xz_shell_play_rect(view,XZ_SHELL_R_STRIP,deck);
 return sh_play_deck_count(view)==4?(struct xz_shell_rect){s.x+6,s.y+2,56,22}:(struct xz_shell_rect){s.x+8,s.y+5,44,22};
}
static void check_four(const struct xz_shell *u,const struct xz_shell_model *m,const uint16_t *p){
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 assert(sh_play_deck_count(&view)==4);
 for(int d=0;d<4;d++){
  enum sh_side side=sh_side_of(&view,d);
  if(sh_play_placeholder(&view,d)){assert(side==SH_SIDE_NONE);continue;}
  uint32_t tile=side==SH_SIDE_DRIVES?SH_BLUE:SH_RAIL;
  struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d),badge=strip_badge(&view,d);
  if(side==SH_SIDE_NONE){check_empty(p,26,64,tag.y+2,tag.y+tag.h-3,tile);check_empty(p,badge.x+28,badge.x+53,badge.y+2,badge.y+19,tile);continue;}
  check_mark(p,MARK_X,tag.y+tag.h/2,32,16,5,d,side,tile);
  check_mark(p,badge.x+24+1+16,badge.y+11,28,16,5,d,side,tile);
 }
}

static void test_four_lanes(void){
 struct xz_shell u;struct xz_shell_model m;
 for(int layout=XZ_LAYOUT_FOUR;layout<=XZ_LAYOUT_MIXER;layout++)for(int sides=0;sides<4;sides++)for(int focus=0;focus<4;focus++){
  xz_shell_init(&u);u.deck=focus;model(&m,layout,sides);render(&u,&m,a);
  for(int d=0;d<4;d++)assert(sh_side_of(&m,d)==(d==(d&1)+2*((sides>>(d&1))&1)?SH_SIDE_DRIVES:SH_SIDE_OTHER));
  check_four(&u,&m,a);
 }
 /* Waiting for decks 3 and 4: decks 1 and 2 are the sides' only decks. */
 xz_shell_init(&u);fx_model_full(&m,&track);m.layout_preference=XZ_LAYOUT_FOUR+1;render(&u,&m,a);
 {struct xz_shell_model view;xz_shell_play_projection(&u,&m,&view);assert(view.play_placeholders==0xc);
  assert(sh_side_of(&view,0)==SH_SIDE_DRIVES&&sh_side_of(&view,1)==SH_SIDE_DRIVES&&sh_side_of(&view,2)==SH_SIDE_NONE);}
 check_four(&u,&m,a);
 /* Decks 3 and 4 up without the deck select seam: the sides are unknown, so no deck is marked. */
 xz_shell_init(&u);model(&m,XZ_LAYOUT_FOUR,1);m.capabilities&=~XZ_SHELL_CAP_DECK_SELECT;render(&u,&m,a);
 for(int d=0;d<4;d++)assert(sh_side_of(&m,d)==SH_SIDE_NONE);
 check_four(&u,&m,a);
 puts("PASS four lanes and strips: every pair, both orders, every focus, waiting and unknown sides");
}
static void test_two_decks(void){
 for(int sides=0;sides<4;sides++)for(int focus=0;focus<4;focus++){
  struct xz_shell u;struct xz_shell_model m,view;xz_shell_init(&u);u.deck=focus;model(&m,XZ_LAYOUT_TWO,sides);
  render(&u,&m,a);xz_shell_play_projection(&u,&m,&view);assert(sh_play_deck_count(&view)==2);
  for(int slot=0;slot<2;slot++){
   int d=sh_play_deck(&view,slot);
   /* The two decks on screen are the ones the sides drive... */
   assert(sh_side_of(&view,d)==SH_SIDE_DRIVES);
   /* ...so the tile carries the number and the beat dots, nothing between, and the badge only the number. */
   struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d),badge=strip_badge(&view,d);
   check_empty(a,28,64,tag.y+2,tag.y+tag.h-3,SH_BLUE);
   check_empty(a,badge.x+33,badge.x+badge.w-3,badge.y+2,badge.y+badge.h-3,SH_BLUE);
   char text[4];snprintf(text,sizeof text,"%d",d+1);
   if(d<2){
    int nx=badge.x+(badge.w-sh_num_width(SH_NUM_MID,text))/2,ink=0;
    for(int y=badge.y+1;y<badge.y+badge.h-1;y++)for(int x=badge.x+2;x<nx;x++)assert(px(a,x,y)==rgb565(SH_BLUE));
    for(int y=badge.y+1;y<badge.y+badge.h-1;y++)for(int x=nx;x<nx+sh_num_width(SH_NUM_MID,text);x++)ink+=px(a,x,y)!=rgb565(SH_BLUE);
    assert(ink>0);
   }
  }
 }
 puts("PASS 2 DECKS: the sides' own decks, no mark, the number centred in its badge");
}
static void test_quarters(void){
 static uint16_t q[W*60];
 for(int sides=0;sides<4;sides++){
  struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);model(&m,XZ_LAYOUT_TWO,sides);
  memset(q,0,sizeof q);struct xz_canvas c={q,W,W,60,XZ_THEME_FLAGSHIP};
  struct xz_shell_rect band={0,0,W,XZ_SHELL_HEIGHT-422};
  assert(sh_deck_quarters(&u,&m,c,band));
  for(int d=0;d<4;d++){
   struct xz_shell_rect cell=sh_strip_cell(&m,band,d);
   int seam=sh_strip_column(4,d)?1:0,bx=cell.x+seam+4,by=cell.y+4,bh=cell.h-7;
   enum sh_side side=sh_side_of(&m,d);
   check_mark(q,bx+24,by+23+(bh-23)/2,32,16,5,d,side,side==SH_SIDE_DRIVES?SH_BLUE:SH_RAIL);
  }
 }
 puts("PASS four-deck status strip: every deck marked in every pair");
}
/* One side switches decks: only that side's two tiles and two strip badges change, and every touch answers
   as before. */
static void tap_all(const struct xz_shell *u0,const struct xz_shell_model *m,struct xz_shell_action *log,size_t *count){
 size_t k=0;
 for(int y=1;y<H;y+=6)for(int x=1;x<W;x+=6){
  struct xz_shell u=*u0;struct xz_shell_action out[XZ_SHELL_ACTIONS],rest[XZ_SHELL_ACTIONS];
  memset(out,0,sizeof out);memset(rest,0,sizeof rest);
  size_t n=xz_shell_input(&u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,x,y,0,0,-1},out);
  size_t r=xz_shell_input(&u,m,&(struct xz_shell_event){XZ_SHELL_TOUCH_UP,x,y,0,0,-1},rest);
  for(size_t i=0;i<r&&n<XZ_SHELL_ACTIONS;i++)out[n++]=rest[i];
  memset(&log[k],0,sizeof *log);log[k].kind=(enum xz_shell_action_kind)n;k++;
  for(size_t i=0;i<n;i++)log[k++]=out[i];
 }
 *count=k;
}
static struct xz_shell_action logs[2][(W/6+1)*(H/6+1)*(XZ_SHELL_ACTIONS+1)];
static void test_switch(void){
 for(int layout=XZ_LAYOUT_FOUR;layout<=XZ_LAYOUT_MIXER;layout++)for(int sides=0;sides<4;sides++)for(int side=0;side<2;side++){
  struct xz_shell u;struct xz_shell_model m,n,view;xz_shell_init(&u);
  model(&m,layout,sides);model(&n,layout,sides^(1<<side));render(&u,&m,a);render(&u,&n,b);
  xz_shell_play_projection(&u,&m,&view);
  int changed=0;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(px(a,x,y)!=px(b,x,y)){
   int inside=0;changed++;
   for(int d=side;d<4;d+=2){
    struct xz_shell_rect t=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d),s=strip_badge(&view,d);
    inside|=sh_inside(t,x,y)||sh_inside(s,x,y);
   }
   assert(inside);
  }
  assert(changed>0);
  /* That side's two decks trade wheels: the one it leaves dims, the one it takes goes white, in tile and badge. */
  {struct xz_shell_model nview;xz_shell_play_projection(&u,&n,&nview);int whites[2]={0,0};
   for(int d=side;d<4;d+=2){
    enum sh_side was=sh_side_of(&view,d),now=sh_side_of(&nview,d);
    assert(was!=SH_SIDE_NONE&&now!=SH_SIDE_NONE&&was!=now);
    uint16_t before=rgb565(was==SH_SIDE_DRIVES?SH_WHITE:SH_WHEEL_DIM),after=rgb565(now==SH_SIDE_DRIVES?SH_WHITE:SH_WHEEL_DIM);
    struct xz_shell_rect t=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d),s=strip_badge(&view,d);
    int tx=own_wheel_x(MARK_X,32,16,5,d),ty=t.y+t.h/2,sx=own_wheel_x(s.x+24+1+16,28,16,5,d),sy=s.y+11;
    assert(px(a,tx,ty)==before&&px(b,tx,ty)==after&&px(a,sx,sy)==before&&px(b,sx,sy)==after);
    whites[0]+=was==SH_SIDE_DRIVES;whites[1]+=now==SH_SIDE_DRIVES;
    check_mark(b,MARK_X,ty,32,16,5,d,now,now==SH_SIDE_DRIVES?SH_BLUE:SH_RAIL);
   }
   assert(whites[0]==1&&whites[1]==1);}
  size_t ka,kb;tap_all(&u,&m,logs[0],&ka);tap_all(&u,&n,logs[1],&kb);
  assert(ka==kb&&!memcmp(logs[0],logs[1],ka*sizeof logs[0][0]));
 }
 /* The four-deck status strip (under BROWSE, INFO, UTILITY) changes only in the side's two quarter badges. */
 for(int sides=0;sides<4;sides++)for(int side=0;side<2;side++){
  static uint16_t q[2][W*60];struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);
  struct xz_shell_rect band={0,0,W,XZ_SHELL_HEIGHT-422};
  for(int k=0;k<2;k++){
   model(&m,XZ_LAYOUT_TWO,sides^(k<<side));memset(q[k],0,sizeof q[k]);
   assert(sh_deck_quarters(&u,&m,(struct xz_canvas){q[k],W,W,60,XZ_THEME_FLAGSHIP},band));
  }
  int changed=0;
  for(int y=0;y<band.h;y++)for(int x=0;x<W;x++)if(q[0][y*W+x]!=q[1][y*W+x]){
   int inside=0;changed++;
   for(int d=side;d<4;d+=2){
    struct xz_shell_rect cell=sh_strip_cell(&m,band,d);int seam=sh_strip_column(4,d)?1:0;
    inside|=sh_inside((struct xz_shell_rect){cell.x+seam+4,cell.y+4,48,cell.h-7},x,y);
   }
   assert(inside);
  }
  assert(changed>0);
  for(int d=side;d<4;d+=2){
   struct xz_shell_rect cell=sh_strip_cell(&m,band,d);int seam=sh_strip_column(4,d)?1:0;
   int bx=cell.x+seam+4,by=cell.y+4,bh=cell.h-7,wx=own_wheel_x(bx+24,32,16,5,d),wy=by+23+(bh-23)/2;
   int was=d==(d&1)+2*((sides>>(d&1))&1);
   assert(q[0][wy*W+wx]==rgb565(was?SH_WHITE:SH_WHEEL_DIM)&&q[1][wy*W+wx]==rgb565(was?SH_WHEEL_DIM:SH_WHITE));
  }
 }
 puts("PASS a side switch changes only its two decks' tiles and badges, one wheel white as the other dims; every touch answers alike");
}
/* The track title row of deck d's quarter in band: from the text column to 6 px left of the BPM box, from the
   quarter's top to under the small face's descenders (shell_wave.c Q_TITLE_Y, Q_TIME_Y). */
static struct xz_shell_rect title_row(const struct xz_shell_model *m,struct xz_shell_rect band,int d){
 struct xz_shell_rect cell=sh_strip_cell(m,band,d);
 int seam=sh_strip_column(4,d)?1:0,x=cell.x+seam,w=cell.w-seam-1,tx=x+58,px=x+w-4-52;
 return (struct xz_shell_rect){tx,cell.y+1+2,px-6-tx,13};
}
static int white_in(const uint16_t *p,int stride,struct xz_shell_rect r){
 int n=0;
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=p[y*stride+x]==rgb565(SH_WHITE);
 return n;
}
static void test_strip_titles(void){
 static struct bx_library lib;static struct xz_shell_browse_model browse;static struct xz_shell_action out[XZ_SHELL_ACTIONS];
 static const char *const long_title="The Extraordinarily Long Extended Twelve Inch Club Mix of Everything (Remastered)";
 bx_build(&lib);(void)bx_folders;
 struct xz_shell_rect band={0,422,W,H-422};
 for(int layout=XZ_LAYOUT_FOUR;layout<=XZ_LAYOUT_MIXER;layout++)for(int sides=0;sides<4;sides++)for(int d=0;d<4;d++){
  struct xz_shell u;struct xz_shell_model m[2];
  for(int k=0;k<2;k++){
   model(&m[k],layout,sides);m[k].wave_order=layout==XZ_LAYOUT_MIXER;
   m[k].capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;m[k].browse=&browse;
   for(int e=0;e<4;e++){m[k].deck[e].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TITLE);m[k].deck[e].title="";}
   if(k)m[k].deck[d].title=long_title;
  }
  bx_tracks(&browse,&lib,XZ_SHELL_BROWSE);
  xz_shell_init(&u);xz_shell_input(&u,&m[0],&(struct xz_shell_event){XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1},out);
  assert(u.stack[u.depth-1].screen==XZ_SHELL_BROWSE);
  render(&u,&m[0],a);render(&u,&m[1],b);
  /* Only deck d's title row changes, and the title is there. */
  struct xz_shell_rect row=title_row(&m[1],band,d);
  int changed=0;
  for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(a[y*W+x]!=b[y*W+x]){changed++;assert(sh_inside(row,x,y));}
  assert(changed>0&&white_in(b,W,row)>40&&white_in(a,W,row)==0);
  /* The rows of the other three decks hold no white with the titles empty. */
  for(int e=0;e<4;e++)assert(white_in(b,W,title_row(&m[1],band,e))==(e==d?white_in(b,W,row):0));
 }
 /* An empty deck draws no title, whatever pointer it still holds; a loaded deck whose title is not known yet
    shows "---" (the play tiles' dim dashes) and no white. INFO and UTILITY's strip is 48 px: the title still
    sits in its row there. */
 for(int h=48;h<=58;h+=10){
  static uint16_t q[2][W*60];
  struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);model(&m,XZ_LAYOUT_FOUR,0);
  struct xz_shell_rect qb={0,0,W,h};
  for(int e=0;e<4;e++){m.deck[e].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TITLE);m.deck[e].title=long_title;}
  memset(q[0],0,sizeof q[0]);assert(sh_deck_quarters(&u,&m,(struct xz_canvas){q[0],W,W,60,XZ_THEME_FLAGSHIP},qb));
  for(int e=0;e<4;e++)assert(white_in(q[0],W,title_row(&m,qb,e))>40);
  fx_deck_unloaded(&m.deck[3],XZ_SHELL_SRC_USB1);
  m.deck[3].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TITLE);m.deck[3].title=long_title;
  m.deck[2].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_TITLE);
  memset(q[1],0,sizeof q[1]);assert(sh_deck_quarters(&u,&m,(struct xz_canvas){q[1],W,W,60,XZ_THEME_FLAGSHIP},qb));
  assert(white_in(q[1],W,title_row(&m,qb,3))==0);
  struct xz_shell_rect untitled=title_row(&m,qb,2);int dim=0;
  for(int y=untitled.y;y<untitled.y+untitled.h;y++)for(int x=untitled.x;x<untitled.x+untitled.w;x++)dim+=q[1][y*W+x]!=rgb565(SH_CHROME);
  assert(white_in(q[1],W,untitled)==0&&dim>0);
  for(int e=0;e<2;e++){
   struct xz_shell_rect r=title_row(&m,qb,e);
   for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)assert(q[0][y*W+x]==q[1][y*W+x]);
  }
 }
 puts("PASS status strip titles: each in its own quarter's row only, both orders and every pair, none on an empty deck");
}
int main(void){
 fx_track_build(&track,7);
 test_wheels();test_four_lanes();test_two_decks();test_quarters();test_switch();test_strip_titles();
 return 0;
}
