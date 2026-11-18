/* SOURCE screen, after the flagship device chooser: one large tile per source the XZ's media keys offer
   (USB 1, USB 2, LINK, REKORDBOX) with its icon on the owner's background colour, status, free space, track
   count and the decks playing from it, over a bar that says what holds the rear USB port, over the compact
   deck strip. The rear USB port is the one a computer needs for DJ software and rekordbox over a USB cable,
   and the one the DECK 3/4 loopback cable takes (XZ manual pp.24, 115, 128-129): the bar says which it is,
   and the LINK and REKORDBOX tiles mark the part the cable blocks. */
#include "shell_source.h"
#include "shell_info_chrome.h"
#include "../ui/ui_about.h"
#include "../ui/l10n.h"
#include "../extra_decks.h"

#define T(s) xz_l10n_authored(s)
enum {
 TILE_GAP=8, TILE_X=8, TILE_Y=SI_HEAD_H+8, TILE_W=(XZ_SHELL_WIDTH-2*TILE_X-3*TILE_GAP)/4,
 BAR_H=72, BAR_Y=SI_STRIP_Y-8-BAR_H, TILE_H=BAR_Y-8-TILE_Y,
 ICON_H=96, NAME_H=40, PAD=12, AMBER=SH_KEY_AMBER,
 LINK_X=TILE_X+TILE_W/2, LINK_W=XZ_SHELL_WIDTH-2*LINK_X, LINK_Y=TILE_Y, LINK_HEAD=40, LINK_ROW=48
};

struct xz_shell_rect xz_shell_source_rect(int tile) {
 if(tile==-1)return si_close_rect();
 if(tile==XZ_SHELL_SOURCE_BAR)return (struct xz_shell_rect){TILE_X,BAR_Y,XZ_SHELL_WIDTH-2*TILE_X,BAR_H};
 if(tile<0||tile>=XZ_SHELL_SOURCE_TILES)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){TILE_X+tile*(TILE_W+TILE_GAP),TILE_Y,TILE_W,TILE_H};
}
enum xz_shell_rear_port xz_shell_source_rear(const struct xz_shell_model *m) {
 switch(m?m->route_state:(int)XZ_EXTRA_ROUTE_NO_ENGINE){
 case XZ_EXTRA_ROUTE_READY:return XZ_SHELL_REAR_CABLE;
 case XZ_EXTRA_ROUTE_STARTING:case XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP:return XZ_SHELL_REAR_STARTING;
 case XZ_EXTRA_ROUTE_FAILED:return XZ_SHELL_REAR_FAILED;
 case XZ_EXTRA_ROUTE_NOT_DETECTED:case XZ_EXTRA_ROUTE_SCANNING:case XZ_EXTRA_ROUTE_LOST:return XZ_SHELL_REAR_FREE;
 default:return XZ_SHELL_REAR_UNUSED;
 }
}

int xz_shell_source_mark_loopback(struct xz_shell_source_model *sm, int route_state) {
 if(!sm||route_state!=XZ_EXTRA_ROUTE_READY)return -1;
 int stick[2];
 for(int i=0;i<2;i++){
  const struct xz_shell_device *d=&sm->device[i];
  if(!((d->known>>XZ_SHELL_D_STATUS)&1))return -1;
  if(d->status==XZ_SHELL_DEVICE_LOOPBACK)return i;
  stick[i]=d->status==XZ_SHELL_DEVICE_READY||d->status==XZ_SHELL_DEVICE_LOADING||d->status==XZ_SHELL_DEVICE_UNREADABLE;
  if(!stick[i]&&d->status!=XZ_SHELL_DEVICE_NOT_CONNECTED)return -1;
 }
 if(stick[0]==stick[1])return -1;
 int cable=stick[0]?1:0;
 sm->device[cable].status=XZ_SHELL_DEVICE_LOOPBACK;
 sm->device[cable].known=1u<<XZ_SHELL_D_STATUS;
 return cable;
}

struct xz_shell_rect xz_shell_source_link_rect(int row) {
 if(row<0||row>=XZ_SHELL_LINK_ROWS)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){LINK_X,LINK_Y+LINK_HEAD+row*LINK_ROW,LINK_W,LINK_ROW-4};
}
static int known(const struct xz_shell_device *d, enum xz_shell_device_field f) { return (d->known>>f)&1; }
static int status_is(const struct xz_shell_source_model *sm, int i, enum xz_shell_device_status s) {
 const struct xz_shell_device *d=&sm->device[i];
 return known(d,XZ_SHELL_D_STATUS)&&d->status==s;
}
static int connected(const struct xz_shell_source_model *sm, int i) {
 return status_is(sm,i,XZ_SHELL_DEVICE_LOADING)||status_is(sm,i,XZ_SHELL_DEVICE_READY);
}
/* A slot that opens in the stock screen. */
static int stock(const struct xz_shell_source_model *sm, int i) { return status_is(sm,i,XZ_SHELL_DEVICE_STOCK); }
/* A tile the cursor can rest on and a push or second touch can select. */
static int selectable(const struct xz_shell_source_model *sm, int i) { return connected(sm,i)||stock(sm,i); }
static int ready(const struct xz_shell_source_model *sm, int i) { return status_is(sm,i,XZ_SHELL_DEVICE_READY); }
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
 outline(c,cx-9,cy-28,18,15,2,ink,bg);
 xz_rect(c,cx-5,cy-23,3,4,ink);xz_rect(c,cx+2,cy-23,3,4,ink);
 xz_round_rect(c,cx-14,cy-14,28,43,6,15,ink);
 xz_disc(c,cx,cy+21,3,bg);
}
/* The loopback cable: a USB-A plug, the lead, a USB-B plug. */
static void icon_cable(struct xz_canvas c, int cx, int cy, uint32_t ink) {
 xz_round_rect(c,cx-36,cy-7,18,14,2,15,ink);
 xz_line16(c,(cx-18)*16,cy*16,(cx+18)*16,cy*16,48,ink);
 xz_round_rect(c,cx+18,cy-9,16,18,4,15,ink);
}
static void icon_player(struct xz_canvas c, int x, int y, uint32_t ink, uint32_t bg) {
 outline(c,x,y,36,44,4,ink,bg);
 xz_rect(c,x+7,y+7,22,7,ink);
 xz_ring(c,x+18,y+29,7,3,ink);
}
static void icon_link(struct xz_canvas c, int cx, int cy, uint32_t ink, uint32_t bg) {
 icon_player(c,cx-46,cy-22,ink,bg);
 icon_player(c,cx+10,cy-22,ink,bg);
 xz_rect(c,cx-10,cy-1,20,3,ink);
 xz_disc(c,cx,cy,4,ink);
}
static void icon_computer(struct xz_canvas c, int cx, int cy, uint32_t ink, uint32_t bg) {
 outline(c,cx-30,cy-24,60,38,3,ink,bg);
 xz_round_rect(c,cx-40,cy+16,80,7,3,15,ink);
 xz_rect(c,cx-8,cy+16,16,2,bg);
}
/* The rear USB-B socket, as it looks on the back panel. */
static void icon_socket(struct xz_canvas c, int x, int y, uint32_t ink, uint32_t bg) {
 xz_round_rect(c,x,y,26,24,3,15,ink);
 xz_rect(c,x+4,y+8,18,12,bg);xz_rect(c,x+7,y+4,12,4,bg);
 xz_rect(c,x+9,y+11,8,6,ink);
}
/* A part of a source the cable blocks (amber cross), or one it leaves alone (dim bullet). */
static void mark(struct xz_canvas c, int x, int y, int blocked) {
 if(!blocked){xz_disc(c,x+4,y+6,3,SH_DIM);return;}
 xz_line16(c,x*16,(y+1)*16,(x+9)*16,(y+10)*16,32,AMBER);
 xz_line16(c,(x+9)*16,(y+1)*16,x*16,(y+10)*16,32,AMBER);
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
/* LINK and REKORDBOX open the deck's own screen. Each lists its two ways in: one over the LAN, one through
   the rear USB port; the second is marked while the DECK 3/4 cable holds that port (READY only). */
static void render_stock_parts(const struct xz_shell_model *m, struct xz_canvas c, struct xz_shell_rect r, int i, int y) {
 int blocked=xz_shell_source_rear(m)==XZ_SHELL_REAR_CABLE,x=r.x+PAD+18,w=r.x+r.w-PAD-x;
 const char *lan=i==XZ_SHELL_SRC_LINK-1?T("LINKED PLAYERS"):T("REKORDBOX OVER LAN");
 const char *usb=i==XZ_SHELL_SRC_LINK-1?T("DJ SOFTWARE"):T("REKORDBOX OVER USB");
 sh_text_fit(c,r.x+PAD,y,r.w-2*PAD,T("DECK'S OWN SCREEN"),SH_TEXT,SH_WHITE);
 y+=32;
 mark(c,r.x+PAD,y,0);
 sh_text_fit(c,x,y,w,lan,SH_SMALL,SH_WHITE);
 sh_text_fit(c,x,y+16,w,T("LAN PORT"),SH_SMALL,SH_DIM);
 y+=44;
 mark(c,r.x+PAD,y,blocked);
 sh_text_fit(c,x,y,w,usb,SH_SMALL,blocked?AMBER:SH_WHITE);
 sh_text_fit(c,x,y+16,w,blocked?T("REAR USB IN USE"):T("REAR USB PORT"),SH_SMALL,blocked?AMBER:SH_DIM);
}
static void render_tile(const struct xz_shell_model *m, struct xz_canvas c, int i, int highlighted) {
 const struct xz_shell_source_model *sm=m->source;
 const struct xz_shell_device *d=&sm->device[i];
 struct xz_shell_rect r=xz_shell_source_rect(i);
 enum xz_shell_source slot=(enum xz_shell_source)(i+1);
 int status_known=known(d,XZ_SHELL_D_STATUS),live=connected(sm,i)||stock(sm,i);
 int cable=status_is(sm,i,XZ_SHELL_DEVICE_LOOPBACK),unreadable=status_is(sm,i,XZ_SHELL_DEVICE_UNREADABLE);
 char text[32];
 xz_rect(c,r.x,r.y,r.w,r.h,SH_CHROME);
 /* icon block on the device's colour */
 uint32_t fill=cable||unreadable?0x2a2a2a:!live?0x1e1e1e:known(d,XZ_SHELL_D_COLOR)?d->color&0xffffff:SH_RAIL;
 uint32_t ink=cable?SH_WHITE:unreadable?AMBER:!live?0x5a5a5a:si_ink_on(fill);
 xz_rect(c,r.x,r.y,r.w,ICON_H,fill);
 int cx=r.x+r.w/2,cy=r.y+ICON_H/2+2;
 if(cable)icon_cable(c,cx,cy,ink);
 else if(live&&i<2&&d->logo)xz_ui_logo_draw(c,d->logo,160,cx-40,cy-40,80);
 else if(live&&i<2&&d->profile_name&&d->profile_name[0])sh_text_fit(c,r.x+PAD,cy-8,r.w-2*PAD,d->profile_name,SH_TEXT,ink);
 else switch(slot){
 case XZ_SHELL_SRC_LINK:icon_link(c,cx,cy,ink,fill);break;
 case XZ_SHELL_SRC_COMPUTER:icon_computer(c,cx,cy,ink,fill);break;
 default:icon_usb(c,cx,cy,ink,fill);break;
 }
 if(current_tile(sm)==i&&live)sh_chip(c,r.x+8,r.y+8,T("BROWSING"),ink,ink,0);
 /* name bar: the source as its key is printed */
 int ny=r.y+ICON_H;
 xz_rect(c,r.x,ny,r.w,NAME_H,highlighted?SH_BLUE:SH_RAIL);
 sh_text(c,r.x+PAD,ny+13,slot==XZ_SHELL_SRC_COMPUTER?"REKORDBOX":si_source_name(slot),SH_TEXT,live||cable||unreadable?SH_WHITE:SH_DIM);
 int y=ny+NAME_H+10;
 if(stock(sm,i)){render_stock_parts(m,c,r,i,y);return;}
 /* status */
 const char *word="--";uint32_t word_ink=SH_DIM;
 if(status_known)switch(d->status){
 case XZ_SHELL_DEVICE_READY:word=T("READY");word_ink=SH_GREEN;break;
 case XZ_SHELL_DEVICE_LOADING:word=T("LOADING");word_ink=SH_WHITE;break;
 case XZ_SHELL_DEVICE_UNREADABLE:word=T("CAN'T READ");word_ink=AMBER;break;
 case XZ_SHELL_DEVICE_LOOPBACK:word=T("DECK 3/4 CABLE");word_ink=SH_WHITE;break;
 default:word=i<2?T("NO REKORDBOX USB"):T("UNAVAILABLE");word_ink=SH_DIM;break;
 }
 sh_text_fit(c,r.x+PAD,y,r.w-2*PAD,word,SH_TEXT,word_ink);
 if(status_known&&d->status==XZ_SHELL_DEVICE_LOADING){
  int dots=(int)(m->clock_ms/400%4),x=r.x+PAD+sh_text_width(c,word,SH_TEXT)+4;
  for(int k=0;k<3;k++)xz_rect(c,x+k*7,y+11,3,3,k<dots?SH_WHITE:SH_RAIL);
 }
 y+=24;
 if(cable||unreadable){
  si_text_wrap2(c,r.x+PAD,y,r.w-2*PAD,18,cable?T("The loopback cable for decks 3 and 4 is in this port."):
   T("Its rekordbox library could not be read."),SH_SMALL,SH_DIM);
  return;
 }
 if(!live){
  if(!status_known)sh_text_fit(c,r.x+PAD,y,r.w-2*PAD,T("Status not read yet."),SH_SMALL,SH_DIM);
  return;
 }
 const char *name=known(d,XZ_SHELL_D_NAME)?d->name:NULL;
 if(name)sh_text_fit(c,r.x+PAD,y,r.w-2*PAD,name,SH_SMALL,SH_DIM);
 else sh_text(c,r.x+PAD,y,"--",SH_SMALL,SH_DIM);
 y+=18;
 xz_rect(c,r.x+PAD,y,r.w-2*PAD,1,SH_ROW_ALT);
 /* free space with a used-space bar when the size is known */
 y+=6;
 sh_text(c,r.x+PAD,y,T("FREE"),SH_SMALL,SH_DIM);
 const char *unit="";
 if(known(d,XZ_SHELL_D_FREE)){
  fmt_bytes(text,sizeof text,&unit,d->free_bytes);
  sh_num(c,r.x+PAD,y+13,text,SH_NUM_MID,SH_WHITE);
  int ux=r.x+PAD+sh_num_width(SH_NUM_MID,text)+4;
  sh_text(c,ux,y+24,unit,SH_SMALL,SH_WHITE);
  if(known(d,XZ_SHELL_D_TOTAL)&&d->total_bytes>0){
   const char *tunit;char total[16];fmt_bytes(total,sizeof total,&tunit,d->total_bytes);
   snprintf(text,sizeof text,"/ %s %s",total,tunit);
   if(ux+sh_text_width(c,unit,SH_SMALL)+8<=r.x+r.w-PAD-sh_text_width(c,text,SH_SMALL))
    sh_text_right(c,r.x+r.w-PAD,y+24,text,SH_SMALL,SH_DIM);
   uint64_t used=d->free_bytes<d->total_bytes?d->total_bytes-d->free_bytes:0;
   int bw=r.w-2*PAD,fill_w=(int)((double)used*bw/(double)d->total_bytes);
   xz_rect(c,r.x+PAD,y+40,bw,4,SH_RAIL);
   xz_rect(c,r.x+PAD,y+40,fill_w,4,d->free_bytes*10<d->total_bytes?SH_RED:SH_DIM);
  }
 }else sh_num(c,r.x+PAD,y+13,"--",SH_NUM_MID,SH_DIM);
 /* track count, one line under the space bar */
 y+=48;
 if(known(d,XZ_SHELL_D_TRACKS))snprintf(text,sizeof text,"%d",(int)sh_max(0,d->tracks));
 else snprintf(text,sizeof text,"--");
 sh_text(c,r.x+PAD,y,text,SH_TEXT,known(d,XZ_SHELL_D_TRACKS)?SH_WHITE:SH_DIM);
 sh_text_fit(c,r.x+PAD+sh_text_width(c,text,SH_TEXT)+6,y+4,r.x+r.w-2*PAD-sh_text_width(c,text,SH_TEXT)-6,T("TRACKS"),SH_SMALL,SH_DIM);
 /* decks playing from this device */
 int by=r.y+r.h-24;
 xz_rect(c,r.x+PAD,by-6,r.w-2*PAD,1,SH_ROW_ALT);
 /* "DECK" then one numbered chip per deck, so all four fit */
 int x=r.x+PAD+sh_text_width(c,"DECK",SH_SMALL)+8,any=0;
 for(int deck=0;deck<sh_deck_count(m);deck++){
  const struct xz_shell_deck *dk=&m->deck[deck];
  if(!sh_has(dk,XZ_SHELL_F_SOURCE)||dk->source!=slot)continue;
  if(sh_has(dk,XZ_SHELL_F_LOADED)&&!dk->loaded)continue;
  snprintf(text,sizeof text,"%d",deck+1);
  if(x+sh_text_width(c,text,SH_SMALL)+12>r.x+r.w-PAD)break;
  x+=sh_chip(c,x,by+2,text,SH_BLUE,SH_WHITE,1)+4;any=1;
 }
 if(any)sh_text(c,r.x+PAD,by+5,"DECK",SH_SMALL,SH_DIM);
 else sh_text_fit(c,r.x+PAD,by+5,r.w-2*PAD,T("ON NO DECK"),SH_SMALL,0x6e6e6e);
}
/* What holds the rear USB port, in the DJ's terms: the state, what it means for a computer and, with no
   cable, what decks 3 and 4 need. Only READY claims the cable is in: the connector reports STARTING and
   WAITING before it checks the cable, so those lines are conditional. The runtime's own sentence is the
   first line when decks 3 and 4 did not start. */
static void render_bar(const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_rect r=xz_shell_source_rect(XZ_SHELL_SOURCE_BAR);
 const char *state,*lines[3]={NULL,NULL,NULL};
 uint32_t dot=SH_DIM,inks[3]={SH_WHITE,SH_DIM,SH_DIM};
 char reason[sizeof m->route_reason];
 xz_rect(c,r.x,r.y,r.w,r.h,SH_CHROME);
 icon_socket(c,r.x+16,r.y+14,SH_WHITE,SH_CHROME);
 sh_text(c,r.x+54,r.y+16,T("REAR USB"),SH_TEXT,SH_WHITE);
 switch(xz_shell_source_rear(m)){
 case XZ_SHELL_REAR_CABLE:
  state=T("DECK 3/4 CABLE");dot=SH_GREEN;
  lines[0]=T("Decks 3 and 4 play through this port.");
  lines[1]=T("DJ software and rekordbox over a USB cable cannot connect while it is in.");inks[1]=AMBER;
  lines[2]=T("Linked players and rekordbox over LAN still work.");
  break;
 case XZ_SHELL_REAR_STARTING:
  state=T("DECKS 3/4 STARTING");dot=SH_WHITE;
  lines[0]=T("Decks 3 and 4 play through the loopback cable in this port.");
  lines[1]=T("While it is in, DJ software and rekordbox over a USB cable cannot connect.");
  break;
 case XZ_SHELL_REAR_FAILED:
  state=T("DECKS 3/4 STOPPED");dot=AMBER;
  memcpy(reason,m->route_reason,sizeof reason);reason[sizeof reason-1]=0;
  lines[0]=reason[0]?reason:T("Decks 3 and 4 did not start.");
  lines[1]=T("While the loopback cable is in, DJ software and rekordbox over a USB cable cannot connect.");
  break;
 case XZ_SHELL_REAR_FREE:
  state=T("NO DECK 3/4 CABLE");
  lines[0]=T("A computer can use this port: DJ software, rekordbox over a USB cable.");
  lines[1]=T("Decks 3 and 4 need the loopback cable here, from a top USB port.");
  break;
 default:
  state=T("FOR A COMPUTER");
  lines[0]=T("DJ software and rekordbox over a USB cable connect here.");
  break;
 }
 xz_disc(c,r.x+58,r.y+50,4,dot);
 sh_text_fit(c,r.x+68,r.y+44,138,state,SH_SMALL,dot==SH_DIM?SH_DIM:SH_WHITE);
 int n=lines[2]?3:lines[1]?2:1,lx=r.x+214,ly=r.y+(r.h-n*19)/2+3;
 xz_rect(c,lx-12,r.y+10,1,r.h-20,SH_RAIL);
 for(int k=0;k<n;k++)sh_text_fit(c,lx,ly+k*19,r.x+r.w-12-lx,lines[k],SH_SMALL,inks[k]);
}
static int picker_open(const struct xz_shell *u, const struct xz_shell_source_model *sm);
static void render_picker(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c);
static void source_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 si_header(c,T("SOURCE"));
 si_strip(u,m,c);
 if(!m->source){sh_text_center(c,c.width/2,220,T("No device information."),SH_TEXT,SH_DIM);return;}
 int highlighted=cursor_tile(u,m->source);
 int any_known=0;
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)any_known|=known(&m->source->device[i],XZ_SHELL_D_STATUS);
 const char *hint=highlighted>=0?(stock(m->source,highlighted)?T("OPENS IN THE DECK'S OWN SCREEN"):T("OPENS ITS LIBRARY IN BROWSE")):
  any_known?T("NO DEVICE CONNECTED"):T("DEVICE STATUS NOT READ YET");
 int warn=highlighted>=0&&!selectable(m->source,highlighted);
 if(warn){
  if(status_is(m->source,highlighted,XZ_SHELL_DEVICE_LOOPBACK))hint=T("THIS PORT HOLDS THE DECK 3/4 CABLE");
  else if(status_is(m->source,highlighted,XZ_SHELL_DEVICE_UNREADABLE))hint=T("ITS LIBRARY CANNOT BE READ");
  else if(highlighted==0)hint=T("NO REKORDBOX USB IN USB 1");
  else if(highlighted==1)hint=T("NO REKORDBOX USB IN USB 2");
  else hint=highlighted==XZ_SHELL_SRC_LINK-1?T("LINK IS NOT AVAILABLE"):T("REKORDBOX IS NOT AVAILABLE");
 }
 sh_text_right(c,si_close_rect().x-14,17,hint,SH_SMALL,warn?AMBER:SH_DIM);
 for(int i=0;i<XZ_SHELL_SOURCE_TILES;i++)render_tile(m,c,i,i==highlighted);
 if(highlighted>=0){
  struct xz_shell_rect r=xz_shell_source_rect(highlighted);
  xz_border(c,r.x,r.y,r.w,r.h,SH_BLUE);xz_border(c,r.x+1,r.y+1,r.w-2,r.h-2,SH_BLUE);
 }
 render_bar(m,c);
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
