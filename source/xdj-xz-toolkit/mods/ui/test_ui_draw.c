#ifdef NDEBUG
#error Draw acceptance requires active assertions
#endif
#include "ui_skin.h"
#include <assert.h>
#include <stdio.h>

#define W 96
#define H 80
static uint16_t buf[W*H];
static uint16_t at(struct xz_canvas c,int x,int y){return c.p[(size_t)y*c.stride+(size_t)x];}
static void clear(void){for(int i=0;i<W*H;i++)buf[i]=0x1234;}
int main(void){
 struct xz_canvas root={buf,W,W,H,0};
 clear();
 struct xz_canvas s=xz_sub(root,16,16,64,48);
 assert(s.width==64&&s.height==48&&s.p==buf+16*W+16);
 assert(xz_sub(root,-1,0,10,10).width==0&&xz_sub(root,0,H,10,10).height==0);
 struct xz_canvas edge=xz_sub(root,90,70,40,40);assert(edge.width==6&&edge.height==10);
 const int wild[6]={-100000,-40,-1,30,63,100000};
 for(int i=0;i<6;i++)for(int j=0;j<6;j++){
  int x=wild[i],y=wild[j];
  xz_rect(s,x,y,50,50,0xffffff);xz_border(s,x,y,50,50,0xff0000);xz_plot(s,x,y,0x00ff00,128);
  xz_text(s,x,y,"WIDE TEXT",2,20,0xffffff);xz_pixel_text(s,x,y,"PIXEL",3,20,0xffffff);
  xz_round_rect(s,x,y,70,30,99,15,0x0000ff);xz_disc(s,x,y,20,0xffff00);xz_ring(s,x,y,25,4,0x00ffff);
  xz_line16(s,x*16,y*16,(x+80)*16,(y-50)*16,48,0xff00ff);
  xz_frame(s,7,(struct xz_ui_widget){x,y,60,40,XZ_UI_NONE,0,0,"",-1},0x123456,1);
 }
 for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(x<16||x>=80||y<16||y>=64)assert(buf[y*W+x]==0x1234);

 clear();
 xz_round_rect(root,10,10,40,20,100,15,0xffffff);
 assert(at(root,10,10)==0x1234&&at(root,30,20)==0xffff&&at(root,10,20)==0xffff);
 xz_round_rect(root,60,10,20,20,6,1,0xffffff);
 assert(at(root,60,10)==0x1234&&at(root,79,10)==0xffff&&at(root,79,29)==0xffff);
 clear();
 xz_disc(root,40,40,10,0xffffff);
 assert(at(root,40,40)==0xffff&&at(root,40,31)==0xffff&&at(root,40,29)==0x1234&&at(root,52,40)==0x1234);
 clear();
 xz_ring(root,40,40,20,3,0xffffff);
 assert(at(root,40,40)==0x1234&&at(root,40,21)==0xffff&&at(root,40,30)==0x1234);
 clear();
 xz_line16(root,2*16,10*16+8,60*16,10*16+8,48,0xffffff);
 assert(at(root,30,9)==0xffff&&at(root,30,11)==0xffff&&at(root,30,8)==0x1234&&at(root,30,12)==0x1234&&at(root,63,10)==0x1234);
 clear();
 xz_rect(root,0,0,1,1,0);xz_plot(root,0,0,0xffffff,255);assert(at(root,0,0)==0xffff);
 xz_rect(root,0,0,1,1,0);xz_plot(root,0,0,0xffffff,128);
 {unsigned g=(at(root,0,0)>>5)&63;assert(g>=30&&g<=33);}

 assert(xz_sin1024(0)==0&&xz_sin1024(90)==1024&&xz_sin1024(180)==0&&xz_sin1024(270)==-1024);
 assert(xz_sin1024(-90)==-1024&&xz_sin1024(450)==1024&&xz_sin1024(30)==512&&xz_sin1024(-330)==512);
 assert(xz_cos1024(0)==1024&&xz_cos1024(180)==-1024&&xz_cos1024(-90)==0&&xz_cos1024(60)==512);

 struct xz_canvas pixel=root;pixel.theme=7;
 assert(xz_text_width(pixel,"AB",1,10)==12&&xz_text_width(pixel,"AB",2,10)==24&&xz_text_width(pixel,"AB",2,1)==12);
 assert(xz_text_width(root,"",2,10)==0&&xz_text_width(root,"AB",2,0)==0&&xz_text_width(root,NULL,1,5)==0);
 int one=xz_text_width(root,"M",2,40),two=xz_text_width(root,"MM",2,40);
 assert(one>0&&two==2*one&&xz_text_width(root,"MMMMMMMMMM",1,1)<=6);
 puts("PASS draw primitives clip to sub-canvases; AA shapes, lines, trig table and text width");
 return 0;
}
