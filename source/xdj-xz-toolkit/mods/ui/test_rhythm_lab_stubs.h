#ifndef XZ_TEST_RHYTHM_LAB_STUBS_H
#define XZ_TEST_RHYTHM_LAB_STUBS_H
void xz_rhythm_lab_open(uint32_t now){(void)now;}
void xz_rhythm_lab_open_glyph(uint32_t now){(void)now;}
void xz_rhythm_lab_close(void){}
int xz_rhythm_lab_present(void){return 0;}
int xz_rhythm_lab_leave_requested(void){return 0;}
int xz_rhythm_lab_input(int deck,const void *e,uint32_t n){(void)deck;(void)e;(void)n;return 0;}
int xz_rhythm_lab_touch(int x,int y,int d,uint32_t n){(void)x;(void)y;(void)d;(void)n;return 0;}
int xz_rhythm_lab_draw(uint16_t *p,size_t n,size_t s,uint32_t t,struct xz_game_targets *g){(void)p;(void)n;(void)s;(void)t;(void)g;return 0;}
void xz_rhythm_lab_back(uint32_t t){(void)t;}
int xz_rhythm_lab_pad(int d,int p,unsigned *r,int *e){(void)d;(void)p;(void)r;(void)e;return 0;}
#endif
