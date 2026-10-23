/* Actual C menu/render/input fixture. Synthetic addresses only; no OS backend linked. */
#include "ui_network.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions required
#endif
_Static_assert(XZ_UI_FOUR_WAVE_ORDER==113&&XZ_UI_NETWORK_SETUP_OPEN==114&&XZ_UI_NETWORK_CANCEL==124,"append-only action IDs");
_Static_assert(XZ_UI_BUTTONS==29&&XZ_UI_NETWORK==30&&XZ_UI_NETWORK_EDIT==32,"append-only page IDs");
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
    struct xz_shell_menu menu;assert(xz_ui_menu_build(u,m,&menu)&&menu.row_count>0);assert(strstr(menu.title,"NETWORK")||!strcmp(menu.title,"EDIT IPV4"));
    for(unsigned i=0;i<menu.row_count;i++)assert(menu.row[i].available&&xz_ui_network_kind(menu.row[i].target.kind));
}
static void image(const char *name){FILE *f=fopen(name,"wb");assert(f);fputs("P6\n800 480\n255\n",f);for(unsigned i=0;i<800*480;i++){unsigned v=pixels[i];unsigned char c[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(c,1,3,f)==3);}assert(!fclose(f));}
int main(void){
    struct xz_ui u;struct xz_ui_model m;model(&m);xz_ui_init(&u);struct xz_ui_action out[XZ_UI_MENU_ACTIONS];
    assert(tap(&u,&m,XZ_UI_FEATURE_LINK,XZ_UI_NETWORK,out)==1&&u.page==XZ_UI_NETWORK);check_layout(&u,&m);
    assert(tap(&u,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,out)==0&&u.network.initialized&&u.network.step==XZ_NETWORK_CHOOSE);check_layout(&u,&m);
    assert(!tap(&u,&m,XZ_UI_NETWORK_MODE,XZ_NET_STATIC,out));assert(!tap(&u,&m,XZ_UI_NETWORK_FIELD,XZ_NETWORK_IP,out));assert(u.page==XZ_UI_NETWORK_EDIT);check_layout(&u,&m);
    assert(!tap(&u,&m,XZ_UI_NETWORK_KEY,12,out));for(const char *p="192.0.2.50";*p;p++)assert(!tap(&u,&m,XZ_UI_NETWORK_KEY,*p=='.'?10:*p-'0',out));assert(!tap(&u,&m,XZ_UI_NETWORK_KEY,13,out));assert(!strcmp(u.network.ip,"192.0.2.50"));
    assert(!tap(&u,&m,XZ_UI_NETWORK_PRIORITY,1,out)&&u.network.selected==1);assert(!strcmp(u.network.trial.policy.order[0].adapter.name,"fixture1"));
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){m.theme=theme;check_layout(&u,&m);}m.theme=XZ_THEME_FLAGSHIP;check_layout(&u,&m);image("network-choose.ppm");
    assert(!tap(&u,&m,XZ_UI_NETWORK_STEP,XZ_NETWORK_REVIEW,out)&&u.network.step==XZ_NETWORK_REVIEW);check_layout(&u,&m);image("network-review.ppm");
    struct xz_ui copy=u;struct xz_shell_menu menu;assert(xz_ui_menu_build(&copy,&m,&menu));struct xz_shell_menu_target target={0};
    for(unsigned i=0;i<menu.row_count;i++)if(menu.row[i].target.kind==XZ_UI_NETWORK_APPLY)target=menu.row[i].target;
    assert(target.kind==XZ_UI_NETWORK_APPLY);size_t n=xz_ui_menu_run(&copy,&m,&target,out);assert(n==1&&out[0].kind==XZ_UI_NETWORK_APPLY&&out[0].network_revision==copy.network.revision);
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
    puts("PASS actual C UI: append-only IDs, all theme geometry, setup primary actions, IPv4 keypad, priority, requested-only edits, touch/menu/encoder Apply parity, stale selection, cancel");
}
