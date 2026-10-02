#include "ui_skin.h"
#include <stdio.h>
enum {SIDE=124,X0=148,TOP=40};
static void put(struct xz_ui_widget *a,int x,int y,int w,int h){a->x=x;a->y=y;a->w=w;a->h=h;}
void xz_ui_place(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *w,size_t n){
 (void)u;(void)m;
 for(size_t i=0;i<n;i++){
  struct xz_ui_widget *a=&w[i];int k=a->index;
  switch(a->kind){
  case XZ_UI_PANEL:
   if(k==XZ_UI_STEMS)put(a,X0,176,318,48);else if(k==XZ_UI_XPAD)put(a,474,176,318,48);
   else if(k==XZ_UI_CONTROLS)put(a,0,76,SIDE,80);else if(k==XZ_UI_THEMES)put(a,0,160,SIDE,56);
   else if(k==XZ_UI_EXTRAS)put(a,0,284,SIDE,60);
   else if(k==XZ_UI_GAMES)put(a,X0,u->page==XZ_UI_MYHOUSE_SETUP?368:208,644,64);
   else if(k==XZ_UI_MYHOUSE_SETUP)put(a,474,224,318,64);
   else if(k==XZ_UI_CONNECTION)put(a,X0,u->page==XZ_UI_EXTRAS?110:128,644,u->page==XZ_UI_EXTRAS?54:64);
   else if(k==XZ_UI_DOOM)put(a,X0,u->page==XZ_UI_EXTRAS?183:u->page==XZ_UI_DOOM_SETUP?368:224,644,u->page==XZ_UI_EXTRAS?54:64);
   else if(k==XZ_UI_DOOM_FILES)put(a,u->page==XZ_UI_DOOM?474:X0,u->page==XZ_UI_DOOM?364:u->page==XZ_UI_GAMES?224:288,u->page==XZ_UI_DOOM_SETUP?644:318,64);
   else if(k==XZ_UI_UPDATES)put(a,X0,u->page==XZ_UI_UPDATE_SETUP?368:320,644,64);
   else if(k==XZ_UI_UPDATE_SETUP)put(a,474,364,318,64);
   else if(k==XZ_UI_DOOM_SETUP)put(a,632,410,160,36);
   else put(a,0,220,SIDE,60);
   break;
  case XZ_UI_CLOSE:put(a,720,0,80,40);break;
  case XZ_UI_DECK:put(a,k?518:296,0,k?192:214,TOP);break;
  case XZ_UI_GROOVE_PAD:case XZ_UI_HOTCUE_PAD:put(a,X0+(k%4)*163,216+(k/4)*116,155,108);break;
  case XZ_UI_STRIP:put(a,X0,192,480,128);break;
  case XZ_UI_HOLD:put(a,636,164,156,48);break;
  case XZ_UI_OVERDUB:put(a,636,220,156,48);break;
  case XZ_UI_VOLUME:put(a,636,308,156,44);break;
  case XZ_UI_SAMPLE_PAD:put(a,X0+k*81,360,75,60);break;
  case XZ_UI_ENABLE:
   if(k==XZ_UI_STEM)put(a,X0,100,318,44);
   else put(a,k==XZ_UI_SMART||k==XZ_UI_SAMPLE?474:X0,k==XZ_UI_GATE||k==XZ_UI_SMART?64:120,318,48);
   break;
  case XZ_UI_KEY_SHIFT:if(k<0)put(a,X0,240,92,48);else if(!k)put(a,248,240,118,48);else put(a,374,240,92,48);break;
  case XZ_UI_KEY_SYNC:put(a,474,240,150,48);break;
  case XZ_UI_SHIFT_KEYSYNC:put(a,632,240,160,48);break;
  case XZ_UI_SHIFT_PAGES:put(a,X0,296,318,48);break;
  case XZ_UI_STEMS_OVERLAY:put(a,474,100,318,44);break;
  case XZ_UI_CONTROL_SECTION:put(a,k?474:X0,48,318,40);break;
  case XZ_UI_STEM_PAD_CONFIG:put(a,160+94*(k%4)+20*((k%4)/2),244+96*(k/4),84,84);break;
  case XZ_UI_STEM_BANK:put(a,k?311:X0,176,155,48);break;
  case XZ_UI_PAD_FEEDBACK:put(a,594,244,182,48);break;
  case XZ_UI_STEM_PAGE:put(a,X0+k*163,172,155,44);break;
  case XZ_UI_SET_THEME:put(a,X0+(k%3)*218,48+(k/3)*49,208,44);break;
  case XZ_UI_TAKEOVER_TOGGLE:put(a,500,48,292,48);break;
  case XZ_UI_TAKEOVER_ASSIGN:put(a,k==0?500:k==1?598:704,120,k==1?100:k==0?92:88,44);break;
  case XZ_UI_CONNECTION_ENABLE:put(a,500,180,292,48);break;
  case XZ_UI_DISCOVERY:put(a,500,236,292,48);break;
  case XZ_UI_DOOM_START:put(a,X0,364,318,64);break;
  case XZ_UI_DOOM_CHECK:put(a,X0,288,644,64);break;
  case XZ_UI_WAD_SELECT:put(a,X0,120+(k-m->doom.offset)*46,644,42);break;
  case XZ_UI_WAD_PAGE:put(a,k<0?X0:228,410,76,36);break;
  case XZ_UI_WAD_SCAN:put(a,308,410,160,36);break;
  case XZ_UI_WAD_CLEAR:put(a,476,410,148,36);break;
  case XZ_UI_UPDATE_CHECK:put(a,X0,u->page==XZ_UI_UPDATE_SETUP?288:220,644,64);break;
  case XZ_UI_UPDATE_INSTALL:put(a,X0,296,318,56);break;
  case XZ_UI_UPDATE_ROLLBACK:put(a,474,296,318,56);break;
  case XZ_UI_ARCADE_OPEN:put(a,X0,336,318,64);break;
  case XZ_UI_WAVE_RIDER_OPEN:put(a,474,336,318,64);break;
  case XZ_UI_GAME_SELECT:put(a,k?474:X0,112,318,64);break;
  case XZ_UI_MYHOUSE_CHECK:put(a,X0,288,644,64);break;
  case XZ_UI_WAVE_MODE:put(a,X0+k*218,354,208,44);break;
  default:put(a,X0,400,200,44);break;}
 }
}

static void label(struct xz_canvas c,int x,int y,int width,const char *value,int scale,uint32_t color){
 struct xz_canvas box=xz_sub(c,x,y,width,c.height-y);
 if(xz_text_width(box,value,scale,200)>width)scale=1;
 xz_text(box,0,0,value,scale,200,color);
}
void xz_ui_page_details(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 const struct xz_ui_deck *d=s->d;const struct xz_theme_palette *p=s->p;
 uint32_t ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink,bg=m->theme==XZ_THEME_ANALOG?0xd9c9a3:p->bg;
 uint32_t dim=xz_blend(bg,ink,200);char buf[96];
 if(u->page==XZ_UI_STEMS||u->page==XZ_UI_XPAD){
  label(c,148,50,452,d->track?d->track:"NO TRACK LOADED",2,ink);
  if(d->bpm>0)snprintf(buf,sizeof(buf),"%.1f BPM",(double)d->bpm);else snprintf(buf,sizeof(buf),"---.- BPM");
  label(c,608,50,184,buf,2,ink);
  if(d->wave_peaks&&d->wave_count){
   size_t n=d->wave_count<322?d->wave_count:322;
   for(size_t j=0;j<n;j++){int h=d->wave_peaks[j*d->wave_count/n]*60/255;if(h<2)h=2;xz_rect(c,148+(int)(j*644/n),124-h/2,2,h,p->accent);}
  }else{ xz_rect(c,148,123,644,1,dim);xz_rect(c,356,112,230,24,bg);label(c,362,116,224,"WAVEFORM NOT AVAILABLE",1,dim);}
  if(u->page==XZ_UI_STEMS){
   label(c,148,164,644,"GROOVE PADS",2,ink);
   label(c,148,190,644,"UNASSIGNED PADS KEEP THEIR HOT CUES",1,dim);
  }else{
   snprintf(buf,sizeof(buf),"%s BEATS / %+.1f KEY",xz_ui_lengths[d->loop_index>=0&&d->loop_index<6?d->loop_index:0],(double)d->pitch);
   label(c,148,164,480,buf,1,ink);label(c,636,290,156,"VOLUME",1,dim);
   label(c,148,428,644,"SAMPLE BANK A-H / CLOSING X-PAD STOPS SOUND",1,dim);
  }
 }else if(u->page==XZ_UI_SETTINGS){
  label(c,148,48,644,"DECK SETTINGS + EXPERIMENTAL TOOLS",1,dim);
  label(c,474,300,318,"SCROLLING WAVEFORM",2,ink);
  label(c,474,326,318,"STEMS SHOWS 3-BAND WITHOUT STEMS",1,dim);
  label(c,148,404,644,"STEM SERVER: NOT AVAILABLE",1,dim);
  label(c,148,424,644,"UNAVAILABLE TOOLS ARE MARKED NOT READY",1,dim);
 }else if(u->page==XZ_UI_CONTROLS){
  /* Shared graphical setup draws after each skin. */
 }else if(u->page==XZ_UI_EXTRAS){
  label(c,148,56,644,"EXTRAS",2,ink);
  label(c,148,88,644,"OPTIONAL TOOLS AND GAMES",1,dim);
  label(c,148,169,644,"COMPUTER SCREEN AND FILMSTRIP CONNECTION",1,dim);
  label(c,148,278,644,"CLASSIC GAMES, USB WADS AND BEAT ARCADE",1,dim);
  label(c,148,400,644,"SIGNED MOD UPDATES / ACTIVATE AT NEXT BOOT",1,dim);
 }else if(u->page==XZ_UI_GAMES){
  label(c,148,48,644,"GAMES",2,ink);
  label(c,148,80,644,"NATIVE PLAYBACK / USB GAME FILES",1,dim);
  label(c,148,188,318,"CLASSIC DOOM + DOOM II DATA",1,dim);
  label(c,474,188,318,"ORIGINAL CHEX QUEST + PATCH",1,dim);
  label(c,148,300,318,"CHOOSE A BASE GAME + ADD-ON",1,dim);
  label(c,474,300,318,"NATIVE GZDOOM PORT REQUIRED",1,dim);
 }else if(u->page==XZ_UI_MYHOUSE_SETUP){
  label(c,148,56,644,"MYHOUSE / NATIVE ENGINE SETUP",2,ink);
  label(c,148,104,644,"1  DETECT THE NATIVE GZDOOM ENGINE",1,ink);
  label(c,148,144,644,"2  CHOOSE DOOM II DATA AND MYHOUSE.PK3 ON USB",1,ink);
  label(c,148,184,644,"3  CHECK THE ENGINE, THEN LIVE-VERIFY THE GAME",1,ink);
  label(c,148,224,644,"THE CLASSIC DOOM ENGINE CANNOT LOAD THIS MOD",1,dim);
 }else if(u->page==XZ_UI_DOOM){
  label(c,148,56,644,m->doom.chex?"CHEX QUEST / STANDALONE":"DOOM / STANDALONE",2,ink);
  label(c,148,90,644,m->doom_ready?"USB GAME FILES READY":"USB GAME SETUP NEEDED",1,dim);
  label(c,148,132,644,"JOG: TURN LEFT / RIGHT",2,ink);
  label(c,148,168,644,"PLAY: WALK FORWARD / CUE: SHOOT",2,ink);
  label(c,148,216,644,"HOT CUE A: FORWARD / B: BACK / C: SHOOT",1,ink);
  label(c,148,244,644,"D: USE / E-F: STRAFE / G: ENTER / H: MENU",1,ink);
  label(c,148,294,644,m->doom.selection[0]?m->doom.selection:"BUTTONS CONTROL THE GAME WHILE DOOM IS OPEN",1,dim);
  label(c,148,320,644,"TOUCH EXIT TO RESTORE THE DJ SCREEN / SOUND OFF",1,dim);
 }else if(u->page==XZ_UI_DOOM_SETUP){
  label(c,148,56,644,"USB GAME SETUP",2,ink);
  label(c,148,104,644,"1  DETECT THE DOOM ENGINE FROM THE USB LOADER",1,ink);
  label(c,148,144,644,"2  CHOOSE A USB BASE GAME AND OPTIONAL MAP",1,ink);
  label(c,148,180,644,"   USB: WADS OR VJTOOLS/DOOM",1,dim);
  label(c,148,216,644,"3  CHECK USB, THEN START DOOM TO VERIFY",1,ink);
  label(c,148,250,644,m->doom_ready?"ENGINE AND GAME DATA DETECTED":"PREPARE A DOOM USB WITH THE XZ MODS APP",1,dim);
 }else if(u->page==XZ_UI_DOOM_FILES){
  label(c,148,48,644,"USB WAD LOADER",2,ink);
  label(c,148,78,644,"CHOOSE A BASE GAME, THEN AN OPTIONAL ADD-ON",1,dim);
  label(c,148,98,644,m->doom.selection,1,ink);
  for(int i=0;i<m->doom.count;i++)label(c,164,147+i*46,604,m->doom.details[i],1,dim);
  if(!m->doom.count)label(c,148,180,644,m->doom.scanning?"SEARCHING USB PORTS...":"PUT .WAD FILES ON USB, THEN TAP RESCAN",1,ink);
 }else if(u->page==XZ_UI_UPDATES){
  label(c,148,56,644,"NETWORK UPDATES",2,ink);
  label(c,148,100,644,"DOWNLOAD A SIGNED XZ MOD RUNTIME TO USB",1,ink);
  label(c,148,132,644,"POWER CYCLE TO ACTIVATE / EXISTING MUSIC STAYS",1,dim);
  label(c,148,166,644,m->update.status,1,ink);
  label(c,148,192,644,"MODS ONLY / ORIGINAL USB LOADER IS THE RECOVERY",1,dim);
 }else if(u->page==XZ_UI_UPDATE_SETUP){
  label(c,148,56,644,"NETWORK UPDATE SETUP",2,ink);
  label(c,148,104,644,"1  USE THE UPDATED XZ MODS USB LOADER",1,ink);
  label(c,148,144,644,"2  START NETWORK UPDATES IN THE DESKTOP APP",1,ink);
  label(c,148,184,644,"3  CONNECT THE XZ TO THAT NETWORK",1,ink);
  label(c,148,224,644,"DETECT THE SERVER, THEN DOWNLOAD AN UPDATE",1,dim);
  label(c,148,252,644,m->update.status,1,ink);
 }else if(u->page==XZ_UI_CONNECTION){
  const struct xz_ui_connection *v=&m->connection;
  label(c,148,48,344,"VJ.TOOLS CONNECTION",2,ink);
  label(c,148,72,344,"OPTIONAL COMPUTER LINK",1,dim);
  static const char *labels[]={"STATUS","DEVICE IP","PORT","PROTOCOL","RECEIVED FPS"};
  char port[20];if(v->port)snprintf(port,sizeof(port),"%u",v->port);else snprintf(port,sizeof(port),"NOT REPORTED");
  if(v->ready&&v->connected&&v->stats_valid)snprintf(buf,sizeof(buf),"%.1f HZ",(double)v->frame_hz);else snprintf(buf,sizeof(buf),"NOT REPORTED");
  const char *values[]={!v->ready?"NOT AVAILABLE":v->connected?"CONNECTED":v->enabled?"WAITING FOR VJ.TOOLS":"DISABLED",v->device_ip&&*v->device_ip?v->device_ip:"NOT REPORTED",port,v->protocol&&*v->protocol?v->protocol:"NOT REPORTED",buf};
  for(int k=0;k<5;k++){label(c,148,92+k*38,98,labels[k],1,dim);label(c,262,92+k*38,230,values[k],2,ink);}
  label(c,148,288,344,"STATUS COMES FROM THE RECEIVER",1,dim);
  label(c,148,306,344,m->fb_takeover?"COMPUTER SCREEN ACTIVE":"NATIVE PLAY SCREEN ACTIVE",1,dim);
  label(c,500,104,292,"SCREEN SWITCH BUTTON",1,dim);
  label(c,500,296,292,"OPTIONAL VJ.TOOLS LIBRARY CONNECTION",1,dim);
  label(c,500,314,292,"vj.tools",2,ink);
  label(c,148,424,644,"DJ MODS: CDJ3K-MODS / NSAINTOT + CONTRIBUTORS",1,dim);
 }
}

void xz_ui_stem_setup(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui_model *m=s->m;const struct xz_theme_palette *p=s->p;
 uint32_t ink=m->theme==XZ_THEME_ANALOG?0x3a3020:p->ink;
 label(c,148,156,644,"CHOOSE THE PAD MODE FOR STEM SHORTCUTS",1,ink);
 label(c,160,226,414,"TAP A ROW TO ASSIGN ITS FOUR PADS",1,ink);
 static const char *roles[]={"VOCALS","HARMONICS","DRUMS","BYPASS"};
 static const char *modes[]={"HOT CUE","BEAT LOOP","SLIP LOOP","BEAT JUMP"};
 int conflict=m->stem_page==3&&m->jump.enabled;
 for(size_t i=0;i<s->n;i++)if(s->w[i].kind==XZ_UI_STEM_PAD_CONFIG){
  struct xz_ui_widget a=s->w[i];int pad=a.index,assigned=pad/4==m->stem_bank,k=pad%4;
  uint32_t role=k<3?p->stem[2-k]:p->accent;
  uint32_t edge=assigned?role:xz_blend(p->bg,p->ink,80),fill=xz_blend(p->bg,assigned?role:p->ink,assigned?56:16);
  xz_rect(c,a.x,a.y,a.w,a.h,edge);xz_rect(c,a.x+3,a.y+3,a.w-6,a.h-6,fill);
  char letter[2]={(char)('A'+pad),0};xz_text(c,a.x+9,a.y+8,letter,2,2,p->ink);
  label(c,a.x+7,a.y+39,a.w-14,assigned?roles[k]:"NATIVE",1,p->ink);
  label(c,a.x+7,a.y+62,a.w-14,assigned?(conflict?"OVERRIDDEN":k<3?"MUTE":"BYPASS"):modes[m->stem_page],1,p->ink);
 }
 label(c,604,312,174,"STEMS USE",1,ink);label(c,604,331,174,m->stem_bank?"PADS E-H":"PADS A-D",2,ink);
 if(conflict){label(c,594,365,198,"BEAT JUMP HAS PRIORITY",1,p->alarm);label(c,594,382,198,"CHOOSE ANOTHER MODE",1,ink);}
 else{label(c,594,365,198,"OTHER PADS KEEP",1,ink);label(c,594,382,198,"THEIR NORMAL ACTION",1,ink);}
 if(!(m->enabled&XZ_UI_STEM))label(c,594,405,198,"STEM AUDIO IS OFF",1,p->alarm);
}
