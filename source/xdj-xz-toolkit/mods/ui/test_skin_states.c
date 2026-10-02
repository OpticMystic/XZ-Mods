#ifdef NDEBUG
#error Skin acceptance requires assertions
#endif
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t before[800*480],after[800*480];
static struct xz_ui_widget widget(struct xz_ui *u,struct xz_ui_model *m,enum xz_ui_action_kind kind,int index){
 struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,w);
 for(size_t i=0;i<n;i++)if(w[i].kind==kind&&w[i].index==index)return w[i];
 assert(0);return w[0];
}
static size_t changed(struct xz_ui_widget w){
 size_t n=0;for(int y=w.y;y<w.y+w.h;y++)for(int x=w.x;x<w.x+w.w;x++)n+=before[y*800+x]!=after[y*800+x];return n;
}
int main(void){
 assert(XZ_THEME_LCARS==21&&XZ_THEME_MATRIX==22&&XZ_THEME_ANALOG==23);
 assert(!strcmp(xz_theme_name(12),"GAME BOY DARK"));
 assert(!strcmp(xz_theme_name(14),"WINDOWS 95 DARK"));
 assert(!strcmp(xz_theme_name(20),"LIQUID GLASS DARK"));
 for(int theme=XZ_THEME_LCARS;theme<XZ_THEME_COUNT;theme++){
  struct xz_ui u;struct xz_ui_model m={0};xz_ui_init(&u);u.page=XZ_UI_STEMS;m.theme=theme;m.enabled=XZ_UI_STEM;
  m.deck[0].ready=XZ_UI_STEM|XZ_UI_GROOVE;m.deck[0].groove_assigned=255;m.deck[0].groove_loaded=255;m.deck[0].groove_active=-1;
  struct xz_ui_widget a=widget(&u,&m,XZ_UI_GROOVE_PAD,2);
  assert(xz_ui_render(&u,&m,before,800*480,800));m.deck[0].groove_active=2;
  assert(!m.blink);assert(xz_ui_render(&u,&m,after,800*480,800));
  /* Active feedback changes the control fill, not only the PLAYING label. */
  assert(changed(a)>(size_t)(a.w*a.h/4));
  m.deck[0].groove_assigned=0;m.deck[0].ready=0;a=widget(&u,&m,XZ_UI_HOTCUE_PAD,0);
  assert(xz_ui_render(&u,&m,before,800*480,800));m.deck[0].ready=XZ_UI_HOTCUE;
  assert(xz_ui_render(&u,&m,after,800*480,800));assert(changed(a)>30);
  /* Muting does not erase the stored gain readout. */
  m.deck[0].ready=XZ_UI_STEM;m.deck[0].muted=1;m.deck[0].levels[0]=1;
  assert(xz_ui_inline_render(&u,&m,before,800*96,800,800,96));m.deck[0].levels[0]=.42f;
  assert(xz_ui_inline_render(&u,&m,after,800*96,800,800,96));assert(memcmp(before,after,800*96*2));
  /* A source renderer must be deterministic and leave the model untouched. */
  struct xz_ui_model snapshot=m;assert(xz_ui_render(&u,&m,before,800*480,800));assert(xz_ui_render(&u,&m,after,800*480,800));
  assert(!memcmp(before,after,sizeof(before)));assert(!memcmp(&snapshot,&m,sizeof(m)));
 }
 puts("PASS preserved theme IDs, steady active grooves, hot-cue readiness, muted gain and deterministic rendering");
}
