/* Host-only scripted capture. No device, file-system backend or network runtime is linked. */
#include "browse_fixtures.h"
#include "four_deck_fixtures.h"
#include "shell_settings.h"
#include "shell_source.h"
#include "shell_info.h"
#include "../ui/ui.h"
#include "../ui/menu_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct fx_track track;
static struct bx_library library;
static struct xz_shell sh;
static struct xz_shell_model sm;
static struct xz_shell_browse_model browse;
static struct xz_shell_source_model source;
static struct xz_shell_info_model info;
static struct xz_ui_dj dj;
static struct xz_ui ui;
static struct xz_ui_model um;
static uint16_t pixels[800*480];
static int panel, classic_inline;
static uint16_t surface[536*96];
static char effects[4096];
static int effects_len;
static void effect(const char *host,int kind,int deck,int index,float value){effects_len+=snprintf(effects+effects_len,sizeof effects-(size_t)effects_len,"%s{\"host\":\"%s\",\"kind\":%d,\"deck\":%d,\"index\":%d,\"value\":%.3f}",effects_len?",":"",host,kind,deck,index,(double)value);}
static unsigned now;
static const char *const time_choices[]={"REMAIN","TIME"},*const needle_choices[]={"UNLOCK","LOCK"};
static struct xz_shell_setting_item setting_items[]={{XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"TIME MODE",time_choices,2,0,1,1},{XZ_SHELL_ITEM_NEEDLE_LOCK,XZ_SHELL_SETTING_CAT_DJ_SETTING,"NEEDLE LOCK",needle_choices,2,1,1,1}};
static struct xz_shell_settings_model settings={2,setting_items,NULL};
static const char *output;
static void string(const char *s){putchar('"');for(;s&&*s;s++){if(*s=='"'||*s=='\\')putchar('\\');if((unsigned char)*s>=32)putchar(*s);}putchar('"');}
static void apply_ui(struct xz_ui_action *a,size_t n){
 for(size_t i=0;i<n;i++){struct xz_ui_action v=a[i];int d=v.deck>=0&&v.deck<4?v.deck:ui.deck;effect("MODS",v.kind,v.deck,v.index,v.value);
 switch(v.kind){
 case XZ_UI_CLOSE:panel=0;break;
 case XZ_UI_OPEN_UTILITY:panel=0;{struct xz_shell_action o[XZ_SHELL_ACTIONS];xz_shell_input(&sh,&sm,&(struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=XZ_SHELL_KEY_MENU_HOLD,.deck=-1},o);}break;
 case XZ_UI_SET_INTERFACE:um.ui_interface=(int)v.value;um.theme=um.ui_interface?XZ_THEME_FLAGSHIP:um.classic_theme;break;
 case XZ_UI_SET_THEME:um.classic_theme=(int)v.value;um.theme=um.ui_interface?XZ_THEME_FLAGSHIP:um.classic_theme;break;
 case XZ_UI_ENABLE:if(v.value)um.enabled|=(unsigned)v.index;else um.enabled&=~(unsigned)v.index;break;
 case XZ_UI_MUTE:um.deck[d].muted^=1u<<v.index;break;
 case XZ_UI_BYPASS:um.deck[d].bypass=(int)v.value;break;
 case XZ_UI_KEY_SHIFT:um.deck[d].key_semitones=v.value?um.deck[d].key_semitones+(int)v.value:0;snprintf(um.deck[d].key_readout,48,"Am %+d",um.deck[d].key_semitones);break;
 case XZ_UI_KEY_SYNC:um.deck[d].key_synced=1;break;
 case XZ_UI_STEM_PAGE:um.stem_page=v.index;break;
 case XZ_UI_STEM_BANK:um.stem_bank=v.index;break;
 case XZ_UI_PAD_FEEDBACK:um.pad_feedback=(int)v.value;break;
 case XZ_UI_KEY_FORMAT:um.key_format=(int)v.value;break;
 case XZ_UI_KEY_FEEDBACK:um.key_feedback=(int)v.value;break;
 case XZ_UI_BROWSE_TRAFFIC:um.browse_traffic=(int)v.value;break;
 case XZ_UI_BROWSE_KEY_PREFIX:um.browse_key_prefix=(int)v.value;break;
 case XZ_UI_STEMS_OVERLAY:um.modern_stems=(int)v.value-1;break;
 case XZ_UI_JUMP_SIZE:um.jump.sizes[ui.jump_edit_page*4+ui.jump_edit_pair]=v.index;break;
 case XZ_UI_REKORDBOX_ASSIGN:um.rekordbox_assign[v.index/XZ_UI_RB_CHOICES]=v.index%XZ_UI_RB_CHOICES;break;
 case XZ_UI_JUMP_ENABLE:um.jump.enabled=(int)v.value;break;
 case XZ_UI_JUMP_SHIFT:um.jump.shift_page=(int)v.value;break;
 case XZ_UI_RAM_REQUEST:um.deck[ui.ram_deck].ram.state=XZ_UI_RAM_LOADING;break;
 default:break;
 }
 }
}
static void shell_event(struct xz_shell_event e){
 struct xz_shell_action a[XZ_SHELL_ACTIONS];size_t n=xz_shell_input(&sh,&sm,&e,a);
 for(size_t i=0;i<n;i++){effect("shell",a[i].kind,a[i].deck,a[i].id,(float)a[i].value);switch(a[i].kind){
 case XZ_SHELL_ACT_OPEN_MODS:panel=1;xz_ui_navigate(&ui,a[i].value>=0?(enum xz_ui_page)a[i].value:XZ_UI_HOME);break;
 case XZ_SHELL_ACT_DECK_LAYOUT:sm.layout_preference=a[i].value+1;break;
 case XZ_SHELL_ACT_WAVE_ORDER:sm.layout_preference=a[i].value?3:2;break;
 case XZ_SHELL_ACT_BROWSE_PREVIEW:sm.browse_preview=a[i].value;break;
 case XZ_SHELL_ACT_STEM_MUTE:sm.deck[a[i].deck].stem_muted^=1u<<a[i].id;break;
 case XZ_SHELL_ACT_STEM_BYPASS:sm.deck[a[i].deck].stem_bypass=a[i].value;break;
 case XZ_SHELL_ACT_SET_SETTING:for(int k=0;k<2;k++)if(a[i].id==setting_items[k].id)setting_items[k].current=a[i].value;break;
 case XZ_SHELL_ACT_BROWSE:
 if(a[i].id==XZ_SHELL_BROWSE_REQUEST_WINDOW)bx_window(&browse,&library,a[i].value);
 else if(a[i].id==XZ_SHELL_BROWSE_OPEN){bx_tracks(&browse,&library,xz_shell_screen(&sh));bx_panels(&browse,&library);}
 else if(a[i].id==XZ_SHELL_BROWSE_SORT){browse.sort_column=(enum xz_shell_browse_column)(a[i].value&255);browse.sort_descending=a[i].value>>8;}
 break;
 case XZ_SHELL_ACT_LOAD:if(a[i].id>=1000&&a[i].id<1000+BX_TRACKS){const struct xz_shell_browse_row *r=&library.tracks[a[i].id-1000];struct xz_shell_deck *d=&sm.deck[a[i].deck];d->title=r->title;d->artist=r->artist;d->key=d->key_original=r->key;d->bpm_x100=d->track_bpm_x100=r->bpm_x100;d->total_ms=r->duration_ms;d->track_no=a[i].id-999;d->key_shift=d->key_cents=d->key_synced=0;d->elapsed_ms=0;d->playing=0;d->cue_standby=1;}break;
 default:break;
 }
}
}
static void rect_widget(const char *label,struct xz_shell_rect r,int *n){if(r.w<=0||r.h<=0)return;if((*n)++)putchar(',');printf("{\"label\":");string(label);printf(",\"x\":%d,\"y\":%d}",r.x+r.w/2,r.y+r.h/2);}
static void render(void){
 um.clock_ms=sm.clock_ms=now;
 enum xz_shell_screen screen=xz_shell_screen(&sh);
 if(screen==XZ_SHELL_BROWSE||screen==XZ_SHELL_SEARCH||screen==XZ_SHELL_TAG_LIST||screen==XZ_SHELL_PLAYLIST){if(browse.screen!=screen){if(screen==XZ_SHELL_PLAYLIST)bx_folders(&browse,&library,screen);else bx_tracks(&browse,&library,screen);}bx_panels(&browse,&library);}

 if(classic_inline){for(int i=0;i<800*480;i++)pixels[i]=0xf81f;if(!xz_ui_inline_render(&ui,&um,surface,536*96,536,536,96))exit(4);for(int y=0;y<96;y++)memcpy(pixels+(y+18)*800+131,surface+y*536,536*sizeof(uint16_t));}
 else if(panel){if(!xz_ui_render(&ui,&um,pixels,800*480,800))exit(4);}
 else if(!xz_shell_render_frame(&sh,&sm,pixels,800))exit(4);
 static uint16_t previous[800*480];static int first=1;int changed=first||memcmp(previous,pixels,sizeof pixels);first=0;
 if(changed){static unsigned char rgb[800*480*3];memcpy(previous,pixels,sizeof pixels);for(int i=0;i<800*480;i++){unsigned p=pixels[i];rgb[i*3]=(unsigned char)(((p>>11)&31)*255/31);rgb[i*3+1]=(unsigned char)(((p>>5)&63)*255/63);rgb[i*3+2]=(unsigned char)((p&31)*255/31);}FILE *f=fopen(output,"wb");if(!f)exit(5);fprintf(f,"P6\n800 480\n255\n");if(fwrite(rgb,1,sizeof rgb,f)!=sizeof rgb||fclose(f))exit(5);}
 printf("{\"changed\":%d,\"panel\":%d,\"page\":%d,\"title\":",changed,panel,panel?(int)ui.page:(int)sh.stack[sh.depth-1].screen);string(panel?xz_ui_page_title(ui.page):xz_shell_screen_name(sh.stack[sh.depth-1].screen));printf(",\"widgets\":[");
 if(panel){struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&um,w);for(size_t i=0;i<n;i++){if(i)putchar(',');printf("{\"label\":");string(w[i].label);printf(",\"kind\":%d,\"index\":%d,\"x\":%d,\"y\":%d}",w[i].kind,w[i].index,w[i].x+w[i].w/2,w[i].y+w[i].h/2);}}
 if(!panel){int n=0;if(xz_shell_screen(&sh)==XZ_SHELL_SHORTCUT)rect_widget("NEEDLE LOCK",xz_shell_shortcut_slot(0,3),&n);struct xz_shell_model projection;xz_shell_play_projection(&sh,&sm,&projection);
 rect_widget("DECKS",xz_shell_play_rect(&projection,XZ_SHELL_R_VIEW_TWO,0),&n);
 rect_widget("DRUMS",xz_shell_stem_button(&sh,&projection,0,0),&n);
 rect_widget("BYPASS",xz_shell_bypass_button(&sh,&projection,0),&n);
 if(sh.stack[sh.depth-1].screen==XZ_SHELL_SEARCH){for(int k=0;k<XZ_SHELL_BROWSE_KEYS;k++)rect_widget(xz_shell_browse_key_label(k),xz_shell_browse_rect(&sh,&sm,XZ_SHELL_BR_KEY,k),&n);}
 }
 printf("],\"focus\":");struct xz_ui_widget focus;if(panel&&xz_ui_focused(&ui,&um,&focus))string(focus.label);else string("");printf(",\"effects\":[%s],\"classic_overlay\":%d}\n",effects,classic_inline);fflush(stdout);
}
int main(int argc,char **argv){
 if(argc!=2)return 2;output=argv[1];fx_track_build(&track,7);bx_build(&library);fx_model_four(&sm,&track);xz_shell_init(&sh);xz_ui_init(&ui);bx_tracks(&browse,&library,XZ_SHELL_BROWSE);bx_folders(&browse,&library,XZ_SHELL_PLAYLIST);sm.browse=&browse;bx_tracks(&browse,&library,XZ_SHELL_BROWSE);sm.settings_screen=&settings;sm.settings.writable=(uint64_t)1<<XZ_SHELL_SET_TIME_MODE;
 sm.source=&source;sm.info=&info;sm.layout_preference=1;sm.stem_layout=XZ_SHELL_STEMS_BELOW_FULL;sm.stems_known=sm.stems_on=1;
 for(int d=0;d<4;d++)xz_shell_info_player(&info.deck[d],&sm.deck[d]);
 for(int d=0;d<2;d++){source.device[d]=(struct xz_shell_device){.known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1,.status=XZ_SHELL_DEVICE_READY,.name=d?"FIXTURE USB 2":"FIXTURE USB 1",.tracks=148,.free_bytes=32000000000ULL,.total_bytes=64000000000ULL};}source.current_known=1;source.current=XZ_SHELL_SRC_USB1;
 dj.current=-1;dj.pending=-1;dj.detail=-1;dj.usb_present=3;um.dj=&dj;
 sm.capabilities|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_BYPASS|XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_TAG_LIST;
 um.route.state=XZ_UI_ROUTE_READY;um.modern_look.known=1;um.ui_interface=XZ_UI_INTERFACE_MODERN;um.theme=XZ_THEME_FLAGSHIP;um.classic_theme=0;um.jump=xz_jump_defaults();um.jump.enabled=1;um.enabled=XZ_UI_STEM;um.pad_feedback=1;um.settings_status="FIXTURE: SETTINGS SAVED TO USB 1";um.rekordbox_assign[0]=XZ_UI_RB_INTERFACE;
 for(int d=0;d<4;d++){um.deck[d].ready=0x3fff;um.deck[d].track=sm.deck[d].title;um.deck[d].bpm=128;um.deck[d].status="STEMS READY";um.deck[d].levels[0]=um.deck[d].levels[1]=um.deck[d].levels[2]=1;um.deck[d].ram.state=XZ_UI_RAM_EMPTY;um.deck[d].ram.supported=um.deck[d].ram.source_current=um.deck[d].ram.can_request=1;snprintf(um.deck[d].key_readout,48,"Am");}
 char line[512],cmd[64];int a,b,t;
 while(fgets(line,sizeof line,stdin)){a=b=t=0;if(sscanf(line,"%63s %d %d %d",cmd,&a,&b,&t)<1)continue;now=100000u+(unsigned)t;um.clock_ms=sm.clock_ms=now;effects_len=0;effects[0]=0;struct xz_ui_action o[XZ_UI_ACTIONS];size_t n=0;
 if(!strcmp(cmd,"fixture")){if(a==1){classic_inline=1;um.ui_interface=0;um.theme=um.classic_theme;um.stems_overlay=1;}else if(a==2){um.deck[ui.ram_deck].ram.state=XZ_UI_RAM_READY;um.deck[ui.ram_deck].ram.can_request=0;um.deck[ui.ram_deck].ram.can_release=1;um.deck[ui.ram_deck].ram.bytes=um.deck[ui.ram_deck].ram.copied=16000000;}}
 else if(!strcmp(cmd,"turn")){if(panel)n=xz_ui_encoder_turn(&ui,&um,a,o);else shell_event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_TURN,.delta=a,.deck=-1});}
 else if(!strcmp(cmd,"push")){if(panel)n=xz_ui_encoder_push(&ui,&um,o);else shell_event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_PUSH,.deck=-1});}
 else if(!strcmp(cmd,"down")||!strcmp(cmd,"up")){int down=!strcmp(cmd,"down");if(classic_inline)n=xz_ui_inline_touch(&ui,&um,536,96,a-131,b-18,down,o);else if(panel)n=xz_ui_touch(&ui,&um,a,b,down,o);else shell_event((struct xz_shell_event){.kind=down?XZ_SHELL_TOUCH_DOWN:XZ_SHELL_TOUCH_UP,.x=a,.y=b,.deck=-1});}
 else if(!strcmp(cmd,"back")){if(panel)n=xz_ui_back(&ui,o);else shell_event((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=XZ_SHELL_KEY_BACK,.deck=-1});}
 else if(!strcmp(cmd,"key")){if(panel&&a==XZ_SHELL_KEY_MENU){if(xz_menu_rule(XZ_MENU_AT_MODS)->job==XZ_MENU_JOB_CLOSE_MODS)panel=0;}else shell_event((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=(enum xz_shell_key)a,.deck=a==XZ_SHELL_KEY_LOAD?0:-1});}
 apply_ui(o,n);render();
 }
 return 0;
}
