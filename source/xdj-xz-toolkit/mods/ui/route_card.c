/* SPDX-License-Identifier: MIT */
#include "route_card.h"
#include "cable_anim.h"
#include <string.h>

/* Design section 5.1. MODERN: the card spans the lanes right of the info column, x 98..799. CLASSIC:
   the card is drawn inside the 536 x 268 wave surface (screen 131,18), so its coordinates are surface
   coordinates. */
const struct xz_route_card_style xz_route_card_modern={
 .classic=0,.theme=XZ_THEME_FLAGSHIP,.x=98,.w=702,.pitch=78,.row_h=77,.accent_h=3,
 .picture={8,5,262,68},.head_x=292,.head_dy=14,.step_dy={40,57},
 .dot_dx=16,.dot_dy=23,.dot_r=5,.title_dx=28,.title_dy=16,.line_dx=10,.line_dy={44,60},
 .primary={386,17,150,44},.two={546,17,150,44},.labels=1,
 .fill=0x0c0c0c,.edge=0x3c3c3c,.rule=0x262626,.ink=0xffffff,.text=0xc8c8c8,.step_text=0xd8d8d8,
 .amber=0xffa82e,.blue=0x007de1,.green=0x28e028,.red=0xff4619,.white=0xffffff,.grey=0x8c8c8c,
 .primary_fill=0x007de1,.primary_ink=0xffffff,.scan_fill=0x095694,.scan_track=0x0b3f6b,
 .two_fill=0x191919,.two_edge=0x8c8c8c,.two_ink=0xffffff,.focus=0xffffff,
 .body=0x161616,.body_edge=0x9a9a9a
};
const struct xz_route_card_style xz_route_card_classic={
 .classic=1,.theme=0,.x=0,.w=536,.pitch=68,.row_h=64,.accent_h=2,
 .picture={8,5,196,58},.head_x=216,.head_dy=10,.step_dy={32,47},
 .dot_dx=16,.dot_dy=15,.dot_r=5,.title_dx=28,.title_dy=8,.line_dx=10,.line_dy={34,49},
 .primary={268,11,128,40},.two={404,11,128,40},.labels=0,
 .fill=0x0c0e10,.edge=0x52616a,.rule=0x232a30,.ink=0xf2f2f2,.text=0xb8bcc0,.step_text=0xc8ccd0,
 .amber=0xffa82e,.blue=0xacceff,.green=0x28e028,.red=0xf34232,.white=0xffffff,.grey=0x8a929a,
 .primary_fill=0xacceff,.primary_ink=0x081008,.scan_fill=0x2a3a4c,.scan_track=0x18222c,
 .two_fill=0x101410,.two_edge=0xacaeb4,.two_ink=0xf2f2f2,.focus=0xffffff,
 .body=0x0e1216,.body_edge=0xa8b0b8
};

enum xz_route_card_kind xz_route_card_kind(int state) {
 switch(state){
 case XZ_EXTRA_ROUTE_NOT_DETECTED:return XZ_ROUTE_CARD_NOT_DETECTED;
 case XZ_EXTRA_ROUTE_SCANNING:return XZ_ROUTE_CARD_SCANNING;
 case XZ_EXTRA_ROUTE_STARTING:return XZ_ROUTE_CARD_STARTING;
 case XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP:return XZ_ROUTE_CARD_WAITING;
 case XZ_EXTRA_ROUTE_READY:return XZ_ROUTE_CARD_READY;
 case XZ_EXTRA_ROUTE_FAILED:return XZ_ROUTE_CARD_FAILED;
 case XZ_EXTRA_ROUTE_LOST:return XZ_ROUTE_CARD_LOST;
 default:return XZ_ROUTE_CARD_NO_ENGINE;
 }
}
/* The words. Titles are short; CLASSIC uses short_title where the full one does not fit beside its
   buttons. Lines are the design's, or the runtime's own sentence where only it knows the reason. */
struct words { const char *title, *short_title, *line[2]; int reason_line; };
static struct words words_of(enum xz_route_card_kind k, int classic) {
 switch(k){
 case XZ_ROUTE_CARD_NOT_DETECTED:return (struct words){"NO CABLE FOUND",NULL,
  {classic?"The XZ keeps looking on its own.":"The XZ keeps looking on its own. RESCAN checks right now.","Keep the music stick in USB 2."},-1};
 case XZ_ROUTE_CARD_SCANNING:return (struct words){"LOOKING FOR THE CABLE...",NULL,{"Checking for the loopback cable now.",NULL},0};
 case XZ_ROUTE_CARD_STARTING:return (struct words){"STARTING DECKS 3 AND 4","STARTING 3 AND 4",{NULL,"This takes a few seconds."},0};
 case XZ_ROUTE_CARD_WAITING:return (struct words){"STOP DECKS 1 AND 2",NULL,
  {classic?"Decks 3 and 4 start once 1 and 2 stop.":"Decks 3 and 4 start once decks 1 and 2 are both stopped.",
   classic?"This card changes by itself.":"This card changes by itself once they stop."},-1};
 case XZ_ROUTE_CARD_READY:return (struct words){"DECKS 3 AND 4 ARE READY","3 AND 4 READY",{NULL,NULL},-1};
 case XZ_ROUTE_CARD_FAILED:return (struct words){"COULD NOT START DECKS 3 AND 4","3 AND 4 DID NOT START",{NULL,NULL},0};
 case XZ_ROUTE_CARD_LOST:return (struct words){"DECKS 3 AND 4 DISCONNECTED","3 AND 4 DISCONNECTED",
  {"The loopback cable was unplugged or lost power.",NULL},0};
 default:return (struct words){"FOUR DECKS ARE NOT AVAILABLE","NOT AVAILABLE",
  {"Decks 3 and 4 are not running on this player.",NULL},0};
 }
}
static uint32_t accent_of(const struct xz_route_card_style *s, enum xz_route_card_kind k) {
 switch(k){
 case XZ_ROUTE_CARD_SCANNING:return s->blue;
 case XZ_ROUTE_CARD_STARTING:return s->white;
 case XZ_ROUTE_CARD_READY:return s->green;
 case XZ_ROUTE_CARD_FAILED:case XZ_ROUTE_CARD_LOST:return s->red;
 case XZ_ROUTE_CARD_NO_ENGINE:return s->grey;
 default:return s->amber;
 }
}

/* ---- state over time ---- */
static uint32_t since(const struct xz_route_card_input *in, uint32_t t) { return in->now_ms-t; }
int xz_route_card_visible(const struct xz_route_card_input *in) {
 if(!in)return 0;
 if(xz_route_card_kind(in->state)!=XZ_ROUTE_CARD_READY)return 1;
 return in->ready_at_ms&&since(in,in->ready_at_ms)<XZ_ROUTE_CARD_READY_HOLD_MS+XZ_ROUTE_CARD_WIPE_MS;
}
int xz_route_card_wipe_x(const struct xz_route_card_style *s, const struct xz_route_card_input *in) {
 if(!in||xz_route_card_kind(in->state)!=XZ_ROUTE_CARD_READY||!in->ready_at_ms)return s->x;
 uint32_t t=since(in,in->ready_at_ms);
 if(t<XZ_ROUTE_CARD_READY_HOLD_MS)return s->x;
 uint32_t frame=(t-XZ_ROUTE_CARD_READY_HOLD_MS)*XZ_ROUTE_CARD_WIPE_FRAMES/XZ_ROUTE_CARD_WIPE_MS;
 if(frame>=XZ_ROUTE_CARD_WIPE_FRAMES)return s->x+s->w;
 return s->x+s->w*(int)(frame+1)/XZ_ROUTE_CARD_WIPE_FRAMES;
}
int xz_route_card_offers(const struct xz_route_card_input *in, int hit) {
 enum xz_route_card_kind k=xz_route_card_kind(in->state);
 if(hit==XZ_ROUTE_CARD_HIT_PRIMARY)return k==XZ_ROUTE_CARD_NOT_DETECTED||k==XZ_ROUTE_CARD_FAILED||k==XZ_ROUTE_CARD_LOST;
 if(hit==XZ_ROUTE_CARD_HIT_TWO)return k!=XZ_ROUTE_CARD_READY;
 return 0;
}
int xz_route_card_focus_order(const struct xz_route_card_input *in, int out[2]) {
 int n=0;
 if(xz_route_card_offers(in,XZ_ROUTE_CARD_HIT_PRIMARY))out[n++]=XZ_ROUTE_CARD_HIT_PRIMARY;
 if(xz_route_card_offers(in,XZ_ROUTE_CARD_HIT_TWO))out[n++]=XZ_ROUTE_CARD_HIT_TWO;
 return n;
}
static uint32_t pulse_period(enum xz_route_card_kind k) {
 return k==XZ_ROUTE_CARD_NOT_DETECTED?1600u:k==XZ_ROUTE_CARD_SCANNING?800u:
  k==XZ_ROUTE_CARD_STARTING||k==XZ_ROUTE_CARD_WAITING?1200u:0u;
}
static int pulsing(const struct xz_route_card_input *in, enum xz_route_card_kind k) {
 return pulse_period(k)&&(k==XZ_ROUTE_CARD_SCANNING||since(in,in->rest_from_ms)<XZ_ROUTE_CARD_REST_MS);
}
/* The picture moves: its loop or its signal, until it rests (scanning never rests). */
static int moving(const struct xz_route_card_input *in, enum xz_route_card_kind k) {
 enum xz_cable_motion m=xz_cable_motion_of(in->state);
 return (m==XZ_CABLE_LOOP||m==XZ_CABLE_SIGNAL)&&(k==XZ_ROUTE_CARD_SCANNING||since(in,in->rest_from_ms)<XZ_ROUTE_CARD_REST_MS);
}
uint32_t xz_route_card_motion_ms(const struct xz_route_card_input *in) {
 if(!xz_route_card_visible(in))return 0;
 enum xz_route_card_kind k=xz_route_card_kind(in->state);
 if(moving(in,k))return in->step_ms?in->step_ms:XZ_ROUTE_CARD_FRAME_MS;
 if(k==XZ_ROUTE_CARD_READY&&in->ready_at_ms)return 30;
 return 0;
}
/* The frame clock in whole 20 fps frames, so every frame of a period is the same picture. */
static uint32_t frame_clock(const struct xz_route_card_input *in) { return in->now_ms/XZ_ROUTE_CARD_FRAME_MS*XZ_ROUTE_CARD_FRAME_MS; }

/* ---- drawing through a pen: a canvas whose pixel (0,0) is card point (ox,oy). The wipe clips the left;
   the cache draws one row into a buffer of its own. ---- */
struct pen { struct xz_canvas c; int ox, oy; };
static void rect(struct pen p, int x, int y, int w, int h, uint32_t col) { xz_rect(p.c,x-p.ox,y-p.oy,w,h,col); }
static void border(struct pen p, int x, int y, int w, int h, uint32_t col) { xz_border(p.c,x-p.ox,y-p.oy,w,h,col); }
static void text(struct pen p, int x, int y, const char *t, int scale, uint32_t col) { xz_text(p.c,x-p.ox,y-p.oy,t,scale,99,col); }
static int width(struct pen p, const char *t, int scale) { return xz_text_width(p.c,t,scale,99); }
static void line16(struct pen p, int x0, int y0, int x1, int y1, int w, uint32_t col) {
 xz_line16(p.c,x0-16*p.ox,y0-16*p.oy,x1-16*p.ox,y1-16*p.oy,w,col);
}
static void rounded(struct pen p, int x, int y, int w, int h, int r, uint32_t col) { xz_round_rect(p.c,x-p.ox,y-p.oy,w,h,r,15,col); }
static void round_box(struct pen p, int x, int y, int w, int h, int r, uint32_t fill, uint32_t edge, int t) {
 if(t>0){rounded(p,x,y,w,h,r,edge);x+=t;y+=t;w-=2*t;h-=2*t;r-=t;}
 rounded(p,x,y,w,h,r<1?1:r,fill);
}
/* Anti-aliased disc or ring in 1/16 px: 4 x 4 samples per pixel, as the shared primitives. */
static void annulus16(struct pen p, int cx, int cy, int outer, int inner, uint32_t col) {
 cx-=16*p.ox;cy-=16*p.oy;
 int x0=(cx-outer)/16-1,x1=(cx+outer)/16+1,y0=(cy-outer)/16-1,y1=(cy+outer)/16+1;
 if(x1<0||y1<0||x0>=p.c.width||y0>=p.c.height)return;
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
  int n=0;
  for(int j=0;j<4;j++)for(int i=0;i<4;i++){
   int dx=x*16+4*i+2-cx,dy=y*16+4*j+2-cy,d=dx*dx+dy*dy;
   n+=d<=outer*outer&&(inner<=0||d>=inner*inner);
  }
  if(n)xz_plot(p.c,x,y,col,n*255/16);
 }
}
/* Filled triangle in 1/16 px, 4 x 4 samples per pixel. */
static int side(int ax, int ay, int bx, int by, int px, int py) { return (bx-ax)*(py-ay)-(by-ay)*(px-ax); }
static void triangle16(struct pen p, const int v[6], uint32_t col) {
 int ax=v[0]-16*p.ox,ay=v[1]-16*p.oy,bx=v[2]-16*p.ox,by=v[3]-16*p.oy,cx=v[4]-16*p.ox,cy=v[5]-16*p.oy;
 int lo_x=ax<bx?ax:bx,hi_x=ax>bx?ax:bx,lo_y=ay<by?ay:by,hi_y=ay>by?ay:by;
 if(cx<lo_x)lo_x=cx;if(cx>hi_x)hi_x=cx;if(cy<lo_y)lo_y=cy;if(cy>hi_y)hi_y=cy;
 int area=side(ax,ay,bx,by,cx,cy);if(!area)return;
 for(int y=lo_y/16-1;y<=hi_y/16+1;y++)for(int x=lo_x/16-1;x<=hi_x/16+1;x++){
  int n=0;
  for(int j=0;j<4;j++)for(int i=0;i<4;i++){
   int px=x*16+4*i+2,py=y*16+4*j+2;
   int a=side(ax,ay,bx,by,px,py),b=side(bx,by,cx,cy,px,py),c=side(cx,cy,ax,ay,px,py);
   n+=area>0?(a>=0&&b>=0&&c>=0):(a<=0&&b<=0&&c<=0);
  }
  if(n)xz_plot(p.c,x,y,col,n*255/16);
 }
}
/* Circular arc from a0 to a1 degrees (clockwise from +x, screen y down), 1/16 px. */
static void arc16(struct pen p, int cx, int cy, int r, int a0, int a1, int w, uint32_t col) {
 for(int a=a0;a<a1;a+=15){
  int b=a+15>a1?a1:a+15;
  line16(p,cx+r*xz_cos1024(a)/1024,cy+r*xz_sin1024(a)/1024,cx+r*xz_cos1024(b)/1024,cy+r*xz_sin1024(b)/1024,w,col);
 }
}
static void refresh_icon(struct pen p, int cx, int cy, int r, uint32_t col) {
 arc16(p,cx*16,cy*16,r*16,40,330,32,col);
 int hx=cx*16+r*16*xz_cos1024(330)/1024,hy=cy*16+r*16*xz_sin1024(330)/1024;
 const int v[6]={hx+64,hy-16,hx-48,hy-80,hx-32,hy+48};
 triangle16(p,v,col);
}

/* ---- the route picture (design 5.1): top-down XZ, mixer strips 3 1 2 4, USB 1 on top, the rear socket,
   and the cable from USB 1 over the rear edge into the rear socket. Coordinates relative to the box. ---- */
struct route_geom { int ux,uy,uw,uh, jr16, jx16[2], jy16, mx0_16, sw16, px16, sx16, top; };
static struct route_geom route_geom(int w, int h) {
 struct route_geom g;
 g.uh=(h*52+50)/100;if(g.uh<30)g.uh=30;
 g.uw=w-26;g.ux=4;g.uy=h-g.uh-2;
 g.jr16=g.uh*16*34/100;
 g.jx16[0]=g.ux*16+g.uh*16*55/100;g.jx16[1]=(g.ux+g.uw)*16-g.uh*16*55/100;g.jy16=g.uy*16+g.uh*8+16;
 g.mx0_16=g.ux*16+g.uh*16*115/100;
 int mx1=(g.ux+g.uw)*16-g.uh*16*115/100;g.sw16=(mx1-g.mx0_16)/4;
 g.px16=g.ux*16+g.uw*8;g.sx16=g.ux*16+g.uw*16*78/100;g.top=4;
 return g;
}
/* The cable as a polyline in 1/16 px: up from USB 1, an arch over to the rear socket, down into it,
   with each segment's length so a point at a distance along it costs one short walk. */
enum { PATH_POINTS=17 };
struct path { int n, total, x[PATH_POINTS], y[PATH_POINTS], len[PATH_POINTS]; };
static int isqrt(long long v) {
 long long r=0,bit=1LL<<40;
 while(bit>v)bit>>=2;
 while(bit){if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}
 return (int)r;
}
static void route_path(const struct route_geom *g, int ox, int oy, struct path *p) {
 int x0=ox*16,y0=oy*16,arch=(g->top+8)*16,n=0;
 p->x[n]=x0+g->px16;p->y[n++]=y0+(g->uy+3)*16;
 for(int i=0;i<=14;i++){
  p->x[n]=x0+g->px16+(g->sx16-g->px16)*i/14;
  p->y[n++]=y0+arch-6*16*xz_sin1024(180*i/14)/1024;
 }
 p->x[n]=x0+g->sx16;p->y[n++]=y0+(g->uy-3)*16;
 p->n=n;p->total=0;
 for(int i=0;i+1<n;i++){
  long long dx=p->x[i+1]-p->x[i],dy=p->y[i+1]-p->y[i];
  p->len[i]=isqrt(dx*dx+dy*dy);p->total+=p->len[i];
 }
}
/* The point at distance d (1/16 px) along the path; 0 past either end. */
static int path_point(const struct path *p, int d, int *x, int *y) {
 if(d<0||d>p->total)return 0;
 for(int i=0;i+1<p->n;i++){
  if(d<=p->len[i]&&p->len[i]){*x=p->x[i]+(p->x[i+1]-p->x[i])*d/p->len[i];*y=p->y[i]+(p->y[i+1]-p->y[i])*d/p->len[i];return 1;}
  d-=p->len[i];
 }
 return 0;
}
static void route_still(struct pen p, const struct xz_route_card_style *s, int ox, int oy, int w, int h,
 enum xz_route_card_kind k, int part) {
 struct route_geom g=route_geom(w,h);
 int failed=k==XZ_ROUTE_CARD_FAILED;
 round_box(p,ox+g.ux,oy+g.uy,g.uw,g.uh,5,s->body,s->body_edge,1);
 for(int j=0;j<2;j++){
  annulus16(p,ox*16+g.jx16[j],oy*16+g.jy16,g.jr16,g.jr16-24,s->body_edge);
  annulus16(p,ox*16+g.jx16[j],oy*16+g.jy16,32,0,s->body_edge);
 }
 static const int strips[4]={3,1,2,4};
 for(int i=0;i<4;i++){
  int sx=(ox*16+g.mx0_16+g.sw16*i+g.sw16/2)/16,outer=strips[i]==3||strips[i]==4;
  rect(p,sx,oy+g.uy+g.uh*42/100,1,g.uh*46/100,0x5a5a5a);
  rect(p,sx-3,oy+g.uy+g.uh*70/100,7,3,0x8c8c8c);
  if(outer){
   uint32_t col=k==XZ_ROUTE_CARD_READY?s->green:failed&&part==XZ_EXTRA_ROUTE_PART_SELECTOR?s->red:s->amber;
   rounded(p,sx-4,oy+g.uy+4,9,7,2,col);
  }
 }
 int px=(ox*16+g.px16)/16,sx=(ox*16+g.sx16)/16;
 rect(p,px-5,oy+g.uy+3,10,5,failed&&part==XZ_EXTRA_ROUTE_PART_PORT?s->red:s->white);
 rect(p,sx-6,oy+g.uy-3,12,6,failed&&part==XZ_EXTRA_ROUTE_PART_SOCKET?s->red:s->white);
 struct path path;route_path(&g,ox,oy,&path);
 uint32_t cable=k==XZ_ROUTE_CARD_READY?s->green:k==XZ_ROUTE_CARD_LOST?s->red:
  failed?(part==XZ_EXTRA_ROUTE_PART_CABLE?s->red:0x6a6a6a):
  k==XZ_ROUTE_CARD_STARTING||k==XZ_ROUTE_CARD_WAITING?s->white:0x6a6a6a;
 if(k==XZ_ROUTE_CARD_NOT_DETECTED||k==XZ_ROUTE_CARD_SCANNING){
  /* Dashed: the cable the DJ still has to plug in. 4 px on, 4 px off. */
  for(int d=0;d<path.total;d+=128){
   int x0,y0,x1,y1;
   if(path_point(&path,d,&x0,&y0)&&path_point(&path,d+64<path.total?d+64:path.total,&x1,&y1))line16(p,x0,y0,x1,y1,26,cable);
  }
 }else for(int i=0;i+1<path.n;i++)line16(p,path.x[i],path.y[i],path.x[i+1],path.y[i+1],32,cable);
 if(s->labels){
  int ty=oy+g.top+6;
  text(p,px-6-width(p,"USB 1",1),ty,"USB 1",1,0xb8b8b8);
  text(p,sx+9,ty,"REAR USB",1,0xb8b8b8);
 }
 if(failed&&part!=XZ_EXTRA_ROUTE_PART_NONE){
  int fx,fy;
  switch(part){
  case XZ_EXTRA_ROUTE_PART_PORT:fx=px;fy=oy+g.uy+5;break;
  case XZ_EXTRA_ROUTE_PART_SOCKET:fx=sx;fy=oy+g.uy-1;break;
  case XZ_EXTRA_ROUTE_PART_CABLE:fx=(px+sx)/2;fy=oy+g.top+2;break;
  default:fx=(ox*16+g.mx0_16+g.sw16/2)/16;fy=oy+g.uy+7;break;
  }
  /* The marker stays inside the picture box. */
  int mx=fx+9,my=fy-6;
  if(mx>ox+w-8)mx=ox+w-8;if(my<oy+7)my=oy+7;
  int cx=mx*16,cy=my*16;
  annulus16(p,cx,cy,96,0,s->red);
  line16(p,cx-40,cy-40,cx+40,cy+40,22,s->white);
  line16(p,cx+40,cy-40,cx-40,cy+40,22,s->white);
 }
}
/* The pulse: three dots travel USB 1 to the rear socket, colours pre-blended over the box. */
static void route_pulse(struct pen p, const struct xz_route_card_style *s, int bx, int by,
 const struct xz_route_card_input *in, enum xz_route_card_kind k) {
 if(!pulsing(in,k))return;
 struct route_geom g=route_geom(s->picture.w,s->picture.h);
 struct path path;route_path(&g,bx,by,&path);
 uint32_t period=pulse_period(k),phase=frame_clock(in)%period;
 int head=(int)((uint64_t)phase*(uint64_t)path.total/period);
 uint32_t col=k==XZ_ROUTE_CARD_SCANNING?s->blue:k==XZ_ROUTE_CARD_NOT_DETECTED?s->amber:s->white;
 static const int back[3]={0,7*16,14*16},radius[3]={51,38,29},shade[3]={256,150,80};
 for(int i=2;i>=0;i--){
  int x,y;
  if(path_point(&path,head-back[i],&x,&y))annulus16(p,x,y,radius[i],0,xz_blend(s->classic?s->body:0x000000,col,(unsigned)shade[i]));
 }
}

/* The cache that can hold this style's picture, or NULL (the flat drawing). */
static struct xz_route_card_cache *pic_cache(const struct xz_route_card_style *s, struct xz_route_card_cache *cache) {
 return cache&&s->picture.w>0&&s->picture.h>0&&s->picture.w*s->picture.h<=XZ_ROUTE_CARD_PIC_MAX?cache:NULL;
}
/* The explainer (cable_anim.h) in the card's colours. */
static void picture_look(const struct xz_route_card_style *s, const struct xz_route_card_input *in, struct xz_cable_look *look) {
 struct xz_cable_palette pal={s->fill,s->body,xz_blend(s->fill,0x000000,150),s->body_edge,s->amber,s->blue,s->white,s->green,s->red,
  xz_blend(s->body,s->grey,170),s->labels?0xb8b8b8:0,s->white,s->theme};
 xz_cable_look_of(look,&pal,in->state,in->fail_part);
}
/* The picture into the cache's copy when its frame changed (a frame over the budget keeps the last one of
   the same state), then onto the pen. */
static void route_picture(struct pen p, const struct xz_route_card_style *s, int bx, int by,
 const struct xz_route_card_input *in, struct xz_route_card_cache *cache, int move) {
 struct xz_cable_picture pic={cache->pic,cache->pic_cov,s->picture.w,s->picture.h};
 uint32_t t;int signal;
 uint32_t base=((uint32_t)in->state<<8|(uint32_t)in->fail_part<<1|(uint32_t)s->classic)+1u;
 uint32_t key=xz_cable_frame_of(in->state,in->fail_part,in->now_ms,move,in->step_ms,&t,&signal)^base*2654435761u;
 if(cache->pic_key!=key||cache->pic_base!=base){
  struct xz_cable_look look;picture_look(s,in,&look);
  if(xz_cable_draw(&pic,t,&look,signal,move&&cache->pic_base==base?XZ_ROUTE_CARD_PIC_BUDGET:0u)){cache->pic_key=key;cache->pic_base=base;}
 }
 xz_cable_blit(p.c,bx-p.ox,by-p.oy,&pic);
}

/* ---- rows ---- */
struct xz_route_card_rect xz_route_card_row(const struct xz_route_card_style *s, int ya, int yb, int row) {
 int joined=yb==ya+s->pitch;
 if(joined)return (struct xz_route_card_rect){s->x,ya,s->w,s->pitch+s->row_h};
 return (struct xz_route_card_rect){s->x,row?yb:ya,s->w,s->row_h};
}
struct xz_route_card_rect xz_route_card_button(const struct xz_route_card_style *s, int yb, int hit) {
 const struct xz_route_card_rect *b=hit==XZ_ROUTE_CARD_HIT_PRIMARY?&s->primary:hit==XZ_ROUTE_CARD_HIT_TWO?&s->two:NULL;
 if(!b)return (struct xz_route_card_rect){0,0,0,0};
 return (struct xz_route_card_rect){s->x+b->x,yb+b->y,b->w,b->h};
}
static void frame_row(struct pen p, const struct xz_route_card_style *s, struct xz_route_card_rect r, uint32_t accent) {
 rect(p,r.x,r.y,r.w,r.h,s->fill);border(p,r.x,r.y,r.w,r.h,s->edge);rect(p,r.x,r.y,r.w,s->accent_h,accent);
}
/* Up to two lines of the small face, broken at spaces, the last cut with "..." when it does not fit. */
static int wrap(struct pen p, const char *t, int room, char out[2][96]) {
 if(!t||!*t)return 0;
 int lines=0;
 while(*t&&lines<2){
  while(*t==' ')t++;
  size_t n=strlen(t),best=0;
  char buf[96];
  for(size_t i=1;i<=n&&i<sizeof buf;i++){
   if(t[i]!=' '&&t[i]!=0)continue;
   memcpy(buf,t,i);buf[i]=0;
   if(width(p,buf,1)<=room)best=i;else break;
  }
  if(!best||(lines==1&&best<n)){
   /* Last line, or one word wider than the room: cut whole characters and end with "...". */
   size_t k=n<sizeof buf-4?n:sizeof buf-4;memcpy(buf,t,k);buf[k]=0;
   if(width(p,buf,1)>room){
    while(k){buf[--k]=0;char test[100];memcpy(test,buf,k);memcpy(test+k,"...",4);if(width(p,test,1)<=room)break;}
    memcpy(buf+k,"...",4);
   }
   memcpy(out[lines++],buf,sizeof buf);out[lines-1][95]=0;break;
  }
  memcpy(out[lines],t,best);out[lines][best]=0;lines++;t+=best;
 }
 return lines;
}
static void focus_ring(struct pen p, const struct xz_route_card_style *s, struct xz_route_card_rect b) {
 if(s->classic){round_box(p,b.x-3,b.y-3,b.w+6,b.h+6,9,s->fill,s->focus,2);}
 else for(int i=0;i<2;i++)border(p,b.x-3+i,b.y-3+i,b.w+6-2*i,b.h+6-2*i,s->focus);
}
static void two_button(struct pen p, const struct xz_route_card_style *s, struct xz_route_card_rect b, int primary, int focused) {
 if(focused)focus_ring(p,s,b);
 uint32_t fill=primary?s->primary_fill:s->two_fill,ink=primary?s->primary_ink:s->two_ink;
 if(s->classic)round_box(p,b.x,b.y,b.w,b.h,6,fill,primary?s->primary_fill:s->two_edge,1);
 else{rect(p,b.x,b.y,b.w,b.h,fill);if(!primary)border(p,b.x,b.y,b.w,b.h,s->two_edge);}
 int gw=s->classic?14:16,gx=b.x+(s->classic?10:12),gy=b.y+b.h/2-6;
 rect(p,gx,gy,gw,5,ink);rect(p,gx,gy+7,gw,5,ink);
 text(p,gx+gw+(s->classic?7:10),b.y+(b.h-14)/2,"TWO DECKS",2,ink);
}
/* The SCANNING button's sweep track; the moving segment is motion. */
static struct xz_route_card_rect sweep_track(struct xz_route_card_rect b) { return (struct xz_route_card_rect){b.x+10,b.y+b.h-8,b.w-20,3}; }
static void primary_button(struct pen p, const struct xz_route_card_style *s, struct xz_route_card_rect b,
 enum xz_route_card_kind k, int focused) {
 if(k==XZ_ROUTE_CARD_SCANNING){
  /* Disabled while it scans: the label and a sweep, the only part that moves. */
  if(s->classic)round_box(p,b.x,b.y,b.w,b.h,6,s->scan_fill,s->scan_fill,0);else rect(p,b.x,b.y,b.w,b.h,s->scan_fill);
  text(p,b.x+16,b.y+(b.h-14)/2-3,"SCANNING",2,s->classic?s->ink:s->white);
  struct xz_route_card_rect t=sweep_track(b);rect(p,t.x,t.y,t.w,t.h,s->scan_track);
  return;
 }
 if(focused)focus_ring(p,s,b);
 if(s->classic)round_box(p,b.x,b.y,b.w,b.h,6,s->primary_fill,s->primary_fill,0);else rect(p,b.x,b.y,b.w,b.h,s->primary_fill);
 int r=s->classic?7:8,cx=b.x+(s->classic?22:26),cy=b.y+b.h/2;
 refresh_icon(p,cx,cy,r,s->primary_ink);
 text(p,cx+r+(s->classic?9:10),b.y+(b.h-14)/2,k==XZ_ROUTE_CARD_FAILED?"TRY AGAIN":"RESCAN",2,s->primary_ink);
}
static void steps(struct pen p, const struct xz_route_card_style *s, int x, int y, int classic) {
 /* 1 Rear USB socket to USB 1 or 2 on top (USB-B to USB-A cable).  2 CH3 and CH4 input switches to USB.
    Either top slot works; USB 1, the one most DJs use, stands out. */
 static const char *const first[2][4]={{"Rear USB socket to ","USB 1"," or 2 on top (USB-B to USB-A cable).",NULL},{"Rear USB socket to ","USB 1"," or 2 on top.",NULL}};
 static const char *const second[4]={"CH3"," and ","CH4"," input switches to "};
 int tx=x;text(p,tx,y,"1",1,s->amber);tx+=width(p,"1",1)+7;
 for(int i=0;first[classic][i];i++){text(p,tx,y,first[classic][i],1,i==1?s->white:s->step_text);tx+=width(p,first[classic][i],1);}
 int y2=y+s->step_dy[1]-s->step_dy[0];
 tx=x;text(p,tx,y2,"2",1,s->amber);tx+=width(p,"2",1)+7;
 for(int i=0;i<4;i++){text(p,tx,y2,second[i],1,i==0||i==2?s->white:s->step_text);tx+=width(p,second[i],1);}
 text(p,tx,y2,"USB",1,s->white);tx+=width(p,"USB",1);text(p,tx,y2,".",1,s->step_text);
}

/* Everything on the card that does not move: frame, picture (its still; a moving picture comes in the
   motion), words, buttons, focus ring. cache: the picture's memory, NULL for the flat drawing. */
static void draw_still(struct pen p, const struct xz_route_card_style *s, int ya, int yb, const struct xz_route_card_input *in,
 struct xz_route_card_cache *cache) {
 enum xz_route_card_kind k=xz_route_card_kind(in->state);
 struct words wd=words_of(k,s->classic);
 uint32_t accent=accent_of(s,k);
 int joined=yb==ya+s->pitch;
 if(joined){
  frame_row(p,s,xz_route_card_row(s,ya,yb,0),accent);
  rect(p,s->x+8,yb,s->w-16,1,s->rule);
 }else{frame_row(p,s,xz_route_card_row(s,ya,yb,0),accent);frame_row(p,s,xz_route_card_row(s,ya,yb,1),accent);}
 /* Row A: the picture and what to do. No engine has nothing to connect: the picture hides. */
 int x=s->x;
 if(k!=XZ_ROUTE_CARD_NO_ENGINE){
  int bx=x+s->picture.x,by=ya+s->picture.y;
  rect(p,bx,by,s->picture.w,s->picture.h,s->fill);
  struct xz_route_card_cache *pc=pic_cache(s,cache);
  if(!pc)route_still(p,s,bx,by,s->picture.w,s->picture.h,k,in->fail_part);
  else if(!moving(in,k))route_picture(p,s,bx,by,in,pc,0);
  text(p,x+s->head_x,ya+s->head_dy,"CONNECT THE LOOPBACK CABLE",2,s->ink);
  steps(p,s,x+s->head_x,ya+s->step_dy[0],s->classic);
 }else{
  text(p,x+s->line_dx+6,ya+s->head_dy,"DECKS 1 AND 2 PLAY AS USUAL",2,s->ink);
  text(p,x+s->line_dx+6,ya+s->step_dy[0],"TWO DECKS gives them the whole screen again.",1,s->step_text);
 }
 /* Row B: the state, its lines and the buttons. */
 int offers_primary=xz_route_card_offers(in,XZ_ROUTE_CARD_HIT_PRIMARY)||k==XZ_ROUTE_CARD_SCANNING;
 int offers_two=xz_route_card_offers(in,XZ_ROUTE_CARD_HIT_TWO);
 struct xz_route_card_rect pb=xz_route_card_button(s,yb,XZ_ROUTE_CARD_HIT_PRIMARY),tb=xz_route_card_button(s,yb,XZ_ROUTE_CARD_HIT_TWO);
 int room=(offers_primary?pb.x:offers_two?tb.x:x+s->w)-10-(x+s->line_dx);
 annulus16(p,(x+s->dot_dx)*16+8,(yb+s->dot_dy)*16+8,s->dot_r*16,0,accent);
 const char *title=wd.title;
 int title_room=(offers_primary?pb.x:offers_two?tb.x:x+s->w)-10-(x+s->title_dx);
 if(width(p,title,2)>title_room&&wd.short_title)title=wd.short_title;
 text(p,x+s->title_dx,yb+s->title_dy,title,2,k==XZ_ROUTE_CARD_SCANNING||k==XZ_ROUTE_CARD_STARTING?s->ink:accent);
 char lines[2][96];int count=0;
 const char *reason=in->reason&&in->reason[0]?in->reason:NULL;
 if(wd.reason_line==0&&reason){
  count=wrap(p,reason,room,lines);
  char one[2][96];
  if(count<2&&wd.line[1]&&wrap(p,wd.line[1],room,one))memcpy(lines[count++],one[0],96);
 }else{
  for(int i=0;i<2&&count<2;i++)if(wd.line[i]){char one[2][96];if(wrap(p,wd.line[i],room,one)){memcpy(lines[count++],one[0],96);}}
 }
 for(int i=0;i<count;i++)text(p,x+s->line_dx,yb+s->line_dy[i],lines[i],1,s->text);
 if(offers_primary)primary_button(p,s,pb,k,in->focus==XZ_ROUTE_CARD_HIT_PRIMARY);
 if(offers_two)two_button(p,s,tb,k==XZ_ROUTE_CARD_NO_ENGINE,in->focus==XZ_ROUTE_CARD_HIT_TWO);
}
/* What moves: the picture (or the flat drawing's pulse), the scanning ring round the state dot and the sweep. */
static void draw_motion(struct pen p, const struct xz_route_card_style *s, int ya, int yb, const struct xz_route_card_input *in,
 struct xz_route_card_cache *cache) {
 enum xz_route_card_kind k=xz_route_card_kind(in->state);
 if(k!=XZ_ROUTE_CARD_NO_ENGINE){
  struct xz_route_card_cache *pc=pic_cache(s,cache);
  if(!pc)route_pulse(p,s,s->x+s->picture.x,ya+s->picture.y,in,k);
  else if(moving(in,k))route_picture(p,s,s->x+s->picture.x,ya+s->picture.y,in,pc,1);
 }
 if(k!=XZ_ROUTE_CARD_SCANNING)return;
 static const unsigned shades[3]={120,180,240};
 unsigned shade=shades[(frame_clock(in)/300u)%3u];
 annulus16(p,(s->x+s->dot_dx)*16+8,(yb+s->dot_dy)*16+8,8*16,8*16-19,xz_blend(s->fill,s->blue,shade));
 struct xz_route_card_rect t=sweep_track(xz_route_card_button(s,yb,XZ_ROUTE_CARD_HIT_PRIMARY));
 int travel=t.w-30,offset=(int)((frame_clock(in)%1000u)*(uint32_t)travel/1000u);
 rect(p,t.x+offset,t.y,30,t.h,s->white);
}

/* The cached still: one buffer per row, drawn whole at a cache miss, copied every frame. */
static uint32_t still_key(const struct xz_route_card_style *s, int ya, int yb, const struct xz_route_card_input *in) {
 uint32_t h=2166136261u;
 enum xz_route_card_kind k=xz_route_card_kind(in->state);
 const int v[8]={s->classic,ya,yb,k,in->fail_part,in->focus,s->x,moving(in,k)};
 for(int i=0;i<8;i++){h^=(uint32_t)v[i];h*=16777619u;}
 for(const char *c=in->reason?in->reason:"";*c;c++){h^=(unsigned char)*c;h*=16777619u;}
 return h|1u;
}
static void row_bounds(const struct xz_route_card_style *s, int ya, int yb, int row, int *y, int *h) {
 int joined=yb==ya+s->pitch;
 *y=row?yb:ya;*h=row||!joined?s->row_h:s->pitch;
}
static int fill_cache(const struct xz_route_card_style *s, int ya, int yb, const struct xz_route_card_input *in,
 struct xz_route_card_cache *cache) {
 if(s->w*(s->pitch>s->row_h?s->pitch:s->row_h)>XZ_ROUTE_CARD_ROW_MAX)return 0;
 uint32_t key=still_key(s,ya,yb,in);
 if(cache->key==key)return 1;
 for(int row=0;row<2;row++){
  int y,h;row_bounds(s,ya,yb,row,&y,&h);
  struct pen still={{cache->row[row],(size_t)s->w,s->w,h,s->theme},s->x,y};
  draw_still(still,s,ya,yb,in,cache);
 }
 cache->key=key;
 return 1;
}
static void blit_rows(struct pen p, const struct xz_route_card_style *s, int ya, int yb, const struct xz_route_card_cache *cache) {
 for(int row=0;row<2;row++){
  int y,h;row_bounds(s,ya,yb,row,&y,&h);
  int x0=s->x-p.ox,from=x0<0?-x0:0,to=x0+s->w>p.c.width?p.c.width-x0:s->w;
  if(to<=from)continue;
  for(int r=0;r<h;r++){
   int dy=y+r-p.oy;if(dy<0||dy>=p.c.height)continue;
   memcpy(p.c.p+(size_t)dy*p.c.stride+(size_t)(x0+from),cache->row[row]+(size_t)r*(size_t)s->w+(size_t)from,(size_t)(to-from)*sizeof(uint16_t));
  }
 }
}

void xz_route_card_draw(struct xz_canvas c, const struct xz_route_card_style *s, int ya, int yb,
 const struct xz_route_card_input *in, struct xz_route_card_cache *cache) {
 if(!s||!in||!c.p||!xz_route_card_visible(in))return;
 c.theme=s->theme;
 int edge=xz_route_card_wipe_x(s,in);
 if(edge>=s->x+s->w)return;
 struct pen p={c,0,0};
 if(edge>s->x){p.c=xz_sub(c,edge,0,c.width-edge,c.height);p.ox=edge;if(!p.c.width)return;}
 if(cache&&fill_cache(s,ya,yb,in,cache)){blit_rows(p,s,ya,yb,cache);draw_motion(p,s,ya,yb,in,cache);}
 else{draw_still(p,s,ya,yb,in,NULL);draw_motion(p,s,ya,yb,in,NULL);}
}
int xz_route_card_hit(const struct xz_route_card_style *s, int ya, int yb, const struct xz_route_card_input *in, int x, int y) {
 if(!s||!in||!xz_route_card_visible(in))return XZ_ROUTE_CARD_HIT_NONE;
 int inside=0;
 for(int row=0;row<2;row++){
  struct xz_route_card_rect r=xz_route_card_row(s,ya,yb,row);
  inside|=x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;
 }
 if(!inside)return XZ_ROUTE_CARD_HIT_NONE;
 for(int hit=XZ_ROUTE_CARD_HIT_PRIMARY;hit<=XZ_ROUTE_CARD_HIT_TWO;hit++){
  struct xz_route_card_rect b=xz_route_card_button(s,yb,hit);
  if(xz_route_card_offers(in,hit)&&x>=b.x&&y>=b.y&&x<b.x+b.w&&y<b.y+b.h)return hit;
 }
 return XZ_ROUTE_CARD_HIT_CARD;
}
