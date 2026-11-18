/* SPDX-License-Identifier: MIT */
/* STATIC SURFER, a game by VJ OpticMystic. Renderer for the Rhythm Lab glyph
   mode: BRIEF, RUN and PAUSE. Pure drawing into an 800x480 RGB565 buffer.
   The renderer only reads lab state and the engine's impact ring; it never
   writes either. Layers, back to front: world panorama, muted scenery props,
   ground slab with music veins, obstacles, contact effects, rider, chrome.
   Bright interactive materials (crystal, gold marks, rider) are the only
   saturated things on screen; scenery is tinted toward ink. */
#include "glyph_run.h"
#include "game_feel_art.h"
#include "game_motion_draw.h"
#include "rhythm_lab.h"
#include "../ui/ui_skin.h"
#include "game_nav.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct xz_canvas cv;
typedef struct gm_tgt tg;

#define INK     0x07041au
#define PANEL   0x170e3au
#define PANEL2  0x0c0724u
#define LINE    0x3b2b7au
#define TEXT    0xf3efffu
#define DIM     0xa99dd9u
#define FAINT   0x5b4d92u
#define CYAN    0x3fe8ffu
#define MAGENTA 0xff4fd8u
#define UV      0x8a5cffu
#define GOLD    0xffcf4au
#define MISS    0x7b7498u
#define RED     0xff3d6eu
#define VOIDC   0x030108u
#define CRYST   0x1a0a3cu
#define GLASS   0x7fdcffu
#define PROP    0x100828u

#define SCROLL      XZ_GLYPH_SCROLL
#define JUMP_PULSES 1.04
#define HERO_HALF   28.f
#define GATE_LO     36.f
#define GATE_HI     130.f
#define GATE_TOL    .29f
#define GATE_CENTER 111.f
#define BG_TOP      48
#define SCENE_BOT   424

enum { B_NORMAL, B_PRIMARY, B_ACTIVE, B_OFF };

/* One player's world strip. s scales every world length (solo 1, duo .6). */
struct band { int top, bot, floor, herox, duo; float s; unsigned pi; };

static float clampf(float v,float lo,float hi){if(!(v>lo))return lo;return v>hi?hi:v;}
static int imax(int a,int b){return a>b?a:b;}
static int imin(int a,int b){return a<b?a:b;}
static int iabs(int v){return v<0?-v:v;}
static int ri(float v){return (int)floorf(v+.5f);}
static int roundi(float v){return (int)(v+(v<0?-.5f:.5f));}
static int sc(const struct band *b,float v){return ri(v*b->s);}
static uint16_t C(uint32_t c){return gm_c565(c);}
static double per_of(const struct xz_rhythm_lab *g){return g->period>1?g->period:75;}
static uint32_t pcol(unsigned pi){return pi?MAGENTA:CYAN;}

/* ---- text ---- */
/* All text is the game's Barlow atlas: 12 px labels, 18/28 px UI, 40 px
   headline. dot 1 -> 12 px, dot 2 -> 18 px; scale 1/2/3 -> 18/28/40 px. */
static tg tgc(cv c){tg t;t.p=c.p;t.stride=c.stride;t.x0=0;t.y0=0;t.x1=800;t.y1=480;return t;}
static int fdot(int dot){return dot>=2?GM_F18:GM_F12;}
static int fscale(int scale){return scale>=3?GM_F40:scale==2?GM_F28:GM_F18;}
static void tx(cv c,int x,int y,const char *s,int dot,uint32_t col){
    if(!s||!*s)return;
    tg t=tgc(c);gm_text(&t,x,y,s,fdot(dot),C(col),256u);
}
static void txc(cv c,int cx,int y,const char *s,int dot,uint32_t col,int maxw){
    if(!s||!*s)return;
    tg t=tgc(c);int w=gm_text_width(s,fdot(dot));
    t.x0=imax(0,cx-maxw/2);t.x1=imin(800,cx+maxw/2);
    gm_text(&t,cx-w/2,y,s,fdot(dot),C(col),256u);
}
static void txr(cv c,int rx,int y,const char *s,int dot,uint32_t col){
    if(!s||!*s)return;
    tg t=tgc(c);gm_text(&t,rx-gm_text_width(s,fdot(dot)),y,s,fdot(dot),C(col),256u);
}
static void at(cv c,int x,int y,const char *s,int scale,uint32_t col){
    if(!s||!*s)return;
    tg t=tgc(c);gm_text_lift(&t,x,y,s,fscale(scale),C(col));
}
static void atc(cv c,int cx,int y,const char *s,int scale,uint32_t col){
    if(!s||!*s)return;
    tg t=tgc(c);gm_text_mid(&t,cx,y,s,fscale(scale),C(col));
}

struct sb{char s[128];unsigned n;};
#define SB(v) struct sb v; v.n=0; v.s[0]=0
static void sbs(struct sb *b,const char *s){while(s&&*s&&b->n<sizeof(b->s)-1)b->s[b->n++]=*s++;b->s[b->n]=0;}
static void sbu(struct sb *b,unsigned v){char t[12];int i=0;do{t[i++]=(char)('0'+v%10);v/=10;}while(v&&i<11);while(i>0&&b->n<sizeof(b->s)-1)b->s[b->n++]=t[--i];b->s[b->n]=0;}
static void sbi(struct sb *b,int v){if(v<0){sbs(b,"-");sbu(b,(unsigned)(-(long)v));}else{sbs(b,"+");sbu(b,(unsigned)v);}}

/* ---- lab facts ---- */
static int pulse_ready(const struct xz_rhythm_lab *g){return g->valid&&(g->manual_pulse||g->wave.pulse_confidence>=.25f);}
static const char *clock_tag(const struct xz_rhythm_lab *g){
    if(!g->valid)return "NO TRACK CLOCK";
    if(g->manual_pulse)return "TAP PULSE";
    if(g->wave.pulse_confidence>=.25f)return "ESTIMATED PULSE";
    return "NO PULSE YET";
}
static unsigned deck_of(const struct xz_rhythm_lab *g,unsigned pi){return xz_lab_players(g)==2?(pi?1u:0u):(g->control_deck?1u:0u);}
static const char *action_name(const struct xz_rhythm_lab *g,unsigned deck){
    const char *s=xz_lab_action_label(g,deck);return s&&*s?s:"ACTION";
}
static int is_glyph(const struct xz_lab_event *e){
    return e->kind==LAB_GLYPH_SPIKE||e->kind==LAB_GLYPH_GATE||e->kind==LAB_GLYPH_GAP;
}
static int ev_x(const struct band *b,double at0,double pos,double per){
    double d=(at0-pos)/per*SCROLL*b->s;
    if(d<-2000)d=-2000;
    if(d>2000)d=2000;
    return b->herox+(int)floor(d+.5);
}
static float jump_h(double tp){
    if(tp<0||tp>JUMP_PULSES)return 0;
    double v=XZ_GLYPH_JUMP_SPEED*tp-.5*XZ_GLYPH_GRAVITY*tp*tp;
    return v>0?(float)v:0.f;
}
static float band_level(const struct xz_rhythm_lab *g,double at0,unsigned k){
    if(!g->valid||!g->wave.count)return 0;
    float norm=fmaxf(64.f,(float)g->wave.normalization);
    return clampf(xz_lab_band(g,at0,k)*3.f/norm,0,1);
}

/* ---- world: panorama, props, ground ---- */
static void panorama(const tg *t,const struct xz_rhythm_lab *g,unsigned sa,unsigned sb,unsigned mixv,int scroll,int dim){
    static uint8_t gain[SCENE_BOT-BG_TOP];
    float lift=clampf(g->music[0],0,1)*1.2f+clampf(g->pulse,0,1)*.6f;
    for(int y=0;y<SCENE_BOT-BG_TOP;y++){
        float f=y<120?12.5f:y<250?11.f-(float)(y-120)*.02f:8.5f;
        if(dim)f=9.f;
        int k=(int)(f+lift);
        gain[y]=(uint8_t)(k<0?0:k>16?16:k);
    }
    gm_panorama(t,xz_feel_background[sa>2?2:sa],xz_feel_background[sb>2?2:sb],mixv,
        XZ_FEEL_BG_W,XZ_FEEL_BG_H,0,XZ_FEEL_BG_H,BG_TOP,SCENE_BOT,scroll,gain);
}
/* Act tile choice: jungle props in act one, crystal and tentacles later. */
static unsigned prop_tile(unsigned act,uint32_t h){
    static const unsigned char set[3][4]={{0,3,0,1},{2,1,2,3},{1,2,0,1}};
    return set[act>2?2:act][h&3u];
}
static void props(const tg *t,const struct band *b,double ph,unsigned act){
    float sp=250.f*b->s;
    double scroll=ph*SCROLL*b->s*.42;
    long k0=(long)floor(scroll/sp)-1;
    uint16_t tint=C(PROP);
    for(long k=k0;k<k0+6;k++){
        uint32_t h=gm_hash((uint32_t)k*2654435761u+31u);
        if(h%7u<2u)continue;
        int x=ri((float)((double)k*sp-scroll))+(int)((h>>8)%60u);
        float s=b->s*(.62f+.3f*(float)((h>>16)&255u)/255.f);
        gm_draw_scenery(t,prop_tile(act,h>>4),x,b->floor+sc(b,8),s,(int)((h>>24)&1u),tint,150);
    }
}
/* Ground slab: lit lip, shaded body, three energy veins that are the real
   bands at each column's music time, plus slow caustic glints. */
static void ground(const tg *t,const struct xz_rhythm_lab *g,const struct band *b,double per,double ph){
    int depth=b->bot-b->floor;
    if(depth<4)return;
    for(int y=b->floor;y<b->bot;y++){
        int d=y-b->floor;
        gm_span(t,t->x0,t->x1-1,y,C(xz_blend(0x1c1046u,0x040210u,(unsigned)imin(255,d*255/imax(1,depth)))));
    }
    float pu=clampf(g->pulse,0,1);
    int th=imax(2,sc(b,3));
    gm_rect_mix(t,0,b->floor-4,800,4,C(UV),60);
    gm_rect(t,0,b->floor,800,th,C(xz_blend(CYAN,TEXT,(unsigned)(60.f+pu*120.f))));
    gm_rect(t,0,b->floor+th,800,1,C(MAGENTA));
    static const uint32_t vc[3]={0x6a3cffu,0xff4fd8u,0x3fe8ffu};
    double pxp=SCROLL*b->s,world=ph*pxp;
    int step=b->duo?4:3;
    for(int k=0;k<3;k++){
        int base=b->floor+th+sc(b,8)+k*(depth-th-sc(b,12))/3;
        uint16_t col=C(vc[k]);
        for(int x=t->x0-(t->x0%step);x<t->x1;x+=step){
            double at0=g->position+(double)(x-b->herox)/pxp*per;
            float v=band_level(g,at0,(unsigned)k);
            if(v<.04f)continue;
            int wx=(int)fmod(world+(double)x,100000.0);
            int y=base+xz_sin1024(wx*2+k*120)*sc(b,3)/1024;
            int hh=1+ri(v*2.f*b->s);
            unsigned a=(unsigned)(30.f+210.f*v);
            for(int r=0;r<hh;r++)gm_span_add(t,x,x+step-1,y+r,gm_mix(0,col,a));
        }
    }
    /* caustics: short glints riding the slab top at ground speed */
    uint16_t gl=C(0x5a6cffu);
    for(int i=0;i<10;i++){
        uint32_t h=gm_hash((uint32_t)i*7919u+3u);
        double xx=fmod((double)(h%900u)-world,900.0);if(xx<0)xx+=900.0;
        int x=(int)xx-50,y=b->floor+th+2+(int)((h>>10)%(uint32_t)imax(1,depth/3));
        gm_span_add(t,x,x+sc(b,18.f+(float)((h>>20)%20u)),y,gm_mix(0,gl,70));
    }
}

/* ---- obstacles ---- */
static void spike(const tg *t,const struct band *b,int x,float energy,uint16_t edge,int lit,float recoil){
    float e=clampf(energy,0,1);
    int h=imax(4,sc(b,22.f+e*28.f)),hw=imax(3,sc(b,11.f+e*6.f)),f=b->floor;
    int tip=x+ri(recoil*(float)hw);
    int lx[3]={x-hw,tip,x},ly[3]={f,f-h,f},rx[3]={x,tip,x+hw},ry[3]={f,f-h,f};
    gm_poly(t,lx,ly,3,C(xz_blend(CRYST,UV,90)),236);
    gm_poly(t,rx,ry,3,C(CRYST),236);
    gm_line(t,x-hw,f,tip,f-h,1,edge,256);
    gm_line(t,tip,f-h,x+hw,f,1,gm_mix(edge,C(INK),110),256);
    gm_line(t,tip,f-h,x,f-1,1,gm_mix(C(CRYST),edge,150),200);
    if(lit)gm_rect(t,tip-1,f-h-2,3,3,C(GOLD));
}
static void gap(const tg *t,const struct band *b,int x,uint16_t edge){
    int hw=imax(6,sc(b,27)),depth=b->bot-b->floor;
    gm_rect(t,x-hw,b->floor-1,2*hw+1,depth+1,C(VOIDC));
    for(int k=1;k<=4;k++){
        int y=b->floor+depth*k*k/18,in=hw*k/6;
        gm_span_mix(t,x-hw+in,x+hw-in,y,C(UV),(unsigned)(120/k));
    }
    gm_rect(t,x-hw-1,b->floor-1,2,sc(b,8),edge);
    gm_rect(t,x+hw,b->floor-1,2,sc(b,8),edge);
}
/* The gate is ONE gemstone, about 76x118 logical px, centred at floor-111
   (the physics window centre) and rotated to the gate angle. Its long axis
   carries the seam and the gold notch: line the sail up with it. The same
   outline feeds gm_shatter, so the solid that was drawn is the one that
   breaks. */
#define GEM_N 8
static const float gem_lx[GEM_N]={-4.f,20.f,38.f,30.f,6.f,-22.f,-38.f,-30.f};
static const float gem_ly[GEM_N]={-59.f,-47.f,-10.f,34.f,59.f,48.f,8.f,-38.f};
static void gem_outline(const struct band *b,float ang,float k,int *ox,int *oy){
    float S=sinf(ang),Cs=cosf(ang),s=b->s*k;
    for(int i=0;i<GEM_N;i++){
        float lx=gem_lx[i]*s,ly=gem_ly[i]*s;
        ox[i]=ri(Cs*lx-S*ly);oy[i]=ri(S*lx+Cs*ly);
    }
}
static uint16_t gem_face(int ax,int ay,int bx,int by){
    float nx=(float)(by-ay),ny=(float)(ax-bx),l=sqrtf(nx*nx+ny*ny)+.001f;
    nx/=l;ny/=l;
    float lv=clampf(-.6f*nx-.8f*ny,-1.f,1.f);
    uint32_t hue=nx<0?xz_blend(UV,CYAN,150):xz_blend(UV,MAGENTA,120);
    return lv>0?C(xz_blend(0x2a0f5cu,hue,(unsigned)(70.f+170.f*lv))):C(xz_blend(0x12052eu,0x2a0f5cu,(unsigned)(200.f+200.f*lv)));
}
static void crystal_gate(const tg *t,const struct band *b,int x,float ang,uint16_t seam,int state,int hot,double ph){
    int cy=b->floor-sc(b,GATE_CENTER),ox[GEM_N],oy[GEM_N],X[GEM_N],Y[GEM_N],IX[GEM_N],IY[GEM_N];
    gem_outline(b,ang,1.f,ox,oy);
    float S=sinf(ang),Cs=cosf(ang),s=b->s;
    int tcx=x+ri(-3.f*s*Cs+6.f*s*S),tcy=cy+ri(-3.f*s*S-6.f*s*Cs);
    for(int i=0;i<GEM_N;i++){X[i]=x+ox[i];Y[i]=cy+oy[i];IX[i]=tcx+ox[i]*12/25;IY[i]=tcy+oy[i]*12/25;}
    unsigned a=state?170u:250u;
    if(!state)gm_glow(t,x,cy,sc(b,58),C(UV),hot?110u:55u);
    gm_poly(t,X,Y,GEM_N,C(0x12052eu),a);
    for(int i=0;i<GEM_N;i++){
        int j=(i+1)%GEM_N;
        int qx[4]={X[i],X[j],IX[j],IX[i]},qy[4]={Y[i],Y[j],IY[j],IY[i]};
        uint16_t f=gem_face(X[i],Y[i],X[j],Y[j]);
        if(state)f=gm_mix(f,C(MISS),150);
        gm_poly(t,qx,qy,4,f,a);
    }
    gm_poly(t,IX,IY,GEM_N,state?C(0x2a2440u):C(xz_blend(0x1a0a44u,CYAN,60)),state?170u:225u);
    /* refraction: two coloured caustics inside the table */
    if(!state){
        gm_line(t,IX[7],IY[7],IX[3],IY[3],1,C(CYAN),110);
        gm_line(t,IX[1],IY[1],IX[5],IY[5],1,C(MAGENTA),90);
    }
    uint16_t rim=state?C(MISS):C(xz_blend(UV,TEXT,170));
    for(int i=0;i<GEM_N;i++){
        int j=(i+1)%GEM_N;
        int lit=(X[j]-X[i])*-8+(Y[j]-Y[i])*6>0;
        gm_line(t,X[i],Y[i],X[j],Y[j],1,lit?rim:gm_mix(rim,C(INK),140),256);
        gm_line(t,X[i],Y[i],IX[i],IY[i],1,gm_mix(C(0x12052eu),rim,90),200);
    }
    /* seam along the long axis, gold notch past the upper tip */
    int ex=ri(52.f*s*S),ey=ri(52.f*s*Cs);
    gm_line(t,x-ex,cy+ey,x+ex,cy-ey,imax(3,sc(b,5)),C(VOIDC),230);
    gm_line(t,x-ex,cy+ey,x+ex,cy-ey,imax(1,sc(b,2)),seam,256);
    int nx0=x+ri(60.f*s*S),ny0=cy-ri(60.f*s*Cs);
    gm_line(t,nx0,ny0,nx0+ri(10.f*s*S),ny0-ri(10.f*s*Cs),imax(2,sc(b,3)),C(GOLD),256);
    if(hot)gm_glow(t,x,cy,sc(b,30),seam,130);
    if(!state){
        /* restrained glints: one steady at the lit shoulder, one breathing */
        int g1x=X[7]+(IX[7]-X[7])/3,g1y=Y[7]+(IY[7]-Y[7])/3,r=imax(2,sc(b,4));
        gm_line(t,g1x-r,g1y,g1x+r,g1y,1,C(0xf4f8ffu),230);gm_line(t,g1x,g1y-r,g1x,g1y+r,1,C(0xf4f8ffu),230);
        unsigned tw=(unsigned)(120+xz_sin1024((int)(ph*180.0))*100/1024);
        gm_rect_mix(t,IX[1]-1,IY[1]-1,2,2,C(0xf4f8ffu),tw);
    }else{
        for(int i=0;i<4;i++){int d=i*90+35;gm_line(t,x,cy,x+xz_cos1024(d)*sc(b,30)/1024,cy+xz_sin1024(d)*sc(b,30)/1024,1,C(TEXT),110);}
    }
}
/* Launch mark: gold chevron on the rail at e.at. A real ARROW event at this
   mark compresses it, then it springs back past rest and settles. */
static void launch_mark(const tg *t,const struct band *b,int lx,double dist,float flex){
    unsigned a=dist<=1?256u:dist<3?(unsigned)(256.0-(dist-1.0)*60.0):136u;
    uint16_t col=gm_mix(C(INK),C(GOLD),a);
    int f=b->floor,u=imax(3,sc(b,7)),w=imax(2,sc(b,2));
    int hgt=ri((float)u*(1.f-flex*.55f));
    int wid=ri((float)u*(1.f+flex*.35f));
    gm_rect(t,lx-1,f-1,3,imax(3,sc(b,6)),col);
    gm_line(t,lx-wid,f-hgt-1,lx,f-2*hgt-1,w,col,256);
    gm_line(t,lx,f-2*hgt-1,lx+wid,f-hgt-1,w,col,256);
    if(flex>.05f)gm_glow(t,lx,f-hgt,sc(b,14),C(GOLD),(unsigned)(flex*200.f));
}
static void ideal_arc(const tg *t,const struct band *b,int lx){
    for(int k=1;k<8;k++){
        float u=(float)k/8.f;
        int x=lx+ri(u*(float)JUMP_PULSES*SCROLL*b->s),y=b->floor-ri((HERO_HALF+360.f*u*(1.f-u))*b->s);
        gm_rect_mix(t,x-1,y-1,2,2,C(UV),130);
    }
}

/* ---- the rider: glass craft, sail cue, posed cat ---- */
struct rider { int x, feet; float bank, sail, sq, flex; unsigned pose,pose_b; float blend; uint16_t pc; int lock; float flash; };
static void craft(const tg *t,const struct band *b,const struct rider *r){
    float s=b->s,S=sinf(r->bank),Cs=cosf(r->bank),L=74.f*s,T=8.f*s*(1.f-.45f*r->flex),sag=6.f*s*r->flex;
    static const float bx[6]={.56f,.30f,-.44f,-.50f,-.28f,.36f},by[6]={.15f,.5f,.4f,0.f,-.5f,-.4f};
    int X[6],Y[6];
    for(int i=0;i<6;i++){
        float lx=bx[i]*L,ly=by[i]*T-(fabsf(bx[i])<.35f?sag:0.f);
        X[i]=r->x+ri(Cs*lx+S*ly);Y[i]=r->feet+sc(b,4)+ri(S*lx-Cs*ly);
    }
    gm_poly(t,X,Y,6,C(GLASS),120);
    gm_line(t,X[0],Y[0],X[1],Y[1],1,C(TEXT),230);
    gm_line(t,X[1],Y[1],X[2],Y[2],1,C(TEXT),200);
    gm_line(t,X[3],Y[3],X[4],Y[4],imax(1,sc(b,2)),r->pc,256);
    gm_line(t,X[4],Y[4],X[5],Y[5],imax(1,sc(b,2)),r->pc,256);
    gm_line(t,X[5],Y[5],X[0],Y[0],1,r->pc,256);
}
static void sail(const tg *t,const struct band *b,const struct rider *r){
    float S=sinf(r->sail),Cs=cosf(r->sail),Lm=70.f*b->s;
    int bx=r->x-sc(b,8),by=r->feet+sc(b,1);
    int tx2=bx+ri(S*Lm),ty2=by-ri(Cs*Lm);
    int px=bx+ri(S*Lm*.42f-Cs*24.f*b->s),py=by-ri(Cs*Lm*.42f)-ri(S*24.f*b->s);
    int xs[3]={bx,tx2,px},ys[3]={by,ty2,py};
    uint16_t mast=r->lock?C(GOLD):r->pc;
    if(r->flash>0)mast=gm_mix(mast,C(RED),(unsigned)(r->flash*200.f));
    gm_poly(t,xs,ys,3,r->pc,60);
    gm_line(t,tx2,ty2,px,py,1,gm_mix(r->pc,C(TEXT),120),180);
    gm_line(t,bx,by,tx2,ty2,imax(2,sc(b,3)),C(INK),256);
    gm_line(t,bx,by,tx2,ty2,imax(1,sc(b,2)),mast,256);
    gm_line(t,tx2,ty2,tx2+ri(8.f*b->s*S),ty2-ri(8.f*b->s*Cs),imax(2,sc(b,3)),C(GOLD),256);
}
/* Pose state from the engine: height, velocity, jump/landing stamps and the
   approach of the next launch mark. Only reads. */
static void pose_rider(struct rider *r,const struct xz_rhythm_lab *g,const struct xz_glyph_player *gp,const struct xz_lab_event *next,double per){
    double pos=g->position,jt=(pos-gp->jump_at)/per,lt=(pos-gp->landing_at)/per;
    float h=gp->height,v=gp->velocity,vn=clampf(v/XZ_GLYPH_JUMP_SPEED,-1.2f,1.2f);
    r->pose=0;r->pose_b=0;r->blend=0;r->sq=1;r->flex=0;
    if(h>.5f){
        if(jt>=0&&jt<.14){r->pose=2;r->pose_b=3;r->blend=(float)(jt/.14);r->sq=1.22f-.12f*(float)(jt/.14);}
        else if(v>140){r->pose=3;r->pose_b=4;r->blend=clampf((180.f-v)/40.f,0,1);r->sq=1.f+.1f*vn;}
        else if(v>-140){r->pose=4;r->pose_b=v<0?5:3;r->blend=clampf((fabsf(v)-100.f)/40.f,0,1);r->sq=1.f;}
        else{r->pose=5;r->sq=1.f+.14f*fabsf(vn);}
        return;
    }
    if(lt>=0&&lt<.6){
        float u=(float)(lt/.6),A=.16f+.32f*clampf(gp->landing_impact,0,1);
        float k=gm_spring(u,10.f,5.f);
        r->sq=1.f-A*k;r->flex=clampf(k,0,1)*clampf(gp->landing_impact*1.4f,.3f,1);
        if(lt<.16){r->pose=6;}
        else if(lt<.42){r->pose=6;r->pose_b=7;r->blend=(float)((lt-.16)/.1);}
        else{r->pose=7;r->pose_b=0;r->blend=(float)((lt-.42)/.18);}
        return;
    }
    if(next){
        double d=(next->at-pos)/per;
        if(d>0&&d<.45){float c=1.f-(float)(d/.45);r->pose=0;r->pose_b=1;r->blend=c;r->sq=1.f-.13f*c;r->flex=.35f*c;return;}
    }
    r->sq=1.f-.03f*clampf(g->pulse,0,1);
}
static void draw_rider(const tg *t,const struct band *b,const struct rider *r){
    sail(t,b,r);
    craft(t,b,r);
    struct gm_cat c;
    c.x=r->x+ri(r->flash>0?(float)xz_sin1024((int)(r->flash*1440.f))*3.f*b->s/1024.f:0.f);
    c.y=r->feet;c.scale=(b->duo?.5f:.74f)*(b->duo?1.f:b->s);c.sq=r->sq;c.ang=r->bank*1.4f;
    c.pose=r->pose;c.pose_b=r->pose_b;c.blend=r->blend;c.tint=C(RED);c.tinta=(unsigned)(clampf(r->flash,0,1)*150.f);
    gm_draw_cat(t,&c);
}

/* ---- contact effects from the real impact ring ---- */
static void effects(const tg *t,const struct xz_rhythm_lab *g,const struct band *b,const struct rider *r){
    const struct xz_glyph_player *gp=&g->glyph[b->pi];
    double per=per_of(g),pos=g->position;
    for(unsigned i=0;i<XZ_GAME_IMPACTS;i++){
        const struct xz_game_impact *e=&gp->effects.events[i];
        if(!e->serial)continue;
        double age=(pos-e->time)/per;
        if(age<0||age>1.2)continue;
        int x=ev_x(b,e->at,pos,per);
        if(x<-120||x>920)continue;
        float st=clampf(e->strength,0,1);
        switch(e->kind){
        case XZ_IMPACT_LAND:
            if(age<.7){
                gm_ripple(t,x,b->floor,0,(float)(age/.7),sc(b,3.f+6.f*st),sc(b,40.f+50.f*st),C(xz_blend(UV,TEXT,120)),220);
                if(st>.5f&&age<.35){ /* rough: scrape sparks thrown back */
                    float u=(float)(age/.35);
                    for(int k=0;k<6;k++){
                        int sx=x-sc(b,6.f+40.f*u*(1.f+(float)k*.3f)),sy=b->floor-sc(b,2.f+12.f*u*(float)(k%3))+sc(b,20.f*u*u);
                        gm_rect_mix(t,sx,sy,2,1,C(0xffa860u),(unsigned)(230.f*(1.f-u)));
                    }
                }
            }
            break;
        case XZ_IMPACT_PERFECT_LAND:
            if(age<.7){
                float u=(float)(age/.7);
                gm_ripple(t,x,b->floor,0,u,sc(b,4.f+5.f*st),sc(b,60.f+40.f*st),C(GOLD),240);
                if(u<.5f){
                    float v=u/.5f;
                    for(int k=-2;k<=2;k++){
                        int lx=x+k*sc(b,7),y0=b->floor-sc(b,4.f+60.f*v),y1=y0+sc(b,10.f+(float)(2-iabs(k))*6.f);
                        gm_line(t,lx,y0,lx,y1,1,C(GOLD),(unsigned)(240.f*(1.f-v)));
                    }
                    gm_glow(t,x,b->floor-sc(b,6),sc(b,24),C(GOLD),(unsigned)(180.f*(1.f-v)));
                }
            }
            break;
        case XZ_IMPACT_LAUNCH:
        case XZ_IMPACT_BOOST:
            if(age<.3){
                float u=(float)(age/.3);
                uint16_t col=e->kind==XZ_IMPACT_BOOST?C(GOLD):r->pc;
                int tx0=r->x-sc(b,34),ty0=r->feet+sc(b,4);
                for(int k=0;k<4;k++){
                    int len=sc(b,(10.f+22.f*st)*(1.f-u*.5f)+(float)k*3.f);
                    gm_line(t,tx0-k*sc(b,3),ty0+k*sc(b,2),tx0-k*sc(b,3)-len,ty0+k*sc(b,2)+len/2,imax(1,sc(b,2)),col,(unsigned)(230.f*(1.f-u)));
                }
                for(int k=0;k<5;k++){int d=ri((6.f+u*30.f+(float)k*5.f)*b->s);gm_rect_mix(t,x+((k&1)?d:-d),b->floor-2-k%2,3,2,col,(unsigned)(200.f*(1.f-u)));}
            }
            break;
        case XZ_IMPACT_GLANCE:
            if(age<.6){
                float u=(float)(age/.6);
                int y=b->floor-sc(b,e->altitude);
                for(int k=0;k<7;k++){
                    int d=150+k*30,len=sc(b,6.f+30.f*u);
                    gm_rect_mix(t,x+xz_cos1024(d)*len/1024,y+xz_sin1024(d)*len/1024+sc(b,30.f*u*u),2,2,C(k&1?MISS:RED),(unsigned)(220.f*(1.f-u)));
                }
                {
                    int r=imax(3,sc(b,7)),cx[4]={0,r,0,-r},cyy[4]={-r,0,r,0};
                    gm_shatter(t,x,y,cx,cyy,4,r,0,u,-1.f,-.3f,.2f,C(xz_blend(CRYST,MISS,140)),C(CRYST),C(MISS),C(RED),e->serial);
                }
            }
            break;
        default:break;
        }
    }
}

/* ---- birthday cow ---- */
static void ufo(const tg *t,int cx,int cy,int rw,double ph){
    int rh=imax(3,rw/4),dw=rw*11/20,dh=imax(3,rw*2/5);
    for(int r=-dh;r<=0;r++){int hw=gm_isqrt(dh*dh-r*r)*dw/dh;gm_span_mix(t,cx-hw,cx+hw,cy-rh/2+r,C(CYAN),110);}
    for(int r=-rh;r<=rh;r++){int hw=gm_isqrt(rh*rh-r*r)*rw/rh;gm_span(t,cx-hw,cx+hw,cy+r,C(r>0?0x3a3160u:0x8f86b8u));}
    static const uint32_t lc[3]={GOLD,MAGENTA,CYAN};
    int tick=(int)(ph*4.0);
    for(int i=0;i<6;i++){int x=cx-rw+rw*(2*i+1)/6;gm_rect(t,x-1,cy,3,2,C(lc[((i+tick)%3+3)%3]));}
}
/* One cow, articulated on the music: two poses a beat, squash on the beat,
   the cone pops on the pulse. Shared by the fly-by and the brief. */
static void party_cow(const tg *t,int x,int y,float scale,double beats,float pulse,int flip){
    double fr=beats*2.0-floor(beats*2.0);
    unsigned pose=(unsigned)((long)floor(beats*2.0)&3);
    float k=(float)(1.0-fr);
    float sq=1.f+.09f*k*k-.04f;
    int hat=ri((3.f*k*k+4.f*clampf(pulse,0,1))*scale/.8f);
    float sway=(float)xz_sin1024((int)(beats*180.0))*.10f/1024.f;
    gm_draw_cow(t,x,y,scale,sq,sway,pose,hat,flip,256u);
}
static void cow_flyby(const tg *t,const struct xz_rhythm_lab *g,const struct band *b,double per){
    const struct xz_glyph_player *gp=&g->glyph[b->pi];
    double age=(g->position-gp->party_at)/per;
    if(gp->party_at<-9000||age<0||age>=4)return;
    float u=(float)(age/4.0);
    int size=b->duo?50:84,half=size/2;
    float travel=u<.62f?u/.62f:1.f;
    int cx=-half-40+ri(travel*(float)(b->herox+80+half));
    int climb=u>.7f?ri((u-.7f)/.3f*(float)(size+60)):0;
    int uy=b->top+imax(8,sc(b,14))-climb,cy=uy+imax(6,sc(b,10))+half;
    int bx[4]={cx-sc(b,16),cx+sc(b,16),cx+half+4,cx-half-4},by[4]={uy,uy,cy+half,cy+half};
    gm_poly(t,bx,by,4,C(GOLD),34);
    float low=band_level(g,g->position,0);
    int floorlim=b->floor-sc(b,175);
    static const uint32_t cc[4]={GOLD,MAGENTA,CYAN,0xff9a3cu};
    for(unsigned i=0;i<20u;i++){
        uint32_t h=gm_hash(i*2654435761u+0x51u);
        float life=(float)fmod(age*1.3+(double)(h%100u)/100.0,1.0);
        int x=cx+(int)((h>>8)%(uint32_t)(size+20))-half-10+ri((float)xz_sin1024((int)(age*240.0)+(int)(i*40u))*(4.f+10.f*low)*b->s/1024.f);
        int y=cy-half+ri(life*(float)(size+sc(b,40)));
        if(y>floorlim)continue;
        int sz=imax(1,sc(b,2.f+2.f*low));
        gm_rect_mix(t,x,y,sz+(int)((h>>20)&1u),sz,C(cc[i&3u]),(unsigned)(230.f*(1.f-life*.6f)));
    }
    party_cow(t,cx,cy,(float)size/96.f*1.1f,age,g->pulse,0);
    ufo(t,cx,uy,imax(18,sc(b,40)),age);
}

/* ---- one band ---- */
static void world(cv c,const tg *T,const struct xz_rhythm_lab *g,const struct band *b,unsigned act){
    tg tt=*T;
    if(tt.y0<b->top)tt.y0=b->top;
    if(tt.y1>b->bot)tt.y1=b->bot;
    const tg *t=&tt;
    unsigned pi=b->pi;
    const struct xz_lab_player *p=&g->player[pi];
    const struct xz_glyph_player *gp=&g->glyph[pi];
    double per=per_of(g),pos=g->position,ph=(pos-g->origin)/per;
    uint16_t pc=C(pcol(pi));
    unsigned n=g->event_count<XZ_LAB_EVENTS?g->event_count:XZ_LAB_EVENTS;

    props(t,b,ph,act);
    ground(t,g,b,per,ph);

    const struct xz_lab_event *next=NULL,*ngate=NULL;
    for(unsigned i=0;i<n;i++){
        const struct xz_lab_event *e=&g->events[i];
        if(!is_glyph(e))continue;
        if(e->grade[pi]==LAB_PENDING&&e->end>=pos-per*.08){
            if(!next||e->end<next->end)next=e;
            if(e->kind==LAB_GLYPH_GATE&&(!ngate||e->end<ngate->end))ngate=e;
        }
        if(e->kind==LAB_GLYPH_GAP){
            int x=ev_x(b,e->end,pos,per);
            unsigned gr=e->grade[pi];
            if(x>-60&&x<860)gap(t,b,x,C(gr==LAB_PENDING?MAGENTA:gr==LAB_MISSED?MISS:GOLD));
        }
    }
    float ang=clampf(p->angle,-1.2f,1.2f);
    int gclose=ngate&&ngate->end-pos<per*2.4;
    int lock=gclose&&fabsf(ang-ngate->angle)<=GATE_TOL;

    for(unsigned i=0;i<n;i++){
        const struct xz_lab_event *e=&g->events[i];
        if(!is_glyph(e)||e->kind==LAB_GLYPH_GAP)continue;
        int x=ev_x(b,e->end,pos,per);
        if(x<-140||x>940)continue;
        unsigned gr=e->grade[pi];
        if(e->kind==LAB_GLYPH_SPIKE){
            const struct xz_game_impact *gl=gm_find(&gp->effects,XZ_IMPACT_GLANCE,e->end,1.0);
            float rec=0;
            if(gl){double a=(pos-gl->time)/per;if(a>=0&&a<.5)rec=.5f*gm_spring((float)(a/.5),12.f,4.f);}
            uint16_t ed=gr==LAB_PENDING?C((e->id&1)?MAGENTA:CYAN):gr==LAB_MISSED?C(MISS):gr==LAB_PERFECT?C(GOLD):pc;
            spike(t,b,x,e->energy,ed,gr==LAB_PENDING&&e==next,rec);
        }else{
            const struct xz_game_impact *sh=gm_find(&gp->effects,XZ_IMPACT_SHATTER,e->end,1.0);
            if(sh){
                double a=(pos-sh->time)/(per*.8);
                if(a>=0&&a<1){
                    /* the same gem, cut at the rider's side of the seam; it
                       breaks forward along the rider's travel */
                    int ox[GEM_N],oy[GEM_N];
                    gem_outline(b,e->angle,1.f,ox,oy);
                    int cy=b->floor-sc(b,GATE_CENTER);
                    uint16_t core=gr==LAB_PERFECT?C(GOLD):C(0xf4f8ffu);
                    gm_shatter(t,x,cy,ox,oy,GEM_N,-sc(b,26),sc(b,6),(float)a,.95f,-.22f,sh->strength,
                        C(xz_blend(UV,CYAN,150)),C(0x12052eu),C(xz_blend(UV,TEXT,170)),core,sh->serial);
                }
                continue;
            }
            if(gr==LAB_PERFECT||gr==LAB_GOOD)continue;
            int hot=gr==LAB_PENDING&&e==ngate&&e->end-pos<per*.35;
            uint16_t seam=gr==LAB_MISSED?C(MISS):e==ngate&&gclose?C(lock?GOLD:MAGENTA):C(UV);
            crystal_gate(t,b,x,e->angle,seam,gr==LAB_MISSED,hot,ph);
        }
    }
    for(unsigned i=0;i<n;i++){
        const struct xz_lab_event *e=&g->events[i];
        if(!is_glyph(e)||e->grade[pi]!=LAB_PENDING)continue;
        int lx=ev_x(b,e->at,pos,per);
        if(lx<b->herox-30||lx>840)continue;
        float flex=0;
        const struct xz_game_impact *ar=gm_find(&gp->effects,XZ_IMPACT_ARROW,e->at,1.0);
        if(ar){double a=(pos-ar->time)/(per*.5);if(a>=0&&a<1)flex=clampf(ar->strength,0,1)*gm_spring((float)a,13.f,4.5f);}
        launch_mark(t,b,lx,(e->at-pos)/per,flex);
    }
    if(next){int lx=ev_x(b,next->at,pos,per);if(lx>=b->herox-4)ideal_arc(t,b,lx);}

    struct rider r;
    float h=clampf(gp->height,-40.f,220.f);
    r.x=b->herox;r.feet=b->floor-sc(b,h);r.pc=pc;r.lock=lock;r.flash=clampf(gp->flash,0,1);
    r.sail=ang;r.bank=ang*.18f-clampf(gp->velocity/XZ_GLYPH_JUMP_SPEED,-1,1)*.12f;
    pose_rider(&r,g,gp,next,per);
    /* afterimages from the engine's own parabola */
    if(h>2){
        for(int k=3;k>=1;k--){
            double at0=pos-(double)k*.06*per;
            float hh=gp->jump_at<=at0?jump_h((at0-gp->jump_at)/per):0.f;
            gm_rect_mix(t,r.x-sc(b,7.f*(float)k*SCROLL/120.f),b->floor-sc(b,hh+20.f),sc(b,8),sc(b,8),pc,(unsigned)(150-k*40));
        }
    }
    effects(t,g,b,&r);
    if(pos<gp->safe_until)gm_glow(t,r.x,r.feet-sc(b,30),sc(b,40),C(UV),90);
    draw_rider(t,b,&r);
    cow_flyby(t,g,b,per);

    if(gclose&&ngate&&!lock){
        int dir=ngate->angle>ang?1:-1,ay=r.feet-sc(b,92),x0=r.x-dir*sc(b,8),x1=r.x+dir*sc(b,12),hd=imax(3,sc(b,5));
        gm_line(t,x0,ay,x1,ay,2,C(MAGENTA),256);gm_line(t,x1,ay,x1-dir*hd,ay-hd,2,C(MAGENTA),256);gm_line(t,x1,ay,x1-dir*hd,ay+hd,2,C(MAGENTA),256);
    }
    if(p->feedback&&p->feedback[0]&&p->feedback_until>pos&&p->feedback_until-pos<per*1.4){
        int ly=imax(b->top+4,r.feet-sc(b,120));
        if(b->duo)txc(c,r.x+90,b->top+6,p->feedback,1,DIM,300);
        else atc(c,r.x+40,ly,p->feedback,1,DIM);
    }
    if(b->duo){
        SB(s);sbs(&s,pi?"P2  ":"P1  ");sbu(&s,p->score);sbs(&s,"  X");sbu(&s,p->streak);
        gm_rect_mix(t,4,b->top+4,gm_text_width(s.s,GM_F18)+12,24,C(INK),170);
        tx(c,10,b->top+6,s.s,2,pcol(pi));
    }
    if(!g->valid||!g->wave.count)txc(c,400,b->floor+sc(b,16),"NO WAVEFORM FROM THE DECK",1,FAINT,500);
}

static unsigned root32(unsigned n){unsigned r=0,bit=1u<<30;while(bit>n)bit>>=2;while(bit){if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;}
static void rrect(cv c,int x,int y,int w,int h,int radius,uint32_t color){
    if(w<=0||h<=0)return;int r=imin(radius,imin(w,h)/2);
    for(int row=0;row<h;row++){int d=row<r?r-1-row:row>=h-r?row-(h-r):0,inset=d?r-(int)root32((unsigned)(r*r-d*d)):0;xz_rect(c,x+inset,y+row,w-inset*2,1,color);}
}

/* ---- chrome ---- */
/* Controls drawn this frame, for encoder focus. Set only inside a render call. */
static struct xz_game_targets *targets;
static void paint_button(cv c,int x,int y,int w,int h,const char *label,const char *sub,int style,uint32_t accent){
    uint32_t fill=PANEL,edge=LINE,text=TEXT,subc=DIM;
    if(style==B_PRIMARY){fill=xz_blend(INK,accent,90);edge=accent;}
    else if(style==B_ACTIVE){fill=accent;edge=TEXT;text=INK;subc=INK;}
    else if(style==B_OFF){fill=PANEL2;edge=0x241a50u;text=FAINT;subc=FAINT;}
    rrect(c,x+3,y+3,w-6,h-6,10,edge);
    rrect(c,x+5,y+5,w-10,h-10,8,fill);
    tg t=tgc(c);t.x0=imax(0,x+8);t.x1=imin(800,x+w-8);
    int lf=h>=70?GM_F28:GM_F18,lh=gm_text_height(lf),sh=sub?gm_text_height(GM_F12):0;
    int y0=y+(h-lh-sh)/2;
    if(sub){gm_text(&t,x+w/2-gm_text_width(sub,GM_F12)/2,y0,sub,GM_F12,C(subc),256u);y0+=sh-2;}
    gm_text(&t,x+w/2-gm_text_width(label,lf)/2,y0,label,lf,C(text),256u);
}
static void button(cv c,int x,int y,int w,int h,const char *label,const char *sub,int style,uint32_t accent){
    if(style!=B_OFF)xz_game_target_add(targets,x,y,w,h,style==B_PRIMARY?XZ_GAME_PRIMARY:XZ_GAME_NORMAL);
    paint_button(c,x,y,w,h,label,sub,style,accent);
}
/* Returns to the deck; encoder focus never starts on it. */
static void deck_button(cv c){xz_game_target_add(targets,704,0,96,48,XZ_GAME_EXIT);paint_button(c,704,0,96,48,"DECK",NULL,B_NORMAL,CYAN);}

static void hud(cv c,const struct xz_rhythm_lab *g,unsigned players,float dur){
    xz_rect(c,0,0,800,48,PANEL2);xz_rect(c,0,47,800,1,LINE);
    at(c,14,3,"STATIC SURFER",1,GOLD);
    tx(c,14,28,"BY VJ OPTICMYSTIC",1,DIM);
    if(players==1){
        const struct xz_lab_player *p=&g->player[0];
        tx(c,176,2,"SCORE",1,DIM);
        SB(s);sbu(&s,p->score);at(c,176,12,s.s,2,TEXT);
        tx(c,300,2,"STREAK",1,DIM);
        SB(k);sbu(&k,p->streak);at(c,300,12,k.s,2,p->streak>=12?GOLD:p->streak>=4?CYAN:TEXT);
        SB(bs);sbs(&bs,"BEST ");sbu(&bs,p->best_streak);tx(c,346,26,bs.s,1,FAINT);
        tx(c,410,2,"TIMING",1,DIM);
        SB(m);if(p->timing_hits){sbi(&m,roundi(p->last_error_ms));sbs(&m," MS");}else sbs(&m,"--");
        tx(c,410,20,m.s,2,TEXT);
    }else{
        for(unsigned i=0;i<2;i++){
            const struct xz_lab_player *p=&g->player[i];int x=176+(int)i*170;
            tx(c,x,2,i?"P2 SCORE":"P1 SCORE",1,pcol(i));
            SB(s);sbu(&s,p->score);tx(c,x,16,s.s,2,TEXT);
            SB(k);sbs(&k,"X");sbu(&k,p->streak);tx(c,x+100,16,k.s,2,p->streak>=12?GOLD:DIM);
        }
    }
    float el=clampf(g->elapsed,0,dur),prog=el/dur;
    int px=520,pw=170,py=33,fx=px+(int)((float)pw*prog);
    static const uint32_t segc[3]={UV,MAGENTA,CYAN};
    for(int k=0;k<3;k++){
        int x0=px+pw*k/3,x1=px+pw*(k+1)/3-2;
        xz_rect(c,x0,py,x1-x0,6,xz_blend(PANEL2,segc[k],70));
        if(fx>x0)xz_rect(c,x0,py,imin(fx,x1)-x0,6,segc[k]);
    }
    xz_rect(c,fx-1,py-3,3,12,TEXT);
    if(g->position<g->practice_until)txr(c,690,10,"PRACTICE",2,GOLD);
    else{
        unsigned left=(unsigned)ceilf(dur-el);
        SB(tl);sbu(&tl,left/60);sbs(&tl,":");if(left%60<10)sbs(&tl,"0");sbu(&tl,left%60);
        txr(c,690,10,tl.s,2,TEXT);
        tx(c,px,12,"LEFT",1,DIM);
    }
    deck_button(c);
}

static void act_of(const struct xz_rhythm_lab *g,float dur,unsigned *sa,unsigned *sb,unsigned *mixv){
    float prog=clampf(g->elapsed/dur,0,1),seg=prog*3.f;
    *sa=seg>=2.f?2u:(unsigned)seg;*sb=*sa;*mixv=0;
    float within=seg-(float)*sa;
    if(*sa<2&&within>.94f){*sb=*sa+1;*mixv=(unsigned)((within-.94f)/.06f*16.f);if(*mixv>16)*mixv=16;}
}

static void run_screen(cv c,const tg *t,const struct xz_rhythm_lab *g){
    unsigned players=xz_lab_players(g)==2?2u:1u;
    float dur=g->duration>1?g->duration:120.f;
    unsigned sa,sb,mixv;act_of(g,dur,&sa,&sb,&mixv);
    struct band bs[2];
    for(unsigned i=0;i<2;i++){
        if(players==2){bs[i].top=BG_TOP+188*(int)i;bs[i].bot=bs[i].top+188;bs[i].floor=bs[i].top+150;bs[i].herox=190;bs[i].duo=1;bs[i].s=.6f;}
        else{bs[i].top=BG_TOP;bs[i].bot=SCENE_BOT;bs[i].floor=340;bs[i].herox=196;bs[i].duo=0;bs[i].s=1.f;}
        bs[i].pi=i;
    }
    double ph=(g->position-g->origin)/per_of(g);
    panorama(t,g,sa,sb,mixv,(int)(ph*SCROLL*.1),0);
    for(unsigned i=0;i<players;i++)world(c,t,g,&bs[i],sa);
    if(players==2)gm_rect(t,0,BG_TOP+187,800,2,C(LINE));
    hud(c,g,players,dur);

    xz_rect(c,0,SCENE_BOT,800,480-SCENE_BOT,PANEL2);xz_rect(c,0,SCENE_BOT,800,1,LINE);
    int paused=g->phase==LAB_PAUSE,held=!!g->player[0].held||g->touch_down;
    unsigned dk=deck_of(g,0);
    button(c,0,430,160,50,"RETRY","PAD G",B_NORMAL,CYAN);
    button(c,160,430,160,50,"BUTTONS",action_name(g,dk),B_NORMAL,GOLD);
    SB(ac);sbs(&ac,"TOUCH OR ");sbs(&ac,action_name(g,dk));
    button(c,320,430,200,50,"JUMP",paused?"PAUSED":held?"HELD":ac.s,paused?B_OFF:held?B_ACTIVE:B_PRIMARY,CYAN);
    button(c,520,430,120,50,"RATE","PAD F",B_NORMAL,GOLD);
    button(c,640,430,160,50,"MENU","PAD H",B_NORMAL,CYAN);
    if(paused){
        rrect(c,196,156,408,128,14,GOLD);rrect(c,199,159,402,122,12,PANEL2);
        int ended=g->wave.position>=g->wave.total-1;
        atc(c,400,172,ended?"TRACK ENDED":"MUSIC PAUSED",1,GOLD);
        if(ended)button(c,220,204,360,42,"BACK TO DECK",NULL,B_PRIMARY,CYAN);
        else atc(c,400,212,"PRESS PLAY ON THE DECK",1,TEXT);
        txc(c,400,256,"BUTTONS, MENU AND DECK STILL WORK",1,DIM,390);
    }
}

/* ---- brief: one screen, same world ---- */
static void demo(cv c,const tg *T,const struct xz_rhythm_lab *g){
    tg t=*T;t.x0=452;t.x1=784;t.y0=60;t.y1=318;
    struct band b;b.top=60;b.bot=318;b.floor=280;b.herox=530;b.duo=0;b.s=.62f;b.pi=0;
    double per=per_of(g),ph=(g->position-g->origin)/per;
    gm_rect_mix(&t,452,60,332,258,C(INK),120);
    ground(&t,g,&b,per,ph);
    double cyc=fmod(ph,2.0);if(cyc<0)cyc+=2.0;
    int gx=530+ri((float)(.6-cyc)*SCROLL*b.s);
    /* the gate rides in, the cat meets it at the seam, it breaks */
    if(cyc<.6)crystal_gate(&t,&b,gx,.5f,C(GOLD),0,cyc>.4,ph);
    else if(cyc<1.4){
        int ox[GEM_N],oy[GEM_N];gem_outline(&b,.5f,1.f,ox,oy);
        gm_shatter(&t,gx,b.floor-sc(&b,GATE_CENTER),ox,oy,GEM_N,-sc(&b,26),sc(&b,6),(float)((cyc-.6)/.8),.95f,-.22f,.9f,
            C(xz_blend(UV,CYAN,150)),C(0x12052eu),C(xz_blend(UV,TEXT,170)),C(GOLD),7u);
    }
    float h=jump_h(cyc);
    struct xz_glyph_player fake;memset(&fake,0,sizeof fake);
    fake.height=h;fake.jump_at=g->position-cyc*per;
    fake.landing_at=g->position-(cyc-JUMP_PULSES)*per;fake.landing_impact=.6f;
    fake.velocity=h>0?(float)(XZ_GLYPH_JUMP_SPEED-XZ_GLYPH_GRAVITY*cyc):0.f;
    struct rider r;r.x=b.herox;r.feet=b.floor-sc(&b,h);r.pc=C(CYAN);r.lock=1;r.flash=0;r.sail=.5f;r.bank=.08f;
    pose_rider(&r,g,&fake,NULL,per);
    draw_rider(&t,&b,&r);
    int ccx=500,ccy=126+ri((float)xz_sin1024((int)(ph*90.0))*4.f/1024.f);
    int bx[4]={ccx-12,ccx+12,ccx+44,ccx-44},by[4]={70,70,ccy+44,ccy+44};
    gm_poly(&t,bx,by,4,C(GOLD),40);
    party_cow(&t,ccx,ccy,.9f,ph,g->pulse,0);
    ufo(&t,ccx,70,30,ph);
    { tg q=tgc(c);gm_text_mid(&q,700,72,"Match the seam",GM_F18,C(GOLD)); }
    xz_border(c,452,60,332,258,LINE);
}

static void brief_screen(cv c,const tg *t,const struct xz_rhythm_lab *g){
    panorama(t,g,0,0,0,(int)(g->position*.2),1);
    gm_rect(t,0,SCENE_BOT,800,480-SCENE_BOT,C(PANEL2));
    gm_rect_mix(t,0,BG_TOP,444,282,C(INK),160);
    xz_rect(c,0,0,800,48,PANEL2);xz_rect(c,0,47,800,1,LINE);
    tx(c,16,16,"RHYTHM LAB GAMES",2,DIM);
    deck_button(c);

    at(c,24,58,"STATIC SURFER",3,GOLD);
    tx(c,26,106,"A GAME BY VJ OPTICMYSTIC",1,TEXT);
    tx(c,26,132,"Jump the crystal spines. Tilt your sail",2,DIM);
    tx(c,26,154,"to the seam and break the crystal gem.",2,DIM);
    unsigned players=xz_lab_players(g);
    if(players==2){
        SB(a);sbs(&a,"P1 TOP   JUMP  ");sbs(&a,action_name(g,0));tx(c,26,184,a.s,2,CYAN);
        SB(b);sbs(&b,"P2 LOWER   JUMP  ");sbs(&b,action_name(g,1));tx(c,26,206,b.s,2,MAGENTA);
    }else{
        unsigned dk=g->control_deck?1u:0u;
        SB(a);sbs(&a,"JUMP  ");sbs(&a,action_name(g,dk));tx(c,26,184,a.s,2,CYAN);
        tx(c,26,206,dk?"TILT  RIGHT JOG":"TILT  LEFT JOG",2,CYAN);
    }
    int ok=pulse_ready(g);
    rrect(c,22,238,400,30,8,ok?xz_blend(INK,CYAN,80):xz_blend(INK,RED,80));
    txc(c,222,242,!g->valid?"No track. Open music setup":!ok?"Track ready. Pulse needs setup":g->study?"Ready: study pattern":"Ready: track accents",2,TEXT,392);
    SB(k);sbs(&k,"CLOCK  ");sbs(&k,clock_tag(g));sbs(&k,players==2?"   DUO":"   SOLO");
    tx(c,26,280,k.s,1,DIM);
    demo(c,t,g);
    if(ok)button(c,0,330,800,80,"START STATIC SURFER","120 SECONDS / ACTION ALSO STARTS",B_PRIMARY,CYAN);
    else button(c,0,330,800,80,"SET UP MUSIC FIRST","THIS OPENS MUSIC SETUP",B_PRIMARY,RED);
    txc(c,400,414,"ART BY VJ OPTICMYSTIC / NEON FAMILIAR AND BIRTHDAY-HAT COWS",1,DIM,790);
    button(c,0,430,160,50,"TEN STUDIES","RHYTHM LAB MENU",B_NORMAL,CYAN);
    button(c,160,430,160,50,"BUTTONS",action_name(g,g->control_deck?1u:0u),B_NORMAL,GOLD);
    button(c,320,430,280,50,"MUSIC SETUP",NULL,B_NORMAL,CYAN);
    button(c,600,430,200,50,g->duo?"DUO":"SOLO",g->duo?"PLAYERS / TAP FOR SOLO":"PLAYERS / TAP FOR DUO",g->duo?B_PRIMARY:B_NORMAL,MAGENTA);
}

int xz_glyph_render(const struct xz_rhythm_lab *g,uint16_t *pixels,size_t count,size_t stride){
    return xz_glyph_render_targets(g,pixels,count,stride,NULL);
}
int xz_glyph_render_targets(const struct xz_rhythm_lab *g,uint16_t *pixels,size_t count,size_t stride,struct xz_game_targets *out){
    if(out)out->count=0;
    if(!g||!pixels||stride<800)return 0;
    if(stride>(SIZE_MAX-800)/479||count<stride*479+800)return 0;
    cv c;c.p=pixels;c.stride=stride;c.width=800;c.height=480;c.theme=0;
    tg t;t.p=pixels;t.stride=stride;t.x0=0;t.y0=0;t.x1=800;t.y1=480;
    targets=out;
    if(g->phase==LAB_BRIEF)brief_screen(c,&t,g);
    else run_screen(c,&t,g);
    targets=NULL;
    return 1;
}
