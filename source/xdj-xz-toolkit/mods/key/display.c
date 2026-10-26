/* SPDX-License-Identifier: MIT */
#include "display.h"
#include "../ui/ui_skin.h"
#include <stdio.h>

void xz_key_display_panel(uint16_t *pixels,size_t stride,const struct xz_ui *ui,const struct xz_ui_model *model){
    struct xz_canvas c={pixels,stride,800,480,model->theme};
    const struct xz_theme_palette *p=xz_theme_palette(model->theme);
    /* KEYS: each native deck's key as it plays now, under the key buttons. */
    if(ui->page==XZ_UI_KEY_SETUP){
        int y=262;
        for(int deck=0;deck<2;deck++){
            char text[64];
            snprintf(text,sizeof(text),"D%d %s%s",deck+1,model->deck[deck].key_readout,model->deck[deck].key_synced?"  KEY SYNC":"");
            xz_round_rect(c,136+deck*332,y,324,28,8,15,xz_blend(p->bg,p->ink,22));
            xz_round_rect(c,136+deck*332,y,6,28,3,15,model->deck[deck].key_color);
            xz_text(c,152+deck*332,y+9,text,1,50,p->ink);
        }
    }
}
