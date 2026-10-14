#include "ui_skin.h"
#include <stdio.h>

static float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}

/* Flat themes get the modern rounded card; themes with their own chrome keep it. */
static int flat(int theme){return xz_theme(theme)->chrome==XZ_CHROME_FLAT;}
static void card(struct xz_canvas c,int theme,struct xz_ui_widget a,uint32_t fill,uint32_t edge,int selected){
 if(!flat(theme)){xz_frame(c,theme,a,fill,selected);return;}
 xz_round_rect(c,a.x,a.y,a.w,a.h,8,15,edge);
 xz_round_rect(c,a.x+1,a.y+1,a.w-2,a.h-2,7,15,fill);
}
static void theme_card(struct xz_canvas c,const struct xz_ui_widget *a,int selected){
 const struct xz_theme_palette *sample=xz_theme_palette(a->index);
 struct xz_canvas own=c;own.theme=a->index;
 xz_frame(c,a->index,*a,sample->bg,selected);
 /* Swatches: the theme's three stem colours and its accent. */
 for(int k=0;k<4;k++)xz_round_rect(c,a->x+a->w-46+k*10,a->y+8,8,8,2,15,k<3?sample->stem[k]:sample->accent);
 xz_text(own,a->x+10,a->y+8,a->label,1,(a->w-62)/6,sample->ink);
 if(selected)xz_text(own,a->x+10,a->y+24,"SELECTED",1,12,sample->accent);
}
static int is_toggle(enum xz_ui_action_kind k){
 return k==XZ_UI_ENABLE||k==XZ_UI_PAD_FEEDBACK||k==XZ_UI_JUMP_ENABLE||k==XZ_UI_JUMP_SHIFT||k==XZ_UI_SHIFT_PAGES||
  k==XZ_UI_SHIFT_KEYSYNC||k==XZ_UI_TAKEOVER_TOGGLE||k==XZ_UI_KEY_FEEDBACK||k==XZ_UI_RAM_DEBUG||k==XZ_UI_REKORDBOX_KEY||
  k==XZ_UI_CONNECTION_ENABLE||k==XZ_UI_DISCOVERY||k==XZ_UI_HOLD||k==XZ_UI_OVERDUB||k==XZ_UI_METRICS_TOGGLE;
}
/* One of a set: the selected member is tinted with the accent. */
static int is_choice(enum xz_ui_action_kind k){
 return k==XZ_UI_STEM_PAGE||k==XZ_UI_WAVE_MODE||k==XZ_UI_KEY_FORMAT||k==XZ_UI_TAKEOVER_ASSIGN||k==XZ_UI_CONTROL_SECTION||
  k==XZ_UI_JUMP_EDIT_PAGE||k==XZ_UI_RAM_DECK||k==XZ_UI_SDK_STEP||k==XZ_UI_SDK_CHOICE||k==XZ_UI_WAD_SELECT;
}
/* The one thing to do next on a setup or launch page. */
static int is_primary(enum xz_ui_action_kind k,int index){
 if(k==XZ_UI_PC_CONFIRM)return index==1;
 return k==XZ_UI_DOOM_START||k==XZ_UI_UPDATE_CHECK||k==XZ_UI_RAM_REQUEST||k==XZ_UI_PC_CHECK||k==XZ_UI_PC_TEST||
  k==XZ_UI_KEY_CHECK||k==XZ_UI_LOCAL_STEM_SETUP_STEP||k==XZ_UI_METRICS_CHECK||k==XZ_UI_MYHOUSE_CHECK||k==XZ_UI_DOOM_CHECK||k==XZ_UI_FEATURE_SETUP_OPEN||k==XZ_UI_FEATURE_SETUP_STEP||k==XZ_UI_LOCAL_STEM_SETUP_OPEN;
}
/* Launch cards on EXTRAS and GAMES: icon tile, title and a short tagline. */
static const char *tagline(const struct xz_ui_widget *a){
 switch(a->kind){
 case XZ_UI_PANEL:switch(a->index){
  case XZ_UI_CONNECTION:return "COMPUTER SCREEN AND FILMSTRIP LINK";
  case XZ_UI_GAMES:return "CLASSIC GAMES AND BEAT ARCADE";
  case XZ_UI_APPS:return "XZ SDK APPS FROM USB";
  case XZ_UI_UPDATES:return "SIGNED UPDATES, ACTIVE NEXT BOOT";
  case XZ_UI_DOOM_FILES:return "BASE GAME + ADD-ON";
  case XZ_UI_MYHOUSE_SETUP:return "NEEDS THE NATIVE GZDOOM PORT";
  default:return NULL;}
 case XZ_UI_GAME_SELECT:return a->index?"ORIGINAL GAME + PATCH":"DOOM AND DOOM II DATA";
 case XZ_UI_ARCADE_OPEN:return "BEAT-SYNCED, JOG WHEEL";
 case XZ_UI_WAVE_RIDER_OPEN:return "RIDE THE TRACK WAVEFORM";
 case XZ_UI_RHYTHM_LAB_OPEN:return "PLAYED ON THE PADS";
 case XZ_UI_GLYPH_RUN_OPEN:return "RUNNER SET TO THE TRACK";
 default:return NULL;}
}
static enum xz_ui_page card_icon(const struct xz_ui_widget *a){
 return a->kind==XZ_UI_PANEL?(enum xz_ui_page)a->index:XZ_UI_GAMES;
}
static void chevron(struct xz_canvas c,int x,int y,int dir,uint32_t color){
 xz_line16(c,x*16,(y-5)*16,(x+5*dir)*16,y*16,28,color);xz_line16(c,(x+5*dir)*16,y*16,x*16,(y+5)*16,28,color);
}
/* Large type when it fits; narrow cells use the small face throughout so a row reads evenly. */
static void fit_text(struct xz_canvas c,int x,int y,int w,const char *s,uint32_t color){
 if(w>=96&&xz_text_width(c,s,2,100)<=w){xz_text(c,x,y,s,2,100,color);return;}
 xz_text(c,x,y+3,s,1,w/6,color);
}
static const char *section_title(enum xz_ui_page page){
 switch(page){
 case XZ_UI_GENERAL:return "GENERAL";case XZ_UI_FEATURE_SETUP:return "FEATURE SETUP";
 case XZ_UI_CONTROLS:case XZ_UI_BEAT_JUMP:case XZ_UI_PC_STEMS:return "CONTROLS";
 case XZ_UI_THEMES:return "APPEARANCE";case XZ_UI_STEM_COLORS:return "STEM COLORS";
 case XZ_UI_SETTINGS:return "ADVANCED";case XZ_UI_EXTRAS:return "EXTRAS";
 case XZ_UI_STEMS:return "GROOVE PADS";case XZ_UI_XPAD:return "X-PAD";
 case XZ_UI_KEY_SETUP:return "KEY SETUP";case XZ_UI_BROWSE_SETUP:return "KEY FORMAT";
 case XZ_UI_RAM_SETUP:return "RAM TRACKS";case XZ_UI_CONNECTION:return "VJ.TOOLS";
 case XZ_UI_GAMES:case XZ_UI_DOOM:case XZ_UI_DOOM_FILES:case XZ_UI_DOOM_SETUP:case XZ_UI_MYHOUSE_SETUP:return "GAMES";
 case XZ_UI_UPDATES:case XZ_UI_UPDATE_SETUP:return "UPDATES";
 case XZ_UI_APPS:case XZ_UI_APPS_SETUP:return "APPS";
 case XZ_UI_LOCAL_STEM_SETUP:return "USB STEMS";
 case XZ_UI_METRICS_SETUP:return "METRICS SETUP";
 case XZ_UI_PROFILE_SETUP:case XZ_UI_PROFILE_EDIT:return "DJ PROFILE";
 default:return "MODS";}
}
static void render(const struct xz_ui_scene *s){
 struct xz_canvas c=s->c;const struct xz_ui *u=s->u;const struct xz_ui_model *m=s->m;
 const struct xz_ui_deck *d=s->d;const struct xz_theme_palette *p=s->p;char buf[96];size_t i;
 int theme=m->theme,win=theme==9,modern=flat(theme);
 uint32_t ink=p->ink,head_ink=win?0xffffff:ink;
 uint32_t panel=xz_blend(p->bg,ink,22),edge=xz_blend(p->bg,ink,56),dim=xz_blend(p->bg,ink,150),faint=xz_blend(p->bg,ink,10);
 xz_theme_background(xz_surface(c),theme,(struct xz_theme_rect){0,0,800,480});
 /* Header and section rail. */
 xz_theme_frame(xz_surface(c),theme,(struct xz_theme_rect){0,0,800,XZ_UI_HEAD},xz_blend(p->bg,ink,14),0,XZ_THEME_HEADER);
 if(modern){
  xz_rect(c,0,XZ_UI_HEAD,800,1,edge);
  xz_rect(c,0,XZ_UI_HEAD+1,XZ_UI_RAIL_W,XZ_UI_DOCK_Y-XZ_UI_HEAD-1,faint);
  xz_rect(c,XZ_UI_RAIL_W,XZ_UI_HEAD+1,1,XZ_UI_DOCK_Y-XZ_UI_HEAD-1,edge);
 }
 int decks=0;for(i=0;i<s->n;i++)decks|=s->w[i].kind==XZ_UI_DECK&&s->w[i].y<XZ_UI_HEAD;
 xz_text(c,XZ_UI_CX,6,"XZ MODS",1,10,win?0xffffff:dim);
 xz_text(c,XZ_UI_CX,18,section_title(u->page),2,decks?13:30,head_ink);
 /* The DJ Profile chip owns the corner when there is one. */
 if(!decks&&!m->dj)xz_text(c,600,16,"xzmods.com",1,20,win?0xffffff:p->accent);
 for(i=0;i<s->n;i++){
  struct xz_ui_widget a=s->w[i];int available=s->look[i].available,selected=s->look[i].selected;uint32_t color=s->look[i].color;
  uint32_t text=available?ink:dim;
  if(a.kind==XZ_UI_PANEL&&a.x<XZ_UI_RAIL_W){
   /* Section rail: icon over label, accent bar on the open section. */
   if(!modern)xz_frame(c,theme,a,panel,selected);
   else if(selected){
    xz_round_rect(c,a.x+6,a.y,a.w-12,a.h,10,15,xz_blend(p->bg,p->accent,48));
    xz_round_rect(c,a.x,a.y+16,4,a.h-32,2,15,p->accent);
   }
   uint32_t tone=selected?(modern?p->accent:ink):dim;
   xz_ui_chrome_icon(c,(enum xz_ui_page)a.index,a.x+a.w/2,a.y+25,tone);
   int tw=xz_text_width(c,a.label,1,14);xz_text(c,a.x+(a.w-tw)/2,a.y+a.h-17,a.label,1,14,selected?ink:dim);
   continue;
  }
  if(a.kind==XZ_UI_BACK||a.kind==XZ_UI_CLOSE){
   int cy=a.y+a.h/2;
   if(!modern)xz_frame(c,theme,a,panel,0);
   if(a.kind==XZ_UI_BACK){chevron(c,a.x+22,cy,-1,head_ink);xz_text(c,a.x+36,cy-7,"BACK",2,6,head_ink);}
   else{
    if(modern)xz_round_rect(c,a.x+6,a.y+7,a.w-12,a.h-14,(a.h-14)/2,15,xz_blend(p->bg,p->alarm,56));
    int x=a.x+18;
    xz_line16(c,x*16,(cy-4)*16,(x+8)*16,(cy+4)*16,26,head_ink);xz_line16(c,x*16,(cy+4)*16,(x+8)*16,(cy-4)*16,26,head_ink);
    xz_text(c,a.x+33,cy-4,"CLOSE",1,8,head_ink);
   }
   continue;
  }
  if(a.kind==XZ_UI_DECK){
   card(c,theme,a,selected?xz_blend(p->bg,p->accent,56):panel,selected?p->accent:edge,selected);
   snprintf(buf,sizeof(buf),"DECK %d",a.index+1);xz_text(c,a.x+12,a.y+5,buf,2,12,ink);
   xz_text(c,a.x+12,a.y+25,a.label,1,(a.w-24)/6,dim);
   if(selected)xz_disc(c,a.x+a.w-16,a.y+a.h/2,4,p->accent);
   continue;
  }
  if(a.kind==XZ_UI_SET_THEME){theme_card(c,&a,selected);continue;}
  const char *line=tagline(&a);
  if(line&&a.h>=72){
   /* Launch card: icon tile, title, tagline and a chevron at the edge. */
   card(c,theme,a,available?panel:faint,edge,0);
   int big=a.h>=120,tile=big?56:44,tx=a.x+14,ty=big?a.y+16:a.y+(a.h-tile)/2;
   xz_round_rect(c,tx,ty,tile,tile,12,15,xz_blend(p->bg,p->accent,modern?64:110));
   xz_ui_chrome_icon(c,card_icon(&a),tx+tile/2,ty+tile/2,modern?p->accent:ink);
   if(big){fit_text(c,a.x+14,ty+tile+18,a.w-48,a.label,text);xz_text(c,a.x+14,ty+tile+42,line,1,(a.w-48)/6,dim);}
   else{fit_text(c,tx+tile+14,a.y+a.h/2-17,a.w-tile-64,a.label,text);xz_text(c,tx+tile+14,a.y+a.h/2+7,line,1,(a.w-tile-64)/6,dim);}
   chevron(c,a.x+a.w-22,a.y+a.h/2,1,dim);
   continue;
  }
  int primary=is_primary(a.kind,a.index)&&available,shared=a.kind==XZ_UI_STEM_PAD_CONFIG||a.kind==XZ_UI_JUMP_PAIR||a.kind==XZ_UI_STEMS_OVERLAY||a.kind==XZ_UI_STEM_ORDER;
  uint32_t fill=!available?faint:primary?p->accent:is_choice(a.kind)&&selected?xz_blend(p->bg,p->accent,modern?64:120):panel;
  uint32_t border=primary||(selected&&is_choice(a.kind))?p->accent:edge;
  if(!shared)card(c,theme,a,fill,border,available&&selected&&!is_toggle(a.kind));
  if(primary)text=modern?p->bg:ink;
  if(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_GROOVE_PAD)xz_round_rect(c,a.x+8,a.y+4,a.w-16,3,1,15,available?color:dim);
  if(a.kind==XZ_UI_LEVEL||a.kind==XZ_UI_VOLUME){
   float value=a.kind==XZ_UI_LEVEL?d->levels[a.index]:d->sample_volume;
   int position=(int)(clampf(value,0,1)*(float)(a.w-24));
   uint32_t bar=a.kind==XZ_UI_LEVEL?p->stem[a.index]:p->accent;
   if(a.kind==XZ_UI_LEVEL&&(d->muted&(1u<<a.index)))bar=dim;
   xz_round_rect(c,a.x+12,a.y+a.h/2-2,a.w-24,4,2,15,xz_blend(p->bg,ink,50));
   xz_round_rect(c,a.x+12,a.y+a.h/2-2,position,4,2,15,available?bar:dim);
   xz_disc(c,a.x+12+position,a.y+a.h/2,7,available?ink:dim);
   snprintf(buf,sizeof(buf),"%d%%",(int)(clampf(value,0,1)*100+.5f));xz_text(c,a.x+a.w-40,a.y+4,buf,1,6,dim);
  }else if(a.kind==XZ_UI_STRIP){
   for(int k=0;k<6;k++){if(k)xz_rect(c,a.x+k*a.w/6,a.y+6,1,a.h-12,edge);xz_text(c,a.x+k*a.w/6+10,a.y+10,xz_ui_lengths[k],2,6,ink);}
   xz_rect(c,a.x+8,a.y+a.h/2,a.w-16,1,edge);xz_text(c,a.x+10,a.y+34,"+12",1,5,dim);xz_text(c,a.x+10,a.y+a.h-16,"-12",1,5,dim);
   if(d->loop_index>=0&&d->loop_index<6){int py=(int)((12-clampf(d->pitch,-12,12))*(float)(a.h-8)/24);xz_round_rect(c,a.x+d->loop_index*a.w/6+6,a.y+2+py,a.w/6-12,4,2,15,available?p->accent:dim);}
  }else if(a.kind==XZ_UI_KEY_SHIFT&&a.index==0){
   if(available)snprintf(buf,sizeof(buf),"%+d",d->key_semitones);else snprintf(buf,sizeof(buf),"--");
   xz_text(c,a.x+12,a.y+5,buf,2,5,text);
   xz_text(c,a.x+12,a.y+25,"RESET",1,8,dim);
  }else if(!shared){
   int compact=a.w<200,right=is_toggle(a.kind)?(compact?40:54):a.kind==XZ_UI_PANEL||(is_choice(a.kind)&&selected)?28:12;
   int two=a.kind==XZ_UI_GROOVE_PAD||a.kind==XZ_UI_HOTCUE_PAD||a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY||
    a.kind==XZ_UI_SERVER_AUTO||a.kind==XZ_UI_SERVER_ADDRESS||a.kind==XZ_UI_SAMPLE_PAD;
   int ty=two||a.h>=56?a.y+10:a.y+(a.h-14)/2;
   fit_text(c,a.x+12,ty,a.w-12-right,a.label,text);
   if(is_toggle(a.kind)){
    uint32_t track=xz_blend(p->bg,ink,60),knob=modern?0xffffff:ink;
    if(compact){
     /* Compact switch for four-across rows. */
     int sx=a.x+a.w-36,sy=a.y+(a.h-16)/2;uint32_t f=!available?xz_blend(track,knob,30):selected?p->accent:track;
     xz_round_rect(c,sx,sy,28,16,8,15,f);xz_disc(c,selected?sx+20:sx+8,sy+8,5,available?knob:xz_blend(track,knob,120));
    }else xz_ui_chrome_switch(c,a.x+a.w-48,a.y+(a.h-20)/2,selected,available,track,knob,p->accent);
   }
   if(a.kind==XZ_UI_PANEL)chevron(c,a.x+a.w-18,a.y+a.h/2,1,dim);
   if(is_choice(a.kind)&&selected&&a.kind!=XZ_UI_WAD_SELECT)xz_disc(c,a.x+a.w-16,a.y+a.h/2,4,p->accent);
   if(a.kind==XZ_UI_MUTE&&selected){xz_round_rect(c,a.x+a.w-63,a.y+10,55,17,8,15,p->alarm);xz_text(c,a.x+a.w-56,a.y+14,"MUTED",1,9,p->bg);}
   if(a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY)
    xz_text(c,a.x+12,a.y+28,m->connection.ready?"":"UNAVAILABLE",1,20,dim);
   if(a.kind==XZ_UI_GROOVE_PAD)xz_text(c,a.x+12,a.y+a.h-20,available?(d->groove_active==a.index?"PLAYING":"READY"):"NOT READY",1,12,available?color:dim);
   if(a.kind==XZ_UI_HOTCUE_PAD)xz_text(c,a.x+12,a.y+a.h-20,"HOT CUE",1,12,dim);
   if(a.kind==XZ_UI_SAMPLE_PAD&&selected)xz_round_rect(c,a.x+8,a.y+a.h-10,a.w-16,4,2,15,p->accent);
   if(a.kind==XZ_UI_SERVER_AUTO)xz_text(c,a.x+12,a.y+27,m->server_auto?"AUTO":"MANUAL",1,30,p->accent);
   if(a.kind==XZ_UI_SERVER_ADDRESS)xz_text(c,a.x+12,a.y+27,m->server_address&&*m->server_address?m->server_address:"NOT SET",1,58,dim);
  }
  if(!available&&!shared&&a.kind!=XZ_UI_GROOVE_PAD&&!is_toggle(a.kind)&&a.h<56){
   /* Short controls: a status dot; the info dock spells out why. */
   xz_disc(c,a.x+a.w-14,a.y+a.h/2,4,p->alarm);
  }else if(!available&&!shared&&a.kind!=XZ_UI_GROOVE_PAD&&!is_toggle(a.kind)&&a.w>=120){
   const char *tag=a.kind==XZ_UI_CONNECTION_ENABLE||a.kind==XZ_UI_DISCOVERY?"READ ONLY":"NOT READY";
   int tw=xz_text_width(c,tag,1,10)+14;
   xz_round_rect(c,a.x+a.w-tw-8,a.y+a.h-20,tw,14,7,15,xz_blend(p->bg,p->alarm,60));
   xz_text(c,a.x+a.w-tw-1,a.y+a.h-17,tag,1,10,p->alarm);
  }
 }
 xz_ui_page_details(s);
}
static void render_inline(const struct xz_ui_scene *s){
 xz_rect(s->c,0,0,s->c.width,s->c.height,s->p->bg);
 xz_ui_stem_rows(s);
}

void xz_ui_render_badge(uint16_t *pixels,size_t stride){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,0};
 xz_rect(c,744,0,56,24,0x15191e);xz_border(c,744,0,56,24,0xa8ceff);
 xz_text(c,751,5,"MODS",2,4,0xf4f5f6);
}
void xz_ui_render_stems_button(uint16_t *pixels,size_t stride,int enabled){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,0};uint32_t color=enabled?0x52d794:0xa8ceff;
 xz_rect(c,674,0,68,24,enabled?0x123c2a:0x15191e);xz_border(c,674,0,68,24,color);
 xz_text(c,681,5,"STEMS",2,5,color);
}
void xz_ui_render_vj_button(uint16_t *pixels,size_t stride,int takeover_active){
 if(!pixels||stride<800)return;
 struct xz_canvas c={pixels,stride,800,480,0};
 uint32_t border_col=takeover_active?0x52d794:0xa8ceff;
 xz_rect(c,0,0,112,24,0x15191e);xz_border(c,0,0,112,24,border_col);
 xz_text(c,7,5,takeover_active?"EXIT VJ":"VJ.TOOLS",2,8,takeover_active?0x52d794:0xf4f5f6);
}
static void render_buttons(struct xz_canvas c,int stems,int vj_visible,int vj_active){
 if(!c.theme){xz_ui_render_badge(c.p,c.stride);xz_ui_render_stems_button(c.p,c.stride,stems);if(vj_visible)xz_ui_render_vj_button(c.p,c.stride,vj_active);return;}
 const struct xz_theme_palette *p=xz_theme_palette(c.theme);
 struct xz_ui_widget buttons[3]={{744,0,56,24,XZ_UI_NONE,0,0,"MODS",-1},{674,0,68,24,XZ_UI_NONE,0,0,"STEMS",-1},{0,0,112,24,XZ_UI_NONE,0,0,vj_active?"EXIT VJ":"VJ.TOOLS",-1}};
 for(int i=0;i<(vj_visible?3:2);i++){
  struct xz_ui_widget a=buttons[i];xz_frame(c,c.theme,a,p->bg,i==1?stems:i==2?vj_active:0);
  xz_text(c,a.x+6,a.y+7,a.label,1,(a.w-12)/6,p->ink);
 }
}
const struct xz_ui_skin xz_skin_classic={xz_ui_place,render,render_inline,render_buttons};
