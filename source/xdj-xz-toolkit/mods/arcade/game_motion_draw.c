/* SPDX-License-Identifier: MIT */
/* Shared bounded drawing for STATIC SURFER and WAVE RIDER (VJ OpticMystic).
   Spans, convex polygons, one inverse-mapped sprite routine and two
   material effects (crystal shatter, surface ripple). No heap, no IO,
   trig only per object or per piece, never per pixel. */
#include "game_motion_draw.h"
#include "game_feel_art.h"
#include "game_font.h"
#include "../ui/ui_skin.h"
#include <math.h>

static int imin(int a,int b){return a<b?a:b;}
static int imax(int a,int b){return a>b?a:b;}
static int iabs(int v){return v<0?-v:v;}
static float clampf(float v,float a,float b){if(!(v>a))return a;return v>b?b:v;}
static int ri(float v){return (int)floorf(v+.5f);}

uint16_t gm_c565(uint32_t c){return (uint16_t)(((c>>8)&0xF800u)|((c>>5)&0x07E0u)|((c>>3)&0x001Fu));}
uint16_t gm_mix(uint16_t d,uint16_t s,unsigned a){
    if(a>=256u)return s;
    unsigned n=(a+4u)>>3,inv=32u-n;
    unsigned rb=(((d&0xf81fu)*inv+(s&0xf81fu)*n)>>5)&0xf81fu;
    unsigned gr=(((d&0x07e0u)*inv+(s&0x07e0u)*n)>>5)&0x07e0u;
    return (uint16_t)(rb|gr);
}
uint16_t gm_add(uint16_t a,uint16_t b){
    uint32_t r=(uint32_t)(a&0xF800u)+(b&0xF800u),g=(uint32_t)(a&0x07E0u)+(b&0x07E0u),bl=(uint32_t)(a&0x1Fu)+(b&0x1Fu);
    if(r>0xF800u)r=0xF800u;
    if(g>0x07E0u)g=0x07E0u;
    if(bl>0x1Fu)bl=0x1Fu;
    return (uint16_t)(r|g|bl);
}
uint16_t gm_scale(uint16_t v,unsigned k){
    if(k>=16u)return v;
    return (uint16_t)(((((uint32_t)(v&0xf81fu))*k)>>4&0xf81fu)|((((uint32_t)(v&0x07e0u))*k)>>4&0x07e0u));
}
uint32_t gm_hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
int gm_isqrt(int v){
    if(v<=0)return 0;
    int r=(int)sqrtf((float)v);
    while(r*r>v)r--;
    while((r+1)*(r+1)<=v)r++;
    return r;
}

static int clipspan(const struct gm_tgt *t,int *xa,int *xb,int y){
    if(y<t->y0||y>=t->y1)return 0;
    if(*xa>*xb){int s=*xa;*xa=*xb;*xb=s;}
    if(*xa<t->x0)*xa=t->x0;
    if(*xb>=t->x1)*xb=t->x1-1;
    return *xa<=*xb;
}
void gm_span(const struct gm_tgt *t,int xa,int xb,int y,uint16_t c){
    if(!clipspan(t,&xa,&xb,y))return;
    uint16_t *r=t->p+(size_t)y*t->stride;
    for(int x=xa;x<=xb;x++)r[x]=c;
}
void gm_span_mix(const struct gm_tgt *t,int xa,int xb,int y,uint16_t c,unsigned a){
    if(!a)return;
    if(a>=256u){gm_span(t,xa,xb,y,c);return;}
    if(!clipspan(t,&xa,&xb,y))return;
    uint16_t *r=t->p+(size_t)y*t->stride;
    for(int x=xa;x<=xb;x++)r[x]=gm_mix(r[x],c,a);
}
void gm_span_add(const struct gm_tgt *t,int xa,int xb,int y,uint16_t c){
    if(!c||!clipspan(t,&xa,&xb,y))return;
    uint16_t *r=t->p+(size_t)y*t->stride;
    for(int x=xa;x<=xb;x++)r[x]=gm_add(r[x],c);
}
void gm_rect(const struct gm_tgt *t,int x,int y,int w,int h,uint16_t c){
    if(w<1||h<1)return;
    for(int yy=imax(y,t->y0);yy<imin(y+h,t->y1);yy++)gm_span(t,x,x+w-1,yy,c);
}
void gm_rect_mix(const struct gm_tgt *t,int x,int y,int w,int h,uint16_t c,unsigned a){
    if(w<1||h<1||!a)return;
    for(int yy=imax(y,t->y0);yy<imin(y+h,t->y1);yy++)gm_span_mix(t,x,x+w-1,yy,c,a);
}
void gm_line(const struct gm_tgt *t,int x0,int y0,int x1,int y1,int w,uint16_t c,unsigned a){
    if(w<1)w=1;
    if(imax(x0,x1)+w<t->x0||imin(x0,x1)-w>=t->x1||imax(y0,y1)+w<t->y0||imin(y0,y1)-w>=t->y1)return;
    int dx=iabs(x1-x0),dy=-iabs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy,h=w/2,n=0;
    for(;;){
        gm_rect_mix(t,x0-h,y0-h,w,w,c,a);
        if((x0==x1&&y0==y1)||++n>1600)break;
        int e2=2*err;
        if(e2>=dy){err+=dy;x0+=sx;}
        if(e2<=dx){err+=dx;y0+=sy;}
    }
}
void gm_poly(const struct gm_tgt *t,const int *xs,const int *ys,int n,uint16_t c,unsigned a){
    if(n<3||n>8||!a)return;
    int ya=ys[0],yb=ys[0];
    for(int i=1;i<n;i++){ya=imin(ya,ys[i]);yb=imax(yb,ys[i]);}
    ya=imax(ya,t->y0);yb=imin(yb,t->y1-1);
    for(int y=ya;y<=yb;y++){
        int lo=1000000,hi=-1000000;
        for(int i=0;i<n;i++){
            int j=i+1<n?i+1:0,y0=ys[i],y1=ys[j];
            if(y0==y1){if(y==y0){lo=imin(lo,imin(xs[i],xs[j]));hi=imax(hi,imax(xs[i],xs[j]));}continue;}
            if((y<y0&&y<y1)||(y>y0&&y>y1))continue;
            int x=xs[i]+(y-y0)*(xs[j]-xs[i])/(y1-y0);
            lo=imin(lo,x);hi=imax(hi,x);
        }
        if(lo<=hi)gm_span_mix(t,lo,hi,y,c,a);
    }
}
static void disc_add(const struct gm_tgt *t,int cx,int cy,int r,uint16_t c){
    if(r<1){gm_span_add(t,cx,cx,cy,c);return;}
    for(int dy=-r;dy<=r;dy++){int hw=gm_isqrt(r*r-dy*dy);gm_span_add(t,cx-hw,cx+hw,cy+dy,c);}
}
void gm_glow(const struct gm_tgt *t,int cx,int cy,int r,uint16_t c,unsigned a){
    if(r<1||!a)return;
    if(r>160)r=160;
    uint16_t k=gm_mix(0,c,a/3u);
    disc_add(t,cx,cy,r,k);
    disc_add(t,cx,cy,r*3/5,k);
    disc_add(t,cx,cy,r/4,gm_mix(0,c,a/2u));
}

void gm_blit(const struct gm_tgt *t,const struct gm_sprite *s,int x,int y){
    if(!s->rgb||!s->alpha||s->size<2||!(s->sx>.02f)||!(s->sy>.02f)||!s->amul)return;
    float S=s->sn,C=s->cs,fl=s->flip?-1.f:1.f;
    /* forward map the four source corners for a tight box */
    float us[2]={-s->au,(float)s->size-s->au},vs[2]={-s->av,(float)s->size-s->av};
    float bx0=1e9f,bx1=-1e9f,by0=1e9f,by1=-1e9f;
    for(int i=0;i<2;i++)for(int j=0;j<2;j++){
        float lx=us[i]*s->sx*fl,ly=vs[j]*s->sy,X=C*lx-S*ly,Y=S*lx+C*ly;
        if(X<bx0)bx0=X;
        if(X>bx1)bx1=X;
        if(Y<by0)by0=Y;
        if(Y>by1)by1=Y;
    }
    int xa=imax(x+(int)floorf(bx0)-1,t->x0),xb=imin(x+(int)ceilf(bx1)+1,t->x1-1);
    int ya=imax(y+(int)floorf(by0)-1,t->y0),yb=imin(y+(int)ceilf(by1)+1,t->y1-1);
    if(xa>xb||ya>yb)return;
    int32_t dudx=(int32_t)(C/s->sx*fl*65536.f),dudy=(int32_t)(S/s->sx*fl*65536.f);
    int32_t dvdx=(int32_t)(-S/s->sy*65536.f),dvdy=(int32_t)(C/s->sy*65536.f);
    int32_t u0=(int32_t)(s->au*65536.f),v0=(int32_t)(s->av*65536.f);
    unsigned N=(unsigned)s->size,vlo=(unsigned)imax(0,s->vlo),vhi=(unsigned)imin(s->size,s->vhi>0?s->vhi:s->size);
    for(int yy=ya;yy<=yb;yy++){
        int dy=yy-y,dx=xa-x;
        int32_t u=u0+dx*dudx+dy*dudy,v=v0+dx*dvdx+dy*dvdy;
        uint16_t *row=t->p+(size_t)yy*t->stride;
        for(int xx=xa;xx<=xb;xx++,u+=dudx,v+=dvdx){
            if(u<0||v<0)continue;
            unsigned iu=(unsigned)(u>>16),iv=(unsigned)(v>>16);
            if(iu>=N||iv<vlo||iv>=vhi)continue;
            unsigned idx=iv*N+iu,a=s->alpha[idx];
            uint16_t sp;
            if(s->rgb2){
                unsigned w1=s->blend2>256u?256u:s->blend2,p0=a*(256u-w1),p1=(unsigned)s->alpha2[idx]*w1,ps=p0+p1;
                a=ps>>8;
                if(a<8u)continue;
                if(!p1)sp=s->rgb[idx];
                else if(!p0)sp=s->rgb2[idx];
                else{
                    unsigned c0=s->rgb[idx],c1=s->rgb2[idx];
                    unsigned r=(((c0>>11)&31u)*p0+((c1>>11)&31u)*p1)/ps;
                    unsigned gg=(((c0>>5)&63u)*p0+((c1>>5)&63u)*p1)/ps;
                    unsigned bb=((c0&31u)*p0+(c1&31u)*p1)/ps;
                    sp=(uint16_t)((r<<11)|(gg<<5)|bb);
                }
            }else{
                if(a<8u)continue;
                sp=s->rgb[idx];
            }
            if(s->amul<256u)a=a*s->amul>>8;
            if(s->tinta)sp=gm_mix(sp,s->tint,s->tinta);
            row[xx]=a>=248u?sp:gm_mix(row[xx],sp,a+1u);
        }
    }
}

void gm_panorama(const struct gm_tgt *t,const uint16_t *A,const uint16_t *B,unsigned mix16,
                 int srcw,int srch,int src_y0,int src_h,int y0,int y1,int scroll,const uint8_t *gain){
    static const unsigned char bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    if(!A||srcw<8||srch<1||y1<=y0)return;
    if(!B)mix16=0;
    int off=scroll%srcw;if(off<0)off+=srcw;
    int ya=imax(y0,t->y0),yb=imin(y1,t->y1);
    for(int y=ya;y<yb;y++){
        int sy=src_y0+(y-y0)*src_h/(y1-y0);
        if(sy<0)sy=0;
        if(sy>=srch)sy=srch-1;
        unsigned k=gain?gain[y-y0]:16u;
        const uint16_t *ra=A+(size_t)sy*(size_t)srcw,*rb=mix16?B+(size_t)sy*(size_t)srcw:ra;
        const unsigned char *bq=bayer+(y&3)*4;
        uint16_t *row=t->p+(size_t)y*t->stride;
        int sx=(off+t->x0)%srcw;
        for(int x=t->x0;x<t->x1;x++){
            uint16_t v=(mix16&&(unsigned)bq[x&3]<mix16)?rb[sx]:ra[sx];
            row[x]=k<16u?gm_scale(v,k):v;
            if(++sx>=srcw)sx=0;
        }
    }
}

void gm_shatter(const struct gm_tgt *t,int cx,int cy,const int *ox,const int *oy,int n,
                int kx,int ky,float age,float dx,float dy,float strength,
                uint16_t lit,uint16_t dark,uint16_t edge,uint16_t core,unsigned seed){
    if(!(age>=0.f)||age>=1.f||n<3||n>8)return;
    float st=clampf(strength,0.f,1.f),ext=1.f;
    for(int i=0;i<n;i++){float d=sqrtf((float)(ox[i]*ox[i]+oy[i]*oy[i]));if(d>ext)ext=d;}
    float push=ext*(.55f+.9f*st),fall=ext*1.4f;
    float e=gm_ease_out(age);
    unsigned a=(unsigned)(250.f*(1.f-age*age*age));
    /* fracture origin: between the centre and the contact */
    float fx=(float)kx*.45f,fy=(float)ky*.45f;
    if(age<.14f){
        float u=age/.14f;
        gm_glow(t,cx+ri(fx),cy+ri(fy),ri(ext*(.35f+.9f*u)),core,(unsigned)(240.f*(1.f-u)));
    }
    for(int i=0;i<n;i++){
        uint32_t hs=gm_hash(seed*977u+(unsigned)i*131u);
        int j=i+1<n?i+1:0;
        float vx[3]={fx,(float)ox[i],(float)ox[j]},vy[3]={fy,(float)oy[i],(float)oy[j]};
        float gx=(vx[0]+vx[1]+vx[2])/3.f,gy=(vy[0]+vy[1]+vy[2])/3.f;
        float rx=gx-(float)kx,ry=gy-(float)ky,rl=sqrtf(rx*rx+ry*ry)+.001f;
        float spd=.75f+.5f*(float)((hs>>16)&255u)/255.f;
        float px=gx+(rx/rl*push*.55f+dx*push)*spd*e;
        float py=gy+(ry/rl*push*.55f+dy*push)*spd*e+fall*age*age;
        int deg=(int)((float)(((hs>>24)&1u)?1:-1)*(90.f+220.f*st)*spd*e);
        float sn=(float)xz_sin1024(deg)/1024.f,cs=(float)xz_cos1024(deg)/1024.f;
        int X[3],Y[3];
        for(int k=0;k<3;k++){
            float lx=vx[k]-gx,ly=vy[k]-gy;
            X[k]=cx+ri(px+cs*lx-sn*ly);Y[k]=cy+ri(py+sn*lx+cs*ly);
        }
        /* outward edge normal against an upper-left light keeps facet shading */
        float nx=(float)(oy[j]-oy[i]),ny=(float)(ox[i]-ox[j]);
        float lv=(-nx*.6f-ny*.8f)/(sqrtf(nx*nx+ny*ny)+.001f);
        uint16_t face=gm_mix(dark,lit,(unsigned)clampf(128.f+lv*128.f,0.f,256.f));
        gm_poly(t,X,Y,3,face,a*7u/8u);
        gm_line(t,X[1],Y[1],X[2],Y[2],1,edge,a);
        gm_line(t,X[0],Y[0],X[1],Y[1],1,gm_mix(dark,core,120),a/2u);
    }
}

/* ---- Barlow atlas text ---- */
static int fsz(int size){return size<0?0:size>3?3:size;}
int gm_text_height(int size){return xz_game_font_height[fsz(size)];}
int gm_text_width(const char *s,int size){
    int w=0,z=fsz(size);
    for(int n=0;s&&s[n]&&n<160;n++){
        unsigned ch=(unsigned char)s[n];
        if(ch<32u)continue;
        w+=xz_game_font_glyphs[z][ch-32u].advance;
    }
    return w;
}
void gm_text(const struct gm_tgt *t,int x,int y,const char *s,int size,uint16_t c,unsigned a){
    if(!s||!a)return;
    int z=fsz(size),base=y+xz_game_font_ascent[z],pen=x;
    for(int n=0;s[n]&&n<160;n++){
        unsigned ch=(unsigned char)s[n];
        if(ch<32u)continue;
        const struct xz_game_font_glyph *g=&xz_game_font_glyphs[z][ch-32u];
        int gx=pen+g->x,gy=base+g->y;
        pen+=g->advance;
        if(pen<t->x0)continue;
        if(gx>=t->x1)break;
        const uint8_t *al=xz_game_font_alpha+g->offset;
        for(int r=0;r<(int)g->height;r++){
            int yy=gy+r;
            if(yy<t->y0||yy>=t->y1)continue;
            uint16_t *row=t->p+(size_t)yy*t->stride;
            const uint8_t *ar=al+(size_t)r*g->width;
            for(int q=0;q<(int)g->width;q++){
                int xx=gx+q;
                if(xx<t->x0||xx>=t->x1)continue;
                unsigned k=ar[q];
                if(k<6u)continue;
                k=a>=256u?k:k*a>>8;
                row[xx]=k>=250u?c:gm_mix(row[xx],c,k+1u);
            }
        }
    }
}
void gm_text_lift(const struct gm_tgt *t,int x,int y,const char *s,int size,uint16_t c){
    gm_text(t,x+1,y+1+(size>=2),s,size,0,200u);
    gm_text(t,x,y,s,size,c,256u);
}
void gm_text_mid(const struct gm_tgt *t,int cx,int y,const char *s,int size,uint16_t c){
    gm_text_lift(t,cx-gm_text_width(s,size)/2,y,s,size,c);
}

void gm_ripple(const struct gm_tgt *t,int cx,int y,float slope,float age,int amp,int reach,uint16_t c,unsigned a){
    if(!(age>=0.f)||age>=1.f||reach<2)return;
    float e=gm_ease_out(age),fade=1.f-age;
    int h=ri((float)amp*fade),r=ri((float)reach*e);
    unsigned al=(unsigned)((float)a*fade);
    for(int side=-1;side<=1;side+=2){
        int x=cx+side*r;
        for(int i=-7;i<=7;i++){
            int hh=h*(49-i*i)/49,xx=x+i,yy=y+ri(slope*(float)(xx-cx));
            gm_rect_mix(t,xx,yy-hh-1,1,2,c,al);
            if(hh>1)gm_rect_mix(t,xx,yy-hh+1,1,hh,c,al/4u);
        }
    }
    /* compressed surface right under the contact */
    if(age<.35f){
        int cw=ri((float)reach*.18f*(1.f-age/.35f))+2;
        gm_span_mix(t,cx-cw,cx+cw,y+1,c,al/2u);
    }
}

void gm_draw_cat(const struct gm_tgt *t,const struct gm_cat *c){
    float sq=clampf(c->sq,.45f,1.6f);
    float sy=c->scale*sq,sx=c->scale*(1.f+(1.f-sq)*.7f);
    struct gm_sprite s;
    s.size=XZ_FEEL_CAT_SIZE;s.au=XZ_FEEL_CAT_ANCHOR_X;s.av=XZ_FEEL_CAT_ANCHOR_Y;
    s.sx=sx;s.sy=sy;s.sn=sinf(c->ang);s.cs=cosf(c->ang);s.flip=0;s.vlo=0;s.vhi=0;
    s.tint=c->tint;s.tinta=c->tinta;
    unsigned pa=c->pose<XZ_FEEL_CAT_FRAMES?c->pose:0u,pb=c->pose_b<XZ_FEEL_CAT_FRAMES?c->pose_b:pa;
    float bl=clampf(c->blend,0.f,1.f);
    if(bl>.97f){pa=pb;bl=0.f;}
    s.rgb=xz_feel_cat_rgb[pa];s.alpha=xz_feel_cat_alpha[pa];s.amul=256u;
    s.rgb2=NULL;s.alpha2=NULL;s.blend2=0;
    if(bl>.03f&&pb!=pa){s.rgb2=xz_feel_cat_rgb[pb];s.alpha2=xz_feel_cat_alpha[pb];s.blend2=(unsigned)(bl*256.f);}
    gm_blit(t,&s,c->x,c->y);
}
void gm_draw_cow(const struct gm_tgt *t,int x,int y,float scale,float sq,float ang,unsigned pose,int hat_lift,int flip,unsigned amul){
    struct gm_sprite s;
    unsigned p=pose%XZ_FEEL_COW_FRAMES;
    sq=clampf(sq,.6f,1.4f);
    s.rgb=xz_feel_cow_rgb[p];s.alpha=xz_feel_cow_alpha[p];s.size=XZ_FEEL_COW_SIZE;
    s.au=48.f;s.av=56.f;s.sx=scale*(1.f+(1.f-sq)*.6f);s.sy=scale*sq;
    s.sn=sinf(ang);s.cs=cosf(ang);s.flip=flip;s.tint=0;s.tinta=0;s.amul=amul;
    s.rgb2=NULL;s.alpha2=NULL;s.blend2=0;
    /* body below the cone band, then the cone lifted on the beat */
    s.vlo=26;s.vhi=XZ_FEEL_COW_SIZE;gm_blit(t,&s,x,y);
    s.vlo=0;s.vhi=26;
    int ly=hat_lift;
    gm_blit(t,&s,x+ri((float)ly*s.sn),y-ri((float)ly*s.cs));
}
void gm_draw_scenery(const struct gm_tgt *t,unsigned tile,int x,int base,float scale,int flip,uint16_t tint,unsigned tinta){
    struct gm_sprite s;
    unsigned k=tile%XZ_FEEL_SCENERY_FRAMES;
    s.rgb=xz_feel_scenery_rgb[k];s.alpha=xz_feel_scenery_alpha[k];s.size=XZ_FEEL_SCENERY_SIZE;
    s.au=96.f;s.av=190.f;s.sx=s.sy=scale;s.sn=0.f;s.cs=1.f;s.flip=flip;s.vlo=0;s.vhi=0;
    s.tint=tint;s.tinta=tinta;s.amul=256u;
    s.rgb2=NULL;s.alpha2=NULL;s.blend2=0;
    gm_blit(t,&s,x,base);
}

float gm_ease_out(float u){u=clampf(u,0.f,1.f);float v=1.f-u;return 1.f-v*v*v;}
float gm_spring(float u,float freq,float damp){
    if(!(u>0.f))return 1.f;
    return expf(-damp*u)*cosf(freq*u);
}
const struct xz_game_impact *gm_find(const struct xz_game_effects *f,unsigned kind,double at0,double tol){
    const struct xz_game_impact *best=NULL;
    for(unsigned i=0;i<XZ_GAME_IMPACTS;i++){
        const struct xz_game_impact *e=&f->events[i];
        if(!e->serial||!e->kind)continue;
        if(kind&&e->kind!=kind)continue;
        if(tol>=0&&fabs(e->at-at0)>tol)continue;
        if(!best||e->serial>best->serial)best=e;
    }
    return best;
}
