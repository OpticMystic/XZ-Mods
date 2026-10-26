#include "ui_skin.h"
#include "ui_menu.h"
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
static int contains(const struct xz_shell_menu *menu,const char *text){
 if(strstr(menu->status,text))return 1;
 for(int i=0;i<menu->line_count;i++)if(strstr(menu->line[i].text,text))return 1;
 for(int i=0;i<menu->row_count;i++)if(strstr(menu->row[i].reason,text))return 1;
 return 0;
}
int main(void){
 struct xz_ui u;struct xz_ui_model m={0};struct xz_shell_menu menu;xz_ui_init(&u);
 u.page=XZ_UI_STEM_PADS;CHECK(!strncmp(title_of(&u,&m,XZ_UI_PAD_FEEDBACK,1),"STEM PAD LIGHTS",15));
 CHECK(!strncmp(title_of(&u,&m,XZ_UI_PAD_FEEDBACK,2),"STEM PAD LIGHTS",15));
 u.page=XZ_UI_MYHOUSE_SETUP;CHECK(!strcmp(label(&u,&m,XZ_UI_MYHOUSE_CHECK),"CHECK ENGINE"));
 u.page=XZ_UI_RAM_SETUP;m.deck[0].ram.state=XZ_UI_RAM_READY;m.deck[0].ram.retained_stems_bytes=1234;
 CHECK(strstr(xz_ui_footer_text(&u,&m),"PREPARED STEMS IN RAM"));
 CHECK(xz_ui_menu_build(&u,&m,&menu));CHECK(contains(&menu,"PREPARED STEMS IN RAM"));CHECK(contains(&menu,"WAVES STILL USE USB"));
 m.deck[0].ram.retained_stems_bytes=0;CHECK(!strstr(xz_ui_footer_text(&u,&m),"PREPARED STEMS IN RAM"));
 CHECK(xz_ui_menu_build(&u,&m,&menu));CHECK(contains(&menu,"STEMS AND WAVES STILL USE USB"));
 u.page=XZ_UI_STEM_PADS;m.stem_page=3;m.jump.enabled=0;CHECK(xz_ui_menu_build(&u,&m,&menu));
 CHECK(contains(&menu,"PAGE 2"));CHECK(contains(&menu,"STEM PADS"));
 u.page=XZ_UI_STEMS;CHECK(xz_ui_menu_build(&u,&m,&menu));
 CHECK(contains(&menu,"THIS LOADER DOES NOT READ PAD HOT CUES."));
 CHECK(!contains(&menu,"NO GROOVE PAD ENGINE"));
 printf("RESULT help_parity checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
