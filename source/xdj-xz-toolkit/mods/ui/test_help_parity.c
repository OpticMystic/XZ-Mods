#include "ui_skin.h"
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Help parity acceptance requires active assertions
#endif
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %s\n",#x);}}while(0)
static const char *label(struct xz_ui *u,struct xz_ui_model *m,int kind){
 struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,widgets);
 for(size_t i=0;i<n;i++)if((int)widgets[i].kind==kind)return widgets[i].label;
 return "MISSING";
}
/* The redesign moved PAD LIGHTS onto STEM PADS as two choices: their title names STEM PAD LIGHTS. */
static const char *title_of(struct xz_ui *u,struct xz_ui_model *m,int kind,int index){
 struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,widgets);struct xz_ui_info info;
 for(size_t i=0;i<n;i++)if((int)widgets[i].kind==kind&&widgets[i].index==index&&xz_ui_describe_widget(u,m,&widgets[i],&info))return info.title;
 return "MISSING";
}
/* The explanation the dock shows for a control of the page, in either interface (one menu). */
static const char *body_of(struct xz_ui *u,struct xz_ui_model *m,int kind,int index){
 struct xz_ui_widget widgets[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,widgets);struct xz_ui_info info;
 for(size_t i=0;i<n;i++)if((int)widgets[i].kind==kind&&widgets[i].index==index&&xz_ui_describe_widget(u,m,&widgets[i],&info))return info.body;
 return "MISSING";
}
int main(void){
 struct xz_ui u;struct xz_ui_model m={0};xz_ui_init(&u);
 u.page=XZ_UI_STEM_PADS;CHECK(!strncmp(title_of(&u,&m,XZ_UI_PAD_FEEDBACK,1),"STEM PAD LIGHTS",15));
 CHECK(!strncmp(title_of(&u,&m,XZ_UI_PAD_FEEDBACK,2),"STEM PAD LIGHTS",15));
 u.page=XZ_UI_MYHOUSE_SETUP;CHECK(!strcmp(label(&u,&m,XZ_UI_MYHOUSE_CHECK),"CHECK ENGINE"));
 u.page=XZ_UI_RAM_SETUP;m.deck[0].ram.state=XZ_UI_RAM_READY;m.deck[0].ram.retained_stems_bytes=1234;
 CHECK(strstr(xz_ui_footer_text(&u,&m),"STEMS IN RAM"));
 CHECK(strstr(xz_ui_footer_text(&u,&m),"WAVES KEPT"));
 m.deck[0].ram.retained_stems_bytes=0;CHECK(!strstr(xz_ui_footer_text(&u,&m),"STEMS IN RAM"));
 CHECK(strstr(xz_ui_footer_text(&u,&m),"STEMS NEED USB"));
 /* BEAT JUMP as the stem pad mode: page 2's jumps win its pads, page 1's with MY SIZES, in both interfaces */
 for(int iface=0;iface<2;iface++){
  m.ui_interface=iface?XZ_UI_INTERFACE_MODERN:XZ_UI_INTERFACE_CLASSIC;m.theme=iface?XZ_THEME_FLAGSHIP:0;
  u.page=XZ_UI_STEM_PADS;m.stem_page=3;m.jump.enabled=0;
  CHECK(strstr(body_of(&u,&m,XZ_UI_STEM_PAGE,3),"page 2"));CHECK(strstr(body_of(&u,&m,XZ_UI_STEM_PAGE,3),"stem bank"));
  /* a pad without a groove keeps its hot cue, and says so */
  u.page=XZ_UI_STEMS;m.deck[0].ready=XZ_UI_HOTCUE;
  CHECK(strstr(body_of(&u,&m,XZ_UI_HOTCUE_PAD,0),"keeps its hot cue"));
  CHECK(!strstr(xz_ui_footer_text(&u,&m),"NO GROOVE PAD ENGINE"));
 }
 printf("RESULT help_parity checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
