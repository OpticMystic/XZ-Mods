/* SPDX-License-Identifier: MIT */
/* Wave Rider stage. The course is the deck's own 150 Hz layered waveform at
 * one sample per pixel, drawn as a landscape rising from a water line with
 * its reflection below. Every shape comes from the rider state and the
 * actual source window; the only decoration not taken from the track is the
 * fixed star field. Column and row fills only: no heap, no IO, no per-pixel
 * formatting, nothing thicker than a rect span. */
#include "wave_rider.h"
#include "../ui/ui_skin.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W 800
#define H 480
#define WATER XZ_RIDER_WATER
#define PH XZ_RIDER_PLAYHEAD
#define EXTENT 200
#define HUD 38
#define RIDGE 116
#define SUN_X 622

static const uint32_t ink=0xf1f5fc,dim=0x8f98b2,faint=0x4a526e,night=0x04050d,
    panel=0x0d1226,rim=0x2a3256,crest_rgb=0xfff1cf,danger=0xff5a6e,
    accent=0x36e6f2,ready=0x4be38a,past_rgb=0x0a0b1a;

/* Provenance decides colour and words together, so band data never wears stem names. */
struct look { uint32_t band[3]; const char *kind,*name[3],*brief[3]; };
static struct look look_of(unsigned kind){
    if(kind==1)return (struct look){{0x2ee6f0,0x9a6bff,0xffc23d},"STEMS",
        {"DRUMS","HARMONICS","VOCALS"},{"DRUMS","HARM","VOX"}};
    return (struct look){{0x2f6bff,0xf2aa3c,0xf3efe2},"3-BAND",
        {"LOW","MID","HIGH"},{"LOW","MID","HIGH"}};
}

struct frame {
    struct xz_canvas c;
    const struct xz_rider *g;
    struct look l;
    int has_wave, quiet, rider_y;
    long long s0;          /* sample under screen column 0 */
    uint32_t sky[WATER];
    uint16_t sky565[WATER];
    int crest_y[W];
};

static uint16_t c565(uint32_t c){return (uint16_t)(((c>>8)&0xF800u)|((c>>5)&0x07E0u)|((c>>3)&0x1Fu));}
static uint16_t half565(uint16_t v){return (uint16_t)((v>>1)&0x7BEFu);}
static uint16_t quarter565(uint16_t v){return (uint16_t)((v>>2)&0x39E7u);}
static int clampi(int v,int a,int b){return v<a?a:v>b?b:v;}
static int isqrt_i(int v){
    if(v<=0)return 0;
    int r=(int)sqrtf((float)v);
    while(r*r>v)r--;
    while((r+1)*(r+1)<=v)r++;
    return r;
}
static int text_w(const char *s,int dot){int n=(int)strlen(s);return n?n*6*dot-dot:0;}
static void text(struct xz_canvas c,int x,int y,const char *s,int dot,uint32_t col){xz_pixel_text(c,x,y,s,dot,100,col);}
static void text_lift(struct xz_canvas c,int x,int y,const char *s,int dot,uint32_t col){
    xz_pixel_text(c,x+dot,y+dot,s,dot,100,night);xz_pixel_text(c,x,y,s,dot,100,col);
}
static void text_mid(struct xz_canvas c,int cx,int y,const char *s,int dot,uint32_t col){text_lift(c,cx-text_w(s,dot)/2,y,s,dot,col);}
static void vrun(const struct frame *f,int x,int y0,int y1,uint16_t v){
    if((unsigned)x>=W)return;
    y0=clampi(y0,0,H);y1=clampi(y1,0,H);
    uint16_t *p=f->c.p+(size_t)y0*f->c.stride+(size_t)x;
    for(int y=y0;y<y1;y++,p+=f->c.stride)*p=v;
}
static void tri(struct xz_canvas c,int x0,int y0,int x1,int y1,int x2,int y2,uint32_t col){
    int t;
    if(y1<y0){t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    if(y2<y0){t=x0;x0=x2;x2=t;t=y0;y0=y2;y2=t;}
    if(y2<y1){t=x1;x1=x2;x2=t;t=y1;y1=y2;y2=t;}
    if(y2==y0){
        int a=x0<x1?x0:x1,b=x0>x1?x0:x1;a=a<x2?a:x2;b=b>x2?b:x2;
        xz_rect(c,a,y0,b-a+1,1,col);return;
    }
    for(int y=y0;y<=y2;y++){
        int xa=x0+(x2-x0)*(y-y0)/(y2-y0);
        int xb=y<y1?x0+(x1-x0)*(y-y0)/(y1-y0):y2==y1?x1:x1+(x2-x1)*(y-y1)/(y2-y1);
        if(xa>xb){t=xa;xa=xb;xb=t;}
        xz_rect(c,xa,y,xb-xa+1,1,col);
    }
}
static void ring_panel(struct xz_canvas c,int x,int y,int w,int h,int r,uint32_t border,uint32_t fill){
    xz_round_rect(c,x,y,w,h,r,15,border);
    xz_round_rect(c,x+1,y+1,w-2,h-2,r-1,15,fill);
}

/* ---- sky, sun, stars ------------------------------------------------- */
static void sky(struct frame *f){
    for(int y=0;y<WATER;y++){
        uint32_t v;
        if(y<120)v=xz_blend(0x02030a,0x0b0e2c,(unsigned)(y*256/120));
        else if(y<236)v=xz_blend(0x0b0e2c,0x1f1543,(unsigned)((y-120)*256/116));
        else v=xz_blend(0x1f1543,0x46214f,(unsigned)((y-236)*256/(WATER-236)));
        f->sky[y]=v;f->sky565[y]=c565(v);
        uint16_t *row=f->c.p+(size_t)y*f->c.stride,pv=f->sky565[y];
        for(int x=0;x<W;x++)row[x]=pv;
    }
    /* Fixed star field: a constant sequence, identical every frame and track. */
    uint32_t r=0x2545f491u;
    int twinkle=f->quiet?-1:(int)(f->g->position/12.0);
    for(int i=0;i<72;i++){
        r=r*1664525u+1013904223u;int x=(int)((r>>8)%W);
        r=r*1664525u+1013904223u;int y=HUD+4+(int)((r>>8)%150);
        unsigned b=60u+(r>>24)%110u;
        if(twinkle>=0&&(i*7+twinkle)%11==0)b=235;
        f->c.p[(size_t)y*f->c.stride+(size_t)x]=c565(xz_blend(f->sky[y],0xdfe6ff,b));
    }
}
static void sun(struct frame *f){
    int cy=WATER-58,R=86;
    const struct xz_rider *g=f->g;
    if(f->has_wave&&!f->quiet){
        /* Breathes with the actual outer band under the playhead. */
        long long i=(long long)floor(g->position)-(long long)g->wave.first;
        if(i>=0&&i<(long long)g->wave.count)R+=clampi((int)(g->wave.bands[i*3]*24u/g->wave.normalization),0,12);
    }
    int Rh=R+22;
    for(int y=clampi(cy-Rh,HUD,WATER);y<WATER&&y<=cy+Rh;y++){
        int dy=y-cy,hh=isqrt_i(Rh*Rh-dy*dy);
        xz_rect(f->c,SUN_X-hh,y,2*hh+1,1,xz_blend(f->sky[y],0xff5f86,38));
    }
    for(int y=clampi(cy-R,HUD,WATER);y<WATER&&y<=cy+R;y++){
        int dy=y-cy;
        if(dy>4&&dy%12<1+dy/26)continue;
        int hh=isqrt_i(R*R-dy*dy);
        xz_rect(f->c,SUN_X-hh,y,2*hh+1,1,xz_blend(0xffe08f,0xff3f86,(unsigned)((y-(cy-R))*255/(2*R))));
    }
}

/* ---- distant ridge: the whole 12 s window, compressed ----------------- */
static void ridge(struct frame *f){
    if(!f->has_wave)return;
    const struct xz_wave_rider_snapshot *w=&f->g->wave;
    uint16_t body[RIDGE];
    for(int d=0;d<RIDGE;d++)body[d]=c565(xz_blend(xz_blend(f->sky[WATER-1-d],0x07081a,200),f->l.band[1],26));
    uint16_t top=c565(xz_blend(0x3a2a5a,f->l.band[2],90));
    int marker=-1,marker_h=0;
    double rel=f->g->position-(double)w->first;
    if(rel>=0&&rel<(double)w->count)marker=(int)(rel*W/w->count);
    for(int x=0;x<W;x++){
        uint32_t i0=(uint32_t)((uint64_t)x*w->count/W),i1=(uint32_t)((uint64_t)(x+1)*w->count/W),best=0;
        if(i1<=i0)i1=i0+1;
        for(uint32_t i=i0;i<i1&&i<w->count;i++){
            const uint8_t *b=w->bands+(size_t)i*3;uint32_t t=(uint32_t)b[0]+b[1]+b[2];
            if(t>best)best=t;
        }
        int h=clampi((int)(best*RIDGE/w->normalization),0,RIDGE);
        if(x==marker)marker_h=h;
        if(!h)continue;
        uint16_t *p=f->c.p+(size_t)(WATER-1)*f->c.stride+(size_t)x;
        for(int d=0;d<h-1;d++,p-=f->c.stride)*p=body[d];
        *p=top;
    }
    if(marker>=0){
        xz_rect(f->c,marker,WATER-marker_h-14,1,12,crest_rgb);
        xz_disc(f->c,marker,WATER-marker_h-16,2,crest_rgb);
    }
}

/* ---- the course ------------------------------------------------------- */
static void course(struct frame *f){
    for(int x=0;x<W;x++)f->crest_y[x]=-1;
    if(!f->has_wave)return;
    const struct xz_rider *g=f->g;const struct xz_wave_rider_snapshot *w=&g->wave;
    uint16_t solid[2][3][8],spray[2][3],edge[2][3],fill[2],crest[2],slip[2];
    for(int pp=0;pp<2;pp++){
        for(int k=0;k<3;k++){
            uint32_t b=f->l.band[k];
            for(int lv=0;lv<8;lv++){
                uint32_t v=xz_blend(0x07081a,b,(unsigned)(118+lv*19));
                solid[pp][k][lv]=c565(pp?xz_blend(v,past_rgb,140):v);
            }
            uint32_t s=xz_blend(0x15102e,b,92),e=xz_blend(b,0xffffff,120);
            spray[pp][k]=c565(pp?xz_blend(s,past_rgb,140):s);
            edge[pp][k]=c565(pp?xz_blend(e,past_rgb,150):e);
        }
        uint32_t fl=xz_blend(0x120d28,crest_rgb,30),cr=crest_rgb;
        uint32_t sl=g->slipped?xz_blend(f->l.band[1],0xffffff,120):f->l.band[1];
        fill[pp]=c565(pp?xz_blend(fl,past_rgb,120):fl);
        crest[pp]=c565(pp?xz_blend(cr,past_rgb,150):cr);
        slip[pp]=c565(pp?xz_blend(sl,past_rgb,150):sl);
    }
    uint8_t lvl[EXTENT];
    for(int d=0;d<EXTENT;d++)lvl[d]=(uint8_t)(d*8/(EXTENT+1));
    const size_t stride=f->c.stride;
    const uint32_t norm=w->normalization;
    int prev=-1;
    for(int x=0;x<W;x++){
        long long s=f->s0+x,idx=s-(long long)w->first;
        if(idx<0||idx>=(long long)w->count){prev=-1;continue;}
        const uint8_t *b=w->bands+(size_t)idx*3;
        int ht=clampi((int)(((uint32_t)b[0]+b[1]+b[2])*EXTENT/norm),0,EXTENT);
        int hi=clampi((int)(((uint32_t)b[1]+b[2])*EXTENT/norm),0,EXTENT);
        int hc=clampi((int)((uint32_t)b[2]*EXTENT/norm),0,EXTENT);
        int sw=clampi((int)g->course.swell[idx],0,EXTENT);
        int pp=x<PH,top=ht>sw?ht:sw;
        uint16_t *p=f->c.p+(size_t)(WATER-1)*stride+(size_t)x;
        for(int d=0;d<top;d++,p-=stride){
            if(d>=ht){*p=fill[pp];continue;}
            int k=d<hc?2:d<hi?1:0,bound=k==2?hc:k==1?hi:ht;
            *p=d+1==bound?edge[pp][k]:d<sw?solid[pp][k][lvl[d]]:spray[pp][k];
        }
        if(sw<=0){prev=-1;continue;}
        /* The collision crest: the line the rider must stay above. */
        int yc=WATER-sw;
        f->crest_y[x]=yc;
        vrun(f,x,yc-1,yc+1,crest[pp]);
        if(prev>=0&&(prev-yc>1||yc-prev>1))vrun(f,x,prev<yc?prev:yc,prev<yc?yc:prev,crest[pp]);
        prev=yc;
        if(g->course.slip[idx]&&((s>>2)&3)!=3)vrun(f,x,yc-9,yc-7,slip[pp]);
    }
}
static int crest_at(const struct xz_rider *g,uint32_t at){
    long long i=(long long)at-(long long)g->wave.first;
    if(i<0||i>=(long long)g->wave.count)return 0;
    return clampi((int)g->course.swell[i],0,EXTENT);
}

static void spike(struct frame *f,int x,int base,int top,int pp,int warn){
    int yt=WATER-top,yb=WATER-base;
    if(yb-yt<10)yt=yb-10;
    int span=yb-yt;
    uint32_t b=f->l.band[0];
    uint32_t lit=xz_blend(b,0xffffff,50),shade=xz_blend(b,0x05060f,110),hl=0xffffff;
    if(pp){lit=xz_blend(lit,past_rgb,160);shade=xz_blend(shade,past_rgb,160);hl=xz_blend(hl,past_rgb,170);}
    for(int y=yt;y<=yb;y++){
        int hw=(y-yt)*7/span;
        xz_rect(f->c,x-hw,y,hw,1,lit);
        xz_rect(f->c,x,y,hw+1,1,shade);
        xz_rect(f->c,x-hw,y,1,1,hl);
    }
    if(warn)tri(f->c,x,yt-5,x-5,yt-12,x+5,yt-12,danger);
}
static void gem(struct frame *f,int x,int alt,int pp,int phase,int aligned){
    int cy=WATER-alt,r=pp?5:8;
    static const int turn[4]={8,6,3,6};
    int k=f->quiet?8:turn[phase&3];
    uint32_t b=f->l.band[2],hi=xz_blend(b,0xffffff,110);
    if(pp){b=xz_blend(b,past_rgb,160);hi=xz_blend(hi,past_rgb,160);}
    else xz_ring(f->c,x,cy,aligned?14:12,1,aligned?0xffffff:xz_blend(night,b,110));
    for(int dy=-r;dy<=r;dy++){
        int hw=(r-(dy<0?-dy:dy))*k/8;
        xz_rect(f->c,x-hw,cy+dy,2*hw+1,1,dy<0?hi:b);
    }
    if(!pp)xz_rect(f->c,x-1,cy-2,2,2,0xffffff);
}
static void objects(struct frame *f){
    if(!f->has_wave)return;
    const struct xz_rider *g=f->g;
    unsigned n=g->course.object_count<XZ_RIDER_OBJECTS?g->course.object_count:XZ_RIDER_OBJECTS;
    int phase=(int)(g->position/5.0);
    for(unsigned i=0;i<n;i++){
        const struct xz_rider_object *o=&g->course.objects[i];
        double rel=(double)o->at-g->position;
        int x=PH+(int)floor(rel+.5);
        if(x<-16||x>W+16)continue;
        int pp=x<PH,alt=clampi((int)o->altitude,0,EXTENT+60);
        int ahead=!pp&&x<PH+150;
        if(o->kind==XZ_RIDER_SPIKE)spike(f,x,crest_at(g,o->at),alt,pp,ahead&&g->altitude<o->altitude);
        else gem(f,x,alt,pp,phase+(int)i,ahead&&fabsf(g->altitude-o->altitude)<=14.f);
    }
}

/* ---- rider ------------------------------------------------------------ */
static void trail(struct frame *f){
    const struct xz_rider *g=f->g;
    unsigned n=g->trail_count<48?g->trail_count:48;
    int nx=PH,ny=f->rider_y;
    for(unsigned j=0;j<n;j++){
        unsigned k=(g->trail_head+48u-1u-j)%48u;
        int x=PH+(int)floor(g->trail_at[k]-g->position+.5),y=WATER-(int)g->trail_alt[k];
        if(x>nx)x=nx;
        uint32_t col=xz_blend(night,f->l.band[2],(unsigned)(250-j*5));
        int span=nx-x;
        for(int xi=x;xi<=nx;xi++){
            int yi=span?y+(ny-y)*(xi-x)/span:ny;
            if(xi<0||xi>=W)continue;
            xz_rect(f->c,xi,yi-1,1,3,col);
            if(j<12)xz_rect(f->c,xi,yi,1,1,0xffffff);
        }
        nx=x;ny=y;
        if(nx<0)break;
    }
}
static void rider_body(struct frame *f,int x,int y,int ghost){
    struct xz_canvas c=f->c;
    static const int off[4][2]={{-2,0},{2,0},{0,-2},{0,2}};
    for(int i=0;i<4;i++){
        int dx=off[i][0],dy=off[i][1];
        tri(c,x+15+dx,y+dy,x-11+dx,y-10+dy,x-4+dx,y-1+dy,0x020308);
        tri(c,x+15+dx,y+dy,x-4+dx,y-1+dy,x-11+dx,y+8+dy,0x020308);
    }
    uint32_t top=0xf5f7ff,low=xz_blend(f->l.band[2],0xffffff,70),stripe=f->l.band[0];
    if(ghost){top=xz_blend(top,night,150);low=xz_blend(low,night,150);stripe=xz_blend(stripe,night,150);}
    tri(c,x+15,y,x-11,y-10,x-4,y-1,top);
    tri(c,x+15,y,x-4,y-1,x-11,y+8,low);
    xz_rect(c,x-4,y-1,17,1,stripe);
    tri(c,x+9,y-1,x+2,y-5,x+2,y-1,0x0b1030);
    if(!ghost){
        int flick=f->quiet?0:((int)(f->g->position/2.0)&1);
        xz_disc(c,x-13,y-1,3+flick,stripe);
        xz_disc(c,x-13,y-1,1,0xffffff);
    }
}
static void jog_glyph(struct xz_canvas c,int cx,int cy,int r,int dir,uint32_t col,uint32_t arrow){
    xz_ring(c,cx,cy,r,2,col);
    int a=r/2+1;
    if(dir>=0)tri(c,cx,cy-a-1,cx-a,cy+a/2,cx+a,cy+a/2,arrow);
    if(dir<=0)tri(c,cx,cy+a+1,cx-a,cy-a/2,cx+a,cy-a/2,arrow);
}
static int jogging(const struct xz_rider *g){return g->jog_ms&&(uint32_t)(g->last_ms-g->jog_ms)<400u;}
static void rider(struct frame *f){
    const struct xz_rider *g=f->g;
    int ghost=g->position<g->shield_until&&((int)(g->position/6.0)&1);
    if(!f->quiet&&g->slipped){
        /* Spray off the crest while skimming the harmonic line. */
        for(int k=0;k<5;k++){
            int x=PH-8-k*7-((int)g->position&3);
            if(x>=0&&f->crest_y[x]>0)xz_rect(f->c,x,f->crest_y[x]-3-(k&1)*2,3,1,xz_blend(f->l.band[1],0xffffff,(unsigned)(200-k*30)));
        }
    }
    rider_body(f,PH,f->rider_y,ghost);
    if(jogging(g))jog_glyph(f->c,PH+32,f->rider_y-26,9,g->jog_direction>0?1:-1,ink,f->l.band[2]);
}
static void feedback(struct frame *f){
    const struct xz_rider *g=f->g;
    if(!g->feedback||g->feedback_until<=g->position)return;
    double left=g->feedback_until-g->position;
    uint32_t col=ink;
    if(strstr(g->feedback,"PERFECT")||strstr(g->feedback,"CATCH"))col=f->l.band[2];
    else if(strstr(g->feedback,"SCRAPE"))col=danger;
    if(!f->quiet&&col!=danger){
        int r=14+(int)(60-(left<60?left:60))/2;
        uint32_t rc=xz_blend(night,col,(unsigned)(90+left*2>250?250:90+left*2));
        xz_ring(f->c,PH,f->rider_y,r,1,rc);
        for(int i=0;i<8;i++)xz_rect(f->c,PH+xz_cos1024(i*45)*(r+6)/1024-1,f->rider_y+xz_sin1024(i*45)*(r+6)/1024-1,3,3,rc);
    }
    int tw=text_w(g->feedback,2);
    int x=clampi(PH-tw/2,8,W-8-tw),y=clampi(f->rider_y-48,HUD+8,WATER-20);
    xz_round_rect(f->c,x-10,y-7,tw+20,28,10,15,xz_blend(night,col,40));
    text(f->c,x,y,g->feedback,2,col);
}

/* ---- water ------------------------------------------------------------ */
static void reflection(struct frame *f){
    static const int wobble[16]={0,1,1,2,1,1,0,-1,-1,-2,-1,-1,0,0,1,0};
    int t=f->quiet?0:(int)(f->g->position/4.0);
    const size_t stride=f->c.stride;
    for(int d=0;d<H-WATER;d++){
        int y=WATER+d,sy=WATER-1-d;
        if(sy<0)break;
        int shift=f->quiet?0:wobble[(d+t)&15]*(1+d/56);
        const uint16_t *src=f->c.p+(size_t)sy*stride;
        uint16_t *dst=f->c.p+(size_t)y*stride;
        int depth=d<24?0:d<70?1:2;
        if((d&3)==3&&depth<2)depth++;
        for(int x=0;x<W;x++){
            int sx=clampi(x+shift,0,W-1);
            uint16_t v=src[sx];
            dst[x]=depth==0?half565(v):depth==1?(uint16_t)(quarter565(v)+quarter565(half565(v))):quarter565(v);
        }
    }
    uint16_t line=c565(xz_blend(f->sky[WATER-1],0xffffff,40));
    uint16_t *row=f->c.p+(size_t)WATER*stride;
    for(int x=0;x<W;x++)if(!(x%5==4))row[x]=line;
}

static void stage(struct frame *f,int play){
    sky(f);sun(f);ridge(f);course(f);
    if(f->has_wave&&play){
        /* Playhead beam: where now is. */
        uint16_t beam=c565(0x3c4468);
        for(int y=HUD+6;y<WATER-2;y+=6)vrun(f,PH,y,y+3,beam);
        objects(f);trail(f);rider(f);
    }
    reflection(f);
    if(f->has_wave&&play)feedback(f);
}

/* ---- HUD -------------------------------------------------------------- */
static int chip_w(const struct look *l){
    int w=20+text_w(l->kind,1);
    for(int i=0;i<3;i++)w+=14+text_w(l->brief[i],1);
    return w;
}
static void chip(struct frame *f,int x,int y){
    const struct look *l=&f->l;
    int w=chip_w(l);
    ring_panel(f->c,x,y,w,24,9,rim,panel);
    text(f->c,x+10,y+9,l->kind,1,ink);
    int cx=x+10+text_w(l->kind,1)+10;
    for(int i=0;i<3;i++){
        xz_disc(f->c,cx+3,y+12,3,l->band[i]);
        text(f->c,cx+9,y+9,l->brief[i],1,l->band[i]);
        cx+=14+text_w(l->brief[i],1);
    }
}
static void clip_title(char *out,size_t n,const char *s,size_t max){
    size_t i=0;
    if(max+1>n)max=n-1;
    for(;s[i]&&i<max;i++)out[i]=(s[i]>=32&&s[i]<127)||(unsigned char)s[i]>=0xC0?s[i]:' ';
    if(s[i]&&i>3){out[i-3]=out[i-2]=out[i-1]='.';}
    out[i]=0;
}
static void deck_button(struct frame *f){
    ring_panel(f->c,692,6,100,36,10,rim,0x161c36);
    text(f->c,692+(100-text_w("DECK",2))/2,14,"DECK",2,ink);
}
static void hud(struct frame *f,int ride,const char *right){
    const struct xz_rider *g=f->g;
    text_lift(f->c,16,12,"WAVE RIDER",2,ink);
    int x=148;
    if(f->has_wave){chip(f,x,7);x+=chip_w(&f->l)+12;}
    char s[64],t[40];
    snprintf(s,sizeof(s),"DECK %u",g->deck+1);
    text(f->c,x,16,s,1,dim);x+=text_w(s,1)+10;
    if(ride&&g->wave.title[0]){
        int room=(520-x)/6;
        if(room>4){clip_title(t,sizeof(t),g->wave.title,(size_t)room);text(f->c,x,16,t,1,faint);}
    }
    if(ride){
        snprintf(s,sizeof(s),"%u",g->score);
        text_lift(f->c,604-text_w(s,2),12,s,2,ink);
        if(g->combo>1){snprintf(s,sizeof(s),"X%u",g->combo);text_lift(f->c,614,12,s,2,f->l.band[2]);}
        if(g->slipped)text_lift(f->c,604-text_w("SKIM X2",1),44,"SKIM X2",1,f->l.band[1]);
    }else if(right)text_lift(f->c,672-text_w(right,1),16,right,1,dim);
    deck_button(f);
    /* Hairline doubles as track progress. */
    xz_rect(f->c,0,HUD,W,1,0x1a1f3a);
    if(f->has_wave&&g->wave.total){
        double fr=g->position/(double)g->wave.total;
        int px=clampi((int)(fr*W),0,W);
        xz_rect(f->c,0,HUD-1,px,2,xz_blend(night,f->l.band[2],170));
    }
}
static void legend(struct frame *f,int y){
    static const char *verb[3]={" = HOP"," = SKIM"," = CATCH"};
    int total=0;
    for(int i=0;i<3;i++)total+=18+text_w(f->l.name[i],1)+text_w(verb[i],1);
    total+=2*28;
    int x=400-total/2;
    for(int i=0;i<3;i++){
        uint32_t b=f->l.band[i];
        if(i==0)tri(f->c,x+6,y-2,x+1,y+7,x+11,y+7,b);
        else if(i==1){xz_rect(f->c,x,y+3,4,2,b);xz_rect(f->c,x+7,y+3,4,2,b);}
        else {tri(f->c,x+6,y-2,x+1,y+3,x+11,y+3,b);tri(f->c,x+6,y+8,x+1,y+3,x+11,y+3,b);}
        x+=18;
        text(f->c,x,y,f->l.name[i],1,b);x+=text_w(f->l.name[i],1);
        text(f->c,x,y,verb[i],1,ink);x+=text_w(verb[i],1)+28;
    }
}
static void controls(struct frame *f,int y){text_mid(f->c,400,y,"CUE = HOP     JOG = ALTITUDE     PLAY = MUSIC",1,dim);}

/* ---- buttons and cards ------------------------------------------------ */
enum { PRIMARY, SECONDARY, DISABLED, CHOSEN };
static void button(struct frame *f,int x0,int y0,int x1,int y1,const char *label,int style){
    int w=x1-x0,h=y1-y0,dot=h>=60&&text_w(label,3)+40<=w?3:2;
    uint32_t col=ink;
    if(style==PRIMARY){
        xz_round_rect(f->c,x0-3,y0-3,w+6,h+6,17,15,xz_blend(night,accent,90));
        xz_round_rect(f->c,x0,y0,w,h,14,15,accent);col=night;
    }else if(style==CHOSEN){
        ring_panel(f->c,x0-2,y0-2,w+4,h+4,15,accent,0x10283a);
    }else if(style==SECONDARY)ring_panel(f->c,x0,y0,w,h,12,rim,panel);
    else{xz_round_rect(f->c,x0,y0,w,h,12,15,0x161b30);col=faint;}
    text(f->c,x0+(w-text_w(label,dot))/2,y0+(h-7*dot)/2,label,dot,col);
}
static void card(struct frame *f,int x0,int y0,int x1,int y1){ring_panel(f->c,x0,y0,x1-x0,y1-y0,16,rim,panel);}

/* Whole window, compressed: the waveform the game just found. */
static void preview(struct frame *f,int x0,int y0,int x1,int y1){
    const struct xz_wave_rider_snapshot *w=&f->g->wave;
    int width=x1-x0,base=y1-10,ext=y1-y0-24;
    uint16_t col[3][2];
    for(int k=0;k<3;k++){col[k][0]=c565(xz_blend(0x07081a,f->l.band[k],200));col[k][1]=c565(xz_blend(f->l.band[k],0xffffff,110));}
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
        vrun(f,x0+x,base-hc,base,col[2][0]);
        vrun(f,x0+x,base-hi,base-hc,col[1][0]);
        vrun(f,x0+x,base-ht,base-hi,col[0][0]);
        if(ht)vrun(f,x0+x,base-ht,base-ht+1,col[0][1]);
    }
    double rel=f->g->position-(double)w->first;
    if(rel>=0&&rel<(double)w->count){
        int mx=x0+(int)(rel*width/w->count);
        xz_rect(f->c,mx,y0+8,1,base-y0-8,crest_rgb);
    }
}

static void setup_step(struct frame *f){
    const struct xz_rider *g=f->g;
    char s[80],t[48];
    switch(g->setup_step){
    case 0:
        stage(f,0);hud(f,0,"SET UP");
        text_mid(f->c,400,88,"WAVE RIDER",6,ink);
        text_mid(f->c,400,152,"RIDE THE REAL WAVEFORM OF YOUR TRACK",2,dim);
        text_mid(f->c,400,176,g->wave.kind?"DRUMS HOP  /  HARMONICS SKIM  /  VOCALS CATCH":"LOW HOP  /  MID SKIM  /  HIGH CATCH",1,faint);
        button(f,160,330,640,410,"SET UP WAVE RIDER",PRIMARY);
        break;
    case 1:
        stage(f,0);hud(f,0,"SETUP 1 / 3");
        text_mid(f->c,400,92,"CHOOSE THE DECK YOU WILL PLAY",2,ink);
        for(int d=0;d<2;d++){
            int x0=d?420:80,chosen=g->deck==(unsigned)d;
            snprintf(s,sizeof(s),"DECK %d",d+1);
            button(f,x0,150,x0+300,224,"",chosen?CHOSEN:SECONDARY);
            text(f->c,x0+(300-text_w(s,3))/2,166,s,3,ink);
            const char *sub=chosen?"FOLLOWING THIS TRACK":"TAP TO CHOOSE";
            text(f->c,x0+(300-text_w(sub,1))/2,202,sub,1,chosen?accent:faint);
        }
        text_mid(f->c,400,252,"PLAY, TEMPO AND THE OTHER DECK STAY YOURS",1,faint);
        button(f,160,330,640,410,"NEXT",PRIMARY);
        break;
    case 2:{
        stage(f,0);hud(f,0,"SETUP 2 / 3");
        card(f,80,96,720,306);
        if(f->has_wave&&g->valid){
            chip(f,400-chip_w(&f->l)/2,108);
            preview(f,96,140,704,270);
            clip_title(t,sizeof(t),g->wave.title[0]?g->wave.title:"UNTITLED",40);
            snprintf(s,sizeof(s),"DECK %u  /  %s",g->deck+1,t);
            text_mid(f->c,400,282,s,1,dim);
            button(f,160,330,640,410,"VERIFY CONTROLS",PRIMARY);
        }else if(g->wave.loading){
            static const char *dots[4]={"",".","..","..."};
            snprintf(s,sizeof(s),"READING DECK %u WAVEFORM%s",g->deck+1,dots[(g->last_ms/300u)&3u]);
            text_mid(f->c,400,190,s,2,dim);
            button(f,160,330,640,410,"DETECTING",DISABLED);
        }else{
            const char *why="NO WAVEFORM FOR THIS DECK YET";
            if(g->wave.error==XZ_WAVE_RIDER_NO_TRACK)why="NO TRACK LOADED ON THIS DECK";
            else if(g->wave.error==XZ_WAVE_RIDER_NO_ANALYSIS)why="THIS TRACK HAS NO WAVEFORM ANALYSIS";
            else if(g->wave.error==XZ_WAVE_RIDER_POSITION_UNAVAILABLE)why="THIS DECK'S PLAY POSITION CAN'T BE READ";
            snprintf(s,sizeof(s),"DECK %u",g->deck+1);
            text_mid(f->c,400,160,s,2,faint);
            text_mid(f->c,400,196,why,2,ink);
            int position_error=g->wave.error==XZ_WAVE_RIDER_POSITION_UNAVAILABLE;
            text_mid(f->c,400,232,position_error?"RETURN TO THE DECK, THEN REOPEN TO RETRY":"LOAD AN ANALYSED TRACK, THEN REOPEN WAVE RIDER",1,dim);
            button(f,160,330,640,410,position_error?"BACK TO DECK":"LOAD A TRACK",PRIMARY);
        }
        button(f,24,435,200,475,"CHOOSE DECK",SECONDARY);
        break;}
    default:{
        stage(f,1);hud(f,0,"SETUP 3 / 3");
        int jd=jogging(g)?(g->jog_direction>0?1:-1):0;
        uint32_t jb=g->jog_seen?ready:rim,cb=g->cue_seen?ready:rim;
        ring_panel(f->c,80,52,300,64,12,jb,panel);
        jog_glyph(f->c,114,84,15,jd,g->jog_seen?ready:ink,f->l.band[2]);
        text(f->c,146,66,"TURN JOG",2,ink);
        text(f->c,146,92,g->jog_seen?"RIDER FOLLOWS THE JOG":"RAISE AND LOWER THE RIDER",1,g->jog_seen?ready:dim);
        ring_panel(f->c,420,52,300,64,12,cb,panel);
        xz_disc(f->c,454,84,16,g->cue_seen?ready:0xf2aa3c);
        text(f->c,454-text_w("CUE",1)/2,81,"CUE",1,night);
        text(f->c,486,66,"TAP CUE",2,ink);
        text(f->c,486,92,g->cue_seen?"HOP WORKS":"THE RIDER HOPS",1,g->cue_seen?ready:dim);
        int go=g->valid&&g->jog_seen&&g->cue_seen;
        button(f,160,350,640,418,"RIDE THIS TRACK",go?PRIMARY:DISABLED);
        button(f,220,434,580,470,"USE DEFAULTS",SECONDARY);
        break;}
    }
}

static void title_card(struct frame *f){
    const struct xz_rider *g=f->g;
    char s[80],t[48];
    stage(f,1);hud(f,0,NULL);
    text_mid(f->c,400,64,"WAVE RIDER",6,ink);
    if(f->has_wave)chip(f,400-chip_w(&f->l)/2,118);
    clip_title(t,sizeof(t),g->wave.title[0]?g->wave.title:"UNTITLED",36);
    snprintf(s,sizeof(s),"DECK %u  /  %s",g->deck+1,t);
    text_mid(f->c,400,154,s,2,ink);
    button(f,160,330,640,410,"RIDE THIS TRACK",g->valid?PRIMARY:DISABLED);
    legend(f,428);controls(f,452);
}

static void result_card(struct frame *f){
    const struct xz_rider *g=f->g;
    char s[96];
    stage(f,1);hud(f,0,NULL);
    card(f,190,48,610,318);
    unsigned total=g->spike_total+g->gem_total,got=g->hops+g->catches;
    unsigned pct=total?got*100u/total:0;
    const char *grade=pct>=95&&!g->scrapes?"S":pct>=85?"A":pct>=70?"B":pct>=50?"C":"D";
    text_mid(f->c,400,62,"RUN COMPLETE",1,dim);
    text_mid(f->c,400,80,grade,8,f->l.band[2]);
    snprintf(s,sizeof(s),"%u",g->score);
    text_mid(f->c,400,148,s,3,ink);
    snprintf(s,sizeof(s),"%s  %u HOPPED  /  %u PERFECT  /  %u",f->l.name[0],g->hops,g->perfects,g->spike_total);
    text_mid(f->c,400,190,s,1,f->l.band[0]);
    snprintf(s,sizeof(s),"%s  %u CAUGHT  /  %u",f->l.name[2],g->catches,g->gem_total);
    text_mid(f->c,400,208,s,1,f->l.band[2]);
    snprintf(s,sizeof(s),"BEST COMBO %u   /   SCRAPES %u",g->best,g->scrapes);
    text_mid(f->c,400,226,s,1,ink);
    snprintf(s,sizeof(s),"COURSE %04X  /  SAME TRACK, SAME COURSE",(unsigned)(g->wave.track_hash&0xffffu));
    text_mid(f->c,400,252,s,1,faint);
    text_mid(f->c,400,282,f->l.kind,1,dim);
    button(f,160,330,640,410,g->wave.total>1&&g->position>=g->wave.total-1?"BACK TO DECK":"RIDE AGAIN",g->valid?PRIMARY:DISABLED);
}

int xz_rider_render(const struct xz_rider *g,uint16_t *pixels,size_t count,size_t stride){
    if(!g||!pixels||stride<W||stride>count/H)return 0;
    static struct frame f; /* render runs on one thread; keeps 4 KB off the stack */
    f.c=(struct xz_canvas){pixels,stride,W,H,0};
    f.g=g;
    f.l=look_of(g->wave.kind);
    f.quiet=!!g->quiet;
    f.has_wave=g->valid&&g->wave.valid&&g->wave.count&&g->wave.count<=XZ_WAVE_RIDER_SAMPLES&&g->wave.normalization;
    f.s0=(long long)floor(g->position)-PH;
    f.rider_y=clampi(WATER-(int)floorf(g->altitude+.5f),HUD+14,WATER-2);
    switch(g->phase){
    case XZ_RIDER_SETUP:setup_step(&f);break;
    case XZ_RIDER_TITLE:title_card(&f);break;
    case XZ_RIDER_RESULT:result_card(&f);break;
    default:
        stage(&f,1);hud(&f,1,NULL);legend(&f,446);controls(&f,464);
        if(g->phase==XZ_RIDER_HOLD){
            const char *m="DECK PAUSED  /  PLAY RESUMES";
            int w=text_w(m,2)+40;
            ring_panel(f.c,400-w/2,58,w,34,12,rim,panel);
            text(f.c,400-text_w(m,2)/2,68,m,2,ink);
        }
        break;
    }
    return 1;
}
