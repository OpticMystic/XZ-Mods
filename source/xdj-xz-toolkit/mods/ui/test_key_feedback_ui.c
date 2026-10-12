#define main previous_ui_test
#include "test_ui.c"
#undef main
static int overlap(struct xz_ui_widget a,struct xz_ui_widget b){return a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
int main(void){
    struct xz_ui u;struct xz_ui_model m;struct xz_ui_action result;
    for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int page=0;page<2;page++){
        memset(&m,0,sizeof(m));xz_ui_init(&u);m.theme=theme;m.key_feedback=1;
        u.page=page?XZ_UI_BROWSE_SETUP:XZ_UI_SETTINGS;
        struct xz_ui_widget option=find(&u,&m,XZ_UI_KEY_FEEDBACK,0),w[XZ_UI_WIDGETS];
        size_t count=xz_ui_layout(&u,&m,w);
        for(size_t i=0;i<count;i++)if(w[i].kind!=XZ_UI_KEY_FEEDBACK)assert(!overlap(option,w[i]));
        result=tap(&u,&m,XZ_UI_KEY_FEEDBACK,0);assert(result.kind==XZ_UI_KEY_FEEDBACK&&result.value==0);
        m.key_feedback=0;result=tap(&u,&m,XZ_UI_KEY_FEEDBACK,0);assert(result.value==1);
        assert(xz_ui_render(&u,&m,frame+8,800*480,800));
    }
    puts("PASS actual MODS toggle: on/off actions, zero-coordinate release, both pages, no control overlap, 24 themes");
}
