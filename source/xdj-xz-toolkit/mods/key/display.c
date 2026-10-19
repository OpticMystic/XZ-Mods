/* SPDX-License-Identifier: MIT */
#include "display.h"
#include "../ui/ui_skin.h"
#include <stdio.h>

void xz_key_display_panel(uint16_t *pixels,size_t stride,const struct xz_ui *ui,const struct xz_ui_model *model){
    struct xz_canvas c={pixels,stride,800,480,model->theme};
    const struct xz_theme_palette *p=xz_theme_palette(model->theme);
    if(ui->page==XZ_UI_KEY_SETUP){
        xz_text(c,136,52,"KEY SYNC SETUP",2,45,p->ink);
        xz_text(c,136,84,"1  LOAD TWO USB TRACKS WITH ANALYZED KEYS",1,100,p->ink);
        xz_text(c,136,104,"2  SELECT THE OTHER DECK AS MASTER",1,100,p->ink);
        xz_text(c,136,124,"3  DETECT BELOW VERIFIES TRACK KEYS",1,100,p->ink);
        xz_text(c,136,144,"4  PRESS SHIFT + SYNC; WATCH ORIGINAL > PLAYING KEY",1,100,p->ink);
        xz_text(c,136,164,"MASTER TEMPO HOLDS KEY; OTHERWISE TEMPO CHANGES IT",1,100,xz_blend(p->bg,p->ink,170));
        if(model->key_browser_status)xz_text(c,136,184,model->key_browser_status,1,108,p->ink);
    }
    if(ui->page==XZ_UI_SETTINGS||ui->page==XZ_UI_KEY_SETUP){
        int y=ui->page==XZ_UI_KEY_SETUP?344:304;
        for(int deck=0;deck<2;deck++){
            char text[64];
            snprintf(text,sizeof(text),"D%d %s%s",deck+1,model->deck[deck].key_readout,model->deck[deck].key_synced?"  KEY SYNC":"");
            xz_round_rect(c,136+deck*332,y,324,28,8,15,xz_blend(p->bg,p->ink,22));
            xz_round_rect(c,136+deck*332,y,6,28,3,15,model->deck[deck].key_color);
            xz_text(c,152+deck*332,y+9,text,1,50,p->ink);
        }
    }
}
