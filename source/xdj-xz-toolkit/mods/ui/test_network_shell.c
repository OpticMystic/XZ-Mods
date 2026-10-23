/* Actual MODERN shell + actual MODS controls. Reuses the existing recording bridge, not its legacy main. */
#define main uncalled_legacy_shell_suite
#include "../shell/test_menu.c"
#undef main
#include "ui_network.h"
static int row_kind(int kind,int index){for(int i=0;i<menu.row_count;i++)if(menu.row[i].target.kind==kind&&menu.row[i].target.index==index)return i;assert(0);return -1;}
int main(void){
    fx_track_build(&track,7);fx_model_full(&sm,&track);sm.settings_screen=fxs_reset();sm.mods=&mfx_mods;sm.menu=&menu;(void)fxs_item;
    struct xz_shell s;fresh(&s);um.network.actual.count=1;um.network.actual.generation=8;um.network.result=XZ_NET_OK;
    struct xz_net_adapter *a=&um.network.actual.adapter[0];strcpy(a->id.name,"fixture0");a->id.ifindex=7;a->id.identity_epoch=3;a->state_revision=2;a->addresses_known=a->link_up=XZ_NET_YES;a->caps=XZ_NET_CAP_STATIC_ADD;a->address_count=1;a->address[0].ip=(struct xz_net_ipv4){{192,0,2,10}};a->address[0].prefix=24;
    xz_ui_navigate(&ui,XZ_UI_NETWORK);publish();xz_shell_mods_show(&s,&sm,1);render_check(&s);
    cursor_to(&s,"CHOOSE ADAPTER");push(&s);assert(ui.page==XZ_UI_NETWORK_SETUP&&ui.network.initialized);render_check(&s);
    cursor_to(&s,"STATIC IP");push(&s);cursor_to(&s,"EDIT IP");push(&s);assert(ui.page==XZ_UI_NETWORK_EDIT&&menu.layout==XZ_SHELL_MENU_KEYBOARD);render_check(&s);
    /* Every key has real shell keyboard geometry inside the screen. */
    for(int r=0;r<menu.row_count;r++){struct xz_shell_rect box=xz_shell_menu_row_rect(&s,&sm,r);assert(box.w>=40&&box.h>=36&&box.x>=0&&box.y>=0&&box.x+box.w<=XZ_SHELL_WIDTH&&box.y+box.h<=xz_shell_settings_rect(XZ_SHELL_SR_STATUS).y);}
    cursor_to(&s,"CLEAR");push(&s);assert(!ui.network.edit[0]);
    const char *typed="192.0.2.50";
    for(const char *p=typed;*p;p++){
        int r=row_kind(XZ_UI_NETWORK_KEY,*p=='.'?10:*p-'0');struct xz_shell_rect box=xz_shell_menu_row_rect(&s,&sm,r);assert(menu.row[r].kind==XZ_SHELL_MENU_KEY);assert(tap(&s,box.x+box.w/2,box.y+box.h/2)==1);
    }assert(!strcmp(ui.network.edit,typed)&&!strcmp(menu.field,typed));render_check(&s);
    cursor_to(&s,"DONE");push(&s);assert(ui.page==XZ_UI_NETWORK_SETUP&&!strcmp(ui.network.ip,typed));cursor_to(&s,"REVIEW CHANGES");push(&s);render_check(&s);
    cursor_to(&s,"APPLY FOR THIS BOOT");ran_count=0;push(&s);assert(ran_count==0);
    assert(xz_shell_render_frame(&s,&sm,buffer+GUARD+STRIDE,STRIDE));
    xz_ui_network_painted(&ui,&um,xz_ui_network_review_token(&ui,&um));
    push(&s);assert(ran_count==1&&ran[0].kind==XZ_UI_NETWORK_APPLY&&in_mods(&s)&&!others&&!refusals);
    puts("PASS actual MODERN shell: NETWORK navigation, encoder mode/edit, real keypad touch/geometry/render, review and Apply action");
}
