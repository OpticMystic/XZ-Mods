#include "l10n.h"
/* SPDX-License-Identifier: MIT */
#include "key_strip.h"
#include "ui_skin.h"
#include "native_palette.h"
#include <stdio.h>
#include <string.h>

/* Stock 1.26 RGB565 colours sampled from the real controller frame. */
enum { DECK_TITLE_BG = 0x118c, DECK_BODY_BG = 0x0861, BROWSE_HEAD_BG = 0x198c, BROWSE_RULE = 0x118b, BROWSE_EDGE = 0x0000 };

static uint16_t stock(int theme, uint16_t pixel) { return theme ? xz_native_palette_pixel(theme, pixel) : pixel; }
static uint16_t at(const uint16_t *pixels, size_t stride, int x, int y) { return pixels[(size_t)y * stride + (size_t)x]; }
static uint32_t rgb(uint16_t p) {
    return ((uint32_t)((p >> 11) & 31) * 255 / 31) << 16 | ((uint32_t)((p >> 5) & 63) * 255 / 63) << 8 | (uint32_t)(p & 31) * 255 / 31;
}
static void fill565(struct xz_canvas c, int x, int y, int w, int h, uint16_t p) {
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++)
        if (xx >= 0 && yy >= 0 && xx < c.width && yy < c.height) c.p[(size_t)yy * c.stride + (size_t)xx] = p;
}
/* Small solid triangle: right-pointing (dir 0) or down-pointing (dir 1). */
static void triangle(struct xz_canvas c, int x, int y, int size, int dir, uint32_t color) {
    for (int i = 0; i < size; i++) {
        if (dir) xz_rect(c, x + i, y + i, 2 * (size - i) - 1, 1, color);
        else xz_rect(c, x + i, y + i, 1, 2 * (size - i) - 1, color);
    }
}


static int footer_present(const uint16_t *pixels,size_t stride,int theme,int deck) {
    int right=deck?797:395;const int xs[3]={right-300,right-176,right};
    for(int i=0;i<3;i++)if(at(pixels,stride,xs[i],314)!=stock(theme,DECK_TITLE_BG)||
                          at(pixels,stride,xs[i],345)!=stock(theme,DECK_BODY_BG))return 0;
    return 1;
}
int xz_key_strip_area(const uint16_t *pixels,size_t stride,int theme,int deck,struct xz_key_strip_box *box) {
    if(!pixels||stride<800||deck<0||deck>1||!box||!footer_present(pixels,stride,theme,deck))return 0;
    int play=at(pixels,stride,21,31)==stock(theme,0xffff)&&at(pixels,stride,50,43)==stock(theme,0xffff);
    for(int d=0;d<2;d++)for(int i=0;i<4;i++) {
        int x=i&1?112:4,y=(i&2?155:116)+143*d;
        if(at(pixels,stride,x,y)!=stock(theme,0))play=0;
    }
    if(play){*box=(struct xz_key_strip_box){4,119+143*deck,107,35,XZ_KEY_STRIP_PLAY};return 1;}
    if(!xz_browse_head_present(pixels,stride,theme))return 0;
    /* A title reaching the spare header space fails closed. These pixels are
       outside both key rows, so we never sample our previous text. */
    for(int y=4;y<30;y++)for(int x=148;x<158;x++) {
        uint16_t p=at(pixels,stride,x,y);
        if(p!=stock(theme,BROWSE_HEAD_BG)&&p!=stock(theme,BROWSE_RULE)&&p!=stock(theme,DECK_TITLE_BG))return 0;
    }
    *box=(struct xz_key_strip_box){158,deck?17:3,138,14,XZ_KEY_STRIP_BROWSE};return 1;
}
int xz_key_strip_decks_present(const uint16_t *pixels,size_t stride,int theme,int deck) {
    struct xz_key_strip_box box;return xz_key_strip_area(pixels,stride,theme,deck,&box);
}
static void short_error(const char *,char out[32]);
static void error_lines(struct xz_canvas c,const char *error,uint32_t color) {
    char short_text[32],text[64];short_error(error,short_text);snprintf(text,sizeof(text),"SYNC: %s",short_text);const char *p=text;
    for(int row=0;row<2&&*p;row++) {
        size_t n=strlen(p);if(n>38)n=38;
        char line[40];memcpy(line,p,n);line[n]=0;
        while(n&&xz_text_width(c,line,1,40)>c.width-2)line[--n]=0;
        if(p[n]){size_t space=n;while(space&&p[space]!=' ')space--;if(space)n=space;}
        memcpy(line,p,n);line[n]=0;xz_text(c,1,row*15,line,1,40,color);
        p+=n;while(*p==' ')p++;
    }
}
static void short_error(const char *error,char out[32]) {
    const char *last=strrchr(error,' ');
    if(!strncmp(error,"MAKE DECK ",10))snprintf(out,32,"MASTER D%c FIRST",error[10]);
    else if(!strncmp(error,"LOAD A KEYED",12)&&last)snprintf(out,32,"KEY NEEDED ON D%c",last[1]);
    else if(strstr(error,"NO KEY"))snprintf(out,32,"TRACK NEEDS A KEY");
    else if(strstr(error,"ENGINE OFF"))snprintf(out,32,"KEY ENGINE: MODS");
    else snprintf(out,32,"%.21s",error);
}
static void render_key_tile(uint16_t *pixels,int theme,int deck,const struct xz_key_strip_deck *d,struct xz_key_strip_box box) {
    struct xz_canvas c={pixels,(size_t)box.w,box.w,box.h,0};
    uint16_t bg=stock(theme,box.kind==XZ_KEY_STRIP_PLAY?0:BROWSE_HEAD_BG);
    int linked=d->valid&&!d->error[0]&&d->synced&&(!d->match_color||d->match_color==0x52d794);
    if(linked)bg=(uint16_t)((0x14>>3)<<11|(0x67>>2)<<5|(0x43>>3));
    fill565(c,0,0,box.w,box.h,bg);
    uint32_t bg_rgb=rgb(bg);int light=(int)((bg_rgb>>16&255)*3+(bg_rgb>>8&255)*6+(bg_rgb&255))>1600;
    const uint32_t muted=linked?0xd4eedf:light?0x52657a:0x9fb2cf,ink=light?0x142235:0xffffff,green=linked?0xffffff:light?0x087849:0x52d794;
    const uint32_t amber=light?0x945505:0xf0c46a,error_color=light?0x8e2020:0xff8f86;
    uint32_t key_ink=d->match_color?d->match_color:ink;
    char cents[12]="";if(d->valid&&d->cents)snprintf(cents,sizeof(cents),"%+dc",d->cents);
    if(box.kind==XZ_KEY_STRIP_PLAY) {
        if(d->error[0]){error_lines(c,d->error,error_color);return;}
        int x=3;
        const char *first=d->valid&&d->moved?d->original:"KEY";
        xz_text(c,x,4,first,1,8,muted);x+=xz_text_width(c,first,1,8)+5;
        if(d->valid&&d->moved){triangle(c,x,6,3,0,muted);x+=10;}
        xz_text(c,x,0,d->valid?d->effective:"--",2,4,key_ink);
        x=3;
        if(d->synced||d->sync_master){const char *role=d->synced?"SYNC":"MASTER";int w=xz_text_width(c,role,1,8)+8;xz_round_rect(c,x,17,w,17,2,15,light?0xdef4e8:0x0b2b1e);xz_text(c,x+4,18,role,1,8,green);x+=w+6;}
        if(cents[0])xz_text(c,x,18,cents,1,10,d->synced?green:amber);
    } else {
        char label[4];snprintf(label,sizeof(label),"D%d",deck+1);xz_text(c,0,-1,label,1,4,muted);int x=18;
        if(d->error[0]){char line[32];short_error(d->error,line);xz_text(c,x,-1,line,1,20,error_color);return;}
        const char *original=d->valid?d->original:"--";
        xz_text(c,x,-1,original,1,4,d->synced?green:d->moved?muted:ink);x+=xz_text_width(c,original,1,4)+4;
        if(d->valid&&d->moved){triangle(c,x,4,2,0,muted);x+=8;xz_text(c,x,-1,d->effective,1,4,key_ink);x+=xz_text_width(c,d->effective,1,4)+4;}
        if(cents[0]){xz_text(c,x,-1,cents,1,10,d->synced?green:amber);x+=xz_text_width(c,cents,1,10)+4;}
        if(d->synced)xz_text(c,x,-1,"SYNC",1,6,green);
        else if(d->sync_master)xz_text(c,x,-1,xz_l10n_authored("MASTER"),1,8,green);
    }
}

int xz_key_strip_render_deck(uint16_t *pixels,size_t stride,int theme,int deck,const struct xz_key_strip_deck *d,int enabled) {
    struct xz_key_strip_box box;
    if(!enabled||!d||!xz_key_strip_area(pixels,stride,theme,deck,&box))return 0;
    /* The XZ's visible framebuffer can be sampled while a label is painted.
       Finish the small tile first, then publish only rows that changed. */
    uint16_t tile[138*35];
    if(box.w>138||box.h>35)return 0;
    render_key_tile(tile,theme,deck,d,box);
    for(int row=0;row<box.h;row++) {
        uint16_t *destination=pixels+(size_t)(box.y+row)*stride+(size_t)box.x;
        const uint16_t *source=tile+(size_t)row*box.w;
        size_t bytes=(size_t)box.w*sizeof(*tile);
        if(memcmp(destination,source,bytes))memcpy(destination,source,bytes);
    }
    return box.w;
}

int xz_browse_head_present(const uint16_t *pixels, size_t stride, int theme) {
    if (!pixels || stride < 800) return 0;
    uint16_t head = stock(theme, BROWSE_HEAD_BG), rule = stock(theme, BROWSE_RULE), edge = stock(theme, BROWSE_EDGE);
    /* The stock header is rows 3..32: black row 2 above, a darker rule on row 31, header
       row 32, black row 33 below. The cells fill rows 3..30, so none of these rows is
       ever ours. The left edge at text height must be header too: a long stock page
       title that reaches into the cells fails this and the header is not drawn over it. */
    const int xs[3] = { XZ_BROWSE_HEAD_LEFT - 4, 506, XZ_BROWSE_HEAD_RIGHT + 2 };
    for (int i = 0; i < 3; i++)
        if (at(pixels, stride, xs[i], 2) != edge || at(pixels, stride, xs[i], 31) != rule ||
            at(pixels, stride, xs[i], 32) != head || at(pixels, stride, xs[i], 33) != edge) return 0;
    return at(pixels, stride, XZ_BROWSE_HEAD_LEFT - 4, 16) == head;
}

int xz_browse_head_hit(int x, int y) {
    if (y < 0 || y > XZ_BROWSE_HEAD_Y + XZ_BROWSE_HEAD_H + 3) return -1;
    for (int i = 0; i < XZ_BROWSE_HEAD_COUNT; i++) {
        struct xz_browse_head_cell cell = xz_browse_head_cell(i);
        if (x >= cell.x && x < cell.x + cell.w) return i;
    }
    return -1;
}

int xz_browse_head_render(uint16_t *pixels, size_t stride, int theme, const struct xz_browse_head *h) {
    if (!h || !xz_browse_head_present(pixels, stride, theme)) return 0;
    struct xz_canvas c = { pixels, stride, 800, 480, 0 };
    uint16_t head = stock(theme, BROWSE_HEAD_BG);
    uint32_t bg = rgb(head), muted = 0x9fb2cf, ink = 0xffffff, accent = 0xa8ceff;
    int y = XZ_BROWSE_HEAD_Y;
    fill565(c, XZ_BROWSE_HEAD_LEFT, y - 1, XZ_BROWSE_HEAD_RIGHT - XZ_BROWSE_HEAD_LEFT + 1, XZ_BROWSE_HEAD_H + 2, head);
    if (h->notice) {
        xz_text(c, XZ_BROWSE_HEAD_LEFT, y + 5, h->notice, 1, 60, 0xff8f86);
        return 1;
    }
    /* The stock shortcut, drawn as a keycap so the MENU button is recognisable. */
    int kx = XZ_BROWSE_HEAD_LEFT;
    xz_text(c, kx, y + 7, xz_l10n_authored("TAP"), 1, 6, muted);
    kx += xz_text_width(c, "TAP", 1, 6) + 4;
    int kw = xz_text_width(c, "MENU", 1, 6) + 8;
    xz_round_rect(c, kx, y + 4, kw, 17, 3, 15, muted);
    xz_round_rect(c, kx + 1, y + 5, kw - 2, 15, 2, 15, xz_blend(bg, 0x000000, 90));
    xz_text(c, kx + 4, y + 7, "MENU", 1, 6, ink);
    static const char *const labels[XZ_BROWSE_HEAD_COUNT] = { "SORT", "COLUMN" };
    for (int i = 0; i < XZ_BROWSE_HEAD_COUNT; i++) {
        struct xz_browse_head_cell cell = xz_browse_head_cell(i);
        int pressed = h->interactive && h->pressed == i;
        uint32_t line = h->interactive ? accent : muted;
        if (pressed) xz_round_rect(c, cell.x, y, cell.w, XZ_BROWSE_HEAD_H, 4, 15, xz_blend(bg, accent, 120));
        else if (h->interactive) {
            xz_round_rect(c, cell.x, y, cell.w, XZ_BROWSE_HEAD_H, 4, 15, line);
            xz_round_rect(c, cell.x + 1, y + 1, cell.w - 2, XZ_BROWSE_HEAD_H - 2, 3, 15, xz_blend(bg, 0x000000, 70));
        }
        /* Left-aligned like a table header, chevron at the right edge of the cell. */
        xz_text(c, cell.x + 9, y + 5, labels[i], 2, 10, pressed ? 0x0b1830 : ink);
        triangle(c, cell.x + cell.w - 20, y + 10, 6, 1, pressed ? 0x0b1830 : line);
    }
    return 1;
}

void xz_key_sync_failure_text(enum xz_key_sync_failure f, int deck, char out[40]) {
    int other = deck ? 1 : 2;
    switch (f) {
    case XZ_KEY_SYNC_NO_ENGINE: snprintf(out, 40, "KEY ENGINE OFF: SET UP KEYS"); break;
    case XZ_KEY_SYNC_NO_KEY: snprintf(out, 40, "DECK %d TRACK HAS NO KEY", deck + 1); break;
    case XZ_KEY_SYNC_NO_MASTER_KEY: snprintf(out, 40, "LOAD A KEYED TRACK ON DECK %d", other); break;
    case XZ_KEY_SYNC_IS_MASTER: snprintf(out, 40, "MAKE DECK %d MASTER FIRST", other); break;
    case XZ_KEY_SYNC_NEED_MASTER: snprintf(out, 40, "MAKE DECK %d MASTER FIRST", other); break;
    case XZ_KEY_SYNC_RANGE: snprintf(out, 40, "SHIFT OUT OF RANGE"); break;
    default: out[0] = 0; break;
    }
}
