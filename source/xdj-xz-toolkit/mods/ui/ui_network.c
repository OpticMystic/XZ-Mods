/* SPDX-License-Identifier: MIT. One draft/control path for classic touch, encoder and MODERN menu. */
#include "ui_network.h"
#include <stdio.h>
#include <string.h>

int xz_ui_network_page(int page){return page>=XZ_UI_NETWORK&&page<=XZ_UI_NETWORK_EDIT;}
int xz_ui_network_kind(int kind){return kind>=XZ_UI_NETWORK_SETUP_OPEN&&kind<=XZ_UI_NETWORK_CANCEL;}
const char *xz_ui_network_title(int page){return page==XZ_UI_NETWORK_EDIT?"EDIT IPV4":page==XZ_UI_NETWORK_SETUP?"NETWORK SETUP":"NETWORK";}
static int busy(const struct xz_ui_model *m){return m->network.phase>=XZ_NET_DETECTING&&m->network.phase<=XZ_NET_RESTORING;}
static const struct xz_net_adapter *actual(const struct xz_ui *u,const struct xz_ui_model *m){
    if(!u->network.initialized||u->network.selected<0||u->network.selected>=u->network.trial.policy.count)return NULL;
    const struct xz_net_identity *id=&u->network.trial.policy.order[u->network.selected].adapter;
    for(unsigned i=0;i<m->network.actual.count;i++)if(xz_net_identity_equal(id,&m->network.actual.adapter[i].id))return &m->network.actual.adapter[i];return NULL;
}
static void add(struct xz_ui_widget *w,size_t *n,int kind,int index,const char *label){if(*n<XZ_UI_WIDGETS)w[(*n)++]=(struct xz_ui_widget){.kind=(enum xz_ui_action_kind)kind,.index=index,.deck=-1,.label=label};}
size_t xz_ui_network_controls(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
    if(busy(m)){add(w,&n,XZ_UI_NETWORK_CANCEL,0,"CANCEL / RESTORE");return n;}
    if(u->page!=XZ_UI_NETWORK_EDIT&&(m->network.phase==XZ_NET_FAILED||m->network.phase==XZ_NET_NEEDS_SETUP)){
        add(w,&n,XZ_UI_NETWORK_SETUP_OPEN,0,"SET UP NETWORK / INSPECT ACTUAL");
        if(m->network.rollback_available)add(w,&n,XZ_UI_NETWORK_REVERT,0,"RETRY CONDITIONAL REVERT");
        add(w,&n,XZ_UI_NETWORK_DETECT,0,"DETECT ACTUAL STATE");return n;
    }
    if(u->page==XZ_UI_NETWORK_EDIT){
        static const char *keys[]={"0","1","2","3","4","5","6","7","8","9",".","BACKSPACE","CLEAR","DONE","CANCEL"};
        for(int i=0;i<15;i++)add(w,&n,XZ_UI_NETWORK_KEY,i,keys[i]);return n;
    }
    if(u->network.setup_reason){add(w,&n,XZ_UI_NETWORK_SETUP_OPEN,0,"SET UP LOADER NETWORK SUPPORT");add(w,&n,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,"CHOOSE STATIC / KEEP CURRENT");add(w,&n,XZ_UI_NETWORK_DETECT,0,"VERIFY RELOADED LOADER");return n;}
    if(u->page==XZ_UI_NETWORK||!u->network.initialized||u->network.step==XZ_NETWORK_DETECT){
        if(m->network.rollback_available){add(w,&n,XZ_UI_NETWORK_REVERT,0,"REVERT TRIAL / MODS DHCP OFF");add(w,&n,XZ_UI_NETWORK_STEP,XZ_NETWORK_VERIFY,"VERIFY LOCAL STATE");}
        else if(m->network.actual.count&&m->network.result==XZ_NET_OK)add(w,&n,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,"CHOOSE ADAPTER / SETTINGS");
        else add(w,&n,XZ_UI_NETWORK_SETUP_OPEN,0,"SET UP NETWORK");
        add(w,&n,XZ_UI_NETWORK_DETECT,0,"DETECT ADAPTERS AGAIN");return n;
    }
    if(u->network.step==XZ_NETWORK_CHOOSE){
        int count=u->network.trial.policy.count,offset=u->network.adapter_offset;
        for(int i=offset;i<count&&i<offset+3;i++)add(w,&n,XZ_UI_NETWORK_ADAPTER,i,u->network.trial.policy.order[i].adapter.name);
        if(count>3){add(w,&n,XZ_UI_NETWORK_ADAPTER,-1,"<");add(w,&n,XZ_UI_NETWORK_ADAPTER,-2,">");}
        add(w,&n,XZ_UI_NETWORK_MODE,XZ_NET_KEEP,"KEEP CURRENT");add(w,&n,XZ_UI_NETWORK_MODE,XZ_NET_DHCP,"DHCP ON");add(w,&n,XZ_UI_NETWORK_MODE,XZ_NET_STATIC,"STATIC IP");
        const struct xz_net_preference *p=&u->network.trial.policy.order[u->network.selected];
        if(p->mode==XZ_NET_STATIC){add(w,&n,XZ_UI_NETWORK_FIELD,XZ_NETWORK_IP,"EDIT IP");add(w,&n,XZ_UI_NETWORK_FIELD,XZ_NETWORK_MASK,"EDIT MASK / PREFIX");add(w,&n,XZ_UI_NETWORK_FIELD,XZ_NETWORK_GATEWAY,"EDIT GATEWAY");}
        add(w,&n,XZ_UI_NETWORK_PRIORITY,-1,"PRIORITY UP");add(w,&n,XZ_UI_NETWORK_PRIORITY,1,"PRIORITY DOWN");
        add(w,&n,XZ_UI_NETWORK_MODE,3,u->network.trial.policy.request_route?"MODS ROUTE: ON":"MODS ROUTE: OFF");
        if(p->mode==XZ_NET_STATIC)add(w,&n,XZ_UI_NETWORK_MODE,4,p->static_ip.dhcp==XZ_NET_STATIC_DISABLE_DHCP?"MODS DHCP: OFF":"KEEP DHCP OWNER");
        add(w,&n,XZ_UI_NETWORK_STEP,XZ_NETWORK_REVIEW,"REVIEW CHANGES");
    }else{
        if(m->network.rollback_available){add(w,&n,XZ_UI_NETWORK_REVERT,0,"REVERT TRIAL / MODS DHCP OFF");add(w,&n,XZ_UI_NETWORK_STEP,XZ_NETWORK_VERIFY,"VERIFY LOCAL STATE");}
        else{add(w,&n,XZ_UI_NETWORK_APPLY,0,"APPLY FOR THIS BOOT");add(w,&n,XZ_UI_NETWORK_STEP,XZ_NETWORK_CHOOSE,"EDIT CHOICES");}
        add(w,&n,XZ_UI_NETWORK_SETUP_OPEN,0,"SET UP NETWORK AGAIN");
    }return n;
}
static void put(struct xz_ui_widget *w,int x,int y,int width,int height){w->x=x;w->y=y;w->w=width;w->h=height;}
void xz_ui_network_place(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
    (void)m;int secondary=0;
    for(size_t i=0;i<n;i++){
        struct xz_ui_widget *a=&w[i];
        if(u->page==XZ_UI_GENERAL&&a->kind==XZ_UI_FEATURE_LINK&&(a->index==XZ_UI_NETWORK||a->index==XZ_UI_METRICS_SETUP))put(a,a->index==XZ_UI_NETWORK?472:136,288,320,40);
        if(u->page==XZ_UI_CONNECTION&&a->kind==XZ_UI_PANEL&&a->index==XZ_UI_NETWORK)put(a,468,300,324,40);
        if(!xz_ui_network_kind(a->kind))continue;
        if(a->kind==XZ_UI_NETWORK_KEY){put(a,136+(a->index%4)*166,148+(a->index/4)*52,158,44);continue;}
        if(a->kind==XZ_UI_NETWORK_ADAPTER){if(a->index>=0)put(a,136+(a->index-u->network.adapter_offset)*184,184,176,40);else put(a,688,a->index==-1?184:206,104,18);continue;}
        if(a->kind==XZ_UI_NETWORK_MODE){if(a->index<=XZ_NET_STATIC)put(a,136+a->index*224,232,208,40);else put(a,136+(a->index==3?2:3)*166,328,158,32);continue;}
        if(a->kind==XZ_UI_NETWORK_FIELD){put(a,136+a->index*224,280,208,40);continue;}
        if(a->kind==XZ_UI_NETWORK_PRIORITY){put(a,a->index<0?136:302,328,158,32);continue;}
        if(u->page==XZ_UI_NETWORK_SETUP&&!u->network.setup_reason&&u->network.step==XZ_NETWORK_CHOOSE&&a->kind==XZ_UI_NETWORK_STEP){put(a,136,368,656,36);continue;}
        if(!secondary++)put(a,136,320,656,40);else put(a,136+((secondary-2)%2)*336,368,320,36);
    }
}
int xz_ui_network_selected(const struct xz_ui *u,const struct xz_ui_widget *w){
    const struct xz_network_ui_draft *d=&u->network;
    if(!d->initialized||d->selected<0||d->selected>=d->trial.policy.count)return 0;
    const struct xz_net_preference *p=&d->trial.policy.order[d->selected];
    if(w->kind==XZ_UI_NETWORK_ADAPTER)return w->index==d->selected;
    if(w->kind==XZ_UI_NETWORK_MODE){if(w->index<3)return w->index==(int)p->mode;if(w->index==3)return d->trial.policy.request_route;return p->static_ip.dhcp==XZ_NET_STATIC_DISABLE_DHCP;}
    return 0;
}
static void fields(struct xz_ui *u,const struct xz_ui_model *m){
    const struct xz_net_adapter *a=actual(u,m);u->network.ip[0]=u->network.mask[0]=u->network.gateway[0]=0;
    if(a&&a->address_count){xz_net_ipv4_text(a->address[0].ip,u->network.ip);snprintf(u->network.mask,16,"%u",a->address[0].prefix);}
    if(a&&a->gateway_known==XZ_NET_YES&&a->gateway_present)xz_net_ipv4_text(a->gateway,u->network.gateway);
}
static void initialize(struct xz_ui *u,const struct xz_ui_model *m){
    uint64_t revision=u->network.revision+1;memset(&u->network,0,sizeof u->network);struct xz_network_ui_draft *d=&u->network;
    d->revision=revision;d->trial.policy.schema=1;d->trial.policy.count=m->network.actual.count;
    for(unsigned i=0;i<d->trial.policy.count;i++)d->trial.policy.order[i].adapter=m->network.actual.adapter[i].id;
    d->trial.expected_generation=m->network.actual.generation;d->selected=0;d->initialized=d->trial.policy.count>0;d->step=XZ_NETWORK_CHOOSE;fields(u,m);
}
static size_t emit(struct xz_ui *u,int kind,int index,struct xz_ui_action *out){
    out[0]=(struct xz_ui_action){.kind=(enum xz_ui_action_kind)kind,.phase=XZ_UI_PRESS,.deck=-1,.index=index,.network_revision=u->network.revision,.pc_target=-1};return 1;
}
static int fresh(struct xz_ui *u,const struct xz_ui_model *m){
    if(u->network.initialized&&u->network.trial.expected_generation==m->network.actual.generation&&actual(u,m))return 1;
    snprintf(u->notice,sizeof u->notice,"NETWORK CHANGED / DETECT AND CHOOSE AGAIN");u->network.initialized=0;u->network.step=XZ_NETWORK_DETECT;return 0;
}
static int parse_draft(struct xz_ui *u,const struct xz_ui_model *m){
    if(!fresh(u,m))return 0;struct xz_network_ui_draft *d=&u->network;struct xz_net_preference *p=&d->trial.policy.order[d->selected];
    if(p->mode==XZ_NET_STATIC){
        if(xz_net_parse_ipv4(d->ip,&p->static_ip.address.ip)||xz_net_parse_mask(d->mask,&p->static_ip.address.prefix)){snprintf(u->notice,sizeof u->notice,"ENTER A VALID IP AND CONTIGUOUS MASK / PREFIX");return 0;}
        p->static_ip.gateway_mode=XZ_NET_GATEWAY_KEEP;
        if(d->trial.policy.request_route){if(!d->gateway[0])p->static_ip.gateway_mode=XZ_NET_GATEWAY_NONE;else if(xz_net_parse_ipv4(d->gateway,&p->static_ip.gateway)){snprintf(u->notice,sizeof u->notice,"ENTER A VALID GATEWAY OR KEEP ROUTE OFF");return 0;}else p->static_ip.gateway_mode=XZ_NET_GATEWAY_SET;}
    }
    const struct xz_net_adapter *a=actual(u,m);d->trial.selected=p->adapter;d->trial.expected_state_revision=a->state_revision;
    struct xz_net_error error={0};enum xz_net_result r=xz_net_validate_trial(&d->trial,&m->network.actual,&error);
    if(r){snprintf(u->notice,sizeof u->notice,"%.95s",error.detail);if(r==XZ_NET_NEEDS_DHCP_CLIENT||r==XZ_NET_UNSAFE_CLIENT||r==XZ_NET_NEEDS_BACKEND)d->setup_reason=(int)r;return 0;}return 1;
}
size_t xz_ui_network_action(struct xz_ui *u,const struct xz_ui_model *m,int kind,int index,struct xz_ui_action *out){
    struct xz_network_ui_draft *d=&u->network;
    if(kind==XZ_UI_NETWORK_SETUP_OPEN){xz_ui_navigate(u,XZ_UI_NETWORK_SETUP);d->step=XZ_NETWORK_DETECT;d->initialized=0;return emit(u,XZ_UI_NETWORK_DETECT,0,out);}
    if(kind==XZ_UI_NETWORK_DETECT){d->step=XZ_NETWORK_DETECT;d->initialized=0;return emit(u,kind,index,out);}
    if(kind==XZ_UI_NETWORK_CANCEL||kind==XZ_UI_NETWORK_REVERT)return emit(u,kind,index,out);
    if(kind==XZ_UI_NETWORK_STEP){
        if(index==XZ_NETWORK_VERIFY)return emit(u,kind,index,out);
        if(index==XZ_NETWORK_CHOOSE){if(!d->initialized||d->setup_reason){if(!m->network.actual.count||m->network.result!=XZ_NET_OK){snprintf(u->notice,sizeof u->notice,"DETECT ACTUAL ADAPTERS FIRST");return 0;}initialize(u,m);}xz_ui_navigate(u,XZ_UI_NETWORK_SETUP);d->step=XZ_NETWORK_CHOOSE;return 0;}
        if(index==XZ_NETWORK_REVIEW){if(!parse_draft(u,m))return 0;d->step=XZ_NETWORK_REVIEW;u->capture=-1;return 0;}
    }
    if(kind==XZ_UI_NETWORK_KEY){
        size_t n=strlen(d->edit);if(index>=0&&index<=10){if(n<15){d->edit[n]=index==10?'.':(char)('0'+index);d->edit[n+1]=0;}}
        else if(index==11){if(n)d->edit[n-1]=0;}else if(index==12)d->edit[0]=0;
        else if(index==13){char *target=d->field==XZ_NETWORK_IP?d->ip:d->field==XZ_NETWORK_MASK?d->mask:d->gateway;memcpy(target,d->edit,16);d->revision++;xz_ui_navigate(u,XZ_UI_NETWORK_SETUP);d->step=XZ_NETWORK_CHOOSE;}
        else if(index==14){xz_ui_navigate(u,XZ_UI_NETWORK_SETUP);d->step=XZ_NETWORK_CHOOSE;}u->capture=-1;return 0;
    }
    if(!fresh(u,m))return 0;
    if(kind==XZ_UI_NETWORK_ADAPTER){
        if(index<0){int offset=d->adapter_offset+(index==-1?-3:3);if(offset<0)offset=0;if(offset<d->trial.policy.count)d->adapter_offset=offset;}
        else if(index<d->trial.policy.count){for(unsigned i=0;i<d->trial.policy.count;i++)if((int)i!=index)d->trial.policy.order[i].mode=XZ_NET_KEEP;d->selected=index;fields(u,m);d->revision++;}u->capture=-1;return 0;
    }
    struct xz_net_preference *p=&d->trial.policy.order[d->selected];
    if(kind==XZ_UI_NETWORK_PRIORITY){int next=d->selected+(index<0?-1:1);if(next>=0&&next<d->trial.policy.count){struct xz_net_preference swap=d->trial.policy.order[next];d->trial.policy.order[next]=*p;*p=swap;d->selected=next;d->adapter_offset=(next/3)*3;d->revision++;}return 0;}
    if(kind==XZ_UI_NETWORK_MODE){if(index>=0&&index<3)p->mode=(enum xz_net_mode)index;else if(index==3)d->trial.policy.request_route=!d->trial.policy.request_route;else if(index==4)p->static_ip.dhcp=p->static_ip.dhcp==XZ_NET_STATIC_KEEP_DHCP?XZ_NET_STATIC_DISABLE_DHCP:XZ_NET_STATIC_KEEP_DHCP;d->revision++;return 0;}
    if(kind==XZ_UI_NETWORK_FIELD&&index>=0&&index<=2){d->field=(enum xz_network_field)index;memcpy(d->edit,index==0?d->ip:index==1?d->mask:d->gateway,16);xz_ui_navigate(u,XZ_UI_NETWORK_EDIT);return 0;}
    if(kind==XZ_UI_NETWORK_APPLY){if(d->step!=XZ_NETWORK_REVIEW||!parse_draft(u,m))return 0;d->step=XZ_NETWORK_VERIFY;return emit(u,kind,index,out);}
    return 0;
}
const char *xz_ui_network_help(int kind){
    switch(kind){case XZ_UI_NETWORK_APPLY:return "Applies the reviewed policy for this boot. Keeps existing addresses. The worker verifies readback and restores only its own changes on failure.";
    case XZ_UI_NETWORK_REVERT:return "Stops only the MODS DHCP child and removes only this trial's unchanged additions. Native clients and addresses stay under native control.";
    case XZ_UI_NETWORK_PRIORITY:return "Sets the preferred order for the MODS advertised address. Native private binding is unchanged. Apply is required.";
    case XZ_UI_NETWORK_MODE:return "Edits the request only. DHCP OFF refers to the MODS client. Embedded native DHCP remains unknown. A compatible client is detected at runtime.";
    default:return "Detect actual adapters, choose an order and address mode, explicitly Apply, then verify local readback. Choices last until reboot.";}
}
static void text(const struct xz_ui_scene *s,int y,const char *line,uint32_t ink){xz_text(s->c,136,y,line,1,108,ink);}
void xz_ui_network_render(const struct xz_ui_scene *s){
    const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;uint32_t bg,ink,accent;xz_ui_chrome_colors(m,s->p,&bg,&ink,&accent);
    xz_rect(s->c,136,48,656,356,bg);xz_text(s->c,136,52,xz_ui_network_title(u->page),2,50,ink);
    char line[160];snprintf(line,sizeof line,"%s",m->network.detail[0]?m->network.detail:"CHANGES LAST UNTIL REBOOT / SET UP NETWORK TO DETECT");text(s,82,line,accent);
    if(u->network.setup_reason){text(s,124,"RELOAD A PRIVATE USB IMAGE WITH NETWORK SUPPORT.",ink);text(s,150,"DHCP NEEDS THE VERIFIED UDHCPC AND COMPILED EVENT HELPER.",ink);text(s,176,"THEN VERIFY THE RELOADED LOADER. STATIC CAN KEEP NATIVE DHCP.",ink);}
    else if(u->page==XZ_UI_NETWORK_EDIT){snprintf(line,sizeof line,"%s: %s",u->network.field==0?"IP":u->network.field==1?"MASK / PREFIX":"GATEWAY",u->network.edit);text(s,112,line,ink);}
    else if(u->page==XZ_UI_NETWORK_SETUP&&u->network.initialized){
        const struct xz_net_adapter *a=actual(u,m);const struct xz_network_ui_draft *d=&u->network;
        snprintf(line,sizeof line,"STEP %d/4  SELECTED: %s  LINK: %s",d->step+1,a?a->id.name:"CHANGED",!a||a->link_up==XZ_NET_UNKNOWN?"UNKNOWN":a->link_up==XZ_NET_YES?"UP":"DOWN");text(s,108,line,ink);
        char ip[16]="UNKNOWN",gateway[16]="UNKNOWN";if(a&&a->address_count)xz_net_ipv4_text(a->address[0].ip,ip);if(a&&a->gateway_known==XZ_NET_YES){if(a->gateway_present)xz_net_ipv4_text(a->gateway,gateway);else strcpy(gateway,"NONE");}
        snprintf(line,sizeof line,"CURRENT: %s/%s  GATEWAY: %s",ip,a&&a->address_count?"SEE PREFIX":"UNKNOWN",gateway);if(a&&a->address_count)snprintf(line,sizeof line,"CURRENT: %s/%u  GATEWAY: %s",ip,a->address[0].prefix,gateway);text(s,128,line,ink);
        snprintf(line,sizeof line,"REQUEST IP %s  PREFIX %s  GW %s",d->ip[0]?d->ip:"KEEP",d->mask[0]?d->mask:"KEEP",d->gateway[0]?d->gateway:"KEEP");text(s,148,line,ink);
        if(d->step!=XZ_NETWORK_CHOOSE){size_t at=0;line[0]=0;for(unsigned i=0;i<d->trial.policy.count&&at<sizeof line;i++){int n=snprintf(line+at,sizeof line-at,"%s%u:%s",i?"  ":"ORDER ",i+1,d->trial.policy.order[i].adapter.name);if(n<0||(size_t)n>=sizeof line-at)break;at+=(size_t)n;}text(s,180,line,ink);text(s,204,"NATIVE DHCP: UNKNOWN / EXISTING ADDRESSES KEPT",ink);text(s,228,"LOCAL VERIFY DOES NOT CLAIM PC OR PRO DJ LINK ACCEPTANCE",ink);}
        else{snprintf(line,sizeof line,"ORDER #%d OF %u / NATIVE DHCP UNKNOWN",d->selected+1,d->trial.policy.count);text(s,168,line,ink);}
    }else{
        for(unsigned i=0;i<m->network.actual.count&&i<4;i++){
            const struct xz_net_adapter *a=&m->network.actual.adapter[i];char ip[16]="NO ADDRESS";if(a->address_count)xz_net_ipv4_text(a->address[0].ip,ip);
            snprintf(line,sizeof line,"%u %s  LINK %s  %s/%u  +%u MORE",i+1,a->id.name,a->link_up==XZ_NET_YES?"UP":a->link_up==XZ_NET_NO?"DOWN":"UNKNOWN",ip,a->address_count?a->address[0].prefix:0,a->address_count?a->address_count-1:0);text(s,110+(int)i*34,line,ink);
        }text(s,270,"DHCP ORIGIN / NATIVE OWNER: UNKNOWN UNLESS REPORTED",ink);text(s,292,"SETUP SHOWS ALL ADAPTERS. NOTHING CHANGES UNTIL APPLY.",ink);
    }
    for(size_t i=0;i<s->n;i++)if(xz_ui_network_kind(s->w[i].kind)){
        const struct xz_ui_widget *a=&s->w[i];int selected=xz_ui_network_selected(u,a);uint32_t fill=xz_blend(bg,accent,selected?130:40);
        xz_round_rect(s->c,a->x,a->y,a->w,a->h,6,15,selected?accent:xz_blend(bg,ink,80));xz_round_rect(s->c,a->x+1,a->y+1,a->w-2,a->h-2,5,15,fill);
        xz_ui_chrome_fit(s->c,a->x+8,a->y+(a->h<24?4:13),a->w-16,a->label,1,ink);
    }
}
int xz_ui_network_menu(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_shell_menu *out){
    memset(out,0,sizeof *out);out->page=u->page;out->root=xz_ui_menu_root(u,NULL);out->depth=(uint8_t)(u->page==XZ_UI_NETWORK?1:u->page==XZ_UI_NETWORK_SETUP?2:3);out->focus_row=-1;
    snprintf(out->title,sizeof out->title,"%s",xz_ui_network_title(u->page));snprintf(out->path,sizeof out->path,"GENERAL / NETWORK");out->section_count=1;strcpy(out->sections[0],"NETWORK");
    static const char *steps[]={"DETECT","CHOOSE","APPLY","VERIFY"};out->step_count=4;out->step=(uint8_t)u->network.step;for(unsigned i=0;i<4;i++)strcpy(out->steps[i],steps[i]);
    struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
    for(size_t i=0;i<n&&out->row_count<XZ_SHELL_MENU_ROWS;i++)if(xz_ui_network_kind(w[i].kind)){
        struct xz_shell_menu_row *r=&out->row[out->row_count++];r->kind=w[i].kind==XZ_UI_NETWORK_KEY&&w[i].index<13?XZ_SHELL_MENU_KEY:XZ_SHELL_MENU_ACTION;r->available=1;r->current=-1;r->key=((int32_t)w[i].kind<<16)^(w[i].index&0xffff);
        snprintf(r->label,sizeof r->label,"%s",w[i].label);snprintf(r->help,sizeof r->help,"%s",xz_ui_network_help(w[i].kind));if(xz_ui_network_selected(u,&w[i]))strcpy(r->value,"SELECTED");
        r->target=(struct xz_shell_menu_target){(int16_t)w[i].kind,(int16_t)w[i].index,-1,XZ_UI_MENU_TOUCH,0};
        if(u->focus_kind==w[i].kind&&u->focus_index==w[i].index)out->focus_row=(int16_t)(out->row_count-1);
    }
    if(u->page==XZ_UI_NETWORK_EDIT){out->layout=XZ_SHELL_MENU_KEYBOARD;snprintf(out->field_label,sizeof out->field_label,"%s",u->network.field==0?"IP":u->network.field==1?"MASK / PREFIX":"GATEWAY");snprintf(out->field,sizeof out->field,"%s",u->network.edit);}
    for(unsigned i=0;i<m->network.actual.count&&out->line_count<XZ_SHELL_MENU_LINES;i++){
        const struct xz_net_adapter *a=&m->network.actual.adapter[i];char ip[16]="NONE",gateway[16]="UNKNOWN";if(a->address_count)xz_net_ipv4_text(a->address[0].ip,ip);
        if(a->gateway_known==XZ_NET_YES){if(a->gateway_present)xz_net_ipv4_text(a->gateway,gateway);else strcpy(gateway,"NONE");}
        struct xz_shell_menu_line *line=&out->line[out->line_count++];snprintf(line->text,sizeof line->text,"%s LINK %s IP %s/%u GW %s",a->id.name,a->link_up==XZ_NET_YES?"UP":a->link_up==XZ_NET_NO?"DOWN":"UNKNOWN",ip,a->address_count?a->address[0].prefix:0,gateway);
    }
    if(out->line_count<XZ_SHELL_MENU_LINES){int on=0,unknown=!m->network.actual.count;for(unsigned i=0;i<m->network.actual.count;i++){on|=m->network.actual.adapter[i].owned_dhcp_enabled==XZ_NET_YES;unknown|=m->network.actual.adapter[i].owned_dhcp_enabled==XZ_NET_UNKNOWN;}struct xz_shell_menu_line *line=&out->line[out->line_count++];snprintf(line->text,sizeof line->text,"MODS DHCP: %s; NATIVE DHCP: UNKNOWN",on?"ON":unknown?"NOT REPORTED":"OFF");}
    snprintf(out->status,sizeof out->status,"%.99s",u->notice[0]?u->notice:m->network.detail);return 1;
}
