#include "../ui/route_card.h"
/* Host-only fixture callbacks and geometry. Never included by device source lists. */
void nav_runtime_jump_result(int);
const char *nav_runtime_notice(void);
static int nav_perf_fixture(int code){
 if(code==242){um.stem_page=2;return 1;}
 if(code==270){nav_runtime_jump_result(0);return 1;}
 if(code>=200&&code<208){
  int state=code-200;um.route.state=state;sm.route_state=state;
  sm.route_revision++;sm.route_checked_at_ms=now;sm.route_card_since_ms=now;
  sm.deck_count=state==XZ_UI_ROUTE_READY?4:2;
  sm.route_card_ready_at_ms=state==XZ_UI_ROUTE_READY?now:0;
  return 1;
 }
 if(code>=220&&code<229){
  int why=code-220;if(why==XZ_UI_USB_WHY_ENGINE||why==XZ_UI_USB_WHY_OFF)um.enabled&=~XZ_UI_STEM;
  for(int d=0;d<2;d++){um.deck[d].usb_reason=(enum xz_ui_usb_reason)why;um.deck[d].stem_format="OVERCUE 3";um.deck[d].pc_stems.source=XZ_UI_SOURCE_LOCAL;
   if(why==XZ_UI_USB_WHY_NONE)um.deck[d].ready|=XZ_UI_STEM;else um.deck[d].ready&=~XZ_UI_STEM;
  }return 1;
 }
 if(code==240||code==241){for(int d=0;d<2;d++){if(code==240)um.deck[d].ready&=~(XZ_UI_GATE|XZ_UI_SMART);else um.deck[d].ready|=XZ_UI_GATE|XZ_UI_SMART;}return 1;}
 if(code==250){sm.deck[2].known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_MUTE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_BYPASS));sm.deck[2].caps_off|=XZ_SHELL_CAP_STEM_MUTE|XZ_SHELL_CAP_STEM_GAIN|XZ_SHELL_CAP_STEM_BYPASS;sm.deck[2].alert.kind=XZ_SHELL_ALERT_READ_ERROR;sm.deck[2].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);return 1;}
 if(code==251){sm.deck[0].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_RAM_ONLY);sm.deck[0].ram_only=1;return 1;}
 return 0;
}
static void nav_perf_shell(const struct xz_shell_action *a){
 if(a->kind==XZ_SHELL_ACT_TOGGLE_STEMS){if(sm.layout_preference>1)sm.four_stems_hidden=!sm.four_stems_hidden;else {for(int i=0;i<XZ_SHELL_STEM_LAYOUTS;i++)if(xz_shell_stem_cycle[i]==sm.stem_layout){sm.stem_layout=xz_shell_stem_cycle[(i+1)%XZ_SHELL_STEM_LAYOUTS];break;}sm.stem_layout_known=1;}}
 if(a->kind==XZ_SHELL_ACT_ROUTE_RESCAN){um.route.state=sm.route_state=XZ_UI_ROUTE_SCANNING;}
 if(a->kind==XZ_SHELL_ACT_DECK_SELECT){if(a->value)sm.sides_extra|=1u<<a->deck;else sm.sides_extra&=~(1u<<a->deck);}
 if(a->kind==XZ_SHELL_ACT_SET_SETTING&&a->id==XZ_SHELL_SET_TIME_MODE){sm.settings.time_mode=a->value;for(int d=0;d<4;d++)sm.deck[d].time_mode=a->value;}
 if(a->kind==XZ_SHELL_ACT_LOAD&&a->id>=1000&&a->id<1000+BX_TRACKS)sm.deck[a->deck].alert.kind=XZ_SHELL_ALERT_NONE;
 if(a->kind==XZ_SHELL_ACT_SEEK)sm.deck[a->deck].elapsed_ms=a->value;
}
static void rect_widget(const char *,struct xz_shell_rect,int *);
static void nav_perf_widgets(int *n){
 struct xz_shell_model m;xz_shell_play_projection(&sh,&sm,&m);
 if(xz_shell_screen(&sh)==XZ_SHELL_BROWSE){
  rect_widget("TRACK ROW",xz_shell_browse_rect(&sh,&sm,XZ_SHELL_BR_ROW,2),n);
  for(int d=1;d<=4;d++){char label[24];snprintf(label,sizeof label,"LOAD DECK %d",d);rect_widget(label,xz_shell_browse_rect(&sh,&sm,XZ_SHELL_BR_LOAD,d),n);}
 }
 if(xz_shell_screen(&sh)==XZ_SHELL_PLAY){
  if(sm.deck_count<4&&sm.layout_preference>1){struct xz_shell_rect lane=xz_shell_play_rect(&m,XZ_SHELL_R_LANE,3);struct xz_route_card_rect b=xz_route_card_button(&xz_route_card_modern,lane.y,XZ_ROUTE_CARD_HIT_PRIMARY);rect_widget("RESCAN",(struct xz_shell_rect){b.x,b.y,b.w,b.h},n);}

  rect_widget("STEMS HEADER",(struct xz_shell_rect){674,0,68,25},n);
  rect_widget("CHOOSE TRACK",xz_shell_play_rect(&m,XZ_SHELL_R_ALERT_BUTTON,2),n);
  rect_widget("INFO HEADER",xz_shell_play_rect(&m,XZ_SHELL_R_INFO_BUTTON,0),n);
  for(int d=0;d<4;d++){char label[32];snprintf(label,sizeof label,"LANE %d",d+1);rect_widget(label,xz_shell_play_rect(&m,XZ_SHELL_R_LANE,d),n);snprintf(label,sizeof label,"TIME %d",d+1);rect_widget(label,xz_shell_play_rect(&m,XZ_SHELL_R_TIME,d),n);}
 }
}

/* Real cue state machine, with local stock-transport callbacks. */
#include "../cue/cue.c"
static struct xz_cue nav_cue;
static int nav_cue_bits=-1;
static int nav_cue_at(void *ctx,int d,int pad,int32_t *ms){(void)ctx;*ms=sm.deck[d].hot_cue[pad-1].ms;return sm.deck[d].hot_cue[pad-1].set;}
static int nav_cue_pause(void *ctx,int d){(void)ctx;sm.deck[d].playing=0;effect("cue",1,d,0,0);return 1;}
static int nav_cue_seek(void *ctx,int d,int32_t at){(void)ctx;sm.deck[d].elapsed_ms=at;effect("cue",2,d,0,(float)at);return 1;}
static int nav_cue_memory(void *ctx,int d,int32_t at){(void)ctx;sm.deck[d].cue_ms=at;effect("cue",3,d,0,(float)at);return 1;}
static void nav_perf_pad(int code,int side,int release){
 int bits=um.enabled&(XZ_UI_GATE|XZ_UI_SMART);
 if(bits!=nav_cue_bits){xz_cue_init(&nav_cue,(struct xz_cue_api){NULL,nav_cue_at,nav_cue_pause,nav_cue_seek,nav_cue_memory});xz_cue_settings(&nav_cue,bits&XZ_UI_GATE,bits&XZ_UI_SMART);nav_cue_bits=bits;}
 if(!bits||code<10||code>17||pages[side]!=0)return;
 struct xz_cue_event e={.deck=side,.pad=code-10,.operation=release?2:0,.hotcue_mode=1,.mode_button=-1};
 xz_cue_before(&nav_cue,&e);
 if(!release){int32_t at;if(nav_cue_at(NULL,side,e.pad+1,&at)){sm.deck[side].elapsed_ms=at;sm.deck[side].playing=1;}}
 xz_cue_after(&nav_cue,&e,1);
}

static void nav_perf_ui(const struct xz_ui_action *a){
 if(a->kind==XZ_UI_SHELL_LOOK&&a->index==XZ_UI_LOOK_WAVE_COLOR)xz_shell_restore_setting(&sh,XZ_SHELL_ITEM_WAVE_COLOR,um.modern_look.wave_color);
 if(a->kind==XZ_UI_KEY_FEEDBACK||a->kind==XZ_UI_KEY_FORMAT||a->kind==XZ_UI_BROWSE_TRAFFIC){sm.mods_settings_known=1;sm.key_feedback=um.key_feedback;sm.key_format=um.key_format;sm.browse_traffic=um.browse_traffic;}
}
