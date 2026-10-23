/* Network binding inside the actual ui_runtime.c. No startup hooks or legacy test main executes. */
#define main uncalled_legacy_runtime_suite
#include "test_runtime_controls.c"
#undef main
#include <stdio.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
static struct xz_net_view network_fixture;
static struct xz_net_command captured_command;
static unsigned submitted;
enum xz_net_result xz_net_runtime_read(struct xz_net_view *v){*v=network_fixture;return XZ_NET_OK;}
enum xz_net_result xz_net_runtime_submit(const struct xz_net_command *c){captured_command=*c;submitted++;return XZ_NET_OK;}
int main(void){
    memset(&model,0,sizeof model);xz_ui_init(&ui);settings_readonly=1;settings_revision=42;settings_pending=0;
    network_fixture.result=XZ_NET_OK;network_fixture.actual.generation=5;network_fixture.actual.count=1;
    struct xz_net_adapter *a=&network_fixture.actual.adapter[0];strcpy(a->id.name,"fixture0");a->id.ifindex=7;a->id.identity_epoch=3;a->state_revision=9;a->addresses_known=a->link_up=XZ_NET_YES;a->caps=XZ_NET_CAP_STATIC_ADD;a->address_count=1;a->address[0].ip=(struct xz_net_ipv4){{192,0,2,10}};a->address[0].prefix=24;
    network_refresh();assert(model.network.actual.count==1);
    struct xz_ui_action actions[XZ_UI_ACTIONS];size_t n=xz_ui_network_action(&ui,&model,XZ_UI_NETWORK_SETUP_OPEN,0,actions);assert(n==1);apply(NULL,actions,n);assert(submitted==1&&captured_command.kind==XZ_NET_CMD_DETECT);
    assert(!xz_ui_network_action(&ui,&model,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,actions));assert(!xz_ui_network_action(&ui,&model,XZ_UI_NETWORK_MODE,XZ_NET_STATIC,actions));strcpy(ui.network.ip,"192.0.2.50");assert(!xz_ui_network_action(&ui,&model,XZ_UI_NETWORK_STEP,XZ_NETWORK_REVIEW,actions));
    n=xz_ui_network_action(&ui,&model,XZ_UI_NETWORK_APPLY,0,actions);assert(n==1);apply(NULL,actions,n);assert(submitted==2&&captured_command.kind==XZ_NET_CMD_APPLY_TRIAL&&captured_command.body.apply.policy.order[0].static_ip.address.ip.octet[3]==50);
    uint64_t request=captured_command.request_id;strcpy(ui.network.ip,"192.0.2.99");assert(captured_command.body.apply.policy.order[0].static_ip.address.ip.octet[3]==50);
    actions[0].phase=XZ_UI_MOVE;apply(NULL,actions,1);actions[0].phase=XZ_UI_RELEASE;apply(NULL,actions,1);assert(submitted==2);
    actions[0].phase=XZ_UI_PRESS;actions[0].network_revision--;apply(NULL,actions,1);assert(submitted==2&&strstr(ui.notice,"draft changed"));
    model.network.trial_id=request;actions[0]=(struct xz_ui_action){.kind=XZ_UI_NETWORK_REVERT,.phase=XZ_UI_PRESS};apply(NULL,actions,1);assert(submitted==3&&captured_command.kind==XZ_NET_CMD_REVERT&&captured_command.body.trial_id==request);
    assert(settings_revision==42&&!settings_pending&&settings_readonly);
    puts("PASS actual ui_runtime network binding: readback, immutable Apply copy, phase/revision guards, exact trial Revert, zero settings writes");
}
