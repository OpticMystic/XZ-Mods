/* Actual C menu/render/input fixture. Synthetic addresses only; no OS backend linked. */
#include "ui_network.h"
#include "../djlink/djl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions required
#endif
_Static_assert(XZ_UI_FOUR_WAVE_ORDER==113&&XZ_UI_NETWORK_SETUP_OPEN==114&&XZ_UI_NETWORK_CANCEL==124,"append-only action IDs");
_Static_assert(XZ_UI_BUTTONS==29&&XZ_UI_NETWORK==30&&XZ_UI_NETWORK_EDIT==32,"append-only page IDs");
_Static_assert(XZ_UI_PRO_DJ_LINK==XZ_UI_LANGUAGE+1,"append-only action IDs");
_Static_assert(XZ_UI_ABLETON_LINK==XZ_UI_PRO_DJ_LINK+1,"append-only action IDs");
static uint16_t pixels[800*480];
static void model(struct xz_ui_model *m){
    memset(m,0,sizeof *m);m->network.actual.generation=7;m->network.actual.count=2;m->network.result=XZ_NET_OK;
    for(unsigned i=0;i<2;i++){
        struct xz_net_adapter *a=&m->network.actual.adapter[i];snprintf(a->id.name,sizeof a->id.name,"fixture%u",i);a->id.ifindex=7+i;a->id.identity_epoch=12+i;a->state_revision=20+i;
        a->addresses_known=a->link_up=a->gateway_known=XZ_NET_YES;a->address_count=1;a->address[0].ip=(struct xz_net_ipv4){{192,0,2,(uint8_t)(10+i)}};a->address[0].prefix=24;a->caps=XZ_NET_CAP_STATIC_ADD|XZ_NET_CAP_DHCP_START|XZ_NET_CAP_GATEWAY;
    }
}
static struct xz_ui_widget find(struct xz_ui *u,struct xz_ui_model *m,int kind,int index){
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);assert(n<=XZ_UI_WIDGETS);
    for(size_t i=0;i<n;i++){assert(w[i].x>=0&&w[i].y>=0&&w[i].w>0&&w[i].h>0&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=480);if((int)w[i].kind==kind&&w[i].index==index)return w[i];}assert(!"missing network control");return w[0];
}
static size_t tap(struct xz_ui *u,struct xz_ui_model *m,int kind,int index,struct xz_ui_action out[XZ_UI_ACTIONS]){
    struct xz_ui_widget w=find(u,m,kind,index);size_t n=xz_ui_touch(u,m,w.x+w.w/2,w.y+w.h/2,1,out);struct xz_ui_action release[XZ_UI_ACTIONS];assert(!xz_ui_touch(u,m,0,0,0,release));return n;
}
static void check_layout(struct xz_ui *u,struct xz_ui_model *m){
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);int primary=0;
    for(size_t i=0;i<n;i++)if(xz_ui_network_kind(w[i].kind)){
        assert(w[i].x>=136&&w[i].y>=48&&w[i].x+w[i].w<=800&&w[i].y+w[i].h<=404);primary++;
        for(size_t j=i+1;j<n;j++)if(xz_ui_network_kind(w[j].kind))assert(w[i].x+w[i].w<=w[j].x||w[j].x+w[j].w<=w[i].x||w[i].y+w[i].h<=w[j].y||w[j].y+w[j].h<=w[i].y);
    }assert(primary);assert(xz_ui_render(u,m,pixels,800*480,800));
}
static void image(const char *name){FILE *f=fopen(name,"wb");assert(f);fputs("P6\n800 480\n255\n",f);for(unsigned i=0;i<800*480;i++){unsigned v=pixels[i];unsigned char c[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));}
/* PRO DJ LINK DECKS 3/4: a saved switch on the NETWORK page, and one status line per helper state. */
static void pro_dj_link(struct xz_ui *u,struct xz_ui_model *m){
    struct xz_ui_action out[XZ_UI_ACTIONS];
    struct xz_ui_widget w=find(u,m,XZ_UI_PRO_DJ_LINK,0);assert(strstr(w.label,"PRO DJ LINK DECKS 3/4: OFF"));
    assert(tap(u,m,XZ_UI_PRO_DJ_LINK,0,out)==1&&out[0].kind==XZ_UI_PRO_DJ_LINK&&out[0].value==1);
    m->pro_dj_link_decks=1;assert(strstr(find(u,m,XZ_UI_PRO_DJ_LINK,0).label,": ON"));
    assert(tap(u,m,XZ_UI_PRO_DJ_LINK,0,out)==1&&out[0].value==0);
    assert(strstr(xz_ui_network_help(XZ_UI_PRO_DJ_LINK),"players 3 and 4"));
    static const char *const want[]={"OFF / DECKS 3 AND 4 NOT ON THE NETWORK","STARTING","DECKS 3 AND 4 ON THE NETWORK / 2 PEERS",
        "READY / NO DECK 3/4 ROUTE OR TRACK","HELPER MISSING FROM THIS BUILD","NO LINK / STOCK PLAYER NOT ANNOUNCING",
        "PLAYER 4 IN USE BY ANOTHER DEVICE","COULD NOT START / SEE MODS LOG"};
    char seen[8][96];
    for(int s=XZ_DJLINK_OFF;s<=XZ_DJLINK_FAILED;s++){
        memset(&m->djlink,0,sizeof m->djlink);m->djlink.enabled=s!=XZ_DJLINK_OFF;m->djlink.state=(enum xz_djlink_state)s;m->djlink.peers=2;
        if(s==XZ_DJLINK_ONLINE)m->djlink.phase[0]=m->djlink.phase[1]=DJL_RUNNING;
        if(s==XZ_DJLINK_NUMBER_IN_USE){m->djlink.phase[0]=DJL_RUNNING;m->djlink.phase[1]=DJL_EVICTED;}
        xz_ui_djlink_line(&m->djlink,seen[s],sizeof seen[s]);
        assert(!strcmp(seen[s],want[s])&&strlen(seen[s])<=52);
        for(int t=XZ_DJLINK_OFF;t<s;t++)assert(strcmp(seen[s],seen[t]));
        check_layout(u,m);
    }
    m->djlink.phase[0]=DJL_RUNNING;m->djlink.phase[1]=DJL_CLAIMING1;m->djlink.peers=1;m->djlink.state=XZ_DJLINK_ONLINE;
    assert(!strcmp(xz_ui_djlink_line(&m->djlink,seen[0],sizeof seen[0]),"DECK 3 ON THE NETWORK / 1 PEER"));
    m->djlink.phase[0]=m->djlink.phase[1]=DJL_EVICTED;m->djlink.state=XZ_DJLINK_NUMBER_IN_USE;
    assert(!strcmp(xz_ui_djlink_line(&m->djlink,seen[0],sizeof seen[0]),"PLAYERS 3 AND 4 IN USE BY ANOTHER DEVICE"));
    image("network-pro-dj-link.ppm");
    m->pro_dj_link_decks=0;memset(&m->djlink,0,sizeof m->djlink);
    /* Only the NETWORK page carries it, and never while a network change runs. */
    struct xz_ui_widget all[XZ_UI_WIDGETS];m->network.phase=XZ_NET_APPLYING;size_t n=xz_ui_layout(u,m,all);
    for(size_t i=0;i<n;i++)assert(all[i].kind!=XZ_UI_PRO_DJ_LINK);
    m->network.phase=XZ_NET_IDLE;
}
struct band{int y0,y1;const char *what;};
static int apart(struct band a,struct band b){return a.y1<=b.y0||b.y1<=a.y0;}
/* The NETWORK page's default view, top to bottom: four adapter lines, two notes, the two network rows with their
   status lines to the right, then the button grid. No two bands may overlap, the status lines must fit the page
   and stay right of the rows, and every control must sit outside the text bands it does not own. */
static void network_bands(struct xz_ui *u,struct xz_ui_model *m){
    struct band text[]={
        {XZ_UI_NET_ADAPTER_Y,XZ_UI_NET_ADAPTER_Y+(XZ_UI_NET_ADAPTER_LINES-1)*XZ_UI_NET_ADAPTER_STEP+XZ_UI_NET_TEXT_H,"adapter lines"},
        {XZ_UI_NET_NOTE_Y,XZ_UI_NET_NOTE_Y+XZ_UI_NET_NOTE_STEP+XZ_UI_NET_TEXT_H,"notes"},
        {XZ_UI_NET_DJLINK_Y,XZ_UI_NET_DJLINK_Y+XZ_UI_NET_ROW_H,"PRO DJ LINK row"},
        {XZ_UI_NET_ABLETON_Y,XZ_UI_NET_ABLETON_Y+XZ_UI_NET_ROW_H,"ABLETON LINK row"},
        {XZ_UI_NET_GRID_Y,404,"button grid"}};
    size_t nb=sizeof text/sizeof text[0];
    assert(XZ_UI_NET_ADAPTER_STEP>=XZ_UI_NET_TEXT_H&&XZ_UI_NET_NOTE_STEP>=XZ_UI_NET_TEXT_H&&text[0].y0>82+XZ_UI_NET_TEXT_H);
    for(size_t i=0;i<nb;i++)for(size_t j=i+1;j<nb;j++){if(!apart(text[i],text[j]))fprintf(stderr,"%s overlaps %s\n",text[i].what,text[j].what);assert(apart(text[i],text[j]));}
    /* Status lines: one text line inside its row's band, right of the row, inside the 800 px page. */
    assert(XZ_UI_NET_STATUS_X>=XZ_UI_NET_ROW_X+XZ_UI_NET_ROW_W+8&&XZ_UI_NET_STATUS_X+XZ_UI_NET_STATUS_CHARS*6<=792);
    assert(13+XZ_UI_NET_TEXT_H<=XZ_UI_NET_ROW_H);
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);int rows=0;
    for(size_t i=0;i<n;i++){
        if(!xz_ui_network_kind(w[i].kind)&&!(w[i].kind==XZ_UI_PANEL&&w[i].index==XZ_UI_UPDATES))continue;
        struct band c={w[i].y,w[i].y+w[i].h,"control"};
        if(w[i].kind==XZ_UI_PRO_DJ_LINK||w[i].kind==XZ_UI_ABLETON_LINK){
            rows++;assert(w[i].h>=36&&w[i].x==XZ_UI_NET_ROW_X&&w[i].w==XZ_UI_NET_ROW_W);
            assert(c.y0==(w[i].kind==XZ_UI_PRO_DJ_LINK?XZ_UI_NET_DJLINK_Y:XZ_UI_NET_ABLETON_Y));
        }else assert(c.y0>=XZ_UI_NET_GRID_Y);
        assert(apart(c,text[0])&&apart(c,text[1]));
    }
    assert(rows==2);
}
/* ABLETON LINK: a saved switch on the NETWORK page whose label states Enabled or Disabled, and one status line per
   helper state. */
static void ableton_link(struct xz_ui *u,struct xz_ui_model *m){
    struct xz_ui_action out[XZ_UI_ACTIONS];
    struct xz_ui_widget w=find(u,m,XZ_UI_ABLETON_LINK,0);assert(!strcmp(w.label,"ABLETON LINK: DISABLED"));
    struct xz_ui_widget dj=find(u,m,XZ_UI_PRO_DJ_LINK,0);assert(w.y>=dj.y+dj.h&&w.x==dj.x);
    assert(tap(u,m,XZ_UI_ABLETON_LINK,0,out)==1&&out[0].kind==XZ_UI_ABLETON_LINK&&out[0].value==1);
    m->ableton_link=1;assert(!strcmp(find(u,m,XZ_UI_ABLETON_LINK,0).label,"ABLETON LINK: ENABLED"));
    struct xz_ui_info info;w=find(u,m,XZ_UI_ABLETON_LINK,0);assert(xz_ui_describe_widget(u,m,&w,&info)&&!strcmp(info.state,"ENABLED"));
    assert(tap(u,m,XZ_UI_ABLETON_LINK,0,out)==1&&out[0].value==0);
    const char *help=xz_ui_network_help(XZ_UI_ABLETON_LINK);
    assert(strstr(help,"follows this unit's tempo master")&&strstr(help,"never moved by Ableton Link"));
    assert(!strstr(help,"AbletonLink"));
    static const char *const want[]={"DISABLED / NOT IN ANY SESSION","ENABLED / STARTING","ENABLED / 2 PEERS / 128.00 BPM",
        "ENABLED / NO TEMPO MASTER ON THIS UNIT","HELPER MISSING FROM THIS BUILD","COULD NOT START / SEE MODS LOG"};
    char seen[6][96];
    for(int s=XZ_LINK_RUN_OFF;s<=XZ_LINK_RUN_FAILED;s++){
        memset(&m->ablink,0,sizeof m->ablink);m->ablink.enabled=s!=XZ_LINK_RUN_OFF;m->ablink.state=(enum xz_link_run)s;
        m->ablink.peers=2;m->ablink.session_bpm=127.996;
        xz_ui_ablink_line(&m->ablink,seen[s],sizeof seen[s]);
        assert(!strcmp(seen[s],want[s])&&strlen(seen[s])<=XZ_UI_NET_STATUS_CHARS);
        for(int t=XZ_LINK_RUN_OFF;t<s;t++)assert(strcmp(seen[s],seen[t]));
        check_layout(u,m);network_bands(u,m);
    }
    m->ablink.state=XZ_LINK_RUN_ENABLED;m->ablink.peers=1;m->ablink.session_bpm=174.0;
    assert(!strcmp(xz_ui_ablink_line(&m->ablink,seen[0],sizeof seen[0]),"ENABLED / 1 PEER / 174.00 BPM"));
    m->ablink.peers=0;m->ablink.session_bpm=999.994;
    assert(!strcmp(xz_ui_ablink_line(&m->ablink,seen[0],sizeof seen[0]),"ENABLED / 0 PEERS / 999.99 BPM"));
    /* Both rows on, four adapters: the busiest default view. */
    m->pro_dj_link_decks=m->ableton_link=1;m->djlink.enabled=1;m->djlink.state=XZ_DJLINK_NUMBER_IN_USE;m->djlink.phase[0]=m->djlink.phase[1]=DJL_EVICTED;
    m->ablink.state=XZ_LINK_RUN_ENABLED;m->ablink.peers=12;m->ablink.session_bpm=128.0;
    m->network.actual.count=4;for(unsigned i=2;i<4;i++){m->network.actual.adapter[i]=m->network.actual.adapter[0];snprintf(m->network.actual.adapter[i].id.name,sizeof m->network.actual.adapter[i].id.name,"fixture%u",i);}
    check_layout(u,m);network_bands(u,m);image("network-ableton-link.ppm");
    m->network.actual.count=2;m->pro_dj_link_decks=m->ableton_link=0;memset(&m->djlink,0,sizeof m->djlink);memset(&m->ablink,0,sizeof m->ablink);
    struct xz_ui_widget all[XZ_UI_WIDGETS];m->network.phase=XZ_NET_APPLYING;size_t n=xz_ui_layout(u,m,all);
    for(size_t i=0;i<n;i++)assert(all[i].kind!=XZ_UI_ABLETON_LINK);
    m->network.phase=XZ_NET_IDLE;
    puts("PASS ABLETON LINK row: label states ENABLED or DISABLED, toggle, help, six distinct status lines, no overlap on the NETWORK page");
    fflush(stdout);
}
int main(void){
    struct xz_ui u;struct xz_ui_model m;model(&m);xz_ui_init(&u);struct xz_ui_action out[XZ_UI_ACTIONS];
    /* The NETWORK panel is no longer on the first page MODS opens; go to it as the CONNECT section does. */
    xz_ui_navigate(&u,XZ_UI_NETWORK);assert(u.page==XZ_UI_NETWORK);check_layout(&u,&m);
    pro_dj_link(&u,&m);
    ableton_link(&u,&m);
    assert(tap(&u,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,out)==0&&u.network.initialized&&u.network.step==XZ_NETWORK_CHOOSE);check_layout(&u,&m);
    assert(!tap(&u,&m,XZ_UI_NETWORK_MODE,XZ_NET_STATIC,out));assert(!tap(&u,&m,XZ_UI_NETWORK_FIELD,XZ_NETWORK_IP,out));assert(u.page==XZ_UI_NETWORK_EDIT);check_layout(&u,&m);
    assert(!tap(&u,&m,XZ_UI_NETWORK_KEY,12,out));for(const char *p="192.0.2.50";*p;p++)assert(!tap(&u,&m,XZ_UI_NETWORK_KEY,*p=='.'?10:*p-'0',out));assert(!tap(&u,&m,XZ_UI_NETWORK_KEY,13,out));assert(!strcmp(u.network.ip,"192.0.2.50"));
    assert(!tap(&u,&m,XZ_UI_NETWORK_PRIORITY,1,out)&&u.network.selected==1);assert(!strcmp(u.network.trial.policy.order[0].adapter.name,"fixture1"));
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){m.theme=theme;check_layout(&u,&m);}m.theme=XZ_THEME_FLAGSHIP;check_layout(&u,&m);image("network-choose.ppm");
    assert(!tap(&u,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_REVIEW,out)&&u.network.step==XZ_NETWORK_REVIEW);check_layout(&u,&m);image("network-review.ppm");
    struct xz_ui copy=u;
    assert(tap(&u,&m,XZ_UI_NETWORK_APPLY,0,out)==1&&out[0].network_revision==u.network.revision);assert(!tap(&u,&m,XZ_UI_NETWORK_APPLY,0,out));
    /* Encoder uses the identical touch path. */
    copy.network.step=XZ_NETWORK_REVIEW;copy.focus_kind=XZ_UI_NETWORK_APPLY;copy.focus_index=0;copy.focus_deck=-1;
    assert(!xz_ui_encoder_push(&copy,&m,out));assert(xz_ui_render(&copy,&m,pixels,800*480,800));assert(xz_ui_encoder_push(&copy,&m,out)==1&&out[0].kind==XZ_UI_NETWORK_APPLY);
    /* Stale/renamed inventory cannot retarget an old row. */
    copy=u;copy.network.step=XZ_NETWORK_CHOOSE;m.network.actual.generation++;assert(!tap(&copy,&m,XZ_UI_NETWORK_MODE,XZ_NET_DHCP,out));assert(!copy.network.initialized&&strstr(copy.notice,"CHANGED"));check_layout(&copy,&m);
    /* Capability failure before Apply also opens real setup and keeps static usable. */
    xz_ui_init(&copy);xz_ui_navigate(&copy,XZ_UI_NETWORK);m.network.actual.adapter[0].caps&=~XZ_NET_CAP_DHCP_START;
    assert(!tap(&copy,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,out));assert(!tap(&copy,&m,XZ_UI_NETWORK_MODE,XZ_NET_DHCP,out));assert(!tap(&copy,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_REVIEW,out));assert(copy.network.setup_reason==XZ_NET_NEEDS_DHCP_CLIENT);check_layout(&copy,&m);
    assert(tap(&copy,&m,XZ_UI_NETWORK_SETUP_OPEN,0,out)==1&&out[0].kind==XZ_UI_NETWORK_DETECT);check_layout(&copy,&m);assert(!tap(&copy,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,out)&&!copy.network.setup_reason);m.network.actual.adapter[0].caps|=XZ_NET_CAP_DHCP_START;
    /* Every setup/error state retains an actionable setup wizard. */
    for(int r=XZ_NET_INVALID;r<=XZ_NET_CANCELLED;r++){xz_ui_init(&copy);xz_ui_navigate(&copy,XZ_UI_NETWORK);m.network.result=(enum xz_net_result)r;m.network.phase=XZ_NET_FAILED;check_layout(&copy,&m);assert(tap(&copy,&m,XZ_UI_NETWORK_SETUP_OPEN,0,out)==1&&out[0].kind==XZ_UI_NETWORK_DETECT&&copy.page==XZ_UI_NETWORK_SETUP);}
    m.network.phase=XZ_NET_APPLYING;check_layout(&copy,&m);assert(tap(&copy,&m,XZ_UI_NETWORK_CANCEL,0,out)==1);
    m.network.phase=XZ_NET_NEEDS_SETUP;m.network.result=XZ_NET_NEEDS_DHCP_CLIENT;snprintf(m.network.detail,sizeof m.network.detail,"Need /dev/shm/xz-network-udhcpc + xz-network-lease-event");check_layout(&copy,&m);image("network-missing-client.ppm");
    puts("PASS actual C UI: append-only IDs, all theme geometry, setup primary actions, IPv4 keypad, priority, requested-only edits, touch/encoder Apply parity, stale selection, cancel, PRO DJ LINK DECKS 3/4 switch and status lines");
}
