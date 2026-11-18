/* SPDX-License-Identifier: MIT */
/* Rhythm Lab renderer: ten basic control studies, one shared visual system.
   Pure drawing into an 800x480 RGB565 buffer. No heap, no IO, no per-pixel trig. */
#include "rhythm_lab.h"
#include "../ui/ui_skin.h"
#include "game_nav.h"
#include "glyph_run.h"
#include <math.h>
#include <stdint.h>

typedef struct xz_canvas cv;

#define BG     0x0b1530u
#define PANEL  0x16244cu
#define PANEL2 0x0f1b3bu
#define LINE   0x2c3d72u
#define TEXT   0xeef2ffu
#define DIM    0x8d9cc6u
#define FAINT  0x4a5a8au
#define CYAN   0x3fd8f2u
#define CORAL  0xff7d6bu
#define GOLD   0xffcb4au
#define VIOLET 0xa184ffu
#define MISS   0x667196u
#define PIECE1 0xdfe6ffu

#define FY 95
#define FH 260
#define TOP 124
#define CATCH 312
#define STRIP 326
#define LOOK 4.0
#define DEG 57.29578f
#define LAB_PI 3.14159265f

enum { B_NORMAL, B_PRIMARY, B_ACTIVE, B_OFF };

static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static int slen(const char *s){int n=0;if(s)while(s[n]&&n<200)n++;return n;}
static uint32_t pcol(unsigned i){return i?CORAL:CYAN;}
static double per_of(const struct xz_rhythm_lab *g){return g->period>1?g->period:75;}
static double phase_of(const struct xz_rhythm_lab *g){return (g->position-g->origin)/per_of(g);}
static double fracd(double v){return v-floor(v);}

/* ---- text ---- */
static void tx(cv c,int x,int y,const char *s,int dot,uint32_t col){
    int limit=(800-x)/(6*dot)+1;if(limit>0)xz_pixel_text(c,x,y,s,dot,limit,col);
}
static void txn(cv c,int x,int y,const char *s,int dot,int limit,uint32_t col){if(limit>0)xz_pixel_text(c,x,y,s,dot,limit,col);}
static void txc(cv c,int cx,int y,const char *s,int dot,uint32_t col,int maxw){
    int n=slen(s),m=maxw/(6*dot);if(n>m)n=m;if(n<=0)return;
    xz_pixel_text(c,cx-(n*6*dot-dot)/2,y,s,dot,n,col);
}
static void txr(cv c,int rx,int y,const char *s,int dot,uint32_t col){int n=slen(s);if(n)xz_pixel_text(c,rx-(n*6*dot-dot),y,s,dot,n,col);}

struct sb{char s[128];unsigned n;};
#define SB(v) struct sb v; v.n=0; v.s[0]=0
static void sbs(struct sb *b,const char *s){while(s&&*s&&b->n<sizeof(b->s)-1)b->s[b->n++]=*s++;b->s[b->n]=0;}
static void sbn(struct sb *b,const char *s,unsigned max){for(unsigned i=0;s&&i<max&&s[i];i++)if(b->n<sizeof(b->s)-1)b->s[b->n++]=s[i];b->s[b->n]=0;}
static void sbu(struct sb *b,unsigned v){char t[12];int i=0;do{t[i++]=(char)('0'+v%10);v/=10;}while(v&&i<11);while(i>0&&b->n<sizeof(b->s)-1)b->s[b->n++]=t[--i];b->s[b->n]=0;}
static void sbi(struct sb *b,int v){if(v<0){sbs(b,"-");sbu(b,(unsigned)(-(long)v));}else{sbs(b,"+");sbu(b,(unsigned)v);}}
static void sb2(struct sb *b,unsigned v){if(v<10)sbs(b,"0");sbu(b,v);}
static void sbpct(struct sb *b,float f){sbu(b,(unsigned)(clampf(f,0,1)*100+.5f));sbs(b,"%");}
static void sbsec(struct sb *b,float samples){unsigned cs=(unsigned)(clampf(samples,0,15000)/1.5f+.5f);sbu(b,cs/100);sbs(b,".");sb2(b,cs%100);sbs(b," S");}
static int roundi(float v){return (int)(v+(v<0?-.5f:.5f));}

/* ---- primitives ---- */
static void ln(cv c,int x0,int y0,int x1,int y1,int w,uint32_t col){
    int dx=x1>x0?x1-x0:x0-x1,dy=y1>y0?y0-y1:y1-y0;
    int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx+dy,half=w/2;
    if(w<1)return;
    for(;;){xz_rect(c,x0-half,y0-half,w,w,col);if(x0==x1&&y0==y1)break;int e=err*2;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
static unsigned root32(unsigned n){unsigned r=0,bit=1u<<30;while(bit>n)bit>>=2;while(bit){if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;}
static void lab_disc(cv c,int x,int y,int r,uint32_t col){
    if(r<1||r>1000)return;
    for(int dy=-r;dy<=r;dy++){int half=(int)root32((unsigned)(r*r-dy*dy));xz_rect(c,x-half,y+dy,half*2+1,1,col);}
}
static void lab_ring(cv c,int x,int y,int r,int width,uint32_t col){
    if(r<1||width<1)return;if(width>=r){lab_disc(c,x,y,r,col);return;}
    int radius=r-width/2,px=x+radius,py=y;
    for(int i=1;i<=64;i++){int deg=i*360/64,nx=x+xz_cos1024(deg)*radius/1024,ny=y+xz_sin1024(deg)*radius/1024;ln(c,px,py,nx,ny,width,col);px=nx;py=ny;}
}
#define xz_disc lab_disc
#define xz_ring lab_ring

/* Controls drawn this frame, for encoder focus. Set only inside a render call. */
static struct xz_game_targets *targets;
static void paint_button(cv c,int x,int y,int w,int h,const char *label,const char *sub,int style,uint32_t accent){
    uint32_t fill=PANEL,edge=LINE,text=TEXT,subc=DIM;
    if(style==B_PRIMARY){fill=xz_blend(BG,accent,90);edge=accent;}
    else if(style==B_ACTIVE){fill=accent;edge=TEXT;text=BG;subc=BG;}
    else if(style==B_OFF){fill=PANEL2;edge=0x1f2c55u;text=FAINT;subc=FAINT;}
    xz_round_rect(c,x+3,y+3,w-6,h-6,10,15,edge);
    xz_round_rect(c,x+5,y+5,w-10,h-10,8,15,fill);
    if(sub){int y0=y+(h-25)/2;txc(c,x+w/2,y0,sub,1,subc,w-20);txc(c,x+w/2,y0+11,label,2,text,w-20);}
    else txc(c,x+w/2,y+(h-14)/2,label,2,text,w-20);
}
static void button(cv c,int x,int y,int w,int h,const char *label,const char *sub,int style,uint32_t accent){
    if(style!=B_OFF)xz_game_target_add(targets,x,y,w,h,style==B_PRIMARY?XZ_GAME_PRIMARY:XZ_GAME_NORMAL);
    paint_button(c,x,y,w,h,label,sub,style,accent);
}
/* A control that returns to the deck; encoder focus never starts on it. */
static void exit_button(cv c,int x,int y,int w,int h,const char *label){
    xz_game_target_add(targets,x,y,w,h,XZ_GAME_EXIT);
    paint_button(c,x,y,w,h,label,NULL,B_NORMAL,CYAN);
}
static void header(cv c,const char *title,const char *sub){
    xz_rect(c,0,0,800,48,PANEL2);xz_rect(c,0,48,800,1,LINE);
    txn(c,16,9,title,2,56,TEXT);
    if(sub)txn(c,16,32,sub,1,112,DIM);
    exit_button(c,704,0,96,48,"DECK");
}
static void arrow(cv c,int cx,int y,int dir,uint32_t col){
    int tip=cx+dir*42;xz_rect(c,cx-34,y-3,68,6,col);
    ln(c,tip,y,tip-dir*18,y-14,5,col);ln(c,tip,y,tip-dir*18,y+14,5,col);
}
static void coil(cv c,int cx,int top,int bottom,uint32_t col){
    for(int i=0;i<8;i++){int y0=top+(bottom-top)*i/8,y1=top+(bottom-top)*(i+1)/8,s=i%2?1:-1;ln(c,cx+18*s,y0,cx-18*s,y1,3,col);}
    xz_rect(c,cx-26,top-4,52,5,col);xz_rect(c,cx-26,bottom,52,5,col);
}
static void polar(int cx,int cy,float radians,int r,int *x,int *y){
    int d=(int)(radians*DEG);*x=cx+xz_sin1024(d)*r/1024;*y=cy-xz_cos1024(d)*r/1024;
}

/* ---- provenance ---- */
static int pulse_ready(const struct xz_rhythm_lab *g){return g->valid&&(g->manual_pulse||g->wave.pulse_confidence>=.25f);}
static const char *clock_tag(const struct xz_rhythm_lab *g){
    if(!g->valid)return "NO TRACK CLOCK";
    if(g->manual_pulse)return "TAP PULSE";
    if(g->wave.pulse_confidence>=.25f)return "ESTIMATED PULSE";
    return "NO PULSE YET";
}
static const char *source_tag(const struct xz_rhythm_lab *g){
    if(g->mode==LAB_ECHO)return "STUDY PATTERN";
    if(g->mode==LAB_STEPS||g->mode==LAB_PUZZLE)return "NO NOTES / PULSE ONLY";
    if(g->study)return "STUDY PATTERN";
    if(g->mode==LAB_RIBBON)return "TRACK ENVELOPE";
    if(g->mode==LAB_SCRATCH)return g->manual_pulse?"TAP PULSE TIMES / STUDY ARROWS":"TRACK ACCENTS / STUDY ARROWS";
    return g->manual_pulse?"TRACK ENERGY ON TAP PULSE":"TRACK ACCENTS";
}
static void provenance(struct sb *b,const struct xz_rhythm_lab *g){
    sbs(b,"SOURCE ");sbs(b,source_tag(g));sbs(b," / CLOCK ");sbs(b,clock_tag(g));
}
static void case_title(struct sb *b,unsigned m,const char *prefix){
    sbs(b,prefix);sb2(b,m+1);sbs(b," ");sbs(b,xz_lab_description(m)->name);
}

/* ---- action button mapping (per deck, session only) ---- */
static const char *const binding_names[5]={"CUE + PAD A","PAD A ONLY","PAD C ONLY","PAD D ONLY","PAD E ONLY"};
static unsigned binding_of(const struct xz_rhythm_lab *g,unsigned deck){unsigned v=g->action_binding[deck>1?1:deck];return v<5?v:0;}
/* Duo: player n plays deck n. Solo: the control deck plays. */
static unsigned deck_of(const struct xz_rhythm_lab *g,unsigned pi){return xz_lab_players(g)==2?(pi?1u:0u):(g->control_deck?1u:0u);}
static const char *action_full(const struct xz_rhythm_lab *g,unsigned deck){
    const char *s=xz_lab_action_label(g,deck);return s&&*s?s:binding_names[binding_of(g,deck)];
}
static const char *action_short(const struct xz_rhythm_lab *g,unsigned deck){return binding_of(g,deck)?action_full(g,deck):"CUE / A";}
/* ---- waveform trace ---- */
static void trace(cv c,const struct xz_rhythm_lab *g,int x,int y,int w,int h,double center,double span){
    xz_rect(c,x,y,w,h,PANEL2);
    if(!g->valid||!g->wave.count){txc(c,x+w/2,y+h/2-3,"NO WAVEFORM FROM THE DECK",1,FAINT,w);return;}
    static const uint32_t col[3]={0x5d7fd6u,VIOLET,0xc7d2f0u};
    float norm=fmaxf(64,(float)g->wave.normalization);
    for(unsigned b=0;b<3;b++){
        int prev=-1;
        for(int i=0;i<w;i+=2){
            double at=center+(i-w/2)*span/w;
            float v=clampf(xz_lab_band(g,at,b)*3/norm,0,1);
            int yy=y+h-1-(int)(v*(h-1));if(prev<0)prev=yy;
            int lo=prev<yy?prev:yy,hi=prev>yy?prev:yy;
            xz_rect(c,x+i,lo,2,hi-lo+1,col[b]);prev=yy;
        }
    }
    xz_rect(c,x+w/2,y,1,h,GOLD);
}

/* ---- playfield helpers ---- */
static int ty(const struct xz_rhythm_lab *g,double at){
    double f=(at-g->position)/(per_of(g)*LOOK);if(f<-1)f=-1;if(f>2)f=2;
    return CATCH-(int)(f*(CATCH-TOP));
}
static int recent(const struct xz_rhythm_lab *g,double at){double d=g->position-at;return d>=0&&d<per_of(g)*.6;}
static uint32_t grade_col(unsigned gr,uint32_t pc){return gr==LAB_PERFECT?GOLD:gr==LAB_GOOD?pc:MISS;}
static void catch_line(cv c,int x,int w){xz_rect(c,x+14,CATCH-2,w-28,4,FAINT);}

static void draw_pulse_hold(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);int cx=x+w/2,held=!!p->held;
    xz_rect(c,cx-42,TOP-20,84,CATCH-TOP+40,0x142150u);catch_line(c,x,w);
    for(unsigned i=0;i<g->event_count;i++){
        const struct xz_lab_event *e=&g->events[i];unsigned gr=e->grade[pi];
        if(e->kind==LAB_HOLD){
            int y0=ty(g,e->at),y1=ty(g,e->end);if(y1>352||y0<TOP-24)continue;
            int bottom=gr==LAB_HOLDING?CATCH:y0,top=y1<TOP-20?TOP-20:y1;if(bottom>352)bottom=352;
            uint32_t fill=gr==LAB_HOLDING?xz_blend(BG,pc,210):gr==LAB_PENDING?0x3a4d88u:grade_col(gr,pc);
            if(gr==LAB_MISSED)fill=xz_blend(BG,MISS,140);
            if(bottom>top)xz_rect(c,cx-20,top,40,bottom-top,fill);
            if(y1>=TOP-20&&y1<=352){xz_rect(c,cx-34,y1-3,68,6,GOLD);tx(c,cx+40,y1-3,"LET GO",1,GOLD);}
            if(gr==LAB_PENDING&&y0<=352){xz_disc(c,cx,y0,17,TEXT);xz_ring(c,cx,y0,17,3,pc);tx(c,cx+26,y0-3,"HOLD",1,DIM);}
            if(gr==LAB_HOLDING){
                float span=(float)(e->end-e->at),f=span>0?clampf(e->covered[pi]/span,0,1):0;
                xz_rect(c,cx+50,CATCH-60,10,60,PANEL);xz_rect(c,cx+50,CATCH-(int)(60*f),10,(int)(60*f),pc);
            }
        }else{
            int y=ty(g,e->at);
            if(gr==LAB_PENDING){if(y>=TOP-24&&y<=352){xz_disc(c,cx,y,17,TEXT);xz_ring(c,cx,y,17,3,pc);}}
            else if(gr==LAB_MISSED){if(y>=TOP-24&&y<=352)xz_ring(c,cx,y,12,2,MISS);}
            else if(recent(g,e->at))xz_ring(c,cx,CATCH,32,4,grade_col(gr,pc));
        }
    }
    if(held)xz_disc(c,cx,CATCH,20,xz_blend(BG,pc,150));
    xz_ring(c,cx,CATCH,24,held?5:3,held?pc:DIM);
}

static void draw_lanes(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w,int with_cue){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    int lx=x+20,lw=(w-40)/3;unsigned lane=xz_lab_lane(p);
    xz_rect(c,lx+(int)lane*lw,TOP-20,lw,CATCH-TOP+44,xz_blend(PANEL2,pc,40));
    for(int i=0;i<=3;i++)xz_rect(c,lx+i*lw-1,TOP-20,2,CATCH-TOP+44,LINE);
    catch_line(c,x,w);
    for(unsigned i=0;i<g->event_count;i++){
        const struct xz_lab_event *e=&g->events[i];unsigned gr=e->grade[pi];
        int l=e->lane<0?0:e->lane>2?2:e->lane,cx=lx+l*lw+lw/2,y=ty(g,e->at);
        if(gr==LAB_PENDING){if(y>=TOP-24&&y<=352){xz_disc(c,cx,y,15,TEXT);if(with_cue)xz_ring(c,cx,y,15,3,GOLD);}}
        else if(gr==LAB_MISSED){if(y>=TOP-24&&y<=352)xz_ring(c,cx,y,11,2,MISS);}
        else if(recent(g,e->at))xz_ring(c,cx,CATCH,30,4,grade_col(gr,pc));
    }
    int cxp=lx+(int)(clampf(p->cursor,0,1)*(float)(lw*3));
    xz_rect(c,cxp-1,CATCH-34,3,24,pc);
    xz_round_rect(c,cxp-22,CATCH-10,44,20,10,15,pc);
    if(with_cue){
        if(p->held)xz_ring(c,cxp,CATCH,32,6,pc);else xz_ring(c,cxp,CATCH,32,2,FAINT);
    }
}

static void draw_scratch(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    int hx=x+(w>500?w/2-90:w/2),dx=x+w-130,dy=200;
    xz_rect(c,hx-92,TOP-20,184,CATCH-TOP+44,0x142150u);catch_line(c,x,w>500?w-260:w);
    for(unsigned i=0;i<g->event_count;i++){
        const struct xz_lab_event *e=&g->events[i];unsigned gr=e->grade[pi];int y=ty(g,e->at),dir=e->direction<0?-1:1;
        if(gr==LAB_PENDING){if(y>=TOP-24&&y<=352){arrow(c,hx,y,dir,TEXT);tx(c,hx+(dir>0?54:-96),y-7,dir>0?"CW":"CCW",2,dir>0?CYAN:VIOLET);}}
        else if(gr==LAB_MISSED){if(y>=TOP-24&&y<=352)arrow(c,hx,y,dir,MISS);}
        else if(recent(g,e->at))xz_ring(c,hx,CATCH,40,4,grade_col(gr,pc));
    }
    if(w>500){
        int ex,ey;xz_ring(c,dx,dy,46,3,FAINT);polar(dx,dy,p->angle,40,&ex,&ey);
        ln(c,dx,dy,ex,ey,5,pc);xz_disc(c,dx,dy,7,pc);
        txr(c,dx-52,dy-3,"CCW",1,DIM);tx(c,dx+54,dy-3,"CW",1,DIM);
        float f=clampf(p->stroke/.18f,0,1);int bx=dx-80,by=272;
        xz_rect(c,bx,by,160,14,PANEL);xz_rect(c,bx,by,(int)(160*f),14,f>=1?GOLD:pc);xz_border(c,bx,by,160,14,LINE);
        SB(s);sbs(&s,"STROKE ");sbs(&s,p->stroke_direction<0?"CCW":p->stroke_direction>0?"CW":"-");if(p->stroke_used)sbs(&s," / COUNTED");
        txc(c,dx,by-14,s.s,1,TEXT,220);
        txc(c,dx,by+20,"ONE FLICK PER ARROW",1,DIM,220);
    }
}

static void draw_ribbon(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    int lx=x+20,iw=w-40;float cs[48];double look=per_of(g)*LOOK;
    for(int k=0;k<48;k++)cs[k]=clampf(xz_lab_ribbon(g,g->position+look*k/47.0),0,1);
    int half=(int)(.13f*iw);uint32_t fill=xz_blend(PANEL2,VIOLET,70);
    for(int yy=TOP-20;yy<=CATCH;yy++){
        float t=(float)(CATCH-yy)*47.f/(float)(CATCH-TOP);if(t>47)t=47;
        int i=(int)t;float f=t-(float)i;if(i>=47){i=46;f=1;}
        int cx=lx+(int)((cs[i]*(1-f)+cs[i+1]*f)*iw);
        xz_rect(c,cx-half,yy,2*half,1,fill);xz_rect(c,cx-half,yy,3,1,VIOLET);xz_rect(c,cx+half-3,yy,3,1,VIOLET);
    }
    catch_line(c,x,w);
    int rc=lx+(int)(cs[0]*iw);xz_rect(c,rc-half-2,CATCH-10,4,20,VIOLET);xz_rect(c,rc+half-2,CATCH-10,4,20,VIOLET);
    int cx=lx+(int)(clampf(p->cursor,0,1)*iw),inside=fabsf(p->cursor-cs[0])<=.13f;
    if(p->held&&inside){xz_disc(c,cx,CATCH,15,GOLD);xz_ring(c,cx,CATCH,20,3,TEXT);}
    else if(p->held){xz_disc(c,cx,CATCH,13,pc);txc(c,cx,CATCH-34,"OUTSIDE / STEER BACK",1,pc,200);}
    else{xz_ring(c,cx,CATCH,13,3,DIM);txc(c,cx,CATCH-34,"HOLD ACTION",1,DIM,120);}
}

static void draw_echo(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    uint32_t pc=pcol(pi);
    int x0=x+40,sw=(w-80)/8,top=168,h=84,cyd=top+h/2;
    double ph=phase_of(g)-(double)g->echo_base,cyc=floor(ph/8),within=ph-cyc*8;
    for(int j=0;j<8;j++){
        uint32_t tint=j<4?xz_blend(PANEL2,VIOLET,35):xz_blend(PANEL2,pc,35);
        xz_round_rect(c,x0+j*sw+3,top,sw-6,h,8,15,tint);
        char n[2]={(char)('1'+j%4),0};txc(c,x0+j*sw+sw/2,top+h+8,n,1,DIM,20);
    }
    xz_rect(c,x0+4*sw-2,top-30,4,h+50,TEXT);
    txc(c,x0+2*sw,top-26,"WATCH",2,VIOLET,4*sw);txc(c,x0+6*sw,top-26,"ANSWER",2,pc,4*sw);
    for(unsigned i=0;i<g->event_count;i++){
        const struct xz_lab_event *e=&g->events[i];
        int64_t rel=e->id-g->echo_base;double ec=floor((double)rel/8);if(ec!=cyc)continue;
        int k=(int)(rel-(int64_t)ec*8);if(k<4||k>7)continue;
        int j=k-4,dxp=x0+j*sw+sw/2,axp=x0+k*sw+sw/2;unsigned gr=e->grade[pi];
        if(within<4){if(within>=j-.05)xz_disc(c,dxp,cyd,17,VIOLET);else xz_ring(c,dxp,cyd,17,3,xz_blend(PANEL2,VIOLET,150));}
        if(gr==LAB_PENDING){}
        else if(gr==LAB_MISSED){xz_ring(c,axp,cyd,17,2,MISS);ln(c,axp-8,cyd-8,axp+8,cyd+8,3,MISS);ln(c,axp-8,cyd+8,axp+8,cyd-8,3,MISS);}
        else xz_disc(c,axp,cyd,17,grade_col(gr,pc));
    }
    int hx=x0+(int)((within+.5)*sw);if(hx>x0+8*sw)hx-=8*sw;
    xz_rect(c,hx-1,top-6,3,h+12,GOLD);
    txc(c,x+w/2,top+h+26,"STUDY PATTERN IN 4-PULSE GROUPS / NOT SONG NOTES, NOT BARS",1,DIM,w-20);
}

static void draw_steps(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    int cell=40,bx=x+w/2-130,by=118;
    for(int yy=0;yy<5;yy++)for(int xx=0;xx<5;xx++)xz_round_rect(c,bx+xx*cell+2,by+yy*cell+2,cell-4,cell-4,6,15,PANEL);
    int tcx=bx+p->target_x*cell+cell/2,tcy=by+p->target_y*cell+cell/2;
    xz_ring(c,tcx,tcy,14,4,GOLD);
    int dir=(int)floorf((clampf(p->angle,-LAB_PI,LAB_PI)+LAB_PI/4+2*LAB_PI)/(LAB_PI/2))%4;
    static const int ddx[4]={1,0,-1,0},ddy[4]={0,1,0,-1};
    int pcx=bx+p->gx*cell+cell/2,pcy=by+p->gy*cell+cell/2,nx=p->gx+ddx[dir],ny=p->gy+ddy[dir];
    if(nx>=0&&nx<5&&ny>=0&&ny<5){xz_border(c,bx+nx*cell+1,by+ny*cell+1,cell-2,cell-2,pc);xz_border(c,bx+nx*cell+2,by+ny*cell+2,cell-4,cell-4,pc);}
    int ax=pcx+ddx[dir]*30,ay=pcy+ddy[dir]*30;ln(c,pcx,pcy,ax,ay,5,TEXT);
    ln(c,ax,ay,ax-ddx[dir]*10-ddy[dir]*8,ay-ddy[dir]*10-ddx[dir]*8,4,TEXT);
    ln(c,ax,ay,ax-ddx[dir]*10+ddy[dir]*8,ay-ddy[dir]*10+ddx[dir]*8,4,TEXT);
    xz_disc(c,pcx,pcy,13,pc);
    int rx=bx+5*cell+110,ry=by+100;double f=fracd(phase_of(g));
    xz_ring(c,rx,ry,14,4,GOLD);xz_ring(c,rx,ry,14+(int)(46*(1-f)),2,DIM);
    txc(c,rx,ry+68,"NEXT ESTIMATED PULSE",1,DIM,200);
    if(g->manual_pulse)txc(c,rx,ry+80,"(TAP PULSE)",1,DIM,200);
}

static void draw_blaster(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    int cx=x+w/2,cy=222,R=100,ex,ey;double per=per_of(g);
    xz_ring(c,cx,cy,R,1,LINE);
    polar(cx,cy,p->angle-.3f,R+22,&ex,&ey);ln(c,cx,cy,ex,ey,1,xz_blend(PANEL2,pc,120));
    polar(cx,cy,p->angle+.3f,R+22,&ex,&ey);ln(c,cx,cy,ex,ey,1,xz_blend(PANEL2,pc,120));
    for(unsigned i=0;i<g->event_count;i++){
        const struct xz_lab_event *e=&g->events[i];unsigned gr=e->grade[pi];if(e->kind!=LAB_TARGET)continue;
        double from=e->at-per*.6;int px,py;polar(cx,cy,e->angle,R,&px,&py);
        if(g->position<from){if(from-g->position<per*2)xz_ring(c,px,py,9,2,FAINT);continue;}
        if(g->position>e->end)continue;
        if(gr==LAB_PENDING){xz_disc(c,px,py,16,TEXT);xz_ring(c,px,py,16,3,pc);}
        else if(gr!=LAB_MISSED)xz_ring(c,px,py,22,4,grade_col(gr,pc));
    }
    if(g->position>=p->shot_at&&g->position-p->shot_at<per*.35){polar(cx,cy,p->shot_angle,R+24,&ex,&ey);ln(c,cx,cy,ex,ey,4,TEXT);}
    polar(cx,cy,p->angle,40,&ex,&ey);ln(c,cx,cy,ex,ey,8,pc);
    xz_ring(c,cx,cy,26,g->pulse>.5f?4:2,g->pulse>.5f?GOLD:FAINT);
    xz_disc(c,cx,cy,14,pc);
}

static void draw_puzzle(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    int cell=42,bw=6*cell,bx=x+w/2-bw/2,by=150,col=(int)clampf(p->cursor*5,0,4);
    static const uint32_t pieces[3]={0,PIECE1,VIOLET};
    xz_rect(c,bx-2,by-2,bw+4,4*cell+4,LINE);xz_rect(c,bx,by,bw,4*cell,PANEL2);
    xz_rect(c,bx+col*cell,by,2*cell,4*cell,xz_blend(PANEL2,pc,40));
    for(int i=0;i<24;i++){
        int xx=i%6,yy=i/6;unsigned v=p->grid[i];
        if(v==1||v==2)xz_round_rect(c,bx+xx*cell+3,by+yy*cell+3,cell-6,cell-6,6,15,pieces[v]);
        else xz_disc(c,bx+xx*cell+cell/2,by+yy*cell+cell/2,2,LINE);
    }
    uint32_t mask=xz_lab_matches(p);
    for(int i=0;i<24;i++)if(mask&(1u<<i))for(int k=0;k<3;k++)xz_border(c,bx+(i%6)*cell+k,by+(i/6)*cell+k,cell-2*k,cell-2*k,GOLD);
    unsigned a=p->attacks+1,n0=(a/2)%2+1,n1=(a/3)%2+1;if(p->pair_flip){unsigned t=n0;n0=n1;n1=t;}
    xz_round_rect(c,bx+col*cell+8,120,cell-16,24,5,15,pieces[n0]);
    xz_round_rect(c,bx+(col+1)*cell+8,120,cell-16,24,5,15,pieces[n1]);
    txr(c,bx-10,126,"NEXT",1,DIM);
    int sx=bx+(int)(fracd(phase_of(g)/4)*bw);xz_rect(c,sx-1,by-6,3,4*cell+12,TEXT);
}

static void draw_spring(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);double per=per_of(g);
    int held=!!p->held,step=!held?0:p->charge<.25f?1:2;
    static const char *const steps[3]={"1 HOLD ACTION","2 PULL BACK","3 RELEASE"};
    for(int i=0;i<3;i++){int bx=x+20+i*170;xz_round_rect(c,bx,124,160,28,8,15,i==step?xz_blend(BG,pc,120):PANEL);txc(c,bx+80,131,steps[i],2,i==step?TEXT:DIM,156);}
    int kx=x+110,top=176+(int)(clampf(p->charge,0,1)*70);
    coil(c,kx,top,296,held?pc:FAINT);
    int bx=x+170,bt=170,bh=126;xz_rect(c,bx,bt,16,bh,PANEL);
    int fh=(int)(clampf(p->charge,0,1)*bh);xz_rect(c,bx,bt+bh-fh,16,fh,held?pc:FAINT);
    xz_rect(c,bx-6,bt+bh-bh/4,28,3,GOLD);tx(c,bx+26,bt+bh-bh/4-3,"MIN",1,GOLD);
    tx(c,bx+26,bt-3,"CAP 1/4 TURN",1,DIM);
    int hx=x+w-120;xz_rect(c,hx-50,TOP-20,100,CATCH-TOP+44,0x142150u);xz_rect(c,hx-60,CATCH-2,120,4,FAINT);
    txc(c,hx,CATCH+6,"RELEASE LINE",1,DIM,120);
    for(unsigned i=0;i<g->event_count;i++){
        const struct xz_lab_event *e=&g->events[i];unsigned gr=e->grade[pi];int y=ty(g,e->at);
        if(gr==LAB_PENDING){if(y>=TOP-24&&y<=352){xz_ring(c,hx,y,18,4,GOLD);xz_disc(c,hx,y,6,GOLD);}}
        else if(gr==LAB_MISSED){if(y>=TOP-24&&y<=352)xz_ring(c,hx,y,12,2,MISS);}
        else if(recent(g,e->at))xz_ring(c,hx,CATCH,30,4,grade_col(gr,pc));
    }
    double t=(g->position-p->launch_at)/(per*2);
    if(t>=0&&t<=1){
        int sx=kx,sy=172,len=300,hh=12+(int)(clampf(p->launch_charge,0,1)*58),px=sx,py=sy;
        uint32_t ac=p->launch_grade?GOLD:MISS;
        for(int k=1;k<=16;k++){double s=t*k/16;int nx=sx+(int)(s*len),ny=sy-(int)(4*hh*s*(1-s));ln(c,px,py,nx,ny,3,ac);px=nx;py=ny;}
        xz_disc(c,px,py,8,ac);
        if(!p->launch_grade)tx(c,sx+40,sy+8,p->launch_charge<.25f?"EMPTY RELEASE / PULL MORE":"OFF THE DOT",1,MISS);
    }
}

/* ---- field frame ---- */
static void field(cv c,const struct xz_rhythm_lab *g,unsigned pi,int x,int w,unsigned players){
    const struct xz_lab_player *p=&g->player[pi];uint32_t pc=pcol(pi);
    xz_rect(c,x,FY,w,FH,PANEL2);xz_border(c,x,FY,w,FH,players==2?xz_blend(PANEL2,pc,120):LINE);
    switch(g->mode){
    case LAB_PULSE_HOLD:draw_pulse_hold(c,g,pi,x,w);break;
    case LAB_WAVE_LANES:draw_lanes(c,g,pi,x,w,0);break;
    case LAB_WHEEL_MATCH:draw_lanes(c,g,pi,x,w,1);break;
    case LAB_SCRATCH:draw_scratch(c,g,pi,x,w);break;
    case LAB_RIBBON:draw_ribbon(c,g,pi,x,w);break;
    case LAB_ECHO:draw_echo(c,g,pi,x,w);break;
    case LAB_STEPS:draw_steps(c,g,pi,x,w);break;
    case LAB_BLASTER:draw_blaster(c,g,pi,x,w);break;
    case LAB_PUZZLE:draw_puzzle(c,g,pi,x,w);break;
    default:draw_spring(c,g,pi,x,w);break;
    }
    if(p->feedback&&p->feedback_until>g->position)txc(c,x+w/2,101,p->feedback,2,TEXT,w-16);
    /* status strip */
    xz_rect(c,x+1,STRIP,w-2,FY+FH-STRIP-1,xz_blend(PANEL2,BG,128));
    int held=!!p->held;
    SB(cue);if(players==2)sbs(&cue,pi?"P2 ":"P1 ");sbs(&cue,action_short(g,deck_of(g,pi)));
    if(held)xz_round_rect(c,x+8,STRIP+4,104,22,11,15,pc);else{xz_round_rect(c,x+8,STRIP+4,104,22,11,15,pc);xz_round_rect(c,x+10,STRIP+6,100,18,9,15,PANEL2);}
    txc(c,x+60,STRIP+12,cue.s,1,held?BG:pc,98);
    SB(s);sbs(&s,"SCORE ");sbu(&s,p->score);txr(c,x+w-10,STRIP+8,s.s,2,TEXT);
    SB(m);
    switch(g->mode){
    case LAB_PULSE_HOLD:if(held)sbs(&m,"HOLDING");break;
    case LAB_WAVE_LANES:sbs(&m,"A: POSITION ONLY");break;
    case LAB_WHEEL_MATCH:{
        sbs(&m,"B: LANE + ACTION");
        const struct xz_lab_event *next=NULL;
        for(unsigned i=0;i<g->event_count;i++){const struct xz_lab_event *e=&g->events[i];if(e->grade[pi]==LAB_PENDING&&e->at>=g->position-per_of(g)*.3&&(!next||e->at<next->at))next=e;}
        if(next)sbs(&m,xz_lab_lane(p)==(unsigned)next->lane?" / LANE OK":" / MOVE LANE");
        break;}
    case LAB_SCRATCH:sbs(&m,"FLICK, NO HOLD");break;
    case LAB_RIBBON:sbs(&m,"IN RIBBON ");sbpct(&m,p->coverage_total>0?p->coverage/p->coverage_total:0);break;
    case LAB_ECHO:if(p->timing_hits){sbs(&m,"LAST TAP ");sbi(&m,roundi(p->last_error_ms));sbs(&m," MS");}else sbs(&m,"ANSWER WITH ACTION");break;
    case LAB_STEPS:sbs(&m,"TARGETS ");sbu(&m,p->collections);sbs(&m," / ON PULSE ");sbu(&m,p->timing_hits);break;
    case LAB_BLASTER:sbs(&m,"SHOTS ");sbu(&m,p->shots);sbs(&m," / HITS ");sbu(&m,p->hits);break;
    case LAB_PUZZLE:sbs(&m,"CLEARED ");sbu(&m,p->cleared);sbs(&m," / SWEEP EVERY 4 PULSES");break;
    default:sbs(&m,"CHARGE ");sbpct(&m,p->charge);break;
    }
    int mx=x+120,mw=w-120-150;if(mw>0)txn(c,mx,STRIP+11,m.s,1,mw/6,DIM);
}

/* ---- screens ---- */
static const char *const inputs[XZ_LAB_CASES]={
    "ACTION ONLY / TAP + HOLD","JOG ONLY / LANE POSITION","JOG LANE + ACTION PRESS","JOG FLICKS / NO HOLD",
    "ACTION HOLD + JOG STEER","ACTION ONLY / MEMORY","JOG POINTS + ACTION STEPS","JOG AIMS + ACTION FIRES",
    "JOG + ACTION + PAD B","ACTION HOLD + JOG PULL"};

static void status_lines(cv c,const struct xz_rhythm_lab *g,int y){
    SB(a);sbs(&a,"MUSIC DECK ");sbu(&a,g->deck+1);sbs(&a," / ");
    if(g->wave.title[0])sbn(&a,g->wave.title,48);else sbs(&a,"NO TITLE");
    txn(c,16,y,a.s,2,64,TEXT);
    SB(b);
    if(!g->valid)sbs(&b,g->wave.loading?"LOADING ANALYSIS / WAIT OR OPEN MUSIC SETUP":"NO TRACK CLOCK / OPEN MUSIC SETUP");
    else{
        sbs(&b,g->playing?"PLAYING / ":"STOPPED / ");sbs(&b,clock_tag(g));
        
        sbs(&b,g->study?" / SOURCE STUDY PATTERNS":" / SOURCE TRACK");
    }
    txn(c,16,y+22,b.s,1,128,g->valid?DIM:CORAL);
}

static void menu_screen(cv c,const struct xz_rhythm_lab *g){
    header(c,"RHYTHM LAB / 10 STUDIES","A GAME BY VJ OPTICMYSTIC / PROTOTYPE CONTROL STUDIES, NOT FINISHED GAMES");
    status_lines(c,g,56);
    for(unsigned n=0;n<XZ_LAB_STUDIES;n++){
        unsigned r=n%5,col=n/5;int x=col?416:16,y=96+60*(int)r,w=368,sel=n==g->menu_selected;
        const struct xz_lab_description *d=xz_lab_description(n);const struct xz_lab_record *rec=&g->record[n];
        xz_game_target_add(targets,x,y,w,56,sel?XZ_GAME_PRIMARY:XZ_GAME_NORMAL);
        xz_round_rect(c,x,y,w,56,10,15,sel?CYAN:LINE);
        xz_round_rect(c,x+(sel?3:1),y+(sel?3:1),w-(sel?6:2),56-(sel?6:2),8,15,sel?xz_blend(BG,CYAN,45):PANEL);
        SB(num);sb2(&num,n+1);tx(c,x+12,y+10,num.s,2,GOLD);
        txn(c,x+52,y+10,d->name,2,20,TEXT);
        txn(c,x+52,y+34,inputs[n],1,38,DIM);
        int rated=rec->fun||rec->clarity||rec->effort;
        txr(c,x+w-12,y+12,rated?"RATED":rec->played?"PLAYED":"NEW",1,rated?GOLD:rec->played?DIM:FAINT);
        SB(tag);if(n==1||n==2)sbs(&tag,"A/B ");if(d->duo)sbs(&tag,"DUO");
        if(tag.n)txr(c,x+w-12,y+34,tag.s,1,d->duo&&g->duo?CORAL:DIM);
    }
    if(g->duo)txc(c,400,402,"DUO IN 01/02/03/08 / OTHER STUDIES STAY SOLO",2,CORAL,780);
    else txc(c,400,405,"SOLO / DUO IS AVAILABLE IN 01/02/03/08 / JOG SELECTS, ACTION OPENS",1,DIM,780);
    button(c,0,424,160,56,"MUSIC SETUP",NULL,B_NORMAL,CYAN);
    button(c,160,424,160,56,"BUTTONS",action_short(g,g->control_deck?1u:0u),B_NORMAL,GOLD);
    button(c,320,424,160,56,g->study?"STUDY":"TRACK","SOURCE",B_PRIMARY,VIOLET);
    button(c,480,424,160,56,g->duo?"DUO":"SOLO","PLAYERS",g->duo?B_PRIMARY:B_NORMAL,CORAL);
    button(c,640,424,160,56,"RATINGS",NULL,B_NORMAL,CYAN);
}

static void setup_screen(cv c,const struct xz_rhythm_lab *g){
    static const char *const names[4]={"PICK MUSIC","CHECK THE TRACK","TAP OR ALIGN THE PULSE","PICK AND CALIBRATE YOUR JOG"};
    unsigned st=g->setup_step>3?3:g->setup_step;
    SB(h);sbs(&h,"MUSIC SETUP / STEP ");sbu(&h,st+1);sbs(&h," OF 4 / ");sbs(&h,names[st]);
    header(c,h.s,"PULSE ONLY. THE LAB DOES NOT DETECT BARS OR PHRASES, AND NEVER CHANGES PLAYBACK.");
    float conf=g->wave.pulse_confidence;
    if(st==0){
        tx(c,16,62,"WHICH DECK IS PLAYING THE MUSIC?",2,TEXT);
        tx(c,16,86,"THE LAB ONLY READS POSITION AND WAVEFORM FROM THAT DECK.",1,DIM);
        button(c,0,150,400,80,"DECK 1","MUSIC FROM",g->deck==0?B_ACTIVE:B_NORMAL,CYAN);
        button(c,400,150,400,80,"DECK 2","MUSIC FROM",g->deck==1?B_ACTIVE:B_NORMAL,CYAN);
        SB(a);sbs(&a,"NOW READING DECK ");sbu(&a,g->deck+1);sbs(&a," / ");if(g->wave.title[0])sbn(&a,g->wave.title,40);else sbs(&a,"NO TITLE");
        txn(c,16,250,a.s,2,64,TEXT);
        tx(c,16,274,g->valid?(g->playing?"TRACK READY / PLAYING":"TRACK READY / STOPPED"):g->wave.loading?"LOADING ANALYSIS":"NO TRACK OR NO WAVEFORM YET",1,g->valid?DIM:CORAL);
        button(c,0,330,800,80,"NEXT",NULL,B_PRIMARY,CYAN);
    }else if(st==1){
        SB(a);sbs(&a,"DECK ");sbu(&a,g->deck+1);sbs(&a," / ");if(g->wave.title[0])sbn(&a,g->wave.title,48);else sbs(&a,"NO TITLE");
        txn(c,16,58,a.s,2,64,TEXT);
        trace(c,g,16,80,768,64,g->source_position,600);
        tx(c,16,152,g->valid?(g->playing?"PLAYING":"STOPPED / PRESS PLAY ON THE DECK"):g->wave.loading?"LOADING ANALYSIS / WAIT, OR RETURN TO THE DECK":"NO TRACK OR NO WAVEFORM ON THIS DECK",2,g->valid?TEXT:CORAL);
        SB(p);
        if(g->manual_pulse){sbs(&p,"TAP PULSE EVERY ");sbsec(&p,(float)per_of(g));}
        else if(g->valid&&conf>=.25f){sbs(&p,"ESTIMATED PULSE EVERY ");sbsec(&p,(float)per_of(g));}
        else if(g->valid)sbs(&p,"PULSE NOT CONFIDENT / TAP ONE");
        else sbs(&p,"NO PULSE WITHOUT A TRACK");
        tx(c,16,178,p.s,2,g->manual_pulse||conf>=.25f?GOLD:DIM);
        tx(c,16,202,g->wave.kind?"BANDS: DRUMS / HARMONICS / VOCALS":"BANDS: LOW / MID / HIGH",1,DIM);
        tx(c,16,218,"THE PULSE IS ESTIMATED FROM THE WAVEFORM. IT IS NOT A VERIFIED BEAT GRID OR BAR.",1,DIM);
        button(c,0,270,800,45,"TAP / ALIGN PULSE",NULL,B_NORMAL,CYAN);
        if(pulse_ready(g))button(c,0,330,800,80,"USE PULSE",g->manual_pulse?"TAP PULSE":"ESTIMATED PULSE",B_PRIMARY,CYAN);
        else if(g->valid)button(c,0,330,800,80,"TAP A PULSE","ESTIMATE NOT CONFIDENT",B_PRIMARY,CYAN);
        else button(c,0,330,800,80,"LOAD A TRACK / RETURN TO DECK",g->wave.loading?"STILL LOADING / WAITING HERE ALSO WORKS":"NO TRACK",B_PRIMARY,CORAL);
    }else if(st==2){
        tx(c,16,58,"TAP YOUR ACTION BUTTON OR THE BOX 4+ TIMES WITH THE MUSIC",2,TEXT);
        tx(c,16,80,"THIS MAKES A TAP PULSE FROM YOUR TIMING. IT IS NOT AN AUTO-DETECTED METER OR BAR.",1,DIM);
        double f=fracd(phase_of(g));xz_rect(c,16,98,768,16,PANEL);xz_rect(c,16,98,2,16,GOLD);xz_rect(c,782,98,2,16,GOLD);
        if(g->valid)xz_rect(c,16+(int)(f*760),96,8,20,GOLD);
        tx(c,16,120,"PULSE PHASE",1,DIM);
        SB(t);sbs(&t,"TAPS ");sbu(&t,g->tap_count);sbs(&t,g->manual_pulse?" / TAP PULSE SET":g->tap_count<4?" OF 4":" / KEEP STEADY");
        tx(c,16,138,t.s,2,g->manual_pulse?GOLD:TEXT);
        SB(o);sbs(&o,"OFFSET ");sbi(&o,roundi(g->timing_ms));sbs(&o," MS");txr(c,784,138,o.s,2,TEXT);
        SB(ab);sbs(&ab,"WITH THE MUSIC / ACTION = ");sbs(&ab,action_full(g,g->control_deck?1u:0u));
        button(c,0,165,800,105,"TAP HERE OR PRESS ACTION",g->playing?ab.s:"PRESS PLAY ON THE DECK FIRST",g->playing?B_PRIMARY:B_OFF,CYAN);
        button(c,0,285,400,40,"-20 MS",NULL,B_NORMAL,CYAN);
        button(c,400,285,400,40,"+20 MS",NULL,B_NORMAL,CYAN);
        if(g->manual_pulse||conf>=.25f)button(c,0,345,800,65,"USE THIS PULSE",g->manual_pulse?"TAP PULSE":"ESTIMATED PULSE",B_PRIMARY,CYAN);
        else button(c,0,345,800,65,"USE THIS PULSE","NEEDS 4 STEADY TAPS",B_OFF,CYAN);
    }else{
        tx(c,16,58,"PICK THE JOG YOU WILL PLAY",2,TEXT);
        SB(ci);sbs(&ci,"TAP CALIBRATE, TURN THAT JOG ONE FULL TURN CLOCKWISE, THEN PRESS ACTION (");
        sbs(&ci,action_full(g,g->cal_deck?1u:0u));sbs(&ci,").");tx(c,16,80,ci.s,1,DIM);
        tx(c,16,96,"SKIPPING IS FINE. THE DEFAULT IS ABOUT 8378 PULSES PER TURN.",1,DIM);
        button(c,0,140,400,70,"LEFT JOG","DECK 1",g->cal_deck==0?B_ACTIVE:B_NORMAL,CYAN);
        button(c,400,140,400,70,"RIGHT JOG","DECK 2",g->cal_deck==1?B_ACTIVE:B_NORMAL,CYAN);
        button(c,0,240,800,55,g->calibrating?"TURNING / PRESS ACTION WHEN DONE":"CALIBRATE ONE TURN",g->calibrating?"ONE FULL TURN CLOCKWISE":NULL,g->calibrating?B_ACTIVE:B_PRIMARY,GOLD);
        unsigned d=g->cal_deck>1?1:g->cal_deck;
        SB(v);sbs(&v,"RAW ");sbu(&v,(unsigned)fabsf(clampf(g->cal_pulses[d],-1e6f,1e6f)));sbs(&v," / SAVED ");sbu(&v,(unsigned)clampf(g->turn_pulses[d],0,1e6f));sbs(&v," PER TURN");
        txc(c,400,306,v.s,2,TEXT,780);
        button(c,0,330,800,80,"CONTINUE",g->control_deck?"YOU PLAY WITH THE RIGHT JOG":"YOU PLAY WITH THE LEFT JOG",B_PRIMARY,CYAN);
    }
    button(c,0,430,180,50,"BACK",NULL,B_NORMAL,CYAN);
    button(c,180,430,420,50,"BUTTONS","REMAP THE ACTION BUTTON",B_NORMAL,GOLD);
    exit_button(c,600,430,200,50,"BACK TO DECK");
}

static void demo(cv c,unsigned m,int x,int y,int w,int h){
    xz_rect(c,x,y,w,h,PANEL2);xz_border(c,x,y,w,h,LINE);tx(c,x+8,y+6,"GESTURE",1,DIM);
    int cx=x+w/2,cy=y+h/2,base=y+h-34;
    switch(m){
    case LAB_PULSE_HOLD:
        xz_rect(c,cx-30,y+20,60,h-30,0x142150u);xz_rect(c,x+20,base-2,w-40,4,FAINT);
        xz_rect(c,cx-16,base-110,32,60,0x3a4d88u);xz_rect(c,cx-28,base-112,56,5,GOLD);tx(c,cx+34,base-114,"LET GO",1,GOLD);
        xz_disc(c,cx,base-50,13,TEXT);xz_disc(c,cx,base-140,13,TEXT);xz_ring(c,cx,base,20,4,CYAN);break;
    case LAB_WAVE_LANES:case LAB_WHEEL_MATCH:{
        int lw=(w-40)/3;for(int i=0;i<=3;i++)xz_rect(c,x+20+i*lw,y+20,2,h-30,LINE);
        xz_rect(c,x+20,base-2,w-40,4,FAINT);
        xz_disc(c,x+20+lw/2,base-120,12,TEXT);xz_disc(c,x+20+2*lw+lw/2,base-60,12,TEXT);
        int car=x+20+lw+lw/2;xz_round_rect(c,car-18,base-8,36,16,8,15,CYAN);ln(c,car+24,base,car+lw-24,base,3,DIM);
        if(m==LAB_WHEEL_MATCH)xz_ring(c,car,base,26,4,CYAN);
        tx(c,x+w-96,y+6,m==LAB_WAVE_LANES?"A: NO BUTTON":"B: + ACTION",1,GOLD);break;}
    case LAB_SCRATCH:
        arrow(c,cx-60,cy-40,1,TEXT);tx(c,cx-6,cy-47,"CW",2,CYAN);arrow(c,cx-60,cy+30,-1,TEXT);tx(c,cx-6,cy+23,"CCW",2,VIOLET);
        xz_ring(c,cx+110,cy,34,3,FAINT);ln(c,cx+110,cy,cx+130,cy-26,4,CYAN);break;
    case LAB_RIBBON:
        for(int yy=y+20;yy<base;yy++){int c0=cx+xz_sin1024((yy-y)*2)*60/1024;xz_rect(c,c0-40,yy,80,1,xz_blend(PANEL2,VIOLET,70));xz_rect(c,c0-40,yy,3,1,VIOLET);xz_rect(c,c0+37,yy,3,1,VIOLET);}
        xz_rect(c,x+20,base-2,w-40,4,FAINT);xz_disc(c,cx+xz_sin1024((base-y)*2)*60/1024,base,12,GOLD);break;
    case LAB_ECHO:{
        int sw=(w-40)/8;for(int j=0;j<8;j++)xz_round_rect(c,x+20+j*sw+2,cy-24,sw-4,48,6,15,j<4?xz_blend(PANEL2,VIOLET,40):xz_blend(PANEL2,CYAN,40));
        static const int hit[4]={1,1,0,1};
        for(int j=0;j<4;j++)if(hit[j]){xz_disc(c,x+20+j*sw+sw/2,cy,11,VIOLET);xz_ring(c,x+20+(j+4)*sw+sw/2,cy,11,3,CYAN);}
        txc(c,x+20+2*sw,cy-44,"WATCH",2,VIOLET,4*sw);txc(c,x+20+6*sw,cy-44,"ANSWER",2,CYAN,4*sw);break;}
    case LAB_STEPS:{
        int cell=28,bx=cx-70,by=cy-70;for(int i=0;i<25;i++)xz_round_rect(c,bx+(i%5)*cell+2,by+(i/5)*cell+2,cell-4,cell-4,4,15,PANEL);
        xz_ring(c,bx+4*cell+cell/2,by+2*cell+cell/2,10,3,GOLD);xz_disc(c,bx+2*cell+cell/2,by+2*cell+cell/2,10,CYAN);
        ln(c,bx+2*cell+cell/2,by+2*cell+cell/2,bx+3*cell+cell/2,by+2*cell+cell/2,4,TEXT);break;}
    case LAB_BLASTER:{
        int ex,ey;xz_ring(c,cx,cy+10,70,1,LINE);
        static const float a[3]={-1.f,0.f,.9f};
        for(int i=0;i<3;i++){polar(cx,cy+10,a[i],70,&ex,&ey);xz_disc(c,ex,ey,11,TEXT);xz_ring(c,ex,ey,11,3,CYAN);}
        polar(cx,cy+10,-.3f,86,&ex,&ey);ln(c,cx,cy+10,ex,ey,1,FAINT);polar(cx,cy+10,.3f,86,&ex,&ey);ln(c,cx,cy+10,ex,ey,1,FAINT);
        ln(c,cx,cy+10,cx,cy-20,6,CYAN);xz_disc(c,cx,cy+10,11,CYAN);break;}
    case LAB_PUZZLE:{
        int cell=30,bx=cx-90,by=cy-50;static const unsigned char gdemo[24]={0,0,0,0,0,0, 0,0,0,0,0,0, 0,2,2,0,0,0, 1,2,2,1,1,2};
        for(int i=0;i<24;i++){if(gdemo[i])xz_round_rect(c,bx+(i%6)*cell+2,by+(i/6)*cell+2,cell-4,cell-4,4,15,gdemo[i]==1?PIECE1:VIOLET);}
        for(int k=0;k<3;k++)xz_border(c,bx+cell+k,by+2*cell+k,2*cell-2*k,2*cell-2*k,GOLD);
        xz_rect(c,bx+4*cell,by-6,3,4*cell+12,TEXT);xz_border(c,bx-1,by-1,6*cell+2,4*cell+2,LINE);break;}
    default:
        coil(c,x+70,cy-10,base,CYAN);
        for(int k=0;k<12;k++){double s0=k/12.0,s1=(k+1)/12.0;ln(c,x+70+(int)(s0*200),cy-14-(int)(4*50*s0*(1-s0)),x+70+(int)(s1*200),cy-14-(int)(4*50*s1*(1-s1)),3,GOLD);}
        xz_ring(c,x+w-50,base,14,3,GOLD);xz_disc(c,x+w-50,base,5,GOLD);break;
    }
}

static void brief_screen(cv c,const struct xz_rhythm_lab *g){
    unsigned m=g->mode<XZ_LAB_CASES?(unsigned)g->mode:0;const struct xz_lab_description *d=xz_lab_description(m);
    SB(t);case_title(&t,m,"STUDY ");header(c,t.s,"A GAME BY VJ OPTICMYSTIC / RHYTHM LAB / 10 STUDIES / PROTOTYPE");
    SB(ins);sbs(&ins,d->instruction);txn(c,16,60,ins.s,2,64,TEXT);
    SB(q);sbs(&q,"TESTS: ");sbs(&q,d->question);tx(c,16,84,q.s,1,DIM);
    SB(r);sbs(&r,"LIKE: ");sbs(&r,d->reference);tx(c,16,98,r.s,1,DIM);
    int ok=pulse_ready(g);
    xz_round_rect(c,16,122,384,30,8,15,ok?xz_blend(BG,CYAN,70):xz_blend(BG,CORAL,70));
    txc(c,208,130,!g->valid?"NO TRACK / OPEN MUSIC SETUP":!ok?"TRACK OK / PULSE NEEDS SETUP":g->study?"READY / STUDY PATTERN":"READY / TRACK SOURCE",2,TEXT,376);
    SB(s);sbs(&s,"SOURCE ");sbs(&s,source_tag(g));txn(c,16,162,s.s,1,64,DIM);
    SB(k);sbs(&k,"CLOCK ");sbs(&k,clock_tag(g));sbs(&k," / NO BARS");txn(c,16,176,k.s,1,64,DIM);
    unsigned players=xz_lab_players(g);
    if(players==2){
        SB(p1);sbs(&p1,"P1 LEFT / ACTION ");sbs(&p1,action_full(g,0));txn(c,16,196,p1.s,2,32,CYAN);
        SB(p2);sbs(&p2,"P2 RIGHT / ACTION ");sbs(&p2,action_full(g,1));txn(c,16,216,p2.s,2,32,CORAL);
    }else{
        unsigned dk=g->control_deck?1u:0u;
        SB(p1);sbs(&p1,dk?"RIGHT JOG / ACTION ":"LEFT JOG / ACTION ");sbs(&p1,action_full(g,dk));txn(c,16,196,p1.s,2,32,CYAN);
        txn(c,16,218,g->duo?"SOLO STUDY / DUO ONLY IN 01/02/03/08 / BUTTONS ON MENU":"SOLO STUDY / CHANGE BUTTONS ON THE MENU",1,64,g->duo?CORAL:DIM);
    }
    tx(c,16,240,"FIRST 4 PULSES ARE PRACTICE",2,GOLD);
    tx(c,16,262,"THEN 90 SECONDS / NO EARLY FAIL",2,TEXT);
    if(m==LAB_WAVE_LANES)txn(c,16,288,"A/B WITH 03: SAME LANES, POSITION ONLY (A)",1,64,GOLD);
    if(m==LAB_WHEEL_MATCH)txn(c,16,288,"A/B WITH 02: SAME LANES PLUS AN ACTION PRESS (B)",1,64,GOLD);
    if(m==LAB_ECHO)txn(c,16,288,"STUDY PATTERN / NOT THE SONG'S NOTES",1,64,DIM);
    demo(c,m,416,122,368,196);
    if(ok)button(c,0,330,800,80,"START 90 SECOND STUDY","FIRST 4 PULSES ARE PRACTICE / ACTION ALSO STARTS",B_PRIMARY,CYAN);
    else button(c,0,330,800,80,"SET UP MUSIC FIRST","THIS OPENS MUSIC SETUP",B_PRIMARY,CORAL);
    button(c,0,430,220,50,"MENU",NULL,B_NORMAL,CYAN);
    button(c,220,430,580,50,"MUSIC SETUP",NULL,B_NORMAL,CYAN);
}

static const char *const hints[XZ_LAB_CASES]={
    "ACTION TAPS DOTS / HOLD ACTION THROUGH STRIPES","TURN THE JOG TO CHANGE LANE / NO BUTTON",
    "TURN TO THE LANE / TAP ACTION AS IT LANDS","FLICK THE JOG CW OR CCW WITH EACH ARROW",
    "HOLD ACTION / STEER INSIDE THE VIOLET RIBBON","WATCH 4 PULSES / ANSWER WITH ACTION IN THE NEXT 4",
    "JOG POINTS / ACTION STEPS / ON PULSE SCORES MORE","JOG AIMS / ACTION FIRES ANY TIME / ON PULSE BONUS",
    "MAKE SAME-COLOUR 2X2 / ACTION DROPS / PAD B FLIPS","HOLD ACTION / TURN CCW / RELEASE ON THE DOT"};

static void run_screen(cv c,const struct xz_rhythm_lab *g){
    unsigned m=g->mode<XZ_LAB_CASES?(unsigned)g->mode:0,players=xz_lab_players(g);
    SB(t);case_title(&t,m,"");SB(sub);provenance(&sub,g);
    header(c,"",sub.s);txn(c,16,9,t.s,2,34,TEXT);
    int practice=g->position<g->practice_until;
    SB(tl);
    if(practice)sbs(&tl,"PRACTICE");
    else{float left=g->duration-g->elapsed;if(left<0)left=0;sbu(&tl,(unsigned)ceilf(left));sbs(&tl," S LEFT");}
    txr(c,690,9,tl.s,2,practice?GOLD:TEXT);
    txc(c,400,58,hints[m],2,TEXT,790);
    txc(c,400,80,"PAD F RATE / G RETRY / H MENU / PLAY RUNS THE MUSIC",1,DIM,790);
    if(players==2){field(c,g,0,20,360,2);field(c,g,1,420,360,2);}
    else field(c,g,0,40,720,1);
    if(practice){xz_round_rect(c,120,358,560,22,10,15,xz_blend(BG,GOLD,70));txc(c,400,362,"PRACTICE / FIRST 4 PULSES / NOT SCORED",2,GOLD,550);}
    else if(players==2)txc(c,400,364,"DUO / P1 LEFT JOG / P2 RIGHT JOG / SAME MUSIC CLOCK",1,DIM,790);
    else txc(c,400,364,g->duo?"SOLO STUDY / DUO ONLY IN 01/02/03/08":g->control_deck?"SOLO STUDY / RIGHT JOG":"SOLO STUDY / LEFT JOG",1,g->duo?CORAL:DIM,790);
    trace(c,g,0,382,800,34,g->position,per_of(g)*8);
    txn(c,6,384,g->wave.kind?"DRUMS / HARMONICS / VOCALS":"LOW / MID / HIGH",1,40,DIM);
    int paused=g->phase==LAB_PAUSE,held=!!g->player[0].held;
    button(c,0,430,160,50,"RETRY","PAD G",B_NORMAL,CYAN);
    button(c,160,430,160,50,"BUTTONS",action_short(g,deck_of(g,0)),B_NORMAL,GOLD);
    if(m==LAB_WAVE_LANES||m==LAB_SCRATCH)button(c,320,430,200,50,"JOG ONLY","NO ACTION IN THIS STUDY",B_OFF,CYAN);
    else{
        SB(ac);sbs(&ac,"TOUCH OR ");sbs(&ac,action_full(g,deck_of(g,0)));
        button(c,320,430,200,50,"ACTION",paused?"PAUSED":held?"HELD":ac.s,paused?B_OFF:held?B_ACTIVE:B_PRIMARY,CYAN);
    }
    button(c,520,430,120,50,"RATE","PAD F",B_NORMAL,GOLD);
    button(c,640,430,160,50,"MENU","PAD H",B_NORMAL,CYAN);
    if(paused){
        xz_round_rect(c,196,156,408,128,14,15,GOLD);xz_round_rect(c,199,159,402,122,12,15,PANEL2);
        int ended=g->wave.position>=g->wave.total-1;
        txc(c,400,178,ended?"TRACK ENDED":"MUSIC PAUSED",2,GOLD,390);
        if(ended)button(c,220,204,360,42,"BACK TO DECK",NULL,B_PRIMARY,CYAN);
        else txc(c,400,208,"PRESS PLAY ON THE DECK",2,TEXT,390);
        if(!ended)txc(c,400,240,"THE STUDY CONTINUES WITH THE MUSIC",1,DIM,390);
        txc(c,400,256,"BUTTONS, MENU AND DECK STILL WORK",1,DIM,390);
    }
}

static void stats(cv c,const struct xz_rhythm_lab *g,unsigned i,unsigned players,int y){
    const struct xz_lab_player *p=&g->player[i];SB(a);SB(b);
    if(players==2)sbs(&a,i?"P2  ":"P1  ");
    unsigned wrong=p->timing_hits>p->motion_hits?p->timing_hits-p->motion_hits:0,plain=p->misses>wrong?p->misses-wrong:0;
    switch(g->mode){
    case LAB_RIBBON:sbs(&a,"IN RIBBON ");sbpct(&a,p->coverage_total>0?p->coverage/p->coverage_total:0);sbs(&a," OF STUDY TIME");break;
    case LAB_PUZZLE:sbs(&a,"CLEARED ");sbu(&a,p->cleared);sbs(&a," CELLS / ");sbu(&a,p->hits);sbs(&a," SWEEPS / ");sbu(&a,p->attacks);sbs(&a," DROPS");break;
    case LAB_STEPS:sbs(&a,"TARGETS ");sbu(&a,p->collections);sbs(&a," / ON PULSE ");sbu(&a,p->hits);sbs(&a," OF ");sbu(&a,p->hits+p->misses);sbs(&a," STEPS");break;
    case LAB_BLASTER:sbs(&a,"HITS ");sbu(&a,p->hits);sbs(&a," OF ");sbu(&a,p->shots);sbs(&a," SHOTS / ON PULSE ");sbu(&a,p->timing_hits);break;
    case LAB_SCRATCH:sbs(&a,"CLEAN ");sbu(&a,p->hits);sbs(&a," / WRONG WAY ");sbu(&a,wrong);sbs(&a," / MISSED ");sbu(&a,plain);break;
    case LAB_WHEEL_MATCH:sbs(&a,"HITS ");sbu(&a,p->hits);sbs(&a," / WRONG LANE ");sbu(&a,wrong);sbs(&a," / MISSED ");sbu(&a,plain);break;
    case LAB_WAVE_LANES:sbs(&a,"CAUGHT ");sbu(&a,p->hits);sbs(&a," / MISSED ");sbu(&a,p->misses);break;
    default:sbs(&a,"PERFECT ");sbu(&a,p->perfects);sbs(&a," / GOOD ");sbu(&a,p->hits>p->perfects?p->hits-p->perfects:0);sbs(&a," / MISSED ");sbu(&a,p->misses);break;
    }
    tx(c,16,y,a.s,2,pcol(i));
    sbs(&b,"BEST STREAK ");sbu(&b,p->best_streak);
    if(p->timing_hits&&g->mode!=LAB_WAVE_LANES&&g->mode!=LAB_RIBBON&&g->mode!=LAB_PUZZLE){sbs(&b," / LAST TIMING ");sbi(&b,roundi(p->last_error_ms));sbs(&b," MS");}
    unsigned t10=(unsigned)(clampf(p->travel,0,1e6f)/(2*LAB_PI)*10+.5f);
    sbs(&b," / JOG ");sbu(&b,t10/10);sbs(&b,".");sbu(&b,t10%10);sbs(&b," TURNS / SCORE ");sbu(&b,p->score);
    tx(c,16,y+20,b.s,1,DIM);
}

static void results_screen(cv c,const struct xz_rhythm_lab *g){
    unsigned m=g->mode<XZ_LAB_CASES?(unsigned)g->mode:0,players=xz_lab_players(g);const struct xz_lab_record *r=&g->record[m];
    SB(t);case_title(&t,m,"RESULTS / ");header(c,t.s,"A GAME BY VJ OPTICMYSTIC / A HIGH SCORE IS NOT THE SAME AS FUN");
    stats(c,g,0,players,58);
    if(players==2)stats(c,g,1,players,100);
    else{SB(s);sbs(&s,"SOURCE ");sbs(&s,source_tag(g));sbs(&s," / CLOCK ");sbs(&s,clock_tag(g));sbs(&s," / PLAYED ");sbu(&s,(unsigned)r->seconds);sbs(&s," SECONDS");txn(c,16,100,s.s,1,128,DIM);}
    tx(c,16,140,"RATE THIS STUDY / TOUCH 1 TO 5",1,TEXT);
    static const char *const rows[3]={"FUN","CLARITY","EFFORT"},*const notes[3]={"1 DULL / 5 FUN","1 CONFUSING / 5 CLEAR","1 RELAXED / 5 TIRING"};
    unsigned vals[3]={r->fun,r->clarity,r->effort};
    for(int row=0;row<3;row++){
        int y=164+56*row;
        tx(c,16,y+12,rows[row],2,TEXT);tx(c,16,y+34,notes[row],1,row==2?GOLD:DIM);
        for(int i=0;i<5;i++){
            int on=vals[row]==(unsigned)(i+1),bx=230+88*i;
            xz_game_target_add(targets,bx+4,y+4,80,48,XZ_GAME_NORMAL);
            xz_round_rect(c,bx+4,y+4,80,48,8,15,on?GOLD:LINE);
            if(!on)xz_round_rect(c,bx+6,y+6,76,44,7,15,PANEL);
            char n[2]={(char)('1'+i),0};txc(c,bx+44,y+21,n,2,on?BG:TEXT,40);
        }
        if(!vals[row])tx(c,680,y+24,"NOT RATED",1,FAINT);
    }
    button(c,0,350,266,58,"REPLAY",NULL,B_NORMAL,CYAN);
    button(c,266,350,268,58,"EXTEND 90 S",NULL,B_NORMAL,CYAN);
    button(c,534,350,266,58,g->mode==LAB_GLYPH?"TEN STUDIES":"NEXT STUDY",NULL,B_PRIMARY,CYAN);
    button(c,0,430,800,50,"MENU",NULL,B_NORMAL,CYAN);
}

static void ratings_screen(cv c,const struct xz_rhythm_lab *g){
    header(c,"RATINGS / 10 STUDIES","VALUES AS ENTERED / NO AVERAGES / - MEANS NOT RATED / A GAME BY VJ OPTICMYSTIC");
    for(unsigned n=0;n<XZ_LAB_STUDIES;n++){
        const struct xz_lab_record *r=&g->record[n];int y=56+36*(int)n;
        xz_rect(c,8,y,784,32,n%2?PANEL2:PANEL);
        SB(num);sb2(&num,n+1);tx(c,16,y+9,num.s,2,GOLD);
        txn(c,56,y+9,xz_lab_description(n)->name,2,18,TEXT);
        SB(pl);sbs(&pl,"PLAYED ");sbu(&pl,r->played);tx(c,290,y+12,pl.s,1,r->played?DIM:FAINT);
        static const char *const lab[3]={"FUN ","CLARITY ","EFFORT "};unsigned v[3]={r->fun,r->clarity,r->effort};int xs[3]={380,496,628};
        for(int k=0;k<3;k++){SB(s);sbs(&s,lab[k]);if(v[k])sbu(&s,v[k]);else sbs(&s,"-");tx(c,xs[k],y+9,s.s,2,v[k]?TEXT:FAINT);}
    }
    button(c,0,424,800,56,"MENU",NULL,B_NORMAL,CYAN);
}

static void controls_screen(cv c,const struct xz_rhythm_lab *g){
    header(c,"BUTTONS / ACTION BUTTON PER DECK","SESSION ONLY / KEPT ACROSS STUDIES, RESET WHEN THE CONTROLLER RUNTIME RESTARTS");
    static const char *const subs[5]={"DEFAULT","IGNORES CUE IN THE LAB","IGNORES CUE IN THE LAB","IGNORES CUE IN THE LAB","IGNORES CUE IN THE LAB"};
    unsigned players=xz_lab_players(g);
    for(unsigned d=0;d<2;d++){
        int x=d?400:0;uint32_t pc=pcol(d);
        int playing=players==2||(g->control_deck?1u:0u)==d;
        tx(c,x+16,58,d?"DECK 2 / RIGHT JOG":"DECK 1 / LEFT JOG",2,pc);
        SB(now);sbs(&now,"ACTION = ");sbs(&now,action_full(g,d));
        txn(c,x+16,80,now.s,2,30,TEXT);
        txn(c,x+16,100,playing?"PLAYS THIS STUDY":"NOT PLAYING THIS STUDY",1,60,playing?GOLD:FAINT);
        unsigned v=binding_of(g,d);
        for(unsigned i=0;i<5;i++)button(c,x,120+58*(int)i,400,50,binding_names[i],subs[i],v==i?B_ACTIVE:B_NORMAL,pc);
    }
    txc(c,400,406,"PAD ONLY: THE PHYSICAL CUE BUTTON DOES NOTHING IN THE LAB FOR THAT DECK",1,TEXT,790);
    txc(c,400,418,"PAD B FLIPS IN 09 / PAD F RATE / PAD G RETRY / PAD H MENU STAY THE SAME",1,DIM,790);
    button(c,0,430,800,50,"BACK",NULL,B_NORMAL,CYAN);
}

int xz_lab_render(const struct xz_rhythm_lab *g,uint16_t *pixels,size_t count,size_t stride){
    return xz_lab_render_targets(g,pixels,count,stride,NULL);
}
int xz_lab_render_targets(const struct xz_rhythm_lab *g,uint16_t *pixels,size_t count,size_t stride,struct xz_game_targets *out){
    if(out)out->count=0;
    if(!g||!pixels||stride<800)return 0;
    if(stride>(SIZE_MAX-800)/479||count<stride*479+800)return 0;
    if(g->mode==LAB_GLYPH&&(g->phase==LAB_BRIEF||g->phase==LAB_RUN||g->phase==LAB_PAUSE))return xz_glyph_render_targets(g,pixels,count,stride,out);
    targets=out;
    cv c;c.p=pixels;c.stride=stride;c.width=800;c.height=480;c.theme=0;
    xz_rect(c,0,0,800,480,BG);
    switch(g->phase){
    case LAB_SETUP:setup_screen(c,g);break;
    case LAB_BRIEF:brief_screen(c,g);break;
    case LAB_RUN:case LAB_PAUSE:run_screen(c,g);break;
    case LAB_RESULTS:results_screen(c,g);break;
    case LAB_RATINGS:ratings_screen(c,g);break;
    case LAB_CONTROLS:controls_screen(c,g);break;
    default:menu_screen(c,g);break;
    }
    targets=NULL;
    return 1;
}
