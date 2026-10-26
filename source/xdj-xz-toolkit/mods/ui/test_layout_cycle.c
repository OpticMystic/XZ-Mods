#define main uncalled_four_deck_suite
#include "../shell/test_four_deck.c"
#undef main
#include "layout_cycle.h"
#include "deck_view.h"
static void image_file(struct xz_shell *s,struct xz_shell_model *m,const char *dir,int mode){
 char path[1024];snprintf(path,sizeof path,"%s/flagship-layout-%d.ppm",dir,mode);
 assert(xz_shell_render_frame(s,m,frame(buffer_a),STRIDE));FILE *f=fopen(path,"wb");assert(f);fputs("P6\n800 480\n255\n",f);
 for(int y=0;y<480;y++)for(int x=0;x<800;x++){unsigned v=frame(buffer_a)[y*STRIDE+x];unsigned char b[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};assert(fwrite(b,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv){
 assert(argc==2);fx_track_build(&track,1);struct xz_shell s;struct xz_shell_model m,view;struct xz_shell_action out[XZ_SHELL_ACTIONS];
 fx_model_four(&m,&track);m.settings_screen=fxs_reset();xz_shell_init(&s);
 static const int order[2][4]={{0,1,2,3},{2,0,1,3}};unsigned checks=0;
 for(int selected=0;selected<4;selected++)for(int mode=0;mode<XZ_LAYOUT_COUNT;mode++){
  m.layout_preference=mode+1;m.sides_extra=selected;xz_shell_play_projection(&s,&m,&view);
  assert(view.play_view_count==(mode==XZ_LAYOUT_TWO?2:4));
  for(int slot=0;slot<view.play_view_count;slot++){
   int deck=mode==XZ_LAYOUT_TWO?slot+2*((selected>>slot)&1):order[mode==XZ_LAYOUT_MIXER][slot];
   struct xz_shell_rect lane=rect(&view,XZ_SHELL_R_LANE,deck);assert(lane.w>0);
   assert(xz_shell_render_frame(&s,&m,frame(buffer_a),STRIDE));
   tap(&s,&m,50,lane.y+38,out);assert(s.deck==deck);checks++;
   struct xz_shell_rect keybox=rect(&view,XZ_SHELL_R_KEY,deck);
   assert(xz_shell_render_frame(&s,&m,frame(buffer_a),STRIDE));assert(xz_shell_play_key_deck(&s,&m,keybox.x+6,keybox.y+6)==deck);checks++;
  }
  struct xz_shell_rect button=rect(&view,XZ_SHELL_R_VIEW_TWO,0);assert(button.x==504&&button.w==120&&!rect(&view,XZ_SHELL_R_VIEW_FOUR,0).w);
  assert(xz_shell_render_frame(&s,&m,frame(buffer_a),STRIDE));
  /* DECK VIEW: one tap opens the picker; a picture chooses; nothing acts on an unpainted overlay. */
  size_t n=tap(&s,&m,button.x+button.w/2,10,out);assert(n==0&&s.deck_view.open&&s.deck_view.tile==mode);
  int next=xz_deck_layout_next(mode);struct xz_dv_rect tile=xz_deck_view_tile(&xz_deck_view_modern,next);
  assert(!tap(&s,&m,tile.x+tile.w/2,tile.y+tile.h/2,out)&&s.deck_view.open); /* the open picker is not painted yet */
  assert(xz_shell_render_frame(&s,&m,frame(buffer_a),STRIDE));
  n=tap(&s,&m,tile.x+tile.w/2,tile.y+tile.h/2,out);
  assert(n==1&&out[0].kind==XZ_SHELL_ACT_DECK_LAYOUT&&out[0].value==next&&!s.deck_view.open);checks++;
  m.layout_preference=out[0].value+1;
  assert(!tap(&s,&m,button.x+4,10,out)&&!s.deck_view.open); /* changed projection is not painted yet */
  m.layout_preference=mode+1;
  deck_key(&s,&m,XZ_SHELL_KEY_SHORTCUT,-1,out);deck_key(&s,&m,XZ_SHELL_KEY_BACK,-1,out);assert(m.layout_preference==mode+1);
  /* Decks 3 and 4 down: DECK VIEW keeps four slots, 3 and 4 as placeholders; audio stays two decks. */
  m.deck_count=2;xz_shell_play_projection(&s,&m,&view);
  assert(view.play_view_count==(mode==XZ_LAYOUT_TWO?2:4)&&view.play_placeholders==(mode==XZ_LAYOUT_TWO?0:0xc)&&view.deck_count==2&&m.layout_preference==mode+1);
  m.deck_count=4;xz_shell_play_projection(&s,&m,&view);assert(view.play_view_count==(mode==XZ_LAYOUT_TWO?2:4)&&!view.play_placeholders);
  if(selected==3)image_file(&s,&m,argv[1],mode);
 }
 /* Route return uses the shared preference even with a freshly initialized shell. */
 xz_shell_init(&s);m.layout_preference=XZ_LAYOUT_MIXER+1;xz_shell_play_projection(&s,&m,&view);assert(view.play_view_count==4&&view.wave_order==1);
 printf("PASS DECK VIEW: %u header/identity checks; all four side assignments, both sides/all four lanes; picker opens, picture chooses, stale paint blocked; menus and route return retain DECK VIEW; four slots with placeholders while 3/4 are down\n",checks);
 return 0;
}
