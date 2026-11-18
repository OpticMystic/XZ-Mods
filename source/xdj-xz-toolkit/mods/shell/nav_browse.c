/* Host-only metadata from the production browse layout. */
#include "shell_browse.c"
#include "../ui/ui.h"
size_t nav_browse_widgets(const struct xz_shell *u,const struct xz_shell_model *m,struct xz_ui_widget *w){
 if(!panel_open(u))return 0;
 static struct pitem items[MAX_PITEMS];
 int count=panel_items(u,m,items),cur=panel_cursor(u,items,count),sc=panel_scroll(u,cur,count);size_t n=0;
 for(int i=sc;i<count&&i<sc+panel_rows(u);i++)if(selectable_item(&items[i])){
  struct xz_shell_rect r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_PANEL_ROW,i-sc);
  w[n++]=(struct xz_ui_widget){.x=r.x,.y=r.y,.w=r.w,.h=r.h,.label=items[i].label};
 }
 return n;
}
