/* Actual shell, menu adapter and renderer. Only synthetic adapter readback; action sink records I/O. */
#define main retained_full_menu_main
#include "../shell/test_menu.c"
#undef main
#include "ui_network.h"
#include <stdlib.h>

_Static_assert(XZ_UI_NETWORK==30&&XZ_UI_NETWORK_SETUP==31&&XZ_UI_NETWORK_EDIT==32&&XZ_UI_PAGE_COUNT==33,"persistent page IDs unchanged");
_Static_assert(XZ_UI_NETWORK_SETUP_OPEN==114&&XZ_UI_NETWORK_CANCEL==124,"action IDs unchanged");

static void snapshot(struct xz_shell *s,const char *dir,const char *name,int classic){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 uint16_t *p=buffer+GUARD+STRIDE;
 if(classic)assert(xz_ui_render(&ui,&um,p,STRIDE*480,STRIDE));else render_check(s);
 FILE *f=fopen(path,"wb");assert(f);fputs("P6\n800 480\n255\n",f);
 for(int y=0;y<480;y++)for(int x=0;x<800;x++){
  unsigned v=p[y*STRIDE+x];unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
  assert(fwrite(rgb,1,3,f)==3);
 }assert(!fclose(f));
}
static void open_network(struct xz_shell *s){
 fresh(s);detected_network();snprintf(um.network.detail,sizeof um.network.detail,"SYNTHETIC READBACK / NO DEVICE I/O");publish();
 key(s,XZ_SHELL_KEY_MENU);turn(s,6);
 assert(s->mods_view.tab==7&&ui.page==XZ_UI_NETWORK&&menu.root==XZ_UI_NETWORK&&menu.depth==0&&!ui.history_count);
 push(s);assert(!s->mods_view.on_tabs&&pane(s)==1);
 for(unsigned i=0;i<menu.row_count;i++)assert(!strstr(menu.row[i].label,"PRO DJ")&&!strstr(menu.row[i].label,"PRODJ"));
}
static void paint(struct xz_shell *s){assert(xz_shell_render_frame(s,&sm,buffer+GUARD+STRIDE,STRIDE));xz_ui_network_painted(&ui,&um,xz_ui_network_review_token(&ui,&um));}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);assert(argc==2);
 fx_track_build(&track,7);fx_model_full(&sm,&track);sm.settings_screen=fxs_reset();sm.mods=&mfx_mods;sm.menu=&menu;(void)fxs_item;
 struct xz_shell s;open_network(&s);snapshot(&s,argv[1],"network-root",0);
 struct xz_network_ui_draft before=ui.network;
 cursor_to(&s,"NETWORK UPDATES");assert(push(&s)==1&&ui.page==XZ_UI_UPDATES&&menu.depth==1&&menu.root==XZ_UI_NETWORK);
 assert(!memcmp(&before,&ui.network,sizeof before));snapshot(&s,argv[1],"network-updates",0);
 cursor_to(&s,"NETWORK SETUP");push(&s);assert(ui.page==XZ_UI_UPDATE_SETUP&&menu.depth==2);snapshot(&s,argv[1],"network-update-setup",0);
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_UPDATES&&menu.depth==1);
 key(&s,XZ_SHELL_KEY_BACK);assert(ui.page==XZ_UI_NETWORK&&menu.depth==0);
 assert(cursor_row(&s)&&!strcmp(cursor_row(&s)->label,"NETWORK UPDATES"));
 push(&s);assert(ui.page==XZ_UI_UPDATES);key(&s,XZ_SHELL_KEY_BACK);
 key(&s,XZ_SHELL_KEY_BACK);assert(xz_shell_screen(&s)==XZ_SHELL_PLAY);
 puts("PASS NETWORK tab7: one push into each branch; update setup BACK restores link, root BACK closes; network draft unchanged by update navigation");

 open_network(&s);cursor_to(&s,"CHOOSE ADAPTER");push(&s);assert(ui.page==XZ_UI_NETWORK_SETUP&&menu.depth==1);
 cursor_to(&s,"STATIC IP");push(&s);snapshot(&s,argv[1],"network-addresses",0);
 cursor_to(&s,"EDIT IP");push(&s);assert(menu.depth==2);cursor_to(&s,"DONE");push(&s);
 assert(ui.page==XZ_UI_NETWORK_SETUP&&ui.history_count==1);
 cursor_to(&s,"REVIEW CHANGES");push(&s);assert(ui.network.step==XZ_NETWORK_REVIEW);
 cursor_to(&s,"APPLY FOR THIS BOOT");ran_count=0;
 push(&s);assert(!ran_count&&ui.network.step==XZ_NETWORK_REVIEW&&strstr(ui.notice,"WAIT FOR REVIEW"));
 puts("PASS negative paint control: real encoder Apply before review paint emits zero actions");
 paint(&s);uint64_t previous=ui.network_review.painted;assert(previous);
 um.network.actual.adapter[0].state_revision++;push(&s);assert(!ran_count&&!ui.network_review.painted);
 um.network.actual.adapter[0].state_revision--;
 paint(&s);previous=ui.network_review.painted;assert(previous);
 um.network.request_id++;push(&s);assert(!ran_count&&!ui.network_review.painted);
 paint(&s);ui.network.revision++;push(&s);assert(!ran_count&&!ui.network_review.painted);
 paint(&s);snapshot(&s,argv[1],"network-review",0);
 push(&s);assert(ran_count==1&&ran[0].kind==XZ_UI_NETWORK_APPLY&&ran[0].network_publication);
 struct xz_net_trial trial;assert(xz_ui_network_take_apply(&ui,&um,&ran[0],&trial));assert(!xz_ui_network_take_apply(&ui,&um,&ran[0],&trial));
 puts("PASS painted review bound to adapter state, runtime request and draft revision; fresh paint permits one Apply, runtime consumes once");

 open_network(&s);um.ui_interface=XZ_UI_INTERFACE_CLASSIC;snapshot(&s,argv[1],"network-classic",1);
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&um,w);int roots=0,updates=0;
 for(size_t i=0;i<n;i++){
  if(w[i].kind==XZ_UI_PANEL&&xz_ui_page_is_top(w[i].index)){roots++;assert(w[i].x==0&&w[i].h>=44&&w[i].y+w[i].h<=404);}
  if(w[i].kind==XZ_UI_PANEL&&w[i].index==XZ_UI_UPDATES)updates++;
  if(xz_ui_network_kind(w[i].kind)||(w[i].kind==XZ_UI_PANEL&&w[i].index==XZ_UI_UPDATES)){
   assert(w[i].x>=136&&w[i].w>=44&&w[i].h>=36&&w[i].y+w[i].h<=404);
   for(size_t j=i+1;j<n;j++)if(xz_ui_network_kind(w[j].kind)||(w[j].kind==XZ_UI_PANEL&&w[j].index==XZ_UI_UPDATES))assert(w[i].x+w[i].w<=w[j].x||w[j].x+w[j].w<=w[i].x||w[i].y+w[i].h<=w[j].y||w[j].y+w[j].h<=w[i].y);
  }
 }assert(roots==6&&updates==1&&!others&&!refusals);
 puts("PASS classic six-root rail and shared NETWORK/update controls fit 800x480 without overlap");
 return 0;
}
