/* SPDX-License-Identifier: MIT */
/* WAVE RIDER, a game by VJ OpticMystic. The neon familiar rides a glass
 * surfcraft over hills composed from the deck's waveform. The top edge of every
 * hill is xz_rider_surface_at(), the exact surface the physics rides; the craft
 * is drawn at the real physical pitch (screen rotation = -pitch), never a
 * cosmetic angle. Camera: y = 342 - (alt - camera_alt)*zoom, x = 190 +
 * (at - position)*2*zoom, applied to terrain, gems, riders and impacts alike.
 * Contact effects come only from each player's impact ring. QUIET keeps every
 * shape and drops ambient motion. No heap, no IO, no per-pixel trig. */
#include "wave_rider.h"
#include "game_feel_art.h"
#include "game_motion_draw.h"
#include "../ui/ui_skin.h"
#include "wave_rider_art.h"
#include "game_nav.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W 800
#define H 480
#define WATER XZ_RIDER_WATER
#define PH XZ_RIDER_PLAYHEAD
#define HUD 48
#define FOOT 444
#define HZ ((double)XZ_WAVE_RIDER_HZ)

static const uint32_t ink=0xf1f5fc,dim=0x9aa0c4,faint=0x4f4a78,night=0x05030f,
    panel=0x120b2c,rim=0x33295e,danger=0xff5a6e,accent=0x3fe8ff,ready=0x4be38a,
    gold=0xffcf4a,aurora_rgb=0x5dffb4,comet_rgb=0xffa040,glass=0x7fdcff;
static const uint32_t rider_rgb[2]={0x3fe8ff,0xff4fd8};
static const char *const mode_name[3]={"SOLO","CO-OP","VERSUS"};
static const char *const difficulty_name[3]={"EASY","NORMAL","HARD"};

struct look { uint32_t band[3]; const char *kind,*name[3],*brief[3]; };
static struct look look_of(unsigned kind){
    if(kind==1)return (struct look){{0x3fe8ff,0x8a5cff,0xff4fd8},"STEMS",
        {"DRUMS","HARMONICS","VOCALS"},{"DRUMS","HARM","VOX"}};
    return (struct look){{0x5a6cff,0xff4fd8,0x3fe8ff},"3-BAND",
        {"LOW","MID","HIGH"},{"LOW","MID","HIGH"}};
}

struct spot { int x,y,on,lift; };
struct frame {
    struct xz_canvas c;
    struct gm_tgt t;
    const struct xz_rider *g;
    struct look l;
    int has_wave,has_course,quiet,players,play,fever,aurora,base;
    unsigned world,world_b,wmix; /* song third and its 0..16 blend into the next */
    float zoom,cam,energy,mu[3],pulse;
    double now,sim,origin; /* origin: world sample drawn at the playhead column */
    struct spot s[2];
    int16_t top[W];
    uint8_t lit[W],th[W],bl[3][W];
    int16_t sine[256];
    int sine_ready;
};

static uint16_t C(uint32_t c){return gm_c565(c);}
static int clampi(int v,int a,int b){return v<a?a:v>b?b:v;}
static float clampf(float v,float a,float b){if(!(v>a))return a;return v>b?b:v;}
static int iabs(int v){return v<0?-v:v;}
static int ri(float v){return (int)floorf(v+.5f);}
/* All text is the game's Barlow atlas. dot 1/2/3/4+ -> 12/18/28/40 px;
   scale 1/2/3 -> 18/28/40 px. y is the top of the line box. */
static int fdot(int dot){return dot<=1?GM_F12:dot==2?GM_F18:dot==3?GM_F28:GM_F40;}
static int fscale(int scale){return scale>=3?GM_F40:scale==2?GM_F28:GM_F18;}
static struct gm_tgt tg_of(struct xz_canvas c){struct gm_tgt t={c.p,c.stride,0,0,W,H};return t;}
static int text_w(const char *s,int dot){return gm_text_width(s,fdot(dot));}
static int text_h(int dot){return gm_text_height(fdot(dot));}
static void text(struct xz_canvas c,int x,int y,const char *s,int dot,uint32_t col){
    struct gm_tgt t=tg_of(c);gm_text(&t,x,y,s,fdot(dot),gm_c565(col),256u);
}
static void text_lift(struct xz_canvas c,int x,int y,const char *s,int dot,uint32_t col){
    struct gm_tgt t=tg_of(c);gm_text_lift(&t,x,y,s,fdot(dot),gm_c565(col));
}
static void text_mid(struct xz_canvas c,int cx,int y,const char *s,int dot,uint32_t col){text_lift(c,cx-text_w(s,dot)/2,y,s,dot,col);}
static void text_right(struct xz_canvas c,int rx,int y,const char *s,int dot,uint32_t col){text_lift(c,rx-text_w(s,dot),y,s,dot,col);}
static void atext(struct xz_canvas c,int x,int y,const char *s,int scale,uint32_t col){
    struct gm_tgt t=tg_of(c);gm_text_lift(&t,x,y,s,fscale(scale),gm_c565(col));
}
static void atext_mid(struct xz_canvas c,int cx,int y,const char *s,int scale,uint32_t col){
    atext(c,cx-gm_text_width(s,fscale(scale))/2,y,s,scale,col);
}
static uint16_t *px(const struct frame *f,int x,int y){return f->c.p+(size_t)y*f->c.stride+(size_t)x;}
static void vrun(const struct frame *f,int x,int y0,int y1,uint16_t v){
    if((unsigned)x>=W)return;
    y0=clampi(y0,0,H);y1=clampi(y1,0,H);
    uint16_t *p=f->c.p+(size_t)y0*f->c.stride+(size_t)x;
    for(int y=y0;y<y1;y++,p+=f->c.stride)*p=v;
}
/* Owned chrome bands are repainted solid every frame, so nothing from a
   previous phase can show through. */
static void darken_rows(const struct frame *f,int y0,int y1){
    int a=clampi(y0,0,H),b=clampi(y1,0,H);
    for(int y=a;y<b;y++){
        uint16_t v=gm_c565(xz_blend(0x0c0722,0x05030f,(unsigned)((y-a)*255/(b-a>1?b-a:1))));
        uint16_t *row=px(f,0,y);
        for(int x=0;x<W;x++)row[x]=v;
    }
}
static void ring_panel(struct xz_canvas c,int x,int y,int w,int h,int r,uint32_t border,uint32_t fill){
    xz_round_rect(c,x,y,w,h,r,15,border);
    xz_round_rect(c,x+1,y+1,w-2,h-2,r-1,15,fill);
}
static void sine_init(struct frame *f){
    if(f->sine_ready)return;
    for(int i=0;i<256;i++)f->sine[i]=(int16_t)xz_sin1024(i*360/256);
    f->sine_ready=1;
}
static int sn(const struct frame *f,int i){return f->sine[i&255];}
static int cs(const struct frame *f,int i){return f->sine[(i+64)&255];}
/* Fraction of the current beat (0 on the beat), from the waveform pulse when
   it is trusted, else a steady 120 per minute. QUIET holds it at 0. */
static float beat_phase(const struct frame *f,long *index){
    const struct xz_wave_rider_snapshot *w=&f->g->wave;
    double per=w->pulse_period>=8.f&&w->pulse_confidence>=.3f?w->pulse_period:75.0;
    double b=(f->sim-(w->pulse_period>=8.f?w->pulse_origin:0.0))/per;
    if(!isfinite(b))b=0;
    if(index)*index=f->quiet?0:(long)floor(b);
    return f->quiet?0.f:(float)(b-floor(b));
}
/* Additive dotted ring: one table lookup per dot, n<=160. */
static void dot_ring(struct frame *f,int cx,int cy,int r,int n,int phase,uint16_t c){
    if(r<2)return;
    if(n>160)n=160;
    for(int i=0;i<n;i++){
        int a=i*256/n+phase,x=cx+cs(f,a)*r/1024,y=cy-sn(f,a)*r/1024;
        if(y>=HUD&&y+1<H){gm_span_add(&f->t,x,x+2,y,c);gm_span_add(&f->t,x,x+2,y+1,c);}
    }
}

/* ---- camera ---- */
static int sx_of(const struct frame *f,double at){
    double v=(at-f->origin)*XZ_RIDER_WORLD_SCALE*f->zoom;
    if(!(v>-4000.0))v=-4000.0;
    if(v>4000.0)v=4000.0;
    return PH+(int)floor(v+.5);
}
static int sy_of(const struct frame *f,float alt){
    float v=(alt-f->cam)*f->zoom;
    if(!(v>-4000.f))v=-4000.f;
    if(v>4000.f)v=4000.f;
    return WATER-(int)floorf(v+.5f);
}
static double at_of(const struct frame *f,int x){
    return f->origin+(double)(x-PH)/(XZ_RIDER_WORLD_SCALE*f->zoom);
}

/* ---- world backdrop ---- */
/* The backdrops are untouched photos that do not tile, so instead of a
   plain wrap (a hard seam) the strip ping-pongs: every other period is
   read mirrored, so neighbouring columns always match. Same per-pixel cost
   as gm_panorama, including its ordered-dither blend into the next world. */
static void mirror_panorama(struct frame *f,const uint16_t *A,const uint16_t *B,unsigned mix16,
                            int srcw,int srch,int src_y0,int src_h,int y0,int y1,int scroll,const uint8_t *gain){
    static const unsigned char bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    int per=2*srcw,off=scroll%per;if(off<0)off+=per;
    for(int y=clampi(y0,0,H);y<clampi(y1,0,H);y++){
        int sy=clampi(src_y0+(y-y0)*src_h/(y1-y0),0,srch-1);
        unsigned k=gain[y-y0];
        const uint16_t *ra=A+(size_t)sy*(size_t)srcw,*rb=mix16?B+(size_t)sy*(size_t)srcw:ra;
        const unsigned char *bq=bayer+(y&3)*4;
        uint16_t *row=px(f,0,y);
        /* walk in straight runs between mirror turns; scale inline */
        int u=off,x=0;
        while(x<W){
            int fwd=u<srcw,sx=fwd?u:per-1-u,n=(fwd?srcw-sx:sx+1),d=fwd?1:-1;
            if(n>W-x)n=W-x;
            for(int e=x+n;x<e;x++,sx+=d){
                uint32_t v=(mix16&&(unsigned)bq[x&3]<mix16)?rb[sx]:ra[sx];
                row[x]=(uint16_t)(((v&0xf81fu)*k>>4&0xf81fu)|((v&0x07e0u)*k>>4&0x07e0u));
            }
            u=(u+n)%per;
        }
    }
}
static void backdrop(struct frame *f){
    const struct xz_rider *g=f->g;
    unsigned wi=f->world,wb=f->world_b,mix=f->wmix;
    static uint8_t gain[H];
    int y1=clampi(f->base,HUD,H);
    float lift=f->quiet?0.f:f->mu[0]*1.5f+f->pulse*.5f;
    /* the art is bright; the phoenix most of all, so it sits further back */
    static const float level[3]={10.f,10.f,7.f};
    float lv=level[wi]+(level[wb]-level[wi])*(float)mix/16.f;
    for(int y=0;y<y1-HUD;y++){int k=(int)((y<140?lv:lv-2.f)+lift);gain[y]=(uint8_t)clampi(k,0,16);}
    /* the far plate sinks a little when the camera climbs */
    int drop=ri(clampf(f->cam*f->zoom*.12f,0,60));
    int scroll=(int)(g->position*XZ_RIDER_WORLD_SCALE*f->zoom*.08);
    mirror_panorama(f,xz_rider_background[wi],xz_rider_background[wb],mix,XZ_RIDER_BG_W,XZ_RIDER_BG_H,
        0,XZ_RIDER_BG_H-drop*XZ_RIDER_BG_H/300,HUD,y1,scroll,gain);
    if(y1<H)for(int y=y1;y<H;y++){uint16_t *row=px(f,0,y);for(int x=0;x<W;x++)row[x]=C(night);}
}
/* Sky rings and a waveform line, after the ring-dancer art: rings leave the
   centre on every beat in the band colours, sized by each band's level, and
   the line spikes with the deck's real bands around the play position.
   Additive and thin, drawn behind the hills, so the route stays readable. */
static void sky(struct frame *f){
    if(!f->has_wave)return;
    const struct xz_rider *g=f->g;
    const struct xz_wave_rider_snapshot *w=&g->wave;
    int horizon=clampi(f->base,HUD+80,H);
    int cx=560,cy=HUD+(horizon-HUD)*2/5;
    float ph=beat_phase(f,NULL);
    int gap=34,spin=f->quiet?0:(int)(f->sim*.25);
    for(int k=0;k<4;k++){
        float r=(float)(18+k*gap)+ph*(float)gap;
        unsigned band=(unsigned)k%3u;
        float fade=k==3?1.f-ph:k==0?ph*1.5f:1.f;
        float a=(f->quiet?60.f:70.f+150.f*f->mu[band])*clampf(fade,0.f,1.f);
        if(a<6.f)continue;
        dot_ring(f,cx,cy,ri(r),clampi(ri(r*.9f),24,140),k&1?spin:-spin,gm_mix(0,C(f->l.band[band]),(unsigned)a));
    }
    gm_glow(&f->t,cx,cy,10+ri(10.f*f->pulse),C(0xffd6a0),(unsigned)(40.f+80.f*f->pulse));
    /* waveform line: +-120 samples of the real bands around the play head */
    float norm=(float)(w->normalization<64u?64u:w->normalization);
    uint16_t core=C(0xfff0e0),ember=C(0xff7a2a);
    for(int x=cx-240;x<=cx+240;x+=3){
        long long i=(long long)floor(g->position)+(x-cx)/2-(long long)w->first;
        if(i<0||i>=(long long)w->count)continue;
        const uint8_t *b=w->bands+(size_t)i*3;
        int d=iabs(x-cx),h=ri(((float)b[0]+b[1]+b[2])*26.f/norm*(1.f-(float)d/300.f));
        if(h<1)continue;
        unsigned a=(unsigned)clampi(170-d*120/240,40,170)*(f->quiet?1u:2u)/2u;
        gm_rect_mix(&f->t,x,cy-h,1,2*h+1,ember,a);
        if(h>3)gm_rect_mix(&f->t,x,cy-h/3,1,2*(h/3)+1,core,a);
    }
}

/* ---- hills: lit lip, shaded body, flowing veins from the real bands ---- */
static void prepare(struct frame *f){
    for(int x=0;x<W;x++)f->top[x]=-1;
    /* The course descends, so the body/water line sits under the lowest
       baseline on screen (the right edge): every hill, valleys included,
       stays above it and the course reads continuous. */
    f->base=sy_of(f,xz_rider_base_at(at_of(f,W-1))-20.f);
    if(!f->has_course)return;
    const struct xz_rider *g=f->g;
    const struct xz_wave_rider_snapshot *w=&g->wave;
    double lo=(double)w->first,hi=(double)w->first+(double)w->count-1.0;
    long long cnt=(long long)w->count;
    int bottom=f->base<H?f->base:H;
    float norm=(float)(w->normalization<64u?64u:w->normalization);
    for(int x=0;x<W;x++){
        double at=at_of(f,x);
        if(at<lo||at>hi)continue;
        struct xz_rider_surface s=xz_rider_surface_at(g,at);
        int yt=sy_of(f,s.height);
        if(yt>=bottom)continue;
        f->top[x]=(int16_t)clampi(yt,HUD+2,bottom-1);
        f->lit[x]=(uint8_t)clampi(8-(int)floorf(clampf(s.slope,-4.f,4.f)*7.f+.5f),0,15);
        f->th[x]=(uint8_t)(clampf(s.thermal,0.f,1.f)*255.f);
        long long i=(long long)floor(at)-(long long)w->first;
        unsigned sum[3]={0,0,0};
        for(long long j=i-2;j<=i+2;j++){
            if(j<0||j>=cnt)continue;
            const uint8_t *b=w->bands+(size_t)j*3;
            sum[0]+=b[0];sum[1]+=b[1];sum[2]+=b[2];
        }
        for(int k=0;k<3;k++)f->bl[k][x]=(uint8_t)clampi((int)((float)sum[k]*255.f*1.8f/(5.f*norm)),0,255);
    }
}
/* One hill material per song third, read from that third's backdrop art:
   0 liquid wave (turquoise glass, foam lip), 1 ring tunnel (black glass,
   neon contour rings parallel to the surface), 2 phoenix (molten gold lip,
   ember body, rising sparks). Only colour and decoration change; the top
   edge is always the exact physics surface. */
struct mat { uint32_t crest_lo,crest_hi,body_lo,body_hi,line_a,line_b; };
static const struct mat mats[3]={
    {0x138a9e,0xeafffb,0x041c2a,0x0d6178,0xff4fd8,0x7fffe8},
    {0xd02ca0,0xffe4f6,0x07041c,0x241050,0x3fe8ff,0xff4fd8},
    {0xe2620c,0xfff2bc,0x160400,0x6a1c04,0xffcf4a,0xff7a2a},
};
static struct mat mat_now(const struct frame *f){
    const struct mat *a=&mats[f->world],*b=&mats[f->world_b];
    unsigned k=f->wmix*255u/16u;
    return (struct mat){xz_blend(a->crest_lo,b->crest_lo,k),xz_blend(a->crest_hi,b->crest_hi,k),
        xz_blend(a->body_lo,b->body_lo,k),xz_blend(a->body_hi,b->body_hi,k),
        xz_blend(a->line_a,b->line_a,k),xz_blend(a->line_b,b->line_b,k)};
}
static void terrain(struct frame *f){
    if(!f->has_course)return;
    uint16_t crest[16],body[16][8],vein[3];
    struct mat m=mat_now(f);
    for(int i=0;i<16;i++){
        crest[i]=C(xz_blend(m.crest_lo,m.crest_hi,(unsigned)(40+i*13)));
        uint32_t topc=xz_blend(m.body_lo,m.body_hi,(unsigned)(i*16));
        for(int d=0;d<8;d++)body[i][d]=C(xz_blend(topc,0x05030f,(unsigned)(d*36)));
    }
    uint16_t la=C(m.line_a),lb=C(m.line_b);
    unsigned wsel=f->wmix>=8u?f->world_b:f->world;
    for(int k=0;k<3;k++)vein[k]=C(f->l.band[k]);
    uint32_t gc=f->fever?gold:0x9ff4ff;
    uint16_t glow=C(xz_blend(0,gc,(unsigned)((40.f+f->energy*120.f)*(f->quiet?.5f:1.f))));
    const size_t st=f->c.stride;
    int bottom=f->base<H?f->base:H;
    int flow=f->quiet?0:(int)(f->sim*.9);
    int vd[3]={ri(9.f*f->zoom)+3,ri(22.f*f->zoom)+5,ri(38.f*f->zoom)+7};
    for(int x=0;x<W;x++){
        int yt=f->top[x];
        if(yt<0)continue;
        int D=bottom-yt,L=f->lit[x];
        int nb=yt;
        if(x>0&&f->top[x-1]>nb)nb=f->top[x-1];
        if(x+1<W&&f->top[x+1]>nb)nb=f->top[x+1];
        int cl=clampi(nb-yt+3,3,16);
        if(cl>D)cl=D;
        if(yt-1>=HUD){uint16_t *q=px(f,x,yt-1);*q=gm_add(*q,glow);}
        uint16_t *p=px(f,x,yt);
        int d=0;
        for(;d<cl;d++,p+=st)*p=crest[clampi(L+2-d,0,15)];
        int ld=clampi(L-4,0,15);
        for(;d<D;d++,p+=st){int k=d*8/(D>64?D:64);*p=body[ld][k>7?7:k];}
        /* veins follow the surface at fixed depths and flow with the course */
        double atx=at_of(f,x);
        int wx=(int)(atx*1.6);
        for(int k=0;k<3;k++){
            unsigned v=f->bl[k][x];
            if(v<18u)continue;
            int y=yt+vd[k]+sn(f,wx+k*70+flow)*(2+k)/1024;
            if(y<=yt+cl||y>=bottom-1)continue;
            uint16_t c=gm_mix(0,vein[k],v);
            uint16_t *q=px(f,x,y);*q=gm_add(*q,c);q+=st;*q=gm_add(*q,gm_mix(0,c,140));
        }
        long long ai=(long long)floor(atx);
        uint32_t h=gm_hash((uint32_t)ai*2654435761u+wsel);
        if(wsel==1u){
            /* tunnel: neon rings echo the surface at widening depths */
            static const int ring[4]={7,17,31,50};
            for(int k=0;k<4;k++){
                int y=yt+cl+ri((float)ring[k]*f->zoom);
                if(y>=bottom)break;
                uint16_t *q=px(f,x,y);*q=gm_add(*q,gm_mix(0,k&1?lb:la,(unsigned)(150-k*30)));
            }
        }else{
            /* slow reflective band in the body */
            int ry=yt+D*2/5+sn(f,wx/2+flow/2)*5/1024;
            if(ry>yt+cl&&ry<bottom){uint16_t *q=px(f,x,ry);*q=gm_add(*q,gm_mix(0,la,60));}
        }
        if(wsel==0u&&(h&7u)==0u){
            /* liquid: foam spray beads lift off the lip, pinned to the world */
            int y=yt-2-(int)((h>>4)%5u);
            if(y>=HUD){uint16_t *q=px(f,x,y);*q=gm_add(*q,(h>>8)&1u?C(0xb0fff0):gm_mix(0,la,200));}
        }else if(wsel==2u&&(h&15u)==0u){
            /* molten: embers rise from the lip and fade */
            int life=(int)(((h>>4)&63u)+(unsigned)(f->quiet?0:(int)(f->sim*.7)))&63;
            int y=yt-3-life/2;
            if(y>=HUD)gm_rect_mix(&f->t,x,y,1,2,(h>>12)&1u?la:lb,(unsigned)(230-life*3));
        }
    }
}
static void water(struct frame *f){
    int b=f->base;
    if(b>=H||b<HUD)return;
    static const int wobble[16]={0,1,1,2,1,1,0,-1,-1,-2,-1,-1,0,0,1,0};
    int t=f->quiet?0:(int)(f->sim/4.0);
    for(int d=0;d<H-b;d++){
        int y=b+d,sy=b-1-d;
        if(sy<HUD)break;
        int shift=f->quiet?0:wobble[(d+t)&15]*(1+d/56);
        const uint16_t *src=px(f,0,sy);
        uint16_t *dst=px(f,0,y);
        unsigned k=d<24?7u:d<70?5u:3u;
        for(int x=0;x<W;x++)dst[x]=gm_scale(src[clampi(x+shift,0,W-1)],k);
    }
    uint16_t line=C(0x6a5cc0);
    uint16_t *row=px(f,0,b);
    for(int x=0;x<W;x++)if(x%5!=4)row[x]=line;
}
static void chevron(struct frame *f,int x,int y,uint16_t col,unsigned a){
    gm_line(&f->t,x-4,y+3,x,y,1,col,a);gm_line(&f->t,x,y,x+4,y+3,1,col,a);
}
static void thermals(struct frame *f){
    if(!f->has_course)return;
    const double step=12.0;
    double a=floor(at_of(f,0)/step)*step;
    int rise=f->quiet?0:(int)(f->sim*.6);
    uint16_t col=C(xz_blend(f->l.band[1],0xffffff,110));
    for(int n=0;n<200;n++,a+=step){
        int x=sx_of(f,a);
        if(x>=W)break;
        if(x<0||f->top[x]<0||f->th[x]<64)continue;
        long long ai=(long long)floor(a/step);
        int ph=(int)(((ai*13+rise)%36+36)%36);
        for(int k=0;k<2;k++){
            int y=f->top[x]-14-ph-k*18;
            if(y<HUD+4)continue;
            chevron(f,x,y,col,(unsigned)(f->th[x]*(36-ph)/36/(k+1)));
        }
    }
}
static void pulse_ticks(struct frame *f){
    const struct xz_wave_rider_snapshot *w=&f->g->wave;
    if(!f->has_course||f->base>=H||!(w->pulse_period>=8.f)||!(w->pulse_confidence>=.3f))return;
    double per=w->pulse_period,a=w->pulse_origin+ceil((at_of(f,0)-w->pulse_origin)/per)*per;
    uint16_t col=C(xz_blend(night,f->l.band[0],(unsigned)(90.f+clampf(w->pulse_confidence,0.f,1.f)*120.f)));
    for(int n=0;n<200;n++,a+=per){
        int x=sx_of(f,a);
        if(x>=W)break;
        gm_rect(&f->t,x-1,f->base+2,3,2,col);
    }
}

/* ---- pickups: three readable shapes, shattered by the real SHATTER event ----
   Shard (diamond, band colour): beads ride just above the surface, larger
   shards hang in jump arcs. Aurora (mint concentric rings with a waveform
   tick line): soft-touch power. Comet (gold phoenix chevrons with an ember
   tail): launch power. Outlines and low fills keep the hills behind visible. */
static const struct xz_game_impact *pickup_hit(const struct frame *f,double at,float alt){
    const struct xz_game_impact *best=NULL;
    for(int j=0;j<f->players;j++){
        const struct xz_game_effects *fx=&f->g->player[j].effects;
        for(unsigned i=0;i<XZ_GAME_IMPACTS;i++){
            const struct xz_game_impact *e=&fx->events[i];
            if(!e->serial||e->kind!=XZ_IMPACT_SHATTER||fabs(e->at-at)>.6||fabsf(e->altitude-alt)>1.f)continue;
            if(!best||e->time>best->time)best=e;
        }
    }
    return best;
}
static void shard(struct frame *f,int x,int y,int r,uint16_t b,uint16_t hi,unsigned a,int lit,int k){
    int w=r*k/8,xs[4]={x,x+w,x,x-w},ys[4]={y-r-2,y,y+r+1,y};
    if(lit)gm_glow(&f->t,x,y,r*2,b,(unsigned)(40.f+60.f*f->pulse));
    gm_poly(&f->t,xs,ys,4,gm_mix(C(night),b,120),a*3/4);
    for(int i=0;i<4;i++)gm_line(&f->t,xs[i],ys[i],xs[(i+1)&3],ys[(i+1)&3],1,i<2?hi:b,a);
    if(lit){
        gm_rect(&f->t,x-1,y-r/2-1,2,2,C(0xffffff));
        /* data-bar glints trail behind, after the glitch-cat streaks */
        gm_span_add(&f->t,x-w-9,x-w-3,y-2,gm_mix(0,C(f->l.band[0]),200));
        gm_span_add(&f->t,x-w-6,x-w-2,y+2,gm_mix(0,C(f->l.band[1]),200));
    }
}
static void aurora_orb(struct frame *f,int x,int y,int r,unsigned a,int lit,int spin){
    uint16_t m=C(aurora_rgb);
    if(lit)gm_glow(&f->t,x,y,r*2+2,m,(unsigned)(50.f+70.f*f->pulse));
    dot_ring(f,x,y,r+6,32,spin,gm_mix(0,m,a));
    dot_ring(f,x,y,r+1,22,-spin,gm_mix(0,C(0xe8fff4),a*3/4));
    for(int i=-3;i<=3;i++){
        int h=lit?1+((3-iabs(i))*(2+ri(4.f*f->mu[(unsigned)(i+3)%3u])))/3:1;
        gm_rect_mix(&f->t,x+i*3,y-h,1,2*h+1,i?m:C(0xffffff),a);
    }
}
static void comet_mark(struct frame *f,int x,int y,int r,unsigned a,int lit){
    uint16_t gd=C(gold),em=C(comet_rgb);
    if(lit)gm_glow(&f->t,x,y,r*2+2,em,(unsigned)(50.f+70.f*f->pulse));
    for(int i=0;i<2;i++){
        int o=i*5,s=r+2-i*2;
        gm_line(&f->t,x-s+o,y-s,x+o+2,y,2,i?em:gd,a);
        gm_line(&f->t,x-s+o,y+s,x+o+2,y,2,i?em:gd,a);
    }
    for(int i=-1;i<=1;i++)gm_line(&f->t,x-r-4,y+i*3,x-r-12-(1-iabs(i))*6,y+i*3,1,em,a*2/3);
    if(lit)gm_rect(&f->t,x+1,y-1,2,2,C(0xffffff));
}
static void gems(struct frame *f){
    if(!f->has_course)return;
    const struct xz_rider *g=f->g;
    const struct xz_wave_rider_snapshot *w=&g->wave;
    unsigned n=g->course.object_count<XZ_RIDER_OBJECTS?g->course.object_count:XZ_RIDER_OBJECTS;
    double behind=g->player[0].highwater;
    for(int j=1;j<f->players;j++)if(g->player[j].highwater<behind)behind=g->player[j].highwater;
    int spin=f->quiet?0:(int)(f->sim*1.5);
    float zr=clampf(f->zoom,.6f,1.f);
    uint16_t b=C(f->l.band[2]),hi=C(xz_blend(f->l.band[2],0xffffff,150));
    for(unsigned i=0;i<n;i++){
        const struct xz_rider_object *o=&g->course.objects[i];
        int x=sx_of(f,(double)o->at);
        if(x<-40||x>W+40)continue;
        float altitude=o->altitude+xz_rider_base_at(o->at);
        int y=sy_of(f,altitude);
        if(y<HUD+6||y>H-4)continue;
        long long ci=(long long)o->at-(long long)w->first;
        int bead=o->kind==XZ_RIDER_PICKUP_GEM&&ci>=0&&ci<(long long)w->count&&o->altitude-g->course.height[ci]<24.f;
        int r=ri((bead?5.f:o->kind==XZ_RIDER_PICKUP_GEM?8.f:9.f)*zr)+2;
        const struct xz_game_impact *hit=pickup_hit(f,(double)o->at,altitude);
        if(hit){
            float age=(float)((f->sim-hit->time)/(HZ*.5));
            if(age>=0&&age<1){
                uint16_t lit=o->kind==XZ_RIDER_PICKUP_AURORA?C(0xd8fff0):o->kind==XZ_RIDER_PICKUP_COMET?C(0xfff0b0):hi;
                uint16_t dark=o->kind==XZ_RIDER_PICKUP_AURORA?C(aurora_rgb):o->kind==XZ_RIDER_PICKUP_COMET?C(comet_rgb):b;
                float dx=cosf(hit->angle),dy=-sinf(hit->angle);
                int ox[4]={0,r,0,-r},oy[4]={-r-3,0,r+2,0};
                gm_shatter(&f->t,x,y,ox,oy,4,ri(-dx*(float)r),ri(-dy*(float)r),age,dx,dy,hit->strength,lit,gm_mix(C(night),dark,170),lit,C(0xffffff),hit->serial*3u+i);
            }
            continue;
        }
        int passed=(double)o->at<=behind,lit=!passed;
        unsigned a=passed?90u:256u;
        if(o->kind==XZ_RIDER_PICKUP_AURORA)aurora_orb(f,x,y,r,a,lit,spin+(int)i*9);
        else if(o->kind==XZ_RIDER_PICKUP_COMET)comet_mark(f,x,y,r,a,lit);
        else shard(f,x,y,r,b,hi,a,lit&&!bead,f->quiet?8:8-iabs(((spin+(int)i*3)&15)-8)/2);
    }
}

/* ---- the familiar on its glass surfcraft ---- */
struct pose { unsigned a,b; float blend,sq,flex; };
static const struct xz_game_impact *latest(const struct xz_rider_player *p,unsigned k1,unsigned k2){
    const struct xz_game_impact *best=NULL;
    for(unsigned i=0;i<XZ_GAME_IMPACTS;i++){
        const struct xz_game_impact *e=&p->effects.events[i];
        if(!e->serial||(e->kind!=k1&&e->kind!=k2))continue;
        if(!best||e->serial>best->serial)best=e;
    }
    return best;
}
static struct pose rider_pose(const struct frame *f,const struct xz_rider_player *p){
    struct pose r={0,0,0,1,0};
    const struct xz_game_impact *ln=latest(p,XZ_IMPACT_LAND,XZ_IMPACT_PERFECT_LAND);
    const struct xz_game_impact *lc=latest(p,XZ_IMPACT_LAUNCH,XZ_IMPACT_BOOST);
    if(p->grounded){
        double age=ln?(f->sim-ln->time)/HZ:99.0;
        if(p->contact_bad&&age>=0&&age<.7){
            /* a wrong angle: the squash holds flat, then recovers slowly */
            float u=(float)(age/.7),k=age<.22?1.f:gm_spring((u-.31f)/.69f,7.f,4.f);
            r.sq=1.f-.46f*k;r.flex=1.f;r.a=6;
            if(age>=.45){r.b=0;r.blend=(float)((age-.45)/.25);}
            return r;
        }
        if(age>=0&&age<.5){
            float u=(float)(age/.5),A=.12f+.22f*clampf(p->contact_impact*1.5f,0,1);
            float k=gm_spring(u,11.f,5.f);
            if(ln->kind==XZ_IMPACT_PERFECT_LAND){
                /* perfect: a hard snap down, held a moment, then a springy
                   rebound that overshoots into a stretch */
                A=.42f;k=age<.05?1.f:gm_spring((u-.1f)/.9f,13.f,4.2f);
            }
            r.sq=1.f-A*k;r.flex=clampf(k,0,1);
            /* a clean landing pops straight up into the paws-up pose */
            if(age<.08){r.a=6;}
            else if(age<.22){r.a=6;r.b=7;r.blend=(float)((age-.08)/.14);}
            else{r.a=7;r.b=0;r.blend=(float)((age-.22)/.28);}
            return r;
        }
        float dn=clampf(p->downforce,0,1);
        long beat;float ph=beat_phase(f,&beat);
        if(!f->quiet&&dn<.35f){
            /* dance: squash on every beat, paws up on alternate beats */
            float kb=(1.f-ph)*(1.f-ph);
            r.a=(beat&1)?7:0;r.b=(beat&1)?0:7;r.blend=clampf((ph-.55f)/.45f,0,1);
            r.sq=1.f-(.08f+.06f*f->mu[0])*kb;r.flex=.4f*kb;
            return r;
        }
        r.a=0;r.b=1;r.blend=dn;r.sq=1.f-.12f*dn;r.flex=.6f*dn;
        return r;
    }
    double la=lc?(f->sim-lc->time)/HZ:99.0;
    if(la>=0&&la<.16){r.a=2;r.b=3;r.blend=(float)(la/.16);r.sq=1.2f-.15f*(float)(la/.16);return r;}
    if(fabsf(p->air_rotation)>.6f||fabsf(p->spin_velocity)>4.f){r.a=3;r.sq=.92f;return r;}
    if(p->tuck||(p->vy<-60.f&&p->pitch<-.25f)){r.a=5;r.sq=1.f+.1f*clampf(-p->vy/400.f,0,1);return r;}
    if(p->vy>80.f){r.a=3;r.b=4;r.blend=clampf((140.f-p->vy)/60.f,0,1);return r;}
    r.a=4;r.b=p->vy<0?5:3;r.blend=clampf((fabsf(p->vy)-40.f)/60.f,0,1);
    return r;
}
static const float hull_x[6]={31.f,15.f,-22.f,-29.f,-17.f,19.f},hull_y[6]={1.f,4.5f,4.f,1.f,-4.f,-3.f};
/* Board centre (x,y), physical pitch, scale k. */
static void surfcraft(struct frame *f,int x,int y,float pitch,float k,uint32_t col,struct pose ps,float lift,float down,int grounded,float vx,float vy){
    if(!(pitch>-7.f&&pitch<7.f))pitch=0.f;
    float a=-pitch,S=sinf(a),Cc=cosf(a);
    const float *hx=hull_x,*hy=hull_y;
    int X[6],Y[6];
    for(int i=0;i<6;i++){
        float lx=hx[i]*k,ly=(hy[i]-(fabsf(hx[i])<20.f?ps.flex*3.5f:0.f))*k;
        X[i]=x+ri(Cc*lx+S*ly);Y[i]=y+ri(S*lx-Cc*ly);
    }
    struct gm_tgt *t=&f->t;
    uint16_t pc=C(col);
    if(grounded&&down>.12f){
        /* pressure under the keel, along the surface normal */
        for(int i=-1;i<=1;i++){
            float lx=(float)(i*12)*k;
            int bx=x+ri(Cc*lx+S*(-5.f*k)),by=y+ri(S*lx+Cc*5.f*k);
            int len=ri((3.f+8.f*down)*k);
            gm_line(t,bx,by,bx-ri(S*(float)len),by+ri(Cc*(float)len),1,pc,(unsigned)(90.f+150.f*down));
        }
    }
    if(!grounded&&lift>.12f){
        float m=sqrtf(vx*vx+vy*vy);
        float dx=m>1.f?-vx/m:-1.f,dy=m>1.f?vy/m:0.f;
        int len=ri((18.f+46.f*lift)*k);
        for(int i=0;i<2;i++){
            int sx0=i?X[3]:X[0],sy0=i?Y[3]:Y[0];
            gm_line(t,sx0,sy0,sx0+ri(dx*(float)len),sy0+ri(dy*(float)len),1,C(xz_blend(col,0xffffff,120)),(unsigned)(70.f+150.f*lift));
        }
    }
    gm_poly(t,X,Y,6,C(glass),118);
    gm_line(t,X[0],Y[0],X[1],Y[1],1,C(0xffffff),230);
    gm_line(t,X[1],Y[1],X[2],Y[2],1,C(0xe8f8ff),190);
    gm_line(t,X[3],Y[3],X[4],Y[4],2,pc,256);
    gm_line(t,X[4],Y[4],X[5],Y[5],2,pc,256);
    gm_line(t,X[5],Y[5],X[0],Y[0],1,pc,256);
    int fx=x+ri(Cc*(-20.f*k)+S*(-4.f*k)),fy=y+ri(S*(-20.f*k)-Cc*(-4.f*k));
    gm_line(t,fx,fy,fx-ri(S*5.f*k)-ri(Cc*4.f*k),fy+ri(Cc*5.f*k)-ri(S*4.f*k),2,pc,220);
    /* the rider stands on the deck, feet planted, rotated with the craft */
    struct gm_cat c;
    float fl=-1.f*k,fh=4.5f*k;
    c.x=x+ri(Cc*fl+S*fh);c.y=y+ri(S*fl-Cc*fh);
    c.scale=.62f*k;c.sq=ps.sq;c.ang=a;c.pose=ps.a;c.pose_b=ps.b;c.blend=ps.blend;c.tint=0;c.tinta=0;
    gm_draw_cat(t,&c);
}
static float rider_k(const struct frame *f){return clampf(f->zoom*1.15f,.72f,1.15f);}
static void place_riders(struct frame *f){
    const struct xz_rider *g=f->g;
    for(int i=0;i<f->players;i++){
        const struct xz_rider_player *p=&g->player[i];
        int y=sy_of(f,p->altitude-5.f);
        f->s[i].x=sx_of(f,p->at);
        f->s[i].lift=y<HUD+30;
        f->s[i].y=clampi(y,HUD+30,H-8);
        f->s[i].on=f->s[i].x>-60&&f->s[i].x<W+60;
    }
}
static void trail(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    unsigned n=p->trail_count<XZ_RIDER_TRAIL?p->trail_count:XZ_RIDER_TRAIL;
    if(!n||p->grounded)return;
    int fever=p->fever_until>f->sim&&!f->quiet;
    int lx=f->s[who].x,ly=f->s[who].y;
    unsigned head=p->trail_head%XZ_RIDER_TRAIL;
    for(unsigned j=0;j<n&&j<24u;j++){
        unsigned k=(head+XZ_RIDER_TRAIL-1u-j)%XZ_RIDER_TRAIL;
        int x=sx_of(f,p->trail_at[k]),y=sy_of(f,p->trail_alt[k]-5.f);
        if(x>lx+4){lx=x;ly=y;continue;}
        uint32_t c=fever?f->l.band[(j/4)%3]:rider_rgb[who];
        gm_line(&f->t,lx,ly,x,y,j<6?2:1,C(c),(unsigned)(200-j*8));
        lx=x;ly=y;
        if(x<-20)break;
    }
}
static void power_fx(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    int x=f->s[who].x,y=f->s[who].y-ri(16.f*rider_k(f));
    if(p->power==XZ_RIDER_POWER_AURORA){
        int spin=f->quiet?0:(int)(f->sim*2.0);
        uint16_t c=C(xz_blend(aurora_rgb,rider_rgb[who],70));
        for(int i=0;i<24;i++){int a=i*256/24+spin;gm_rect(&f->t,x+cs(f,a)*30/1024,y-sn(f,a)*30/1024,2,2,c);}
    }else if(p->power==XZ_RIDER_POWER_COMET||p->comet_until>f->sim){
        float vx=p->vx,vy=-p->vy,m=sqrtf(vx*vx+vy*vy),dx=-1.f,dy=0.f;
        if(m>1.f){dx=-vx/m;dy=-vy/m;}
        for(int i=0;i<5;i++){
            int len=24+(4-iabs(i-2))*8;
            float o=(float)((i-2)*3);
            int x0=x+(int)(-dy*o),y0=y+(int)(dx*o);
            gm_line(&f->t,x0,y0,x0+(int)(dx*(float)len),y0+(int)(dy*(float)len),i==2?2:1,C(i==2?0xffffff:comet_rgb),220);
        }
    }
}
static int flick_armed(const struct xz_rider *g,const struct xz_rider_player *p){
    return g->spin_lift&&p->flick_until>g->sim_at&&p->flick_strength>0.f;
}
static void spin_fx(struct frame *f,int who){
    const struct xz_rider *g=f->g;
    const struct xz_rider_player *p=&g->player[who];
    const struct spot *s=&f->s[who];
    if(!g->spin_lift||!s->on||!flick_armed(g,p))return;
    int n=1+(int)(clampf(p->flick_strength,0.f,1.f)*2.f+.5f);
    for(int i=0;i<n;i++){
        int cy=s->y-30-i*6;
        if(cy>=HUD+4)chevron(f,s->x+34,cy,C(gold),(unsigned)(150+i*35));
    }
}

/* ---- flight expression: every mark reads a real player field ---- */
static int board_mid_y(const struct frame *f,int who){return f->s[who].y-ri(14.f*rider_k(f));}
/* Progress toward the next full flip: a gold arc from the top, turning the
   way the craft turns, with one faint ring per flip already pending. */
static void spin_arc(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    if(p->grounded||!s->on||(!p->air_tricks&&fabsf(p->air_rotation)<.35f))return;
    int cx=s->x,cy=board_mid_y(f,who),R=ri(40.f*rider_k(f));
    for(unsigned k=0;k<p->air_tricks&&k<3u;k++)dot_ring(f,cx,cy,R+4+(int)k*4,48,0,gm_mix(0,C(gold),90));
    float u=clampf(fabsf(p->air_rotation)/(2.f*3.14159265f),0,1);
    int n=ri(u*64.f),dir=p->air_rotation>=0?1:-1;
    uint16_t c=C(gold),w=C(0xffffff);
    for(int i=0;i<n;i++){
        int a=64+dir*i*4,x=cx+cs(f,a)*R/1024,y=cy-sn(f,a)*R/1024;
        gm_rect(&f->t,x-1,y-1,i>=n-2?3:2,i>=n-2?3:2,i>=n-2?w:c);
    }
}
/* Rainbow data bars behind the rider while a flip boost or comet runs,
   after the glitch-cat streaks. Deterministic per source sample. */
static void boost_bars(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    int air=p->air_boost_until>f->sim,land=p->boost_until>f->sim&&p->landing_boost>0.f;
    if(!s->on||(!air&&!land))return;
    double until=air?p->air_boost_until:p->boost_until;
    float u=clampf((float)((until-f->sim)/60.0),0,1);
    uint32_t seed=(uint32_t)(f->quiet?0:(long)(f->sim/3.0));
    int cy=board_mid_y(f,who);
    for(int i=0;i<9;i++){
        uint32_t h=gm_hash(seed*31u+(uint32_t)i*977u+(uint32_t)who);
        int y=cy-18+i*4+(int)(h%3u),x1=s->x-28-(int)((h>>4)%14u),len=(int)(24.f+60.f*u)+(int)((h>>8)%30u);
        gm_span_add(&f->t,x1-len,x1,y,gm_mix(0,C(f->l.band[i%3]),(unsigned)(90.f+150.f*u)));
    }
}
/* Clean landing with a real speed gain: gold chevrons stream forward along
   the surface ahead, count and reach from the measured landing_boost. */
static void landing_chevrons(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    if(!s->on||!p->grounded||p->contact_bad||!(p->boost_until>f->sim)||!(p->landing_boost>4.f))return;
    float u=1.f-clampf((float)((p->boost_until-f->sim)/75.0),0,1);
    float sl=xz_rider_surface_at(f->g,p->at).slope,m=sqrtf(1.f+sl*sl),tx=1.f/m,ty=-sl/m;
    int n=clampi(1+(int)(p->landing_boost/45.f),2,5);
    for(int i=0;i<n;i++){
        float d=(28.f+(float)i*16.f+70.f*u)*rider_k(f);
        int x=s->x+ri(tx*d),y=s->y-6+ri(ty*d);
        float e=9.f,ax=(float)x-e*tx,ay=(float)y-e*ty;
        unsigned a=(unsigned)(240.f*(1.f-u)*(1.f-(float)i*.12f));
        gm_line(&f->t,ri(ax-e*ty),ri(ay+e*tx),x,y,2,C(gold),a);
        gm_line(&f->t,ri(ax+e*ty),ri(ay-e*tx),x,y,2,C(gold),a);
    }
}
/* Wrong landing angle (contact_bad): the frame tears around the rider in
   offset red and cyan bars, like the glitch art, then settles. */
static void crash_tear(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    double age=(f->sim-p->contact_time)/HZ;
    if(!s->on||!p->contact_bad||!p->grounded||age<0||age>=.45)return;
    float u=(float)(age/.45);
    int cy=board_mid_y(f,who);
    for(int i=0;i<7;i++){
        uint32_t h=gm_hash((uint32_t)i*2654435761u+(uint32_t)(p->contact_time*4.0));
        int y=cy-26+(int)(h%44u),off=(int)((h>>8)%22u)-11,len=18+(int)((h>>16)%40u);
        unsigned a=(unsigned)(230.f*(1.f-u));
        gm_span_add(&f->t,s->x+off-len/2,s->x+off+len/2,y,gm_mix(0,C(danger),a));
        gm_span_add(&f->t,s->x-off-len/2+4,s->x-off+len/2+4,y+1,gm_mix(0,C(accent),a/2));
    }
}
/* The latest crest flown over this flight gets a gold flag on the hill. */
static void crest_mark(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    if(p->grounded||!p->air_crests)return;
    int x=sx_of(f,p->last_air_crest);
    if(x<2||x>=W-2||f->top[x]<0)return;
    int y=f->top[x]-6;
    chevron(f,x,y,C(gold),240);chevron(f,x,y-5,C(gold),150);
}

/* ---- contact effects, anchored to the world point of each real event ---- */
static void impacts(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    struct gm_tgt *t=&f->t;
    uint16_t pc=C(rider_rgb[who]);
    float z=f->zoom;
    for(unsigned i=0;i<XZ_GAME_IMPACTS;i++){
        const struct xz_game_impact *e=&p->effects.events[i];
        if(!e->serial)continue;
        double age=(f->sim-e->time)/HZ;
        if(age<0||age>1.0)continue;
        int x=sx_of(f,e->at),y=sy_of(f,e->altitude);
        if(x<-100||x>W+100)continue;
        float st=clampf(e->strength,0,1),slope=-tanf(clampf(e->angle,-1.3f,1.3f));
        switch(e->kind){
        case XZ_IMPACT_LAND:{
            if(age>=.6)break;
            float u=(float)(age/.6);
            gm_ripple(t,x,y,slope,u,ri((3.f+7.f*st)*z)+1,ri((50.f+70.f*st)*z),C(0xb8b0ff),220);
            if(st>.45f&&u<.6f){ /* rough: scrape sparks skid back along the hill */
                float v=u/.6f;
                for(int k=0;k<7;k++){
                    int d=ri((8.f+(40.f+(float)k*8.f)*v)*z);
                    int sx=x-d,sy=y+ri(slope*(float)(-d))-ri((4.f+(float)(k%3)*5.f)*(1.f-v)*z);
                    gm_rect_mix(t,sx,sy,2,1,C(0xffa860),(unsigned)(240.f*(1.f-v)));
                }
            }
            break;}
        case XZ_IMPACT_PERFECT_LAND:{
            if(age>=.7)break;
            float u=(float)(age/.7);
            gm_ripple(t,x,y,slope,u,ri((4.f+6.f*st)*z)+1,ri((70.f+60.f*st)*z),C(gold),240);
            if(u<.45f){
                float v=u/.45f,nx=sinf(e->angle)*-1.f,ny=-cosf(e->angle);
                for(int k=-2;k<=2;k++){
                    float tx=cosf(e->angle),ty=-sinf(e->angle);
                    int bx=x+ri(tx*(float)(k*8)*z),by=y+ri(ty*(float)(k*8)*z);
                    int d0=ri((4.f+50.f*v)*z),d1=d0+ri((8.f+(float)(2-iabs(k))*7.f)*z);
                    gm_line(t,bx+ri(nx*(float)d0),by+ri(ny*(float)d0),bx+ri(nx*(float)d1),by+ri(ny*(float)d1),1,C(gold),(unsigned)(240.f*(1.f-v)));
                }
                gm_glow(t,x,y-2,ri(14.f*z)+4,C(gold),(unsigned)(150.f*(1.f-v)));
            }
            break;}
        case XZ_IMPACT_LAUNCH:
        case XZ_IMPACT_BOOST:{
            if(age>=.35)break;
            float u=(float)(age/.35);
            uint16_t col=e->kind==XZ_IMPACT_BOOST?C(gold):pc;
            float a=-e->angle,dx=-cosf(a),dy=-sinf(a);
            for(int k=0;k<4;k++){
                float o=(float)(k-1)*3.f*z;
                int x0=x+ri(-dy*o),y0=y+ri(dx*o)-2,len=ri(((12.f+30.f*st)*(1.f-u*.4f)+(float)k*4.f)*z);
                gm_line(t,x0,y0,x0+ri(dx*(float)len),y0+ri(dy*(float)len),1,col,(unsigned)(230.f*(1.f-u)));
            }
            for(int k=0;k<6;k++){
                int d=ri((5.f+40.f*u+(float)k*4.f)*z),sx=x-d+ri((float)(k%2)*6.f*z);
                gm_rect_mix(t,sx,y+ri(slope*(float)(-d))-ri(10.f*u*(1.f-u)*(float)(1+k%3)*z),2,2,C(0xdff8ff),(unsigned)(200.f*(1.f-u)));
            }
            break;}
        default:break; /* SHATTER is drawn by its gem */
        }
    }
}
/* ---- board-attached hardware and speed expression ----
   Board-local frame: lx forward along the deck, ly up from the keel, the
   same transform surfcraft() uses, so anything bolted on turns with the
   real pitch. */
struct basis { int x,y; float S,Cc,k; };
static struct basis basis_of(int x,int y,float pitch,float k){
    if(!(pitch>-7.f&&pitch<7.f))pitch=0.f;
    struct basis b={x,y,sinf(-pitch),cosf(-pitch),k};
    return b;
}
static void bp(const struct basis *b,float lx,float ly,int *X,int *Y){
    lx*=b->k;ly*=b->k;
    *X=b->x+ri(b->Cc*lx+b->S*ly);*Y=b->y+ri(b->S*lx-b->Cc*ly);
}
static void hull_ghost(struct frame *f,int x,int y,float pitch,float k,uint16_t c,unsigned a){
    struct basis b=basis_of(x,y,pitch,k);
    int X[6],Y[6];
    for(int i=0;i<6;i++)bp(&b,hull_x[i],hull_y[i],&X[i],&Y[i]);
    for(int i=0;i<6;i++)gm_line(&f->t,X[i],Y[i],X[(i+1)%6],Y[(i+1)%6],1,c,a);
}
/* Screen direction opposite the real velocity; board tail if near still. */
static void back_dir(const struct xz_rider_player *p,const struct basis *b,float *dx,float *dy){
    float vx=p->vx,vy=-p->vy,m=sqrtf(vx*vx+vy*vy);
    if(m>1.f){*dx=-vx/m;*dy=-vy/m;}else{*dx=-b->Cc;*dy=-b->S;}
}
/* The engine sets rocket_until = ignition + 900 samples (6 s), and a
   further perfect re-lights it, so age is time since the latest light. */
#define ROCKET_SAMPLES 900.0
static int rocket_on(const struct frame *f,const struct xz_rider_player *p){return p->rocket_until>f->sim;}
static double rocket_age(const struct frame *f,const struct xz_rider_player *p){return (f->sim-(p->rocket_until-ROCKET_SAMPLES))/HZ;}
/* Exhaust out of the tail nozzle, aligned against the real velocity: a
   layered neon-to-white-hot plume, flickering, longer with speed, kicked
   long at ignition and sputtering in the last second. Puffs and two glitch
   bars trail off the tip. No glow around the rider: the rocket reads as
   thrust, not protection. */
static void rocket_plume(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    if(!s->on||!rocket_on(f,p))return;
    float k=rider_k(f);
    struct basis b=basis_of(s->x,s->y,p->pitch,k);
    int nx,ny;bp(&b,-31.f,7.f,&nx,&ny);
    float dx,dy;back_dir(p,&b,&dx,&dy);
    float qx=-dy,qy=dx,m=sqrtf(p->vx*p->vx+p->vy*p->vy);
    double age=rocket_age(f,p),left=(p->rocket_until-f->sim)/HZ;
    float ign=age>=0&&age<.45?1.f-(float)(age/.45):0.f;
    uint32_t h=gm_hash((uint32_t)(f->quiet?0L:(long)(f->sim/2.0))*2246822519u+(uint32_t)who*97u);
    float flick=f->quiet?1.f:.84f+.32f*(float)(h&255u)/255.f;
    float L=(36.f+22.f*clampf(m/700.f,0,1))*k*flick*(1.f+1.3f*ign);
    int sputter=left<1.0&&!f->quiet&&((long)(f->sim/5.0)&1);
    if(sputter)L*=.4f;
    static const float len[4]={1.f,.74f,.5f,.26f},wid[4]={6.5f,4.8f,3.2f,1.6f};
    static const uint32_t col[4]={0xff4fd8,0xff7a2a,0xffcf4a,0xffffff};
    static const unsigned al[4]={150,215,240,256};
    for(int l=0;l<4;l++){
        float ln=L*len[l],w=wid[l]*k*(1.f+.5f*ign);
        int X[5],Y[5];
        X[0]=nx+ri(qx*w);Y[0]=ny+ri(qy*w);
        X[1]=nx+ri(dx*ln*.3f+qx*w*1.15f);Y[1]=ny+ri(dy*ln*.3f+qy*w*1.15f);
        X[2]=nx+ri(dx*ln);Y[2]=ny+ri(dy*ln);
        X[3]=nx+ri(dx*ln*.3f-qx*w*1.15f);Y[3]=ny+ri(dy*ln*.3f-qy*w*1.15f);
        X[4]=nx-ri(qx*w);Y[4]=ny-ri(qy*w);
        gm_poly(&f->t,X,Y,5,C(col[l]),al[l]);
    }
    if(!sputter)for(int i=1;i<=2;i++){ /* shock diamonds in the core */
        int cx=nx+ri(dx*L*.13f*(float)i),cy=ny+ri(dy*L*.13f*(float)i);
        gm_rect(&f->t,cx-1,cy-1,2,2,C(0xffffff));
    }
    int tx=nx+ri(dx*L),ty=ny+ri(dy*L);
    for(int i=0;i<6;i++){
        uint32_t r=gm_hash(h+(uint32_t)i*911u);
        float d=(6.f+(float)i*7.f)*k,o=(float)((int)(r%9u)-4)*k;
        int x=tx+ri(dx*d+qx*o),y=ty+ri(dy*d+qy*o);
        gm_rect_mix(&f->t,x,y,i<3?3:2,2,C(i<2?gold:i<4?0xff4fd8:0x8a5cff),(unsigned)(210-i*30));
    }
    for(int i=0;i<2;i++){
        uint32_t r=gm_hash(h*3u+(uint32_t)i);
        int y=ty-4+(int)(r%9u),x1=tx-(int)((r>>8)%10u);
        gm_span_add(&f->t,x1-14-(int)((r>>12)%18u),x1,y,gm_mix(0,C(i?accent:0xff4fd8),170));
    }
    if(ign>0.f)gm_glow(&f->t,nx,ny,ri((3.f+5.f*ign)*k),C(0xffffff),(unsigned)(230.f*ign));
}
/* The pack itself: a compact nozzle bolted to the tail deck. A gold strip
   on the housing burns down with the remaining rocket time. Before the
   rocket, the same mount shows the perfect chain as three tail pips. */
static void rocket_mount(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    int on=rocket_on(f,p);
    if(!s->on||(!on&&!p->perfect_chain))return;
    struct basis b=basis_of(s->x,s->y,p->pitch,rider_k(f));
    int X[5],Y[5];
    if(!on){
        for(int i=0;i<3;i++){
            bp(&b,-24.f+(float)i*4.5f,5.5f,&X[0],&Y[0]);
            gm_rect(&f->t,X[0]-1,Y[0]-1,2,2,C((unsigned)i<p->perfect_chain?gold:faint));
        }
        return;
    }
    double age=rocket_age(f,p),left=(p->rocket_until-f->sim)/HZ;
    float ign=age>=0&&age<.3?1.f-(float)(age/.3):0.f;
    static const float hx[4]={-25.f,-14.f,-14.f,-25.f},hy[4]={4.5f,4.5f,9.5f,9.5f};
    for(int i=0;i<4;i++)bp(&b,hx[i],hy[i],&X[i],&Y[i]);
    gm_poly(&f->t,X,Y,4,C(xz_blend(0x1b1236,0xffffff,(unsigned)(200.f*ign))),256);
    gm_line(&f->t,X[3],Y[3],X[2],Y[2],1,C(gold),256);
    gm_line(&f->t,X[0],Y[0],X[1],Y[1],1,C(comet_rgb),220);
    int fx0,fy0,fx1,fy1;
    bp(&b,-24.f,8.f,&fx0,&fy0);bp(&b,-24.f+10.f*clampf((float)(left/6.0),0,1),8.f,&fx1,&fy1);
    gm_line(&f->t,fx0,fy0,fx1,fy1,1,C(gold),256);
    static const float nxl[4]={-25.f,-25.f,-31.f,-31.f},nyl[4]={4.f,10.f,11.5f,2.5f};
    for(int i=0;i<4;i++)bp(&b,nxl[i],nyl[i],&X[i],&Y[i]);
    gm_poly(&f->t,X,Y,4,C(0x5a2408),256);
    gm_line(&f->t,X[1],Y[1],X[2],Y[2],1,C(comet_rgb),256);
    gm_line(&f->t,X[0],Y[0],X[3],Y[3],1,C(comet_rgb),256);
    gm_line(&f->t,X[2],Y[2],X[3],Y[3],2,C(0xffffff),256);
    static const float cxl[3]={-14.f,-14.f,-10.5f},cyl[3]={4.5f,9.5f,7.f};
    for(int i=0;i<3;i++)bp(&b,cxl[i],cyl[i],&X[i],&Y[i]);
    gm_poly(&f->t,X,Y,3,C(gold),256);
}
/* Rocket speed: thin horizontal streaks race past in the sky, in band
   colours, after the glitch-cat data bars. Deterministic per sample. */
static void rocket_streaks(struct frame *f){
    int any=0;
    for(int i=0;i<f->players;i++)if(rocket_on(f,&f->g->player[i]))any=1;
    if(!any||f->quiet)return;
    int lo=HUD+14,hi=clampi(f->base-20,lo+40,FOOT-10);
    long span=W+200,run=(long)(f->sim*9.0);
    for(int i=0;i<9;i++){
        uint32_t h=gm_hash((uint32_t)i*2654435761u+77u);
        int y=lo+(int)(h%(unsigned)(hi-lo));
        long x=((long)((h>>8)%(uint32_t)span)-run-(long)i*53)%span;
        if(x<0)x+=span;
        x-=200;
        int len=40+(int)((h>>16)%90u);
        gm_span_add(&f->t,(int)x,(int)x+len,y,gm_mix(0,C(f->l.band[i%3]),(unsigned)(70+(h>>24)%60u)));
    }
}
/* Speed you can see. A launch BOOST (the engine's real takeoff vx gain) and
   a perfect landing both leave afterimages of the hull behind the rider;
   the perfect landing also snaps four gold brackets in around it. */
static void speed_ghosts(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    if(!s->on)return;
    float k=rider_k(f);
    struct basis b=basis_of(s->x,s->y,p->pitch,k);
    int cy=board_mid_y(f,who);
    const struct xz_game_impact *bo=latest(p,XZ_IMPACT_BOOST,XZ_IMPACT_BOOST);
    double ba=bo?(f->sim-bo->time)/HZ:99.0;
    if(!p->grounded&&ba>=0&&ba<.45){
        float u=(float)(ba/.45),gain=clampf(p->launch_boost/150.f,.4f,1.4f);
        float dx,dy;back_dir(p,&b,&dx,&dy);
        uint16_t c=C(xz_blend(rider_rgb[who],0xffffff,120));
        for(int i=1;i<=3;i++){
            float d=(float)i*(9.f+9.f*gain)*k*(1.f+.6f*u);
            hull_ghost(f,s->x+ri(dx*d),s->y+ri(dy*d),p->pitch,k,c,(unsigned)(200.f*(1.f-u)/(float)i));
        }
    }
    const struct xz_game_impact *pl=latest(p,XZ_IMPACT_PERFECT_LAND,XZ_IMPACT_PERFECT_LAND);
    double pa=pl?(f->sim-pl->time)/HZ:99.0;
    if(p->grounded&&!p->contact_bad&&pa>=0&&pa<.4){
        float u=(float)(pa/.4);
        float sl=xz_rider_surface_at(f->g,p->at).slope,m=sqrtf(1.f+sl*sl),tx=-1.f/m,ty=sl/m;
        for(int i=1;i<=3;i++){
            float d=(float)i*12.f*k*(.5f+u);
            hull_ghost(f,s->x+ri(tx*d),s->y+ri(ty*d),p->pitch,k,C(gold),(unsigned)(220.f*(1.f-u)/(float)i));
        }
        if(pa<.22){
            float v=(float)(pa/.22);
            int R=ri((52.f-20.f*gm_ease_out(v))*k),e=ri(9.f*k);
            unsigned a=(unsigned)(250.f*(1.f-v*v));
            for(int q=0;q<4;q++){
                int sx=q&1?1:-1,sy=q&2?1:-1,x=s->x+sx*R,y=cy+sy*R*3/4;
                gm_line(&f->t,x,y,x-sx*e,y,2,C(gold),a);
                gm_line(&f->t,x,y,x,y-sy*e,2,C(gold),a);
            }
        }
    }
}
static void riders(struct frame *f){
    const struct xz_rider *g=f->g;
    if(f->play==1)rocket_streaks(f);
    for(int i=0;i<f->players;i++){crest_mark(f,i);trail(f,i);boost_bars(f,i);impacts(f,i);power_fx(f,i);spin_arc(f,i);}
    for(int i=f->players-1;i>=0;i--){
        const struct xz_rider_player *p=&g->player[i];
        if(!f->s[i].on)continue;
        struct pose ps=rider_pose(f,p);
        speed_ghosts(f,i);rocket_plume(f,i);
        surfcraft(f,f->s[i].x,f->s[i].y,p->pitch,rider_k(f),rider_rgb[i],ps,p->lift,p->downforce,p->grounded!=0,p->vx,p->vy);
        rocket_mount(f,i);
        crash_tear(f,i);landing_chevrons(f,i);
        spin_fx(f,i);
    }
}
/* A restrained cameo for earned streaks: a small cow drifts high on the left,
   behind the rider and away from the route ahead. */
static void cow_cameo(struct frame *f){
    unsigned best=0;
    for(int i=0;i<f->players;i++){const struct xz_rider_player *p=&f->g->player[i];if(p->fever_until>f->sim&&p->streak>best)best=p->streak;}
    if(best<5)return;
    double beats=f->g->wave.pulse_period>8.f?f->sim/f->g->wave.pulse_period:f->sim/75.0;
    double fr=beats*2.0-floor(beats*2.0);
    float k=(float)(1.0-fr);
    int bob=f->quiet?0:ri((float)sn(f,(int)(f->sim*.8))*5.f/1024.f);
    gm_draw_cow(&f->t,82,HUD+64+bob,.62f,1.f+.07f*k*k,-.08f,(unsigned)((long)floor(beats*2.0)&3),ri(3.f*k*k+3.f*f->pulse),0,230u);
    if(!f->quiet)for(int i=0;i<8;i++){
        uint32_t h=gm_hash((uint32_t)i*911u+(uint32_t)(beats*2.0));
        static const uint32_t cc[4]={0xffcf4a,0xff4fd8,0x3fe8ff,0xff9a3c};
        gm_rect(&f->t,50+(int)(h%70u),HUD+30+(int)((h>>8)%80u),2,2,C(cc[i&3]));
    }
}
static void feedback(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    if(!p->feedback||!p->feedback[0]||p->feedback_until<=f->sim)return;
    int duo=f->players>1;
    uint32_t tone=duo?rider_rgb[who]:ink;
    if(strstr(p->feedback,"PERFECT")||strstr(p->feedback,"SMOOTH")||strstr(p->feedback,"CATCH")||strstr(p->feedback,"HARMON")||strstr(p->feedback,"SUPER")||strstr(p->feedback,"GEM")||strstr(p->feedback,"360")||strstr(p->feedback,"FLIP")||strstr(p->feedback,"SHARD"))tone=gold;
    else if(strstr(p->feedback,"AURORA"))tone=aurora_rgb;
    else if(strstr(p->feedback,"COMET"))tone=comet_rgb;
    else if(strstr(p->feedback,"HARD")||strstr(p->feedback,"MATCH")||strstr(p->feedback,"CRASH")||strstr(p->feedback,"LOST"))tone=danger;
    int cx=duo?(who?600:200):400,w=xz_text_width(f->c,p->feedback,1,96);
    int y=f->base<H-30?clampi(f->base+12,HUD+4,FOOT-24):FOOT-26;
    xz_round_rect(f->c,cx-w/2-12,y-4,w+24,24,10,15,xz_blend(night,tone,40));
    atext(f->c,cx-w/2,y,p->feedback,1,tone);
}
/* Combo callouts sit behind and above the rider so the route ahead stays
   clear. Pending: "2 FLIPS + 3 CRESTS" over the exact air_points total.
   Landed: "+total" gold, or "total LOST" flashing red, until combo_until. */
static int combo_anchor(const struct frame *f,int who,int w,int *y){
    const struct spot *s=&f->s[who];
    *y=clampi(s->y-ri(104.f*rider_k(f)),HUD+6,FOOT-72);
    int rx=s->x-30;
    return rx-w<8?s->x+30:rx-w;
}
static void combo(struct frame *f,int who){
    const struct xz_rider_player *p=&f->g->player[who];
    const struct spot *s=&f->s[who];
    char a[48],b[24];
    if(!s->on)return;
    if(!p->grounded&&p->air_points){
        a[0]=0;
        if(p->air_tricks)snprintf(a,sizeof a,"%u FLIP%s",p->air_tricks,p->air_tricks>1?"S":"");
        if(p->air_crests){size_t n=strlen(a);snprintf(a+n,sizeof a-n,"%s%u CREST%s",n?" + ":"",p->air_crests,p->air_crests>1?"S":"");}
        snprintf(b,sizeof b,"%u",p->air_points);
        int wa=gm_text_width(a,GM_F18),wb=gm_text_width(b,GM_F28),w=wa>wb?wa:wb,y;
        int x=combo_anchor(f,who,w,&y),ha=gm_text_height(GM_F18);
        int fresh=p->trick_at>f->sim-.3*HZ;
        gm_rect_mix(&f->t,x-8,y-3,w+16,ha+gm_text_height(GM_F28)+6,C(night),150);
        gm_text_lift(&f->t,x+w-wa,y,a,GM_F18,C(xz_blend(ink,gold,120)));
        gm_text_lift(&f->t,x+w-wb,y+ha,b,GM_F28,C(fresh?0xffffff:gold));
    }
    if(!p->grounded&&p->air_tricks&&p->trick_at>f->sim-.45*HZ){
        float u=(float)((f->sim-p->trick_at)/(.45*HZ));
        snprintf(b,sizeof b,"%u",p->air_tricks*360u);
        /* the flip number pops up and right of the spin arc, briefly */
        int y=board_mid_y(f,who)-ri(30.f*rider_k(f))-gm_text_height(GM_F28)-ri(10.f*u);
        gm_text(&f->t,clampi(s->x+ri(36.f*rider_k(f)),4,W-60),clampi(y,HUD+4,FOOT-30),b,GM_F28,C(0xffffff),(unsigned)(256.f*(1.f-u*u)));
    }
    if((p->combo_points||p->landing_points)&&p->combo_until>f->sim){
        float u=clampf(1.f-(float)((p->combo_until-f->sim)/150.0),0,1);
        int bad=p->combo_failed!=0;
        if(bad)snprintf(b,sizeof b,"%u LOST",p->combo_points);
        else snprintf(b,sizeof b,"+%u",p->combo_points+p->landing_points);
        int w=gm_text_width(b,GM_F28),h=gm_text_height(GM_F28),y;
        int x=combo_anchor(f,who,w,&y);
        y=clampi(y+gm_text_height(GM_F18)-ri((bad?6.f:22.f)*u),HUD+4,FOOT-h);
        unsigned al=(unsigned)(u<.6f?256.f:256.f*(1.f-u)/.4f);
        /* a lost combo blinks red and white, then fades with the rest */
        uint16_t c=C(bad?(u<.4f&&((int)(u*12.f)&1)?0xffffff:danger):gold);
        gm_rect_mix(&f->t,x-8,y-2,w+16,h+4,C(night),al*(bad?210u:150u)/256u);
        gm_text(&f->t,x,y,b,GM_F28,c,al);
        if(bad)gm_line(&f->t,x-4,y+h/2,x+gm_text_width(b,GM_F28)-gm_text_width(" LOST",GM_F28)+4,y+h/2,2,c,al);
    }
}
static void riders_text(struct frame *f){
    for(int i=0;i<f->players;i++)combo(f,i);
    for(int i=0;i<f->players;i++){
        const struct spot *s=&f->s[i];
        if(s->lift){
            int x=clampi(s->x,8,W-8);
            gm_line(&f->t,x,HUD+3,x-6,HUD+12,2,C(rider_rgb[i]),256);gm_line(&f->t,x,HUD+3,x+6,HUD+12,2,C(rider_rgb[i]),256);
        }
        if(f->players>1&&s->on){
            int y=clampi(s->y-ri(78.f*rider_k(f)),HUD+2,H-14);
            xz_round_rect(f->c,s->x-11,y,23,15,6,15,rider_rgb[i]);
            text(f->c,s->x-text_w(i?"P2":"P1",1)/2,y+7-text_h(1)/2,i?"P2":"P1",1,night);
        }
    }
    for(int i=0;i<f->players;i++)feedback(f,i);
}

static void stage(struct frame *f,int play){
    f->play=play;
    prepare(f);
    backdrop(f);sky(f);
    terrain(f);water(f);pulse_ticks(f);
    /* play 1: live ride with tags and feedback; 2: scenery riders only, for
       title, setup and result compositions */
    if(play){place_riders(f);thermals(f);if(play==1)cow_cameo(f);gems(f);riders(f);if(play==1)riders_text(f);}
}
/* A soft plate under centred title copy so it reads over the world. */
static void plate(struct frame *f,int y0,int y1){
    for(int y=clampi(y0,0,H);y<clampi(y1,0,H);y++){
        int d=y-y0,e=y1-1-y,m=d<e?d:e;
        unsigned a=(unsigned)clampi(m*12,0,150);
        gm_span_mix(&f->t,120,679,y,C(night),a);
    }
}

/* ---- HUD ---- */
static int chip_w(const struct look *l){
    int w=20+text_w(l->kind,1);
    for(int i=0;i<3;i++)w+=14+text_w(l->brief[i],1);
    return w;
}
static void chip(struct frame *f,int x,int y){
    const struct look *l=&f->l;
    int ty=y+(24-text_h(1))/2;
    ring_panel(f->c,x,y,chip_w(l),24,9,rim,panel);
    text(f->c,x+10,ty,l->kind,1,ink);
    int cx=x+10+text_w(l->kind,1)+10;
    for(int i=0;i<3;i++){
        xz_disc(f->c,cx+3,y+12,3,l->band[i]);
        text(f->c,cx+9,ty,l->brief[i],1,l->band[i]);
        cx+=14+text_w(l->brief[i],1);
    }
}
static void clip_title(char *out,size_t n,const char *s,size_t max){
    size_t i=0;
    if(max+1>n)max=n-1;
    for(;s[i]&&i<max;i++)out[i]=(s[i]>=32&&s[i]<127)?s[i]:' ';
    if(s[i]&&i>3){out[i-3]=out[i-2]=out[i-1]='.';}
    out[i]=0;
}
/* Controls drawn this frame, for encoder focus. Set only inside a render call. */
static struct xz_game_targets *targets;
static void deck_button(struct frame *f){
    xz_game_target_add(targets,692,6,100,36,XZ_GAME_EXIT);
    ring_panel(f->c,692,6,100,36,10,rim,0x1a1238);
    text(f->c,692+(100-text_w("DECK",2))/2,6+(36-text_h(2))/2,"DECK",2,ink);
}
static void progress(struct frame *f){
    xz_rect(f->c,0,HUD-1,W,1,0x22183e);
    if(f->has_wave&&f->g->wave.total){
        double fr=f->g->position/(double)f->g->wave.total;
        xz_rect(f->c,0,HUD-2,clampi((int)(fr*W),0,W),2,xz_blend(night,f->l.band[2],170));
    }
}
static void dial(struct frame *f,int cx,int cy,int r,float pitch,float target,uint32_t col){
    if(!(pitch>-7.f&&pitch<7.f))pitch=0.f;
    if(!(target>-7.f&&target<7.f))target=pitch;
    xz_disc(f->c,cx,cy,r,0x140d30);
    xz_ring(f->c,cx,cy,r,1,rim);
    float tc=cosf(target),ts=sinf(target),pc=cosf(pitch),ps=sinf(pitch),R=(float)r;
    gm_line(&f->t,cx,cy,cx+ri(tc*R),cy-ri(ts*R),1,C(faint),256);
    gm_line(&f->t,cx-ri(pc*(R-3.f)),cy+ri(ps*(R-3.f)),cx+ri(pc*(R-1.f)),cy-ri(ps*(R-1.f)),2,C(col),256);
}
static void key_badge(struct frame *f,int x,int y,const char *k,int on,uint32_t col){
    xz_round_rect(f->c,x,y-2,14,14,4,15,on?col:0x221a46);
    text(f->c,x+7-text_w(k,1)/2,y+5-text_h(1)/2,k,1,on?night:faint);
}
static void player_panel(struct frame *f,int who,int x,int y,int w){
    const struct xz_rider_player *p=&f->g->player[who];
    uint32_t col=rider_rgb[who];
    int fever=p->fever_until>f->sim;
    char s[48];
    ring_panel(f->c,x,y,w,40,10,fever?gold:xz_blend(rim,col,110),panel);
    if(f->players>1){
        xz_round_rect(f->c,x+8,y+5,24,15,6,15,col);
        text(f->c,x+20-text_w(who?"P2":"P1",1)/2,y+12-text_h(1)/2,who?"P2":"P1",1,night);
    }else xz_disc(f->c,x+16,y+12,5,col);
    snprintf(s,sizeof s,"%u",p->score);
    text(f->c,x+38,y+1,s,2,ink);
    /* collected pickups beside the score, popping white on the latest */
    {
        int gx=x+38+text_w(s,2)+14,gy=y+12,pop=p->pickup_at>f->sim-.25*HZ;
        int xs[4]={gx,gx+4,gx,gx-4},ys[4]={gy-6,gy,gy+5,gy};
        gm_poly(&f->t,xs,ys,4,C(pop?0xffffff:f->l.band[2]),256);
        snprintf(s,sizeof s,"%u",p->catches);
        text(f->c,gx+7,y+6,s,1,pop?gold:dim);
    }
    if(p->streak>1){
        snprintf(s,sizeof s,"X%u",p->streak);
        text(f->c,x+w-36-text_w(s,2),y+1,s,2,fever?gold:col);
    }
    int bx=x+8,by=y+27;
    for(int i=0;i<9;i++)xz_rect(f->c,bx+i*8,by,6,6,(unsigned)i<p->charge?col:0x221a46);
    int lx=bx+76;
    if(p->power!=XZ_RIDER_POWER_NONE){
        int au=p->power==XZ_RIDER_POWER_AURORA;
        const char *nm=au?"AURORA":"COMET";
        uint32_t pc=au?aurora_rgb:comet_rgb;
        if(p->power_until>f->sim){
            double left=p->power_until-f->sim;
            snprintf(s,sizeof s,"%s %dS",nm,(int)(left/XZ_WAVE_RIDER_HZ)+1);
            /* the drain bar runs out with the power (pickups last 600 samples) */
            xz_rect(f->c,lx,by+9,56,2,0x221a46);
            xz_rect(f->c,lx,by+9,clampi((int)(left*56.0/600.0),0,56),2,pc);
        }else snprintf(s,sizeof s,"%s",nm);
        text(f->c,lx,by-4,s,1,pc);
    }else{
        key_badge(f,lx,by-2,"E",p->charge>=3,aurora_rgb);
        key_badge(f,lx+15,by-2,"F",p->charge>=5,comet_rgb);
    }
    if(f->players==1){
        /* perfect chain: three gold diamonds; at three the rocket lights and
           a bar burns down its remaining six seconds */
        int mx=x+150,my=by+3,on=p->rocket_until>f->sim;
        for(int i=0;i<3;i++){
            int cx=mx+4+i*11,xs[4]={cx,cx+4,cx,cx-4},ys[4]={my-5,my,my+5,my};
            int lit=on||(unsigned)i<p->perfect_chain;
            gm_poly(&f->t,xs,ys,4,C(lit?gold:0x221a46),256);
        }
        if(on){
            double left=p->rocket_until-f->sim;
            xz_rect(f->c,mx+36,my-1,40,3,0x221a46);
            xz_rect(f->c,mx+36,my-1,clampi((int)(left*40.0/ROCKET_SAMPLES),0,40),3,left<HZ?danger:comet_rgb);
        }
    }
    if(p->held){
        int sw=text_w("DIVE",1)+10,sx=x+w-38-sw;
        xz_round_rect(f->c,sx,by-4,sw,15,5,15,col);
        text(f->c,sx+5,by+3-text_h(1)/2,"DIVE",1,night);
    }
    dial(f,x+w-16,y+20,11,p->pitch,p->pitch_target,col);
}
static void center_panel(struct frame *f,int x,int y,int w){
    const struct xz_rider *g=f->g;
    char s[48];
    if(g->mode==XZ_RIDER_COOP){
        int glow=g->team_feedback_until>f->sim;
        ring_panel(f->c,x,y,w,40,10,glow?gold:rim,panel);
        text(f->c,x+10,y+4,glow?"HARMONY":"TEAM",1,glow?gold:dim);
        snprintf(s,sizeof s,"%u",g->team_score);
        text(f->c,x+w-10-text_w(s,2),y+1,s,2,ink);
        snprintf(s,sizeof s,"CHAIN X%u",g->team_chain);
        text(f->c,x+10,y+22,s,1,g->team_chain>1?gold:dim);
        snprintf(s,sizeof s,"HARMONIES %u",g->harmonies);
        text(f->c,x+w-10-text_w(s,1),y+22,s,1,f->l.band[1]);
        return;
    }
    unsigned a=g->player[0].score,b=g->player[1].score;
    ring_panel(f->c,x,y,w,40,10,rim,panel);
    text_mid(f->c,x+w/2,y+3,"VERSUS",1,dim);
    if(a==b){text_mid(f->c,x+w/2,y+19,"LEVEL",2,ink);return;}
    snprintf(s,sizeof s,"%s LEADS BY %u",a>b?"P1":"P2",a>b?a-b:b-a);
    text_mid(f->c,x+w/2,y+19,s,2,rider_rgb[a>b?0:1]);
}
static void hud_ride(struct frame *f){
    const struct xz_rider *g=f->g;
    darken_rows(f,0,HUD);
    if(f->players==1){
        char t[40];
        atext(f->c,14,3,"WAVE RIDER",1,ink);
        text(f->c,14,27,"BY VJ OPTICMYSTIC",1,faint);
        player_panel(f,0,196,4,316);
        if(g->wave.title[0]){clip_title(t,sizeof t,g->wave.title,25);text(f->c,526,16,t,1,dim);}
    }else{
        player_panel(f,0,8,4,224);
        center_panel(f,240,4,204);
        player_panel(f,1,452,4,224);
    }
    deck_button(f);
    progress(f);
}
static void hud_setup(struct frame *f,const char *right){
    char s[32];
    darken_rows(f,0,HUD);
    /* setup/title/result header: identity left, step right, nothing else */
    atext(f->c,16,12,"WAVE RIDER",1,ink);
    snprintf(s,sizeof s,"MUSIC DECK %u",f->g->deck+1);
    text(f->c,160,17,s,1,dim);
    if(right)text_right(f->c,672,15,right,2,dim);
    deck_button(f);
    progress(f);
}
static void footer(struct frame *f){
    darken_rows(f,FOOT,H);
    text_mid(f->c,400,447,f->g->difficulty==XZ_RIDER_EASY?
        "Jog sets the nose  /  Spin back to slow  /  Hold CUE downhill, release on the rise  /  Land along the hill":
        "Jog leans the board  /  Air spins carry on, spin back to slow  /  Land along the hill  /  3 perfects light the rocket",1,dim);
    char s[96];
    snprintf(s,sizeof s,"E AURORA   F COMET   G %s   H EXIT",f->quiet?"CALM ON":"CALM");
    text(f->c,14,463,s,1,faint);
    text_mid(f->c,400,463,difficulty_name[f->g->difficulty],1,accent);
    text_right(f->c,786,463,"A GAME BY VJ OPTICMYSTIC",1,faint);
}

/* ---- buttons and cards ---- */
enum { PRIMARY, SECONDARY, DISABLED, CHOSEN };
static void button(struct frame *f,int x0,int y0,int x1,int y1,const char *label,int style){
    int w=x1-x0,h=y1-y0,dot=h>=60&&text_w(label,3)+40<=w?3:h>=34?2:1;
    uint32_t col=ink;
    if(style!=DISABLED)xz_game_target_add(targets,x0,y0,w,h,style==PRIMARY?XZ_GAME_PRIMARY:XZ_GAME_NORMAL);
    if(style==PRIMARY){
        xz_round_rect(f->c,x0-3,y0-3,w+6,h+6,17,15,xz_blend(night,accent,90));
        xz_round_rect(f->c,x0,y0,w,h,14,15,accent);col=night;
    }else if(style==CHOSEN)ring_panel(f->c,x0-2,y0-2,w+4,h+4,15,accent,0x14284a);
    else if(style==SECONDARY)ring_panel(f->c,x0,y0,w,h,12,rim,panel);
    else{xz_round_rect(f->c,x0,y0,w,h,12,15,0x1a1436);col=faint;}
    text(f->c,x0+(w-text_w(label,dot))/2,y0+(h-text_h(dot))/2,label,dot,col);
}
static void card(struct frame *f,int x0,int y0,int x1,int y1){ring_panel(f->c,x0,y0,x1-x0,y1-y0,16,rim,panel);}
static void preview(struct frame *f,int x0,int y0,int x1,int y1){
    const struct xz_rider *g=f->g;
    const struct xz_wave_rider_snapshot *w=&g->wave;
    int width=x1-x0,base=y1-6,ext=y1-y0-14;
    uint16_t col[3];
    for(int k=0;k<3;k++)col[k]=C(xz_blend(0x07041a,f->l.band[k],150));
    float hmax=1.f;
    for(uint32_t i=0;i<w->count;i++)if(g->course.height[i]>hmax)hmax=g->course.height[i];
    uint16_t line=C(0xf4f0ff);
    int prev=-1;
    for(int x=0;x<width;x++){
        uint32_t i0=(uint32_t)((uint64_t)x*w->count/(unsigned)width),i1=(uint32_t)((uint64_t)(x+1)*w->count/(unsigned)width),best=0;
        const uint8_t *pick=NULL;
        if(i1<=i0)i1=i0+1;
        for(uint32_t i=i0;i<i1&&i<w->count;i++){
            const uint8_t *b=w->bands+(size_t)i*3;uint32_t t=(uint32_t)b[0]+b[1]+b[2];
            if(!pick||t>best){best=t;pick=b;}
        }
        if(!pick)continue;
        int ht=clampi((int)(best*(uint32_t)ext/w->normalization),0,ext);
        int hi=clampi((int)(((uint32_t)pick[1]+pick[2])*(uint32_t)ext/w->normalization),0,ext);
        int hc=clampi((int)((uint32_t)pick[2]*(uint32_t)ext/w->normalization),0,ext);
        vrun(f,x0+x,base-hc,base,col[2]);
        vrun(f,x0+x,base-hi,base-hc,col[1]);
        vrun(f,x0+x,base-ht,base-hi,col[0]);
        uint32_t ic=(i0+i1)/2;
        if(ic>=w->count)ic=w->count-1;
        float hv=g->course.height[ic];
        if(!(hv>0.f))hv=0.f;
        int yc=base-(int)(hv*(float)ext/hmax);
        vrun(f,x0+x,yc,yc+2,line);
        if(prev>=0&&iabs(prev-yc)>1)vrun(f,x0+x,prev<yc?prev:yc,(prev<yc?yc:prev)+1,line);
        prev=yc;
    }
    double rel=g->position-(double)w->first;
    if(rel>=0&&rel<(double)w->count)xz_rect(f->c,x0+(int)(rel*width/w->count),y0+4,1,base-y0-4,ink);
}
static void icon(struct frame *f,int x,int y,float pitch,int who,float k){
    struct pose ps={0,0,0,1,0};
    surfcraft(f,x,y,pitch,k,rider_rgb[who],ps,0,0,1,300.f,0.f);
}
static void mode_card(struct frame *f,int m,int x0,int x1){
    static const char *const sub[3]={"ONE RIDER, YOUR DECK","TWO RIDERS, ONE TEAM","TWO RIDERS, ONE WINNER"};
    int chosen=(int)f->g->mode==m;
    button(f,x0,220,x1,288,"",chosen?CHOSEN:SECONDARY);
    if(m==0)icon(f,x0+34,276,.15f,0,.55f);
    else{icon(f,x0+26,272,.15f,0,.45f);icon(f,x0+46,282,m==2?-.15f:.15f,1,.45f);}
    text(f->c,x0+70,228,mode_name[m],3,chosen?ink:dim);
    text(f->c,x0+70,262,sub[m],1,chosen?accent:faint);
}
static void check_row(struct frame *f,int x,int y,int ok){
    if(ok){xz_disc(f->c,x,y,9,ready);text(f->c,x-text_w("OK",1)/2,y-text_h(1)/2,"OK",1,night);}
    else xz_ring(f->c,x,y,8,1,rim);
}
static void check_card(struct frame *f,int who,int x0,int y0,int w){
    const struct xz_rider *g=f->g;
    const struct xz_rider_player *p=&g->player[who];
    uint32_t col=rider_rgb[who];
    char s[48];
    ring_panel(f->c,x0,y0,w,86,12,p->jog_seen?ready:xz_blend(rim,col,110),panel);
    if(f->players>1)snprintf(s,sizeof s,"P%d  %s DECK",who+1,who?"RIGHT":"LEFT");
    else snprintf(s,sizeof s,"DECK %u CONTROLS",g->deck+1);
    xz_disc(f->c,x0+14,y0+12,4,col);
    text(f->c,x0+24,y0+5,s,1,ink);
    dial(f,x0+22,y0+38,10,p->pitch,p->pitch_target,col);
    text(f->c,x0+42,y0+22,"JOG LEANS THE BOARD",2,ink);
    text(f->c,x0+42,y0+44,p->jog_seen?"NOSE DOWN HUGS, NOSE UP LAUNCHES":"SMALL TURNS: CW NOSE DOWN, CCW UP",1,p->jog_seen?ready:dim);
    check_row(f,x0+w-20,y0+38,p->jog_seen!=0);
    text(f->c,x0+42,y0+60,"OPTIONAL: CUE OR PADS A-D DIVE",1,dim);
    text(f->c,x0+42,y0+72,p->hold_seen?"DIVE SEEN":"NOT NEEDED TO START",1,p->hold_seen?ready:faint);
}

static void setup_step(struct frame *f){
    const struct xz_rider *g=f->g;
    char s[96],t[48];
    switch(g->setup_step){
    case 0:{
        static const char *const go[3]={"SET UP SOLO","SET UP CO-OP","SET UP VERSUS"};
        unsigned m=(unsigned)g->mode<3u?(unsigned)g->mode:0u;
        stage(f,0);hud_setup(f,"CHOOSE A MODE");
        plate(f,56,206);
        atext_mid(f->c,400,64,"WAVE RIDER",3,ink);
        text_mid(f->c,400,114,"A GAME BY VJ OPTICMYSTIC",1,gold);
        atext_mid(f->c,400,140,"Surf hills composed from your track",1,dim);
        text_mid(f->c,400,172,"Angle down to hug the hill, angle up to launch",2,dim);
        mode_card(f,0,40,268);mode_card(f,1,286,514);mode_card(f,2,532,760);
        button(f,160,330,640,410,go[m],PRIMARY);
        break;}
    case 1:
        stage(f,0);hud_setup(f,"SETUP 1 / 3");
        atext_mid(f->c,400,88,"CHOOSE THE MUSIC DECK",2,ink);
        atext_mid(f->c,400,120,"Its waveform becomes the hills",1,dim);
        for(int d=0;d<2;d++){
            int x0=d?420:80,chosen=g->deck==(unsigned)d;
            snprintf(s,sizeof s,"DECK %d",d+1);
            button(f,x0,150,x0+300,224,"",chosen?CHOSEN:SECONDARY);
            text(f->c,x0+(300-text_w(s,3))/2,166,s,3,ink);
            const char *sub=chosen?"THE HILLS COME FROM HERE":"TAP TO CHOOSE";
            text(f->c,x0+(300-text_w(sub,1))/2,202,sub,1,chosen?accent:faint);
        }
        if(f->players>1)text_mid(f->c,400,252,"P1 RIDES THE LEFT JOG, P2 THE RIGHT JOG",1,ink);
        else text_mid(f->c,400,252,"YOU RIDE WITH THIS DECK'S JOG",1,ink);
        text_mid(f->c,400,294,"PLAY AND TEMPO STAY IN YOUR HANDS",1,faint);
        button(f,160,330,640,410,"NEXT",PRIMARY);
        break;
    case 2:{
        stage(f,0);hud_setup(f,"SETUP 2 / 3");
        card(f,80,90,720,312);
        if(f->has_course){
            const struct xz_wave_rider_snapshot *w=&g->wave;
            chip(f,400-chip_w(&f->l)/2,100);
            preview(f,96,130,704,250);
            clip_title(t,sizeof t,w->title[0]?w->title:"UNTITLED",40);
            snprintf(s,sizeof s,"DECK %u  /  %s",g->deck+1,t);
            text_mid(f->c,400,268,s,1,ink);
            if(w->pulse_confidence>=.3f&&w->pulse_period>=8.f)
                snprintf(s,sizeof s,"PULSE ABOUT %d PER MINUTE, READ FROM THE WAVEFORM",(int)(XZ_WAVE_RIDER_HZ*60.f/w->pulse_period+.5f));
            else snprintf(s,sizeof s,"NO STEADY PULSE FOUND. HILLS FOLLOW THE ENERGY");
            text_mid(f->c,400,288,s,1,faint);
            button(f,160,330,640,410,"VERIFY CONTROLS",PRIMARY);
        }else if(g->wave.loading){
            static const char *dots[4]={"",".","..","..."};
            snprintf(s,sizeof s,"READING DECK %u WAVEFORM%s",g->deck+1,dots[(g->last_ms/300u)&3u]);
            text_mid(f->c,400,190,s,2,dim);
            button(f,160,330,640,410,"DETECTING",DISABLED);
        }else{
            const char *why="NO WAVEFORM FOR THIS DECK YET";
            if(g->wave.error==XZ_WAVE_RIDER_NO_TRACK)why="NO TRACK LOADED ON THIS DECK";
            else if(g->wave.error==XZ_WAVE_RIDER_NO_ANALYSIS)why="THIS TRACK HAS NO WAVEFORM ANALYSIS";
            else if(g->wave.error==XZ_WAVE_RIDER_POSITION_UNAVAILABLE)why="THIS DECK'S PLAY POSITION CAN'T BE READ";
            snprintf(s,sizeof s,"DECK %u",g->deck+1);
            text_mid(f->c,400,160,s,2,faint);
            text_mid(f->c,400,196,why,2,ink);
            int position_error=g->wave.error==XZ_WAVE_RIDER_POSITION_UNAVAILABLE;
            text_mid(f->c,400,232,position_error?"RETURN TO THE DECK, THEN REOPEN TO RETRY":"LOAD AN ANALYSED TRACK, THEN REOPEN WAVE RIDER",1,dim);
            button(f,160,330,640,410,position_error?"BACK TO DECK":"LOAD A TRACK",PRIMARY);
        }
        button(f,24,435,200,475,"CHOOSE DECK",SECONDARY);
        break;}
    default:{
        stage(f,2);hud_setup(f,"SETUP 3 / 3");
        int go=g->valid!=0;
        if(f->players>1){check_card(f,0,40,54,350);check_card(f,1,410,54,350);}
        else check_card(f,0,200,54,400);
        for(int i=0;i<f->players;i++)if(!g->player[i].jog_seen)go=0;
        plate(f,144,272);
        text_mid(f->c,400,150,g->difficulty==XZ_RIDER_EASY?
            "ON EASY YOU SET THE AIR ANGLE. SPIN BACK TO SLOW THE BOARD":
            "IN THE AIR, A JOG SPIN KEEPS THE BOARD TURNING. SPIN BACK TO SLOW IT",1,accent);
        text_mid(f->c,400,166,"LAND ALONG THE HILL TO BANK FLIPS. THREE PERFECTS IN A ROW LIGHT THE ROCKET",1,dim);
        for(unsigned d=0;d<3;d++)button(f,160+(int)d*160,198,318+(int)d*160,244,difficulty_name[d],g->difficulty==(enum xz_rider_difficulty)d?CHOSEN:SECONDARY);
        static const char *const description[3]={
            "THE BOARD HOLDS THE ANGLE YOU SET. CLOSE LANDINGS STILL COUNT",
            "ORIGINAL RIDE. THE BOARD DRIFTS TO FOLLOW ITS ARC",
            "TIGHT LANDINGS. MISSES COST SPEED AND THE STREAK"};
        text_mid(f->c,400,254,description[g->difficulty],1,accent);
        text_mid(f->c,400,276,"OPTIONAL: A FAST JOG SPIN ON THE GROUND HOPS, WITH CUE RELEASED",1,dim);
        button(f,260,294,540,320,g->spin_lift?"SPIN LIFT: ON":"SPIN LIFT: OFF",g->spin_lift?CHOSEN:SECONDARY);
        text_mid(f->c,400,330,"TURN THE JOG A LITTLE TO CONFIRM STEERING",1,dim);
        button(f,160,350,640,418,"RIDE THIS TRACK",go?PRIMARY:DISABLED);
        button(f,220,434,580,470,"RIDE EASY / DEFAULTS",SECONDARY);
        break;}
    }
}
static void title_card(struct frame *f){
    const struct xz_rider *g=f->g;
    static const char *const line[3]={"Ride down the hills of your track. Spin the jog in the air to flip",
        "Two riders, one team score. Land together for harmonies",
        "Two riders, same hills. Highest score wins"};
    unsigned m=(unsigned)g->mode<3u?(unsigned)g->mode:0u;
    char s[96],t[48];
    snprintf(s,sizeof s,"%s / %s",mode_name[m],difficulty_name[g->difficulty]);
    stage(f,2);hud_setup(f,s);
    plate(f,54,226);
    atext_mid(f->c,400,60,"WAVE RIDER",3,ink);
    text_mid(f->c,400,108,"A GAME BY VJ OPTICMYSTIC",1,gold);
    clip_title(t,sizeof t,g->wave.title[0]?g->wave.title:"UNTITLED",36);
    snprintf(s,sizeof s,"DECK %u  /  %s",g->deck+1,t);
    atext_mid(f->c,400,132,s,1,ink);
    if(f->has_wave)chip(f,400-chip_w(&f->l)/2,164);
    text_mid(f->c,400,198,line[m],1,dim);
    button(f,160,330,640,410,"RIDE THIS TRACK",g->valid?PRIMARY:DISABLED);
    footer(f);
}
static int height_of(float v){return (int)floorf(clampf(v,0.f,99999.f)+.5f);}
static unsigned clean_landings(const struct xz_rider_player *p){return p->landings>p->hard_landings?p->landings-p->hard_landings:0u;}
static void result_column(struct frame *f,int who,int cx){
    const struct xz_rider_player *p=&f->g->player[who];
    char s[64];
    snprintf(s,sizeof s,"P%d  %u",who+1,p->score);
    text_mid(f->c,cx,132,s,2,rider_rgb[who]);
    snprintf(s,sizeof s,"%u PERFECT OF %u LANDINGS",p->perfects,p->landings);
    text_mid(f->c,cx,160,s,1,ink);
    snprintf(s,sizeof s,"BEST STREAK %u",p->best_streak);
    text_mid(f->c,cx,176,s,1,ink);
    snprintf(s,sizeof s,"BEST HEIGHT %d",height_of(p->best_height));
    text_mid(f->c,cx,192,s,1,ink);
    snprintf(s,sizeof s,"%u LAUNCHES  /  %u CATCHES",p->launches,p->catches);
    text_mid(f->c,cx,208,s,1,dim);
    snprintf(s,sizeof s,"%u CLEAN  /  %u HARD LANDINGS",clean_landings(p),p->hard_landings);
    text_mid(f->c,cx,224,s,1,dim);
    if(f->g->spin_lift||p->flick_launches)
        snprintf(s,sizeof s,"%u FLIPS  /  BEST COMBO %u  /  %u LIFTS",p->tricks_landed,p->best_combo,p->flick_launches);
    else snprintf(s,sizeof s,"%u FLIPS LANDED  /  BEST COMBO %u",p->tricks_landed,p->best_combo);
    text_mid(f->c,cx,240,s,1,gold);
}
static void result_card(struct frame *f){
    const struct xz_rider *g=f->g;
    char s[96];
    stage(f,2);hud_setup(f,NULL);
    card(f,110,52,690,322);
    text_mid(f->c,400,62,"RUN COMPLETE",1,dim);
    if(f->players==1){
        const struct xz_rider_player *p=&g->player[0];
        snprintf(s,sizeof s,"%u",p->score);
        text_mid(f->c,400,78,s,5,rider_rgb[0]);
        snprintf(s,sizeof s,"%u LANDINGS  /  %u PERFECT  /  %u HARD",p->landings,p->perfects,p->hard_landings);
        text_mid(f->c,400,132,s,1,ink);
        snprintf(s,sizeof s,"BEST STREAK %u  /  LAUNCHES %u  /  CLEAN LANDINGS %u",p->best_streak,p->launches,clean_landings(p));
        text_mid(f->c,400,152,s,1,ink);
        snprintf(s,sizeof s,"BEST HEIGHT %d  /  CATCHES %u  /  POWERS %u",height_of(p->best_height),p->catches,p->powers_used);
        text_mid(f->c,400,172,s,1,ink);
        snprintf(s,sizeof s,"FLIPS LANDED %u  /  BEST COMBO %u",p->tricks_landed,p->best_combo);
        text_mid(f->c,400,192,s,1,gold);
        if(g->spin_lift||p->flick_launches){
            snprintf(s,sizeof s,"SPIN LIFTS %u",p->flick_launches);
            text_mid(f->c,400,212,s,1,gold);
        }
    }else{
        unsigned a=g->player[0].score,b=g->player[1].score;
        if(g->mode==XZ_RIDER_COOP){
            snprintf(s,sizeof s,"TEAM %u",g->team_score);
            text_mid(f->c,400,78,s,4,gold);
            snprintf(s,sizeof s,"HARMONIES %u  /  TEAM CHAIN %u",g->harmonies,g->team_chain);
            text_mid(f->c,400,112,s,1,f->l.band[1]);
        }else{
            text_mid(f->c,400,78,a==b?"TIE":a>b?"P1 WINS":"P2 WINS",4,a==b?ink:rider_rgb[a>b?0:1]);
            snprintf(s,sizeof s,"%u  TO  %u",a>b?a:b,a>b?b:a);
            text_mid(f->c,400,112,s,1,dim);
        }
        result_column(f,0,260);
        result_column(f,1,540);
    }
    snprintf(s,sizeof s,"SAME TRACK, SAME COURSE  /  %04X  /  %s",(unsigned)(g->course_hash&0xffffu),difficulty_name[g->difficulty]);
    text_mid(f->c,400,262,s,1,faint);
    atext_mid(f->c,400,282,"A GAME BY VJ OPTICMYSTIC",1,gold);
    int eof=g->wave.total>1&&g->position>=(double)g->wave.total-1.0;
    button(f,160,330,640,410,eof?"BACK TO DECK":"RIDE AGAIN",eof||g->valid?PRIMARY:DISABLED);
}

int xz_rider_render(const struct xz_rider *g,uint16_t *pixels,size_t count,size_t stride){
    return xz_rider_render_targets(g,pixels,count,stride,NULL);
}
int xz_rider_render_targets(const struct xz_rider *g,uint16_t *pixels,size_t count,size_t stride,struct xz_game_targets *out){
    if(out)out->count=0;
    if(!g||!pixels||stride<W||stride>count/H)return 0;
    targets=out;
    static struct frame f; /* one render thread under the UI lock; scratch off the stack */
    const struct xz_wave_rider_snapshot *w=&g->wave;
    f.c=(struct xz_canvas){pixels,stride,W,H,0};
    f.t=(struct gm_tgt){pixels,stride,0,0,W,H};
    f.g=g;
    f.l=look_of(w->kind);
    f.quiet=!!g->quiet;
    f.has_wave=w->valid&&w->count&&w->count<=XZ_WAVE_RIDER_SAMPLES&&w->normalization;
    f.has_course=f.has_wave&&g->valid;
    f.zoom=g->zoom>0.f?clampf(g->zoom,.1f,1.f):1.f;
    f.cam=isfinite(g->camera_alt)?clampf(g->camera_alt,-200000.f,100000.f):xz_rider_base_at(g->position);
    f.players=xz_rider_players(g)>=2u?2:1;
    f.now=g->position;
    f.sim=g->sim_at>0?g->sim_at:g->position;
    f.world=f.world_b=0;f.wmix=0;
    if(f.has_wave&&g->wave.total>1){
        float pr=clampf((float)(g->position/(double)g->wave.total),0,.999f)*3.f;
        unsigned wi=(unsigned)pr;float in=pr-(float)wi;
        f.world=f.world_b=wi>2u?2u:wi;
        if(wi<2&&in>.95f){f.world_b=wi+1;f.wmix=(unsigned)((in-.95f)/.05f*16.f);}
    }
    /* Lookahead: a rocket lets the rider lead the playhead by up to 280
       samples. Past 110 of lead the frame origin slides forward with the
       trailing rider's lead, so the rider stays mid-screen with route ahead.
       A pure function of lead, so it is continuous without frame state. */
    {
        double lead=1e9;
        for(int i=0;i<f.players;i++){double d=g->player[i].at-g->position;if(d<lead)lead=d;}
        double shift=isfinite(lead)?lead-110.0:0.0;
        f.origin=g->position+(shift>0.0?(shift<170.0?shift:170.0):0.0);
    }
    for(int k=0;k<3;k++)f.mu[k]=clampf(g->music[k],0.f,1.f);
    f.pulse=clampf(g->pulse,0.f,1.f);
    f.fever=0;f.aurora=0;
    unsigned streak=0;
    for(int i=0;i<f.players;i++){
        const struct xz_rider_player *p=&g->player[i];
        if(p->streak>streak)streak=p->streak;
        if(p->fever_until>f.sim)f.fever=1;
        if(p->power==XZ_RIDER_POWER_AURORA)f.aurora=1;
    }
    f.energy=clampf(.3f+.4f*(f.mu[0]+f.mu[1]+f.mu[2])/3.f+.3f*(float)(streak>12u?12u:streak)/12.f+(f.fever?.25f:0.f),0.f,1.f);
    sine_init(&f);
    switch(g->phase){
    case XZ_RIDER_SETUP:setup_step(&f);break;
    case XZ_RIDER_TITLE:title_card(&f);break;
    case XZ_RIDER_RESULT:result_card(&f);break;
    default:
        stage(&f,1);hud_ride(&f);footer(&f);
        if(g->phase==XZ_RIDER_HOLD){
            const char *m="DECK PAUSED  /  PLAY RESUMES";
            int bw=text_w(m,2)+40;
            ring_panel(f.c,400-bw/2,58,bw,34,12,rim,panel);
            text(f.c,400-text_w(m,2)/2,68,m,2,ink);
        }
        break;
    }
    targets=NULL;
    return 1;
}
