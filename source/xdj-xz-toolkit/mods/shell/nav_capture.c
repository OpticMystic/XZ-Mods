/* Host-only scripted capture. No device, file-system backend or network runtime is linked. */
#include "browse_fixtures.h"
#include "four_deck_fixtures.h"
#include "shell_settings.h"
#include "shell_source.h"
#include "shell_info.h"
#include "../ui/ui.h"
#include "../ui/ui_network.h"
#include "../ui/menu_key.h"
#include "../ui/stem_pads.h"
#include "../ui/beat_jump.h"
#include "../deck_select.h"
#include "../ui/key_strip.h"
#include "../ui/deck_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct fx_track track;
static struct bx_library library;
static struct xz_shell_browse_row search_rows[BX_TRACKS];
static int search_total;
static struct xz_shell sh;
static struct xz_shell_model sm;
static struct xz_shell_browse_model browse;
static struct xz_shell_source_model source;
static struct xz_shell_info_model info;
static struct xz_ui_dj dj;
static struct xz_ui ui;
static struct xz_ui_model um;
static uint16_t pixels[800*480];
#include "../extra_decks.h"
#include "../ui/extra_lane.h"
void nav_player_open(const char *);
const struct xz_extra_decks *nav_player_input(int,int);
static int player_active;
static int classic_four;
int nav_classic_detail(void);
void nav_classic_start(struct xz_ui_model *);
void nav_classic_full(const struct xz_shell_model *);
void nav_classic_touch(int,int,int);
void nav_classic_draw(uint16_t *,const uint16_t *,unsigned);
static int panel, classic_inline,classic_picker,classic_down=-9;
static uint16_t stock[800*480];
static uint16_t surface[536*96];
static char effects[4096];
static int effects_len;
static void effect(const char *host,int kind,int deck,int index,float value){effects_len+=snprintf(effects+effects_len,sizeof effects-(size_t)effects_len,"%s{\"host\":\"%s\",\"kind\":%d,\"deck\":%d,\"index\":%d,\"value\":%.3f}",effects_len?",":"",host,kind,deck,index,(double)value);}
void nav_runtime_apply(struct xz_ui_model *,struct xz_ui *,struct xz_ui_action *);
void nav_clock_set(uint32_t);
void nav_runtime_init(void);
int nav_runtime_event(struct xz_ui_model *,int,int,int,int,int);
static unsigned now;
static struct xz_deck_select selection;
static int shifts[2], pages[2], report_result=XZ_SUPPORT_SENT;
static const char *const time_choices[]={"REMAIN","TIME"},*const needle_choices[]={"UNLOCK","LOCK"};
static struct xz_shell_setting_item setting_items[]={{XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_SETTING_CAT_DJ_SETTING,"TIME MODE",time_choices,2,0,1,1},{XZ_SHELL_ITEM_NEEDLE_LOCK,XZ_SHELL_SETTING_CAT_DJ_SETTING,"NEEDLE LOCK",needle_choices,2,1,1,1}};
static struct xz_shell_settings_model settings={2,setting_items,NULL};
static int profile_failure,logo_failure,network_failure,nav_my_failure;
static uint16_t logo_tile[160*160];
static const char logo_images[2][256]={"DJPROFILE/dj test/logo.png","pictures/alternate.jpg"};
static char profile_names[32][641],profile_tags[32][321];
static struct xz_shell_my_settings_model my_settings={.source=XZ_SHELL_SRC_USB1,.source_name="USB 1",.on_stick=1,.writable=1,.stored=&settings};
static const char *output;
#include "nav_adapter_perf.h"
static void string(const char *s){putchar('"');for(;s&&*s;s++){if(*s=='"'||*s=='\\')putchar('\\');if((unsigned char)*s>=32)putchar(*s);}putchar('"');}
#include "nav_adapter_setup.c"
#include "nav_adapter_matrix.h"
static void apply_ui(struct xz_ui_action *a,size_t n){
 for(size_t i=0;i<n;i++){struct xz_ui_action v=a[i];int d=v.deck>=0&&v.deck<4?v.deck:ui.deck;effect("MODS",v.kind,v.deck,v.index,v.value);
 switch(v.kind){
 case XZ_UI_LOCAL_STEM_SETUP_OPEN:xz_ui_navigate(&ui,XZ_UI_LOCAL_STEM_SETUP);ui.deck=v.deck>=0&&v.deck<2?v.deck:0;panel=1;break;
 case XZ_UI_LOCAL_STEM_SETUP_STEP:if(!nav_setup_mode){if(um.deck[ui.deck].usb_reason==XZ_UI_USB_WHY_NONE)panel=0;else if(v.index==1)um.enabled|=XZ_UI_STEM;break;}snprintf(ui.notice,sizeof ui.notice,"%s",xz_ui_usb_next_step(xz_ui_usb_blocked(&um,ui.deck)));break;
 case XZ_UI_ARCADE_OPEN:nav_game_open(1,now);break;
 case XZ_UI_WAVE_RIDER_OPEN:nav_game_open(2,now);break;
 case XZ_UI_SHELL_LOOK:if(v.index==XZ_UI_LOOK_WAVE_COLOR)um.modern_look.wave_color=(int)v.value;else if(v.index==XZ_UI_LOOK_USB_COLORS)um.modern_look.usb_colors=(int)v.value;else if(v.value)um.modern_look.hidden|=1u<<v.index;else um.modern_look.hidden&=~(1u<<v.index);break;
 case XZ_UI_LOGO_PICK:if(v.index>=0){dj.preview=v.index;dj.preview_state=logo_failure?XZ_UI_PREVIEW_FAILED:XZ_UI_PREVIEW_READY;dj.preview_error="THIS LOGO CANNOT BE USED / CHANGE LOGO";dj.preview_tile=logo_tile;}break;
 case XZ_UI_NETWORK_DETECT:um.network.phase=XZ_NET_IDLE;um.network.result=XZ_NET_OK;snprintf(um.network.detail,sizeof um.network.detail,"FIXTURE INVENTORY / NO OS ACCESS");break;
 case XZ_UI_NETWORK_APPLY:{struct xz_net_trial trial;if(!xz_ui_network_take_apply(&ui,&um,&v,&trial))exit(11);}um.network.request_id++;um.network.phase=network_failure?XZ_NET_FAILED:XZ_NET_TRIAL_READY;um.network.rollback_available=1;snprintf(um.network.detail,sizeof um.network.detail,"%s",network_failure?"FIXTURE VERIFY FAILED / REVERT AVAILABLE":"FIXTURE LOCAL VERIFY OK");break;
 case XZ_UI_NETWORK_REVERT:case XZ_UI_NETWORK_CANCEL:um.network.phase=XZ_NET_IDLE;um.network.rollback_available=0;snprintf(um.network.detail,sizeof um.network.detail,"FIXTURE RESTORED");break;
 case XZ_UI_PROFILE_SAVE:{if(profile_failure){dj.status="FIXTURE: PROFILE SAVE FAILED / TRY AGAIN";dj.flash=1;break;}int at=ui.edit_entry>=0?ui.edit_entry:dj.count++;if(at>=32)exit(8);snprintf(profile_names[at],641,"%s",ui.edit_name);snprintf(profile_tags[at],321,"%s",ui.edit_tagline);for(int k=0;k<dj.count;k++)dj.entry[k].stick_active=0;dj.entry[at]=(struct xz_ui_dj_entry){.name=profile_names[at],.tagline=profile_tags[at],.dj_id="fixture",.usb=v.index,.stick_active=1};dj.current=at;dj.detail=at;if(ui.edit_logo!=XZ_UI_LOGO_KEEP)dj.detail_tile=ui.edit_logo>=0?logo_tile:NULL;dj.status="FIXTURE: PROFILE SAVED";break;}
 case XZ_UI_PROFILE_PICK:for(int k=0;k<dj.count;k++)dj.entry[k].stick_active=k==v.index;dj.current=v.index;dj.status="FIXTURE: PROFILE ACTIVE";break;
 case XZ_UI_SUPPORT_SEND:case XZ_UI_SUPPORT_RETRY:
 um.support.phase=(enum xz_support_phase)report_result;
 snprintf(um.support.reference,sizeof um.support.reference,"R-TEST-0001");
 snprintf(um.support.result,sizeof um.support.result,"%s",report_result==XZ_SUPPORT_SAVED?"OFFLINE":report_result==XZ_SUPPORT_UNSAVED?"FULL":"OK");
 snprintf(um.support.detail,sizeof um.support.detail,"OFFLINE");break;
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
 nav_setup_apply(&v);
 nav_runtime_apply(&um,&ui,&v);nav_perf_ui(&v);
 }
}
static void search_fixture(void){
 const char *query=xz_shell_browse_query();search_total=0;
 for(int i=0;i<BX_TRACKS;i++){const char *title=library.tracks[i].title;int found=0;
  for(int j=0;title[j]&&!found;j++){int k=0;while(query[k]&&title[j+k]&&((query[k]|32)==(title[j+k]|32)))k++;if(!query[k])found=1;}
  if(found)search_rows[search_total++]=library.tracks[i];
 }
 browse.rows=search_rows;browse.window_start=0;browse.window_count=search_total<XZ_SHELL_BROWSE_WINDOW?search_total:XZ_SHELL_BROWSE_WINDOW;browse.total=search_total;
}
static void shell_event(struct xz_shell_event e){
 struct xz_shell_action a[XZ_SHELL_ACTIONS];size_t n=xz_shell_input(&sh,&sm,&e,a);
 for(size_t i=0;i<n;i++){effect("shell",a[i].kind,a[i].deck,a[i].id,(float)a[i].value);nav_perf_shell(&a[i]);switch(a[i].kind){
 case XZ_SHELL_ACT_OPEN_MODS:panel=1;xz_ui_navigate(&ui,a[i].value>=0?(enum xz_ui_page)a[i].value:XZ_UI_HOME);break;
 case XZ_SHELL_ACT_DECK_LAYOUT:sm.layout_preference=a[i].value+1;break;
 case XZ_SHELL_ACT_WAVE_ORDER:sm.layout_preference=a[i].value?3:2;break;
 case XZ_SHELL_ACT_BROWSE_PREVIEW:sm.browse_preview=a[i].value;break;
 case XZ_SHELL_ACT_STEM_MUTE:sm.deck[a[i].deck].stem_muted^=1u<<a[i].id;break;
 case XZ_SHELL_ACT_STEM_BYPASS:sm.deck[a[i].deck].stem_bypass=a[i].value;break;
 case XZ_SHELL_ACT_MY_SETTINGS:if(nav_my_failure){my_settings.result="FIXTURE: LOAD FAILED / CHECK USB";my_settings.state=XZ_SHELL_MY_FAILED;break;}my_settings.result=a[i].id==XZ_SHELL_MY_SAVE?"FIXTURE: SAVED TO USB 1":"FIXTURE: LOADED USB 1 SETTINGS";my_settings.state=XZ_SHELL_MY_DONE;sm.notice=my_settings.result;break;
 case XZ_SHELL_ACT_SET_SETTING:if(a[i].id==XZ_SHELL_SET_QUANTIZE)sm.deck[a[i].deck].quantize_on=a[i].value;for(int k=0;k<2;k++)if(a[i].id==setting_items[k].id)setting_items[k].current=a[i].value;break;
 case XZ_SHELL_ACT_BROWSE:
 if(a[i].id==XZ_SHELL_BROWSE_REQUEST_WINDOW){if(xz_shell_screen(&sh)==XZ_SHELL_SEARCH){int start=a[i].value;if(start<0)start=0;if(start>search_total)start=search_total;browse.window_start=start;browse.window_count=search_total-start<XZ_SHELL_BROWSE_WINDOW?search_total-start:XZ_SHELL_BROWSE_WINDOW;browse.rows=search_rows+start;}else bx_window(&browse,&library,a[i].value);}
 else if(a[i].id==XZ_SHELL_BROWSE_SEARCH_QUERY)search_fixture();
 else if(a[i].id==XZ_SHELL_BROWSE_CATEGORY){int cat=a[i].value;if(cat==XZ_SHELL_CAT_PLAYLIST||cat==XZ_SHELL_CAT_FOLDER)bx_folders(&browse,&library,xz_shell_screen(&sh));else bx_tracks(&browse,&library,xz_shell_screen(&sh));browse.category=(enum xz_shell_browse_category)cat;}
 else if(a[i].id==XZ_SHELL_BROWSE_OPEN){bx_tracks(&browse,&library,xz_shell_screen(&sh));bx_panels(&browse,&library);}
 else if(a[i].id==XZ_SHELL_BROWSE_SET_COLUMNS){browse.column_count=0;for(int k=0;k<XZ_SHELL_COL_COUNT&&browse.column_count<XZ_SHELL_BROWSE_COLUMNS;k++)if((a[i].value>>k)&1)browse.columns[browse.column_count++]=(enum xz_shell_browse_column)k;}
 else if(a[i].id==XZ_SHELL_BROWSE_SORT){browse.sort_column=(enum xz_shell_browse_column)(a[i].value&255);browse.sort_descending=a[i].value>>8;}
 break;
 case XZ_SHELL_ACT_PLAYLIST:sm.notice="FIXTURE: PLAYLIST EDIT RECEIVED";break;
 case XZ_SHELL_ACT_TAG_LIST:if(a[i].value>=1000&&a[i].value<1000+BX_TRACKS)library.tracks[a[i].value-1000].tagged=a[i].id==XZ_SHELL_TAG_ADD;sm.notice="FIXTURE: TAG LIST UPDATED";break;
 case XZ_SHELL_ACT_SET_RATING:if(a[i].deck>=0)info.deck[a[i].deck].rating=a[i].value;if(a[i].id>=1000&&a[i].id<1000+BX_TRACKS)library.tracks[a[i].id-1000].rating=a[i].value;break;
 case XZ_SHELL_ACT_LOAD_CUE:sm.notice="FIXTURE: MEMORY POINT LOAD REQUESTED";break;
 case XZ_SHELL_ACT_LOAD:if(a[i].id>=1000&&a[i].id<1000+BX_TRACKS){const struct xz_shell_browse_row *r=&library.tracks[a[i].id-1000];struct xz_shell_deck *d=&sm.deck[a[i].deck];d->title=r->title;d->artist=r->artist;d->key=d->key_original=r->key;d->bpm_x100=d->track_bpm_x100=r->bpm_x100;d->total_ms=r->duration_ms;d->track_no=a[i].id-999;d->key_shift=d->key_cents=d->key_synced=0;d->elapsed_ms=0;d->playing=0;d->cue_standby=1;}break;
 default:break;
 }
}
}
static int setting_action(int kind){switch(kind){
 case XZ_UI_STEM_ORDER:case XZ_UI_STEM_COLOR:case XZ_UI_STEM_COLORS_RESET:case XZ_UI_BAND_COLOR:case XZ_UI_BAND_COLORS_RESET:
 case XZ_UI_PAD_FEEDBACK:case XZ_UI_KEY_FORMAT:case XZ_UI_KEY_FEEDBACK:case XZ_UI_BROWSE_TRAFFIC:case XZ_UI_BROWSE_KEY_PREFIX:
 case XZ_UI_SET_THEME:case XZ_UI_SET_MODERN_THEME:case XZ_UI_SHELL_LOOK:case XZ_UI_SHELL_DISPLAY:case XZ_UI_SHELL_BROWSE_COLUMNS:case XZ_UI_WAVE_MODE:case XZ_UI_STEMS_OVERLAY:case XZ_UI_BROWSE_PREVIEW:case XZ_UI_REKORDBOX_ASSIGN:case XZ_UI_STEM_BANK:case XZ_UI_STEM_PAGE:case XZ_UI_ENABLE:case XZ_UI_DECK_VIEW:return 1;
 default:return 0;}}
static void rect_widget(const char *label,struct xz_shell_rect r,int *n){if(r.w<=0||r.h<=0)return;if((*n)++)putchar(',');printf("{\"label\":");string(label);printf(",\"x\":%d,\"y\":%d}",r.x+r.w/2,r.y+r.h/2);}
size_t nav_browse_widgets(const struct xz_shell *,const struct xz_shell_model *,struct xz_ui_widget *);
static void render(void){
 um.clock_ms=sm.clock_ms=now;
 enum xz_shell_screen screen=xz_shell_screen(&sh);
 if(screen==XZ_SHELL_BROWSE||screen==XZ_SHELL_SEARCH||screen==XZ_SHELL_TAG_LIST||screen==XZ_SHELL_PLAYLIST){if(browse.screen!=screen){if(screen==XZ_SHELL_PLAYLIST)bx_folders(&browse,&library,screen);else bx_tracks(&browse,&library,screen);}bx_panels(&browse,&library);browse.edit=browse.rows==library.folders?XZ_SHELL_EDIT_PLAYLISTS:XZ_SHELL_EDIT_TRACKS;browse.edit_playlist=1;static const struct xz_shell_cue_entry cues[]={{.ms=16000,.comment="INTRO CUE"},{.loop=1,.ms=32000,.end_ms=40000,.comment="EIGHT BEAT LOOP"}};const struct xz_shell_browse_row *r=xz_shell_browse_highlight(&sh,&sm);browse.cue_track=r?r->id:0;browse.cues=cues;browse.cue_count=2;}

 if(nav_game_active)nav_game_render(pixels,now);
 else if(classic_four){nav_classic_draw(pixels,stock,now);}
 else if(classic_inline){memcpy(pixels,stock,sizeof pixels);if(!xz_ui_inline_render(&ui,&um,surface,536*96,536,536,96))exit(4);for(int y=0;y<96;y++)memcpy(pixels+(y+18)*800+131,surface+y*536,536*sizeof(uint16_t));
 struct xz_canvas canvas={pixels,800,800,480,um.theme};
 xz_deck_view_button_draw(canvas,&xz_deck_view_classic,um.deck_view,1,sm.sides_extra,classic_picker,0);
 if(classic_picker)xz_deck_view_picker_draw(canvas,&xz_deck_view_classic,um.deck_view,1,-1);
 for(int d=0;d<2;d++){struct xz_key_strip_deck key={.valid=1,.moved=um.deck[d].key_semitones!=0,.synced=um.deck[d].key_synced,.sync_master=1-d};snprintf(key.original,8,"Am");snprintf(key.effective,8,"%s",um.deck[d].key_semitones?"Bm":"Am");xz_key_strip_render_deck(pixels,800,um.theme,d,&key,1);}
 }
 else if(panel){if(!xz_ui_render(&ui,&um,pixels,800*480,800))exit(4);}
 else if(!xz_shell_render_frame(&sh,&sm,pixels,800))exit(4);
 static uint16_t previous[800*480];static int first=1;int changed=first||memcmp(previous,pixels,sizeof pixels);first=0;
 if(changed){static unsigned char rgb[800*480*3];memcpy(previous,pixels,sizeof pixels);for(int i=0;i<800*480;i++){unsigned p=pixels[i];rgb[i*3]=(unsigned char)(((p>>11)&31)*255/31);rgb[i*3+1]=(unsigned char)(((p>>5)&63)*255/63);rgb[i*3+2]=(unsigned char)((p&31)*255/31);}FILE *f=fopen(output,"wb");if(!f)exit(5);fprintf(f,"P6\n800 480\n255\n");if(fwrite(rgb,1,sizeof rgb,f)!=sizeof rgb||fclose(f))exit(5);}
 printf("{\"changed\":%d,\"panel\":%d,\"page\":%d,\"title\":",changed,panel,panel?(int)ui.page:(int)sh.stack[sh.depth-1].screen);string(nav_game_active==1?"BEAT ARCADE":nav_game_active==2?"WAVE RIDER":panel?xz_ui_page_title(ui.page):xz_shell_screen_name(sh.stack[sh.depth-1].screen));printf(",\"widgets\":[");
 if(panel&&!nav_game_active){struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&ui,&um,w);for(size_t i=0;i<n;i++){if(i)putchar(',');printf("{\"label\":");string(w[i].label);printf(",\"kind\":%d,\"index\":%d,\"setting\":%d,\"keyboard\":%d,\"x\":%d,\"y\":%d}",w[i].kind,w[i].index,setting_action(w[i].kind),w[i].kind==XZ_UI_SUPPORT_KEY||w[i].kind==XZ_UI_KEYBOARD,w[i].x+w[i].w/2,w[i].y+w[i].h/2);}}
 if(!panel){int n=0;nav_perf_widgets(&n);struct xz_ui_widget bw[XZ_UI_WIDGETS];size_t bn=nav_browse_widgets(&sh,&sm,bw);for(size_t i=0;i<bn;i++)rect_widget(bw[i].label,(struct xz_shell_rect){bw[i].x,bw[i].y,bw[i].w,bw[i].h},&n);if(xz_shell_screen(&sh)==XZ_SHELL_SHORTCUT)rect_widget("NEEDLE LOCK",xz_shell_shortcut_slot(0,3),&n);struct xz_shell_model projection;xz_shell_play_projection(&sh,&sm,&projection);
 if(sm.deck[0].alert.kind!=XZ_SHELL_ALERT_NONE)rect_widget("CHOOSE TRACK",xz_shell_play_rect(&projection,XZ_SHELL_R_ALERT_BUTTON,0),&n);
 rect_widget("DECKS",xz_shell_play_rect(&projection,XZ_SHELL_R_VIEW_TWO,0),&n);
 rect_widget("DRUMS",xz_shell_stem_button(&sh,&projection,0,0),&n);
 rect_widget("BYPASS",xz_shell_bypass_button(&sh,&projection,0),&n);
 if(sh.stack[sh.depth-1].screen==XZ_SHELL_SEARCH){for(int k=0;k<XZ_SHELL_BROWSE_KEYS;k++)rect_widget(xz_shell_browse_key_label(k),xz_shell_browse_rect(&sh,&sm,XZ_SHELL_BR_KEY,k),&n);}
 }
 printf("],\"game\":%d,\"game_phase\":%d,\"game_step\":%u,\"focus\":",nav_game_active,nav_game_active==1?(int)nav_arcade.phase:(int)nav_rider.phase,nav_rider.setup_step);struct xz_ui_widget focus;if(panel&&xz_ui_focused(&ui,&um,&focus))string(focus.label);else string("");printf(",\"observations\":{\"classic_detail\":%d,\"view\":%d,\"four_stems_hidden\":%d,\"ram_only\":%d,\"loaded3\":%d,\"software_loaded\":%d,\"sync3\":%d,\"sync4\":%d},\"effects\":[%s],\"classic_overlay\":%d}\n",classic_four?nav_classic_detail():-1,sm.layout_preference,sm.four_stems_hidden,sm.deck[0].ram_only,sm.deck[2].track_no,player_active,sm.deck[2].sync,sm.deck[3].sync,effects,classic_inline);fflush(stdout);
}
int main(int argc,char **argv){
 if(argc!=3)return 2;FILE *bg=fopen(argv[2],"rb");if(!bg||fread(stock,2,800*480,bg)!=800*480)return 9;fclose(bg);output=argv[1];fx_track_build(&track,7);bx_build(&library);fx_model_four(&sm,&track);xz_shell_init(&sh);xz_ui_init(&ui);nav_runtime_init();xz_deck_select_init(&selection);xz_deck_select_route(&selection,1);bx_tracks(&browse,&library,XZ_SHELL_BROWSE);bx_folders(&browse,&library,XZ_SHELL_PLAYLIST);sm.browse=&browse;bx_tracks(&browse,&library,XZ_SHELL_BROWSE);sm.my_settings=&my_settings;sm.settings_screen=&settings;sm.settings.writable=((uint64_t)1<<XZ_SHELL_SET_TIME_MODE)|((uint64_t)1<<XZ_SHELL_SET_QUANTIZE);sm.capabilities|=XZ_SHELL_CAP_SET_SETTING;
 sm.source=&source;sm.info=&info;sm.layout_preference=1;sm.stem_layout=XZ_SHELL_STEMS_BELOW_FULL;sm.stems_known=sm.stems_on=1;
 for(int d=0;d<4;d++)xz_shell_info_player(&info.deck[d],&sm.deck[d]);
 for(int d=0;d<4;d++){info.deck[d].known|=((uint64_t)1<<XZ_SHELL_I_RATING)|((uint64_t)1<<XZ_SHELL_I_TRACK_ID);info.deck[d].track_id=1000;info.deck[d].rating_writable=1;}
 for(int d=0;d<2;d++){source.device[d]=(struct xz_shell_device){.known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1,.status=XZ_SHELL_DEVICE_READY,.name=d?"FIXTURE USB 2":"FIXTURE USB 1",.tracks=148,.free_bytes=32000000000ULL,.total_bytes=64000000000ULL};}source.current_known=1;source.current=XZ_SHELL_SRC_USB1;
 dj.current=-1;dj.pending=-1;dj.detail=-1;dj.usb_present=3;um.dj=&dj;
 sm.capabilities|=XZ_SHELL_CAP_PLAYLIST_EDIT|XZ_SHELL_CAP_TAG_EDIT|XZ_SHELL_CAP_CUE_LIST|XZ_SHELL_CAP_HISTORY_EDIT;
 sm.capabilities|=XZ_SHELL_CAP_DECK_SELECT|XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_BYPASS|XZ_SHELL_CAP_SOURCE|XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_TAG_LIST;
 um.support.ready=um.support.usb_writable=um.support.clock_ready=1;um.route.state=XZ_UI_ROUTE_READY;um.modern_look.known=1;um.ui_interface=XZ_UI_INTERFACE_MODERN;um.theme=XZ_THEME_FLAGSHIP;um.classic_theme=0;um.jump=xz_jump_defaults();um.jump.enabled=1;um.enabled=XZ_UI_STEM;um.pad_feedback=1;um.settings_status="FIXTURE: SETTINGS SAVED TO USB 1";um.rekordbox_assign[0]=XZ_UI_RB_INTERFACE;
 for(int d=0;d<4;d++){um.deck[d].ready=0x3fff;um.deck[d].track=sm.deck[d].title;um.deck[d].bpm=128;um.deck[d].status="STEMS READY";um.deck[d].levels[0]=um.deck[d].levels[1]=um.deck[d].levels[2]=1;um.deck[d].ram.state=XZ_UI_RAM_EMPTY;um.deck[d].ram.supported=um.deck[d].ram.source_current=um.deck[d].ram.can_request=1;snprintf(um.deck[d].key_readout,48,"Am");}
 char line[512],cmd[64];int a,b,t;
 while(fgets(line,sizeof line,stdin)){a=b=t=0;if(sscanf(line,"%63s %d %d %d",cmd,&a,&b,&t)<1)continue;now=100000u+(unsigned)t;nav_clock_set(now);um.clock_ms=sm.clock_ms=now;effects_len=0;effects[0]=0;struct xz_ui_action o[XZ_UI_ACTIONS];size_t n=0;
 if(nav_game_active&&strcmp(cmd,"fixture"))nav_game_input(cmd,a,b,now);
 else if(!strcmp(cmd,"fixture")){if(a==260||a==261){classic_four=1;nav_classic_start(&um);if(a==261)nav_classic_full(&sm);}else if(nav_matrix_fixture(a)||nav_setup_fixture(a)||nav_perf_fixture(a)){}else if(a==37){um.counter_error[0].code="E-8304";um.counter_error[0].text="USB ACCESS ERROR";}else if(a==30){dj.images_usb=0;dj.image_count=2;dj.images=logo_images;for(int i=0;i<160*160;i++)logo_tile[i]=((i/160/20+i%160/20)&1)?0x07ff:0xf81f;}else if(a==31){logo_failure=1;}else if(a==32){profile_failure=1;}else if(a==33){profile_failure=logo_failure=0;}else if(a==34){dj.usb_present=0;}else if(a==35){dj.usb_present=3;}else if(a==36){network_failure=1;}else if(a==22){for(int d=0;d<4;d++)sm.deck[d].playing=0;}else if(a==20){um.network.actual=(struct xz_net_snapshot){.generation=1,.observed_ms=now,.count=1};struct xz_net_adapter *ad=&um.network.actual.adapter[0];snprintf(ad->id.name,16,"fixture0");ad->id.ifindex=7;ad->id.identity_epoch=1;ad->id.mac_known=XZ_NET_YES;ad->admin_up=ad->link_up=ad->addresses_known=ad->gateway_known=XZ_NET_YES;ad->caps=63;ad->dhcp_enabled=XZ_NET_NO;ad->network_owner=XZ_NET_OWNER_MODS;ad->state_revision=1;um.network.phase=XZ_NET_IDLE;}else if(a==21){um.deck[ui.ram_deck].ram.state=XZ_UI_RAM_ERROR;snprintf(um.deck[ui.ram_deck].ram.message,sizeof um.deck[ui.ram_deck].ram.message,"FIXTURE COPY ERROR");}else if(a>=100&&a<124){classic_inline=1;um.ui_interface=0;um.theme=um.classic_theme=a-100;um.stems_overlay=1;}else if(a>=10&&a<=12){report_result=a==10?XZ_SUPPORT_SENT:a==11?XZ_SUPPORT_SAVED:XZ_SUPPORT_UNSAVED;}else if(a==1){classic_inline=1;um.ui_interface=0;um.theme=um.classic_theme;um.stems_overlay=1;}else if(a==2){um.deck[ui.ram_deck].ram.state=XZ_UI_RAM_READY;um.deck[ui.ram_deck].ram.can_request=0;um.deck[ui.ram_deck].ram.can_release=1;um.deck[ui.ram_deck].ram.bytes=um.deck[ui.ram_deck].ram.copied=16000000;}}
 else if(!strcmp(cmd,"player")){
 if(!player_active){char path[2048];snprintf(path,sizeof path,"%s",output);char *slash=strrchr(path,'/');if(!slash)return 11;snprintf(slash+1,(size_t)(path+sizeof path-slash-1),"player-fixture");nav_player_open(path);player_active=1;}
 const struct xz_extra_decks *v=nav_player_input(a,b);
 for(int d=0;d<2;d++){const struct xz_extra_deck *in=&v->deck[d];struct xz_shell_deck *out=&sm.deck[d+2];out->title=in->title;out->artist=in->artist;out->bpm_x100=in->bpm_x100;out->playing=in->playing;out->elapsed_ms=in->elapsed_ms;out->total_ms=in->total_ms;out->quantize_on=in->quantize;out->master=in->master;struct xz_extra_sync_presentation sp=xz_extra_sync_present(in);out->sync=in->sync.requested;out->sync_status=sp.status;out->sync_master_deck=in->sync.master_deck;}
 effect("software-player",a,b+2,0,1);
 }
 else if(!strcmp(cmd,"physical")){
 int release=a>=100,code=a%100,side=b;
 if(side<0||side>1)return 6;
 if(code==0){struct xz_ds_outcome result;shifts[side]=!release;xz_deck_select_shift(&selection,side,!release,now,&result);if(result.toggled){if(selection.side[side].target==XZ_DS_EXTRA)sm.sides_extra|=1u<<side;else sm.sides_extra&=~(1u<<side);effect("deck-select",1,selection.side[side].target==XZ_DS_EXTRA?side+2:side,side,1);}}
 else {struct xz_cue_event e={.deck=side,.pad=-1,.mode_button=-1,.operation=release?2:0,.shift=shifts[side],.pad_page=pages[side]};
 if(code>=1&&code<=4){e.mode_button=code-1;if(!release&&!shifts[side])pages[side]=code-1;e.pad_page=pages[side];}
 else if(code>=10&&code<=17)e.pad=code-10;
 unsigned old_mute=um.deck[side].muted;int old_bypass=um.deck[side].bypass,old_interface=um.ui_interface;
 nav_perf_pad(code,side,release);
 int beats=nav_runtime_event(&um,code,side,release,shifts[side],pages[side]);
 if(*nav_runtime_notice())sm.notice=nav_runtime_notice();
 if(beats){effect("runtime-beat-jump",1,side,e.pad,(float)beats);if(!*nav_runtime_notice())sm.deck[side].elapsed_ms+=beats*60000/128;}
 sm.deck[side].key_synced=um.deck[side].key_synced;sm.deck[side].key_shift=um.deck[side].key_semitones;sm.deck[side].stem_muted=um.deck[side].muted;sm.deck[side].stem_bypass=um.deck[side].bypass;
 if(old_mute!=um.deck[side].muted||old_bypass!=um.deck[side].bypass)effect("runtime-pad",code,side,e.pad,1);
 if(old_interface!=um.ui_interface){classic_inline=!um.ui_interface;effect("rekordbox",1,side,0,(float)um.ui_interface);}
 if(code==31&&!release)effect("runtime-key-sync",1,side,0,(float)um.deck[side].key_synced);

 }
 }
 else if(!strcmp(cmd,"turn")){if(panel)n=xz_ui_encoder_turn(&ui,&um,a,o);else shell_event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_TURN,.delta=a,.deck=-1});}
 else if(!strcmp(cmd,"push")){if(panel)n=xz_ui_encoder_push(&ui,&um,o);else shell_event((struct xz_shell_event){.kind=XZ_SHELL_ENCODER_PUSH,.deck=-1});}
 else if(!strcmp(cmd,"down")||!strcmp(cmd,"up")){int down=!strcmp(cmd,"down");if(classic_four){nav_classic_touch(a,b,down);}else if(classic_inline&&(classic_picker||xz_deck_view_button_hit(&xz_deck_view_classic,a,b))){
 if(down)classic_down=classic_picker?xz_deck_view_picker_hit(&xz_deck_view_classic,a,b):3;
 else{int hit=classic_picker?xz_deck_view_picker_hit(&xz_deck_view_classic,a,b):3;if(hit==classic_down){if(hit==3)classic_picker=1;else{if(hit>=0)um.deck_view=hit;classic_picker=0;}}classic_down=-9;}
 }else if(classic_inline)n=xz_ui_inline_touch(&ui,&um,536,96,a-131,b-18,down,o);else if(panel)n=xz_ui_touch(&ui,&um,a,b,down,o);else shell_event((struct xz_shell_event){.kind=down?XZ_SHELL_TOUCH_DOWN:XZ_SHELL_TOUCH_UP,.x=a,.y=b,.deck=-1});}
 else if(!strcmp(cmd,"back")){if(panel)n=xz_ui_back(&ui,o);else shell_event((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=XZ_SHELL_KEY_BACK,.deck=-1});}
 else if(!strcmp(cmd,"key")){if(panel&&a==XZ_SHELL_KEY_MENU){if(xz_menu_rule(XZ_MENU_AT_MODS)->job==XZ_MENU_JOB_CLOSE_MODS)panel=0;}else shell_event((struct xz_shell_event){.kind=XZ_SHELL_KEY,.key=(enum xz_shell_key)a,.deck=a==XZ_SHELL_KEY_LOAD?0:-1});}
 apply_ui(o,n);render();
 }
 return 0;
}
