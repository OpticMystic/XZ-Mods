/* SOURCE screen, after the flagship device chooser: one large tile per device
   slot with its icon on the owner's background colour, status, free space,
   track count and the decks playing from it, over the compact deck strip. */
#include "shell_source.h"
#include "shell_info_chrome.h"

enum {
 TILE_GAP=8, TILE_X=8, TILE_Y=SI_HEAD_H+8, TILE_W=(XZ_SHELL_WIDTH-2*TILE_X-3*TILE_GAP)/4, TILE_H=SI_STRIP_Y-8-TILE_Y,
 ICON_H=136, NAME_H=44, PAD=12,
 LINK_X=TILE_X+TILE_W/2, LINK_W=XZ_SHELL_WIDTH-2*LINK_X, LINK_Y=TILE_Y, LINK_HEAD=40, LINK_ROW=48
};

struct xz_shell_rect xz_shell_source_rect(int tile) {
 if(tile==-1)return si_close_rect();
 if(tile<0||tile>=XZ_SHELL_SOURCE_TILES)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){TILE_X+tile*(TILE_W+TILE_GAP),TILE_Y,TILE_W,TILE_H};
}

struct xz_shell_rect xz_shell_source_link_rect(int row) {
 if(row<0||row>=XZ_SHELL_LINK_ROWS)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){LINK_X,LINK_Y+LINK_HEAD+row*LINK_ROW,LINK_W,LINK_ROW-4};
}
static int known(const struct xz_shell_device *d, enum xz_shell_device_field f) { return (d->known>>f)&1; }
static int connected(const struct xz_shell_source_model *sm, int i) {
 const struct xz_shell_device *d=&sm->device[i];
 return known(d,XZ_SHELL_D_STATUS)&&(d->status==XZ_SHELL_DEVICE_LOADING||d->status==XZ_SHELL_DEVICE_READY);
}
/* A slot that opens in the stock screen. */
static int stock(const struct xz_shell_source_model *sm, int i) {
 const struct xz_shell_device *d=&sm->device[i];
 return known(d,XZ_SHELL_D_STATUS)&&d->status==XZ_SHELL_DEVICE_STOCK;
}
/* A tile the cursor can rest on and a push or second touch can select. */
static int selectable(const struct xz_shell_source_model *sm, int i) { return connected(sm,i)||stock(sm,i); }
static int ready(const struct xz_shell_source_model *sm, int i) {
 const struct xz_shell_device *d=&sm->device[i];
 return known(d,XZ_SHELL_D_STATUS)&&d->status==XZ_SHELL_DEVICE_READY;
}
static int current_tile(const struct xz_shell_source_model *sm) {
 int i=(int)sm->current-1;
 return sm->current_known&&i>=0&&i<XZ_SHELL_SOURCE_TILES?i:-1;
}
/* The highlighted tile: the view's cursor when it names a connected tile (or a slot a USB button
   asked for, pinned by the view's focus, connected or not), else the current browse source, else
   the first connected device; -1 when none is. */
static int cursor_tile(const struct xz_shell *u, const struct xz_shell_source_model *sm) {
 int c=si_view(u)->cursor-1;
 if(c>=0&&c<XZ_SHELL_SOURCE_TILES&&(selectable(sm,c)||si_view(u)->focus))return c;
 int cur=current_tile(sm);
 if(cur>=0&&connected(sm,cur))return cur;
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)if(connected(sm,i))return i;
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)if(stock(sm,i))return i;
 return -1;
}

/* ---- icons, drawn centred on (cx, cy) ---- */
static void outline(struct xz_canvas c, int x, int y, int w, int h, int radius, uint32_t ink, uint32_t bg) {
 xz_round_rect(c,x,y,w,h,radius,15,ink);
 xz_round_rect(c,x+3,y+3,w-6,h-6,radius>3?radius-3:0,15,bg);
}
static void icon_usb(struct xz_canvas c, int cx, int cy, uint32_t ink, uint32_t bg) {
 outline(c,cx-11,cy-34,22,18,2,ink,bg);
 xz_rect(c,cx-6,cy-28,4,5,ink);xz_rect(c,cx+2,cy-28,4,5,ink);
 xz_round_rect(c,cx-17,cy-17,34,52,7,15,ink);
 xz_disc(c,cx,cy+26,3,bg);
}
static void icon_player(struct xz_canvas c, int x, int y, uint32_t ink, uint32_t bg) {
 outline(c,x,y,40,48,4,ink,bg);
 xz_rect(c,x+8,y+8,24,8,ink);
 xz_ring(c,x+20,y+32,8,3,ink);
}
static void icon_link(struct xz_canvas c, int cx, int cy, uint32_t ink, uint32_t bg) {
 icon_player(c,cx-50,cy-24,ink,bg);
 icon_player(c,cx+10,cy-24,ink,bg);
 xz_rect(c,cx-10,cy-1,20,3,ink);
 xz_disc(c,cx,cy,4,ink);
}
static void icon_computer(struct xz_canvas c, int cx, int cy, uint32_t ink, uint32_t bg) {
 outline(c,cx-32,cy-26,64,42,3,ink,bg);
 xz_round_rect(c,cx-42,cy+18,84,7,3,15,ink);
 xz_rect(c,cx-8,cy+18,16,2,bg);
}

static void fmt_bytes(char *value, size_t vcap, const char **unit, uint64_t bytes) {
 static const char *const units[]={"KB","MB","GB","TB"};
 uint64_t scale=1024;int u=0;
 while(u<3&&bytes>=scale*1024){scale*=1024;u++;}
 uint64_t tenths=bytes/scale*10+bytes%scale*10/scale;
 if(tenths>=1000)snprintf(value,vcap,"%llu",(unsigned long long)(tenths/10));
 else snprintf(value,vcap,"%llu.%llu",(unsigned long long)(tenths/10),(unsigned long long)(tenths%10));
 *unit=units[u];
}
static void render_tile(const struct xz_shell_model *m, struct xz_canvas c, int i, int highlighted) {
 const struct xz_shell_source_model *sm=m->source;
 const struct xz_shell_device *d=&sm->device[i];
 struct xz_shell_rect r=xz_shell_source_rect(i);
 enum xz_shell_source slot=(enum xz_shell_source)(i+1);
 int status_known=known(d,XZ_SHELL_D_STATUS),live=connected(sm,i)||stock(sm,i);
 char text[32];
 xz_rect(c,r.x,r.y,r.w,r.h,SH_CHROME);
 /* icon block on the device's colour */
 uint32_t fill=!live?0x1e1e1e:known(d,XZ_SHELL_D_COLOR)?d->color&0xffffff:SH_RAIL;
 uint32_t ink=!live?0x5a5a5a:si_ink_on(fill);
 xz_rect(c,r.x,r.y,r.w,ICON_H,fill);
 int cx=r.x+r.w/2,cy=r.y+ICON_H/2+2;
 switch(slot){
 case XZ_SHELL_SRC_LINK:icon_link(c,cx,cy,ink,fill);break;
 case XZ_SHELL_SRC_COMPUTER:icon_computer(c,cx,cy,ink,fill);break;
 default:icon_usb(c,cx,cy,ink,fill);break;
 }
 if(current_tile(sm)==i&&live)sh_chip(c,r.x+8,r.y+8,"BROWSING",ink,ink,0);
 /* name bar */
 int ny=r.y+ICON_H;
 xz_rect(c,r.x,ny,r.w,NAME_H,highlighted?SH_BLUE:SH_RAIL);
 /* Export mode playback is what the stock key behind this slot opens. */
 sh_text(c,r.x+PAD,ny+15,slot==XZ_SHELL_SRC_COMPUTER&&stock(sm,i)?"EXPORT MODE":si_source_name(slot),SH_TEXT,live?SH_WHITE:SH_DIM);
 /* status */
 int y=ny+NAME_H+10;
 const char *word="--";uint32_t word_ink=SH_DIM;
 if(status_known)switch(d->status){
 case XZ_SHELL_DEVICE_READY:word="READY";word_ink=SH_GREEN;break;
 case XZ_SHELL_DEVICE_LOADING:word="LOADING";word_ink=SH_WHITE;break;
 case XZ_SHELL_DEVICE_STOCK:word="DECK'S OWN SCREEN";word_ink=SH_WHITE;break;
 default:word="NOT CONNECTED";word_ink=SH_DIM;break;
 }
 sh_text(c,r.x+PAD,y,word,SH_TEXT,word_ink);
 if(status_known&&d->status==XZ_SHELL_DEVICE_LOADING){
  int dots=(int)(m->clock_ms/400%4),x=r.x+PAD+sh_text_width(c,word,SH_TEXT)+4;
  for(int k=0;k<3;k++)xz_rect(c,x+k*7,y+11,3,3,k<dots?SH_WHITE:SH_RAIL);
 }
 y+=24;
 if(stock(sm,i)){
  /* What the deck's own screen opens for this slot. */
  static const char *const what[XZ_SHELL_SOURCE_TILES]={NULL,NULL,"LINKED PLAYERS' MEDIA. DJ SOFTWARE CONTROL.",
   "A LAPTOP'S OR PHONE'S LIBRARY OVER THE NETWORK."};
  char lines[4][SH_WRAP_CHARS];
  int n=sh_wrap(what[i],r.w-2*PAD,lines,4);
  for(int k=0;k<n;k++)sh_text(c,r.x+PAD,y+k*18,lines[k],SH_SMALL,SH_DIM);
  return;
 }
 if(!live){
  static const char *const hints[XZ_SHELL_SOURCE_TILES]={"Insert a USB device.","Insert a USB device.","No player on LINK.","No computer on LINK."};
  sh_text_fit(c,r.x+PAD,y,r.w-2*PAD,status_known?hints[i]:"Status not read yet.",SH_SMALL,SH_DIM);
  return;
 }
 const char *name=known(d,XZ_SHELL_D_NAME)?d->name:NULL;
 if(name)sh_text_fit(c,r.x+PAD,y,r.w-2*PAD,name,SH_SMALL,SH_DIM);
 else sh_text(c,r.x+PAD,y,"--",SH_SMALL,SH_DIM);
 y+=20;
 xz_rect(c,r.x+PAD,y,r.w-2*PAD,1,SH_ROW_ALT);
 /* free space with a used-space bar when the size is known */
 y+=8;
 sh_text(c,r.x+PAD,y,"FREE",SH_SMALL,SH_DIM);
 const char *unit="";
 if(known(d,XZ_SHELL_D_FREE)){
  fmt_bytes(text,sizeof text,&unit,d->free_bytes);
  sh_num(c,r.x+PAD,y+14,text,SH_NUM_MID,SH_WHITE);
  int ux=r.x+PAD+sh_num_width(SH_NUM_MID,text)+4;
  sh_text(c,ux,y+25,unit,SH_SMALL,SH_WHITE);
  if(known(d,XZ_SHELL_D_TOTAL)&&d->total_bytes>0){
   const char *tunit;char total[16];fmt_bytes(total,sizeof total,&tunit,d->total_bytes);
   snprintf(text,sizeof text,"OF %s %s",total,tunit);
   if(ux+sh_text_width(c,unit,SH_SMALL)+8<=r.x+r.w-PAD-sh_text_width(c,text,SH_SMALL))
    sh_text_right(c,r.x+r.w-PAD,y+25,text,SH_SMALL,SH_DIM);
   uint64_t used=d->free_bytes<d->total_bytes?d->total_bytes-d->free_bytes:0;
   int bw=r.w-2*PAD,fill_w=(int)((double)used*bw/(double)d->total_bytes);
   xz_rect(c,r.x+PAD,y+42,bw,4,SH_RAIL);
   xz_rect(c,r.x+PAD,y+42,fill_w,4,d->free_bytes*10<d->total_bytes?SH_RED:SH_DIM);
  }
 }else sh_num(c,r.x+PAD,y+14,"--",SH_NUM_MID,SH_DIM);
 /* track count */
 y+=56;
 sh_text(c,r.x+PAD,y,"TRACKS",SH_SMALL,SH_DIM);
 if(known(d,XZ_SHELL_D_TRACKS)){
  snprintf(text,sizeof text,"%d",(int)sh_max(0,d->tracks));
  sh_num(c,r.x+PAD,y+14,text,SH_NUM_MID,SH_WHITE);
 }else sh_num(c,r.x+PAD,y+14,"--",SH_NUM_MID,SH_DIM);
 /* decks playing from this device */
 int by=r.y+r.h-30;
 xz_rect(c,r.x+PAD,by-8,r.w-2*PAD,1,SH_ROW_ALT);
 int x=r.x+PAD,any=0;
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
  const struct xz_shell_deck *dk=&m->deck[deck];
  if(!sh_has(dk,XZ_SHELL_F_SOURCE)||dk->source!=slot)continue;
  if(sh_has(dk,XZ_SHELL_F_LOADED)&&!dk->loaded)continue;
  snprintf(text,sizeof text,"DECK %d",deck+1);
  x+=sh_chip(c,x,by+2,text,SH_BLUE,SH_WHITE,1)+6;any=1;
 }
 if(!any)sh_text(c,r.x+PAD,by+5,"ON NO DECK",SH_SMALL,0x6e6e6e);
}
static int picker_open(const struct xz_shell *u, const struct xz_shell_source_model *sm);
static void render_picker(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c);
static void source_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 si_header(c,"SOURCE");
 si_strip(u,m,c);
 if(!m->source){sh_text_center(c,c.width/2,220,"No device information.",SH_TEXT,SH_DIM);return;}
 int highlighted=cursor_tile(u,m->source);
 int any_known=0;
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)any_known|=known(&m->source->device[i],XZ_SHELL_D_STATUS);
 const char *hint=highlighted>=0?(stock(m->source,highlighted)?"OPENS IN THE DECK'S OWN SCREEN":"OPENS ITS LIBRARY IN BROWSE"):
  any_known?"NO DEVICE CONNECTED":"DEVICE STATUS NOT READ YET";
 char missing[40];
 if(highlighted>=0&&!selectable(m->source,highlighted)){
  snprintf(missing,sizeof missing,"%s IS NOT CONNECTED",si_source_name((enum xz_shell_source)(highlighted+1)));
  hint=missing;
 }
 sh_text_right(c,si_close_rect().x-14,17,hint,SH_SMALL,highlighted>=0&&!selectable(m->source,highlighted)?SH_KEY_AMBER:SH_DIM);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)render_tile(m,c,i,i==highlighted);
 if(highlighted>=0){
  struct xz_shell_rect r=xz_shell_source_rect(highlighted);
  xz_border(c,r.x,r.y,r.w,r.h,SH_BLUE);xz_border(c,r.x+1,r.y+1,r.w-2,r.h-2,SH_BLUE);
 }
 if(picker_open(u,m->source))render_picker(u,m,c);
}

/* ---- LINK picker: the players on the network and their media (XZ manual pp.67, 114) ---- */
static int link_count(const struct xz_shell_source_model *sm) {
 return sm->link_media?sh_max(0,sh_min(sm->link_media_count,24)):0;
}
static int picker_open(const struct xz_shell *u, const struct xz_shell_source_model *sm) {
 return sm&&link_count(sm)>0&&si_view(u)->scroll>0;
}
static int picker_cursor(const struct xz_shell *u, const struct xz_shell_source_model *sm) {
 return sh_max(0,sh_min(si_view(u)->scroll-1,link_count(sm)-1));
}
static int picker_first(const struct xz_shell *u, const struct xz_shell_source_model *sm) {
 return sh_max(0,picker_cursor(u,sm)-XZ_SHELL_LINK_ROWS+1);
}
static const char *slot_name(enum xz_shell_link_slot s) {
 return s==XZ_SHELL_LINK_SD?"SD":s==XZ_SHELL_LINK_COMPUTER?"COMPUTER":"USB";
}
static void render_picker(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 const struct xz_shell_source_model *sm=m->source;
 int n=link_count(sm),cur=picker_cursor(u,sm),first=picker_first(u,sm);
 int h=LINK_HEAD+XZ_SHELL_LINK_ROWS*LINK_ROW+8;
 xz_rect(c,LINK_X-2,LINK_Y-2,LINK_W+4,h+4,SH_BLUE);
 xz_rect(c,LINK_X,LINK_Y,LINK_W,h,SH_CHROME);
 sh_text(c,LINK_X+PAD,LINK_Y+13,"LINK: CHOOSE A PLAYER'S MEDIUM",SH_TEXT,SH_WHITE);
 for(int row=0;row<XZ_SHELL_LINK_ROWS&&first+row<n;row++){
  int i=first+row;
  const struct xz_shell_link_media *q=&sm->link_media[i];
  struct xz_shell_rect r=xz_shell_source_link_rect(row);
  int on=i==cur;
  xz_rect(c,r.x,r.y,r.w,r.h,on?SH_BLUE:SH_RAIL);
  xz_rect(c,r.x+8,r.y+8,28,r.h-16,q->color?q->color&0xffffff:0x3c3c3c);
  char text[64];
  snprintf(text,sizeof text,"PLAYER %d  %s",sh_max(0,sh_min(q->player,99)),slot_name(q->slot));
  sh_text(c,r.x+46,r.y+(r.h-SH_CAP_TEXT)/2,text,SH_TEXT,q->ready?SH_WHITE:SH_DIM);
  int nx=r.x+46+sh_text_width(c,"PLAYER 00  COMPUTER",SH_TEXT)+16;
  const char *state=q->ready?(sm->current_known&&sm->current==XZ_SHELL_SRC_LINK&&sm->link_current==i?"BROWSING":""):"LOADING";
  int sw=sh_text_width(c,state,SH_SMALL);
  if(q->name)sh_text_fit(c,nx,r.y+(r.h-SH_CAP_TEXT)/2,r.x+r.w-nx-sw-24,q->name,SH_TEXT,on?SH_WHITE:SH_DIM);
  if(*state)sh_text_right(c,r.x+r.w-12,r.y+(r.h-SH_CAP_SMALL)/2,state,SH_SMALL,on?SH_WHITE:SH_DIM);
 }
}
static size_t select_link(struct xz_shell *u, const struct xz_shell_model *m, int i, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_source_model *sm=m->source;
 if(i<0||i>=link_count(sm)||!sm->link_media[i].ready)return 0;
 u->stack[u->depth-1].scroll=0;
 size_t n=si_emit(out,0,(struct xz_shell_action){(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_SOURCE,-1,i+1,XZ_SHELL_SRC_LINK});
 return si_key(u,m,XZ_SHELL_KEY_BROWSE,out,n);
}
size_t sh_source_back_step(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken) {
 (void)out;
 *taken=picker_open(u,m->source);
 if(*taken)u->stack[u->depth-1].scroll=0;
 return 0;
}
static size_t picker_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_source_model *sm=m->source;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 int n=link_count(sm),cur=picker_cursor(u,sm),first=picker_first(u,sm);
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:
  for(int row=0;row<XZ_SHELL_LINK_ROWS&&first+row<n;row++){
   if(!sh_inside(xz_shell_source_link_rect(row),e->x,e->y))continue;
   if(first+row!=cur){v->scroll=first+row+1;return 0;}   /* first touch highlights */
   return select_link(u,m,cur,out);
  }
  return 0;
 case XZ_SHELL_ENCODER_TURN:v->scroll=sh_max(0,sh_min(n-1,cur+e->delta))+1;return 0;
 case XZ_SHELL_ENCODER_PUSH:return select_link(u,m,cur,out);
 default:return 0;
 }
}
static size_t select_tile(struct xz_shell *u, const struct xz_shell_model *m, int i, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 /* LINK with media listed opens the picker; the current medium is highlighted first. */
 if(i==XZ_SHELL_SRC_LINK-1&&link_count(m->source)>0){
  int at=m->source->link_current;
  u->stack[u->depth-1].scroll=(at>=0&&at<link_count(m->source)?at:0)+1;
  return 0;
 }
 /* A stock slot: the runtime hands the screen to stock, which opens it; the shell stays where it is. */
 if(stock(m->source,i))return si_emit(out,0,(struct xz_shell_action){(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_SOURCE,-1,0,i+1});
 if(!ready(m->source,i))return 0;
 size_t n=si_emit(out,0,(struct xz_shell_action){(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_SOURCE,-1,0,i+1});
 return si_key(u,m,XZ_SHELL_KEY_BROWSE,out,n);
}
static size_t source_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_source_model *sm=m->source;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(e->kind==XZ_SHELL_TOUCH_DOWN&&sh_inside(si_close_rect(),e->x,e->y))return sh_back(u,m,out);
 if(!sm)return 0;
 if(picker_open(u,sm))return picker_input(u,m,e,out);
 int at=cursor_tile(u,sm);
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:
  for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++){
   if(!sh_inside(xz_shell_source_rect(i),e->x,e->y))continue;
   if(!selectable(sm,i))return 0;
   if(v->cursor!=i+1||v->focus){v->cursor=i+1;v->focus=0;return 0;}  /* first touch highlights */
   return select_tile(u,m,i,out);
  }
  return 0;
 case XZ_SHELL_ENCODER_TURN:{
  if(at<0)return 0;
  int step=e->delta>0?1:-1;
  v->focus=0;
  for(int k=e->delta>0?e->delta:-e->delta;k>0;k--){
   int next=at+step;
   while(next>=0&&next<XZ_SHELL_SOURCE_TILES&&!selectable(sm,next))next+=step;
   if(next<0||next>=XZ_SHELL_SOURCE_TILES)break;
   at=next;
  }
  v->cursor=at+1;
  return 0;
 }
 case XZ_SHELL_ENCODER_PUSH:return at<0?0:select_tile(u,m,at,out);
 default:return 0;
 }
}
const struct sh_screen xz_shell_source_screen={source_render,source_input};
