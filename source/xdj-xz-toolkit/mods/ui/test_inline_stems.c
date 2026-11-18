#ifdef NDEBUG
#error Inline acceptance requires active assertions
#endif
#include "ui.h"
#include "wave_viewport.h"
#include "../pc_control/stems.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
_Static_assert((int)XZ_UI_PC_DRUMS==(int)XZ_PC_DRUMS&&(int)XZ_UI_PC_INST==(int)XZ_PC_INST&&(int)XZ_UI_PC_BASS==(int)XZ_PC_BASS&&(int)XZ_UI_PC_VOCAL==(int)XZ_PC_VOCAL&&(int)XZ_UI_PC_ROLES==(int)XZ_PC_ROLES,"PC roles match the backend");
_Static_assert((int)XZ_UI_PC_CAP_TOGGLE==(int)XZ_PC_TOGGLE&&(int)XZ_UI_PC_CAP_FEEDBACK==(int)XZ_PC_FEEDBACK,"PC caps match the backend");
static uint16_t frame[540*XZ_WAVE_INLINE_HEIGHT+8];
static uint16_t other[540*XZ_WAVE_INLINE_HEIGHT+8];
static size_t press(struct xz_ui *u,const struct xz_ui_model *m,int x,int y,struct xz_ui_action *a){
    size_t n=xz_ui_inline_touch(u,m,536,XZ_WAVE_INLINE_HEIGHT,x,y,1,a);
    if(n)return n;
    return xz_ui_inline_touch(u,m,536,XZ_WAVE_INLINE_HEIGHT,x,y,0,a);
}
static struct xz_ui_widget pc_widget(const struct xz_ui *u,const struct xz_ui_model *m,enum xz_ui_action_kind kind,int index,int deck){
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_inline_layout(u,m,536,XZ_WAVE_INLINE_HEIGHT,w);
    for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index&&w[i].deck==deck)return w[i];
    assert(!"missing PC widget");return w[0];
}
/* PC rows drive rekordbox over USB; local muted/levels/bypass are never touched. */
static void pc_rows(void){
    struct xz_ui u;struct xz_ui_model m={0},local={0},snapshot;struct xz_ui_action a[XZ_UI_ACTIONS];
    struct xz_ui_widget base[XZ_UI_WIDGETS],w[XZ_UI_WIDGETS];
    xz_ui_init(&u);m.enabled=XZ_UI_STEM;
    for(int deck=0;deck<2;deck++){m.deck[deck].ready=XZ_UI_STEM;for(int i=0;i<3;i++)m.deck[deck].levels[i]=1;}
    local=m;
    /* LOCAL source keeps layout and pixels byte-identical whatever the PC store holds. */
    assert(xz_ui_inline_layout(&u,&local,536,XZ_WAVE_INLINE_HEIGHT,base)==8);
    for(int deck=0;deck<2;deck++)m.deck[deck].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_LOCAL,.connected=1,.verified=1,.caps=3,.known=15,.active=15,.pending=15,.reason="IGNORED",.setup_step=XZ_UI_PC_STEP_DONE,.available=15,.target=0,.epoch=1};
    assert(xz_ui_inline_layout(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,w)==8&&!memcmp(base,w,8*sizeof(*w)));
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        m.theme=local.theme=theme;
        for(unsigned i=0;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=other[i]=0xabcd;
        assert(xz_ui_inline_render(&u,&local,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
        assert(xz_ui_inline_render(&u,&m,other,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
        assert(!memcmp(frame,other,sizeof(frame)));
    }
    m.theme=local.theme=0;
    /* Deck 1 PC, deck 2 local: one button per role, the source tag last, then
       deck 2's four exactly as before. No PC bypass is shown as a stem. */
    m.deck[0].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_PC,.connected=1,.verified=0,.caps=0,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=XZ_UI_PC_STEP_TEST,.available=15,.target=0,.epoch=1};
    static const struct {unsigned available;int count,roles[4];} modes[]={
        {15,4,{XZ_UI_PC_VOCAL,XZ_UI_PC_INST,XZ_UI_PC_BASS,XZ_UI_PC_DRUMS}},
        {11,3,{XZ_UI_PC_VOCAL,XZ_UI_PC_INST,XZ_UI_PC_DRUMS}},
        {0,3,{XZ_UI_PC_VOCAL,XZ_UI_PC_INST,XZ_UI_PC_DRUMS}}};
    size_t n;
    for(size_t k=0;k<sizeof(modes)/sizeof(*modes);k++){
        m.deck[0].pc_stems.available=modes[k].available;int count=modes[k].count;
        n=xz_ui_inline_layout(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,w);assert(n==(size_t)count+1+4);
        for(int slot=0;slot<count;slot++){
            assert(w[slot].kind==XZ_UI_PC_TOGGLE&&w[slot].index==modes[k].roles[slot]&&w[slot].deck==0);
            assert(w[slot].x==536*slot/(count+1)+1&&w[slot].w==536*(slot+1)/(count+1)-536*slot/(count+1)-2);
        }
        assert(w[count].kind==XZ_UI_PC_SETUP&&w[count].index==XZ_UI_PC_WHY_TAG&&!strcmp(w[count].label,"PC1"));
        assert(w[count].x+w[count].w==535);
        for(int i=0;i<=count;i++)assert(w[i].kind!=XZ_UI_PC_BYPASS&&w[i].kind!=XZ_UI_BYPASS&&w[i].kind!=XZ_UI_MUTE);
        /* Three roles sit exactly on the USB grid: VOCALS, HARMONICS, DRUMS, BYPASS columns. */
        if(count==3)for(int slot=0;slot<4;slot++)assert(w[slot].x==base[slot].x&&w[slot].w==base[slot].w&&w[slot].y==base[slot].y&&w[slot].h==base[slot].h);
        for(int i=0;i<4;i++)assert(!memcmp(&w[count+1+i],&base[4+i],sizeof(*w)));
        for(size_t i=0;i<n;i++){
            assert(w[i].x>=0&&w[i].x+w[i].w<=536&&w[i].y>=0&&w[i].y+w[i].h<=XZ_WAVE_INLINE_HEIGHT&&w[i].w>=100&&w[i].h>=40);
            for(size_t q=i+1;q<n;q++)assert(w[i].x+w[i].w<=w[q].x||w[q].x+w[q].w<=w[i].x||w[i].y+w[i].h<=w[q].y||w[q].y+w[q].h<=w[i].y);
        }
    }
    m.deck[0].pc_stems.available=15;
    /* Locked rows lead to setup with the specific reason; no local or PC stem action. */
    struct {struct xz_ui_pc_stems pc;enum xz_ui_pc_reason why;} locked[]={
        {{.source=XZ_UI_SOURCE_UNKNOWN,.connected=1,.verified=1,.caps=1,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=0,.available=15,.target=0,.epoch=1},XZ_UI_PC_WHY_SOURCE},
        {{.source=XZ_UI_SOURCE_PC,.connected=0,.verified=1,.caps=1,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=0,.available=15,.target=0,.epoch=1},XZ_UI_PC_WHY_LINK},
        {{.source=XZ_UI_SOURCE_PC,.connected=1,.verified=0,.caps=1,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=0,.available=15,.target=0,.epoch=1},XZ_UI_PC_WHY_UNVERIFIED},
        {{.source=XZ_UI_SOURCE_PC,.connected=1,.verified=1,.caps=XZ_UI_PC_CAP_FEEDBACK,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=0,.available=15,.target=0,.epoch=1},XZ_UI_PC_WHY_UNVERIFIED}};
    for(size_t k=0;k<sizeof(locked)/sizeof(*locked);k++){
        m.deck[0].pc_stems=locked[k].pc;snapshot=m;assert(xz_ui_pc_blocked(&m,0)==locked[k].why);
        for(int role=0;role<4;role++){
            struct xz_ui_widget r=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,role,0);
            assert(press(&u,&m,r.x+r.w/2,r.y+r.h/2,a)==1);
            assert(a[0].kind==XZ_UI_PC_SETUP&&a[0].deck==0&&a[0].index==(int)locked[k].why&&a[0].phase==XZ_UI_PRESS);
            assert(!strcmp(u.notice,xz_ui_pc_reason_text(locked[k].why))&&u.notice_deck==0);
            xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,r.x+r.w/2,r.y+r.h/2,0,a);
            assert(!u.down&&u.held_kind==XZ_UI_NONE);
        }
        /* A runtime reason replaces the built-in text for a locked deck. */
        m.deck[0].pc_stems.reason="DJ SOFTWARE NOT IN PERFORMANCE MODE";
        struct xz_ui_widget r=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_BASS,0);
        assert(press(&u,&m,r.x+4,r.y+4,a)==1&&!strcmp(u.notice,"DJ SOFTWARE NOT IN PERFORMANCE MODE"));
        xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,0,0,0,a);
        m.deck[0].pc_stems.reason=NULL;
        assert(!memcmp(&snapshot,&m,sizeof(m)));
    }
    /* The source tag keeps the deck identity and opens setup. */
    m.deck[0].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_PC,.connected=1,.verified=1,.caps=XZ_UI_PC_CAP_TOGGLE,.known=0,.active=0,.pending=0,.reason="UNUSED",.setup_step=XZ_UI_PC_STEP_DONE,.available=15,.target=0,.epoch=1};
    assert(xz_ui_pc_blocked(&m,0)==XZ_UI_PC_WHY_NONE);
    struct xz_ui_widget t=pc_widget(&u,&m,XZ_UI_PC_SETUP,XZ_UI_PC_WHY_TAG,0);
    assert(press(&u,&m,t.x+t.w/2,t.y+t.h/2,a)==1&&a[0].kind==XZ_UI_PC_SETUP&&a[0].index==XZ_UI_PC_WHY_TAG&&a[0].deck==0);
    xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,0,0,0,a);
    /* Verified: a tap sends one toggle on release, for every role on both decks. */
    m.deck[1].pc_stems=m.deck[0].pc_stems;snapshot=m;
    for(int deck=0;deck<2;deck++)for(int role=0;role<4;role++){
        struct xz_ui_widget r=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,role,deck);int cx=r.x+r.w/2,cy=r.y+r.h/2;
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,1,a)==0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx+4,cy,1,a)==0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx+4,cy,0,a)==1);
        assert(a[0].kind==XZ_UI_PC_TOGGLE&&a[0].deck==deck&&a[0].index==role&&a[0].phase==XZ_UI_PRESS);
        assert(!u.down&&u.held_kind==XZ_UI_NONE&&u.capture<0);
        /* A level drag sends nothing and says why. */
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,1,a)==0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx+30,cy,1,a)==0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx-30,cy,1,a)==0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx-30,cy,0,a)==0);
        assert(!strcmp(u.notice,xz_ui_pc_reason_text(XZ_UI_PC_WHY_NO_LEVEL))&&u.notice_deck==deck);
        /* Cancel during a press yields only a RELEASE, which the contract makes a no-op. */
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,1,a)==0);
        assert(xz_ui_cancel(&u,a)==1&&a[0].kind==XZ_UI_PC_TOGGLE&&a[0].phase==XZ_UI_RELEASE);
        /* One gesture per role while the previous one is pending. */
        m.deck[deck].pc_stems.pending=1u<<role;
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,1,a)==0);
        assert(strstr(u.notice,"SENT")&&strstr(u.notice,r.label));
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,0,a)==0);
        m.deck[deck].pc_stems.pending=0;
    }
    assert(!memcmp(&snapshot,&m,sizeof(m)));
    /* Three-stem mode: exactly VOCAL, INST and DRUMS, each tap sends its own role. */
    m.deck[0].pc_stems.available=11;
    n=xz_ui_inline_layout(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,w);
    for(size_t i=0;i<n;i++)assert(!(w[i].deck==0&&w[i].kind==XZ_UI_PC_TOGGLE&&w[i].index==XZ_UI_PC_BASS));
    static const int three[3]={XZ_UI_PC_VOCAL,XZ_UI_PC_INST,XZ_UI_PC_DRUMS};
    for(int slot=0;slot<3;slot++){
        int cx=w[slot].x+w[slot].w/2,cy=w[slot].y+w[slot].h/2;
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,1,a)==0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,cx,cy,0,a)==1&&a[0].kind==XZ_UI_PC_TOGGLE&&a[0].index==three[slot]&&a[0].deck==0);
    }
    /* An unreported mode shows the shared three roles, locked, and leads to setup. */
    m.deck[0].pc_stems.available=0;
    struct xz_ui_widget unreported=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_INST,0);
    assert(press(&u,&m,unreported.x+10,unreported.y+10,a)==1&&a[0].kind==XZ_UI_PC_SETUP&&a[0].index==XZ_UI_PC_WHY_ROLE);
    assert(!strcmp(u.notice,xz_ui_pc_reason_text(XZ_UI_PC_WHY_ROLE)));xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,0,0,0,a);
    /* A mode change while a finger is down cancels: the buttons moved under it.
       VOCAL keeps its role but not its rectangle; DRUMS changes slot; BASS vanishes. */
    static const struct {unsigned from,to;int role;} flips[]={
        {11,15,XZ_UI_PC_DRUMS},{11,15,XZ_UI_PC_VOCAL},{15,11,XZ_UI_PC_BASS},{15,11,XZ_UI_PC_VOCAL},{15,0,XZ_UI_PC_INST}};
    for(size_t k=0;k<sizeof(flips)/sizeof(*flips);k++){
        m.deck[0].pc_stems.available=flips[k].from;
        struct xz_ui_widget r=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,flips[k].role,0);
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,r.x+10,r.y+10,1,a)==0&&u.held_kind==XZ_UI_PC_TOGGLE);
        m.deck[0].pc_stems.available=flips[k].to;
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,r.x+10,r.y+10,0,a)==0);
        assert(!u.down&&u.held_kind==XZ_UI_NONE&&u.capture<0);
        /* The next tap acts on the new row. */
        r=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,flips[k].to?XZ_UI_PC_DRUMS:XZ_UI_PC_INST,0);
        size_t sent=press(&u,&m,r.x+10,r.y+10,a);
        if(flips[k].to)assert(sent==1&&a[0].kind==XZ_UI_PC_TOGGLE&&a[0].index==XZ_UI_PC_DRUMS);
        else {assert(sent==1&&a[0].kind==XZ_UI_PC_SETUP&&a[0].index==XZ_UI_PC_WHY_ROLE);xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,0,0,0,a);}
    }
    /* Deck 1 changing mode does not cancel a deck 2 gesture. */
    m.deck[1].pc_stems=m.deck[0].pc_stems;m.deck[1].pc_stems.available=15;m.deck[0].pc_stems.available=11;
    struct xz_ui_widget other_deck=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_VOCAL,1);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,other_deck.x+10,other_deck.y+10,1,a)==0);
    m.deck[0].pc_stems.available=15;
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,other_deck.x+10,other_deck.y+10,0,a)==1&&a[0].kind==XZ_UI_PC_TOGGLE&&a[0].deck==1&&a[0].index==XZ_UI_PC_VOCAL);
    m.deck[1].pc_stems=snapshot.deck[1].pc_stems;
    m.deck[0].pc_stems.available=15;
    /* A software-target or full-width epoch change cancels the held gesture. */
    struct xz_ui_widget drums=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_DRUMS,0);
    for(int change=0;change<3;change++){
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,drums.x+10,drums.y+10,1,a)==0);
        if(change==0)m.deck[0].pc_stems.target=3;
        else if(change==1)m.deck[0].pc_stems.epoch+=0x1000000;
        else m.deck[0].pc_stems.source=XZ_UI_SOURCE_LOCAL;
        assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,drums.x+10,drums.y+10,0,a)==0);
    }
    m.deck[0].pc_stems=snapshot.deck[0].pc_stems;
    /* Mixed decks: deck 2 local controls still mute and drag exactly as before. */
    m.deck[1].pc_stems.source=XZ_UI_SOURCE_LOCAL;
    struct xz_ui_widget vocals=pc_widget(&u,&m,XZ_UI_MUTE,2,1);
    assert(press(&u,&m,vocals.x+10,vocals.y+10,a)==1&&a[0].kind==XZ_UI_MUTE&&a[0].deck==1&&a[0].index==2);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,vocals.x+10,vocals.y+10,1,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,vocals.x+60,vocals.y+10,1,a)==1&&a[0].kind==XZ_UI_LEVEL&&a[0].deck==1);
    xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,vocals.x+60,vocals.y+10,0,a);
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        m.theme=theme;u.notice[0]=0;
        /* Lit/unlit appears only with FEEDBACK; known+active without it draws as unknown. */
        m.deck[0].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_PC,.connected=1,.verified=1,.caps=XZ_UI_PC_CAP_TOGGLE,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=XZ_UI_PC_STEP_DONE,.available=15,.target=0,.epoch=1};
        for(unsigned i=0;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=other[i]=0xabcd;
        assert(xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
        m.deck[0].pc_stems.known=m.deck[0].pc_stems.active=15;
        assert(xz_ui_inline_render(&u,&m,other,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
        assert(!memcmp(frame,other,sizeof(frame)));
        m.deck[0].pc_stems.caps|=XZ_UI_PC_CAP_FEEDBACK;
        assert(xz_ui_inline_render(&u,&m,other,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
        assert(memcmp(frame,other,sizeof(frame)));
        /* Pending keeps the last known fill: only the stripe and status text change. */
        for(unsigned mask=11;mask<=15;mask+=4){
            m.deck[0].pc_stems.available=mask;m.deck[0].pc_stems.pending=0;m.deck[0].pc_stems.active=5;
            assert(xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            m.deck[0].pc_stems.pending=15;
            assert(xz_ui_inline_render(&u,&m,other,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            n=xz_ui_inline_layout(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,w);
            for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_PC_TOGGLE){
                size_t changed=0,area=(size_t)w[i].w*(size_t)w[i].h;
                for(int y=w[i].y;y<w[i].y+w[i].h;y++)for(int x=w[i].x;x<w[i].x+w[i].w;x++)changed+=frame[y*540+x]!=other[y*540+x];
                assert(changed*8<area);
                int fx=w[i].x+w[i].w-6,fy=w[i].y+w[i].h/2;
                assert(frame[fy*540+fx]==other[fy*540+fx]);
            }
            /* Lit DRUMS and unlit INST still differ while both are pending. */
            struct xz_ui_widget lit=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_DRUMS,0),dark=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_INST,0);
            assert(other[(lit.y+lit.h/2)*540+lit.x+lit.w-6]!=other[(dark.y+dark.h/2)*540+dark.x+dark.w-6]);
        }
        m.deck[0].pc_stems.available=15;m.deck[0].pc_stems.pending=0;m.deck[0].pc_stems.active=15;
        /* A finger on a sendable role shows at once, before release sends anything;
           only that button changes, and lifting restores the frame. */
        for(unsigned mask=11;mask<=15;mask+=4){
            m.deck[0].pc_stems.available=mask;
            struct xz_ui_widget r=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_INST,0);
            assert(xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,r.x+10,r.y+10,1,a)==0);
            assert(xz_ui_inline_render(&u,&m,other,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            size_t inside=0,outside=0;
            for(int y=0;y<XZ_WAVE_INLINE_HEIGHT;y++)for(int x=0;x<536;x++){
                int in=x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;
                if(frame[y*540+x]!=other[y*540+x]){if(in)inside++;else outside++;}
            }
            assert(inside>(size_t)(r.w*r.h)/2&&outside==0);
            assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,r.x+10,r.y+10,0,a)==1&&a[0].index==XZ_UI_PC_INST);
            assert(xz_ui_inline_render(&u,&m,other,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            assert(!memcmp(frame,other,sizeof(frame)));
        }
        m.deck[0].pc_stems.available=15;
        /* Every lock state renders inside the window and leaves stride padding alone. */
        for(int source=0;source<3;source++)for(int connected=0;connected<2;connected++){
            for(unsigned i=0;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=0xabcd;
            m.deck[0].pc_stems=(struct xz_ui_pc_stems){.source=source,.connected=connected,.verified=0,.caps=0,.known=0,.active=0,.pending=0,.reason=NULL,.setup_step=0,.available=(unsigned)(source*4+connected*11)&15u,.target=0,.epoch=1};m.deck[1].pc_stems.source=source;
            assert(xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            for(int y=0;y<XZ_WAVE_INLINE_HEIGHT;y++)for(int x=536;x<540;x++)assert(frame[y*540+x]==0xabcd);
            for(unsigned i=540*XZ_WAVE_INLINE_HEIGHT;i<sizeof(frame)/sizeof(*frame);i++)assert(frame[i]==0xabcd);
        }
        m.deck[1].pc_stems.source=XZ_UI_SOURCE_LOCAL;
        /* Bass gets its own theme colour wherever the palette has room for one. */
        uint32_t bass=xz_ui_pc_role_color(theme,XZ_UI_PC_BASS);
        assert(xz_ui_pc_role_color(theme,XZ_UI_PC_DRUMS)==xz_ui_stem_color(theme,0));
        assert(xz_ui_pc_role_color(theme,XZ_UI_PC_INST)==xz_ui_stem_color(theme,1));
        assert(xz_ui_pc_role_color(theme,XZ_UI_PC_VOCAL)==xz_ui_stem_color(theme,2));
        if(theme!=7)for(int stem=0;stem<3;stem++)assert(bass!=xz_ui_stem_color(theme,stem));
        /* PC stripes use the saved resolved colours, with BASS separated from them,
           in both modes and while a send is pending. */
        for(int stem=0;stem<3;stem++)m.stem_colors[stem]=stem+3;
        static const int roles[3]={XZ_UI_PC_DRUMS,XZ_UI_PC_INST,XZ_UI_PC_VOCAL};
        for(unsigned mask=11;mask<=15;mask+=4)for(unsigned pending=0;pending<=15;pending+=15){
            m.deck[0].pc_stems=(struct xz_ui_pc_stems){.source=XZ_UI_SOURCE_PC,.connected=1,.verified=1,.caps=XZ_UI_PC_CAP_TOGGLE,.available=mask,.pending=pending,.target=3};
            assert(xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
            bass=xz_ui_model_pc_role_color(&m,XZ_UI_PC_BASS);
            for(int stem=0;stem<3;stem++){
                uint32_t rgb=xz_ui_model_stem_color(&m,stem);
                assert(xz_ui_model_pc_role_color(&m,roles[stem])==rgb&&bass!=rgb);
                struct xz_ui_widget stripe=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,roles[stem],0);
                uint16_t expected=(uint16_t)(((rgb>>8)&0xf800)|((rgb>>5)&0x7e0)|((rgb>>3)&31));
                assert(frame[(stripe.y+1)*540+stripe.x+2]==expected);
            }
            if(mask==15){
                struct xz_ui_widget stripe=pc_widget(&u,&m,XZ_UI_PC_TOGGLE,XZ_UI_PC_BASS,0);
                uint16_t expected=(uint16_t)(((bass>>8)&0xf800)|((bass>>5)&0x7e0)|((bass>>3)&31));
                assert(frame[(stripe.y+1)*540+stripe.x+2]==expected);
            }
        }
        memset(m.stem_colors,0,sizeof(m.stem_colors));
    }
}
int main(void) {
    struct xz_ui u; struct xz_ui_model m={0}; struct xz_ui_action a[XZ_UI_ACTIONS];
    xz_ui_init(&u); m.enabled=XZ_UI_STEM;
    for(int deck=0;deck<2;deck++) {m.deck[deck].ready=XZ_UI_STEM;for(int i=0;i<3;i++)m.deck[deck].levels[i]=1;}
    struct xz_ui_widget widgets[XZ_UI_WIDGETS];
    assert(xz_ui_inline_layout(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,widgets)==8);
    for(int deck=0;deck<2;deck++)for(int slot=0;slot<4;slot++){
        struct xz_ui_widget w=widgets[deck*4+slot];
        assert(w.deck==deck&&w.y==deck*XZ_WAVE_CONTROL_HEIGHT+1&&w.h==XZ_WAVE_CONTROL_HEIGHT-2);
        assert(w.x==slot*134+1&&w.w==132);
        assert(w.kind==(slot==3?XZ_UI_BYPASS:XZ_UI_MUTE));
        if(slot<3)assert(w.index==2-slot);
    }
    for(unsigned i=0;i<sizeof(frame)/sizeof(*frame);i++)frame[i]=0xabcd;
    for(int theme=0;theme<XZ_THEME_COUNT;theme++) {
        m.theme=theme;assert(xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
        for(int deck=0;deck<2;deck++)for(int col=0;col<3;col++){
            unsigned c=xz_ui_stem_color(theme,2-col);
            uint16_t expected=(uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));
            int x=col*134+1;if(theme<7)assert(frame[(deck*XZ_WAVE_CONTROL_HEIGHT+1)*540+x]==expected);
        }
        for(int y=0;y<XZ_WAVE_INLINE_HEIGHT;y++)for(int x=536;x<540;x++)assert(frame[y*540+x]==0xabcd);
        for(unsigned i=540*XZ_WAVE_INLINE_HEIGHT;i<sizeof(frame)/sizeof(*frame);i++)assert(frame[i]==0xabcd);
    }
    u.deck=1;
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,60,10,1,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,-10,-10,0,a)==1);
    assert(a[0].kind==XZ_UI_MUTE&&a[0].index==2&&a[0].deck==0&&a[0].phase==XZ_UI_PRESS);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,194,50,1,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,60,10,1,a)==1);
    assert(a[0].kind==XZ_UI_LEVEL&&a[0].deck==1&&a[0].index==1&&a[0].value==0);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,900,10,1,a)==1&&a[0].deck==1&&a[0].value==1);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,900,10,0,a)==0);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,460,10,1,a)==1);
    assert(a[0].kind==XZ_UI_BYPASS&&a[0].deck==0&&a[0].value==1);
    xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,460,10,0,a);
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,460,50,1,a)==1);
    assert(a[0].kind==XZ_UI_BYPASS&&a[0].deck==1&&a[0].value==1);
    xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,460,50,0,a);
    m.enabled=0;
    assert(xz_ui_inline_touch(&u,&m,536,XZ_WAVE_INLINE_HEIGHT,60,10,1,a)==1&&a[0].kind==XZ_UI_UNAVAILABLE&&a[0].deck==0);
    assert(!xz_ui_inline_render(&u,&m,frame,1,540,536,XZ_WAVE_INLINE_HEIGHT));
    assert(!xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,300,XZ_WAVE_INLINE_HEIGHT));
    assert(!xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,60));
    u.deck=3;assert(!xz_ui_inline_render(&u,&m,frame,540*XZ_WAVE_INLINE_HEIGHT,540,536,XZ_WAVE_INLINE_HEIGHT));
    pc_rows();
    puts("PASS two inline decks, eight controls, touch ownership and render bounds; PC rows keep local parity, show exactly 3 or 4 roles on the USB grid, cancel on mode change, keep known state while pending, show presses at once and keep saved colours");
}
