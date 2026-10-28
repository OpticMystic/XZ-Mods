/* Host-only snapshot and game routing. Included only by nav_capture.c. */
#include "../arcade/arcade.h"
#include "../arcade/wave_rider.h"
#include "../arcade/game_nav.h"
static int nav_game_active;
static int nav_setup_mode;
static struct xz_arcade nav_arcade;
static struct xz_rider nav_rider;
static struct xz_game_nav nav_game_nav;
static struct xz_wave_rider_snapshot nav_wave;
static uint8_t nav_wave_bytes[18000*3];
static void nav_game_open(int game,unsigned at){
 nav_game_active=game;xz_game_nav_reset(&nav_game_nav);
 if(game==1){xz_arcade_init(&nav_arcade,at);xz_arcade_tempo(&nav_arcade,128,at);}
 else {for(int i=0;i<18000;i++){nav_wave_bytes[i*3]=(i%75<10)?220:8;nav_wave_bytes[i*3+1]=65;nav_wave_bytes[i*3+2]=20;}
 memset(&nav_wave,0,sizeof nav_wave);nav_wave.normalization=300;nav_wave.generation=1;nav_wave.track_hash=12;nav_wave.kind=1;nav_wave.playing=1;nav_wave.pulse_period=75;nav_wave.pulse_confidence=1;snprintf(nav_wave.title,sizeof nav_wave.title,"OFFLINE WAVE FIXTURE");xz_wave_rider_window(&nav_wave,nav_wave_bytes,18000,300);nav_wave.observed_ms=at;
 xz_rider_init(&nav_rider,0,at);xz_rider_feed(&nav_rider,&nav_wave,at);}
}
static void nav_game_touch(int x,int y,int down,unsigned at){
 nav_game_nav.visible=0;
 if(nav_game_active==1)xz_arcade_touch(&nav_arcade,x,y,down,at);else xz_rider_touch(&nav_rider,x,y,down,at);
}
static void nav_game_input(const char *cmd,int a,int b,unsigned at){
 if(!strcmp(cmd,"turn"))xz_game_nav_turn(&nav_game_nav,a);
 else if(!strcmp(cmd,"push")){int x,y;if(xz_game_nav_point(&nav_game_nav,&x,&y)){nav_game_touch(x,y,1,at);nav_game_touch(x,y,0,at);nav_game_nav.visible=1;}}
 else if(!strcmp(cmd,"down")||!strcmp(cmd,"up"))nav_game_touch(a,b,!strcmp(cmd,"down"),at);
 else if(!strcmp(cmd,"back")){if(nav_game_active==1)xz_arcade_back(&nav_arcade,at);else xz_rider_back(&nav_rider,at);}
 else if(!strcmp(cmd,"jog")){if(nav_game_active==1)xz_arcade_move(&nav_arcade,b,(float)a,at);else xz_rider_jog(&nav_rider,b,(float)a,at);}
 else if(!strcmp(cmd,"cue")){if(nav_game_active==1){if(a)xz_arcade_key(&nav_arcade,b,XZ_ARCADE_CUE_KEY,at);}else xz_rider_button(&nav_rider,b,XZ_RIDER_CUE,a,at);}
 else if(!strcmp(cmd,"gamepad")&&nav_game_active==1)xz_arcade_key(&nav_arcade,b,(enum xz_arcade_key)(XZ_ARCADE_PAD_A+a),at);
 if((nav_game_active==1&&nav_arcade.leave)||(nav_game_active==2&&nav_rider.leave))nav_game_active=0;
}
static void nav_game_render(uint16_t *out,unsigned at){
 struct xz_game_targets targets={0};
 if(nav_game_active==1){xz_arcade_step(&nav_arcade,at);xz_arcade_render_targets(&nav_arcade,out,800*480,800,&targets);}
 else {nav_wave.observed_ms=at;xz_rider_feed(&nav_rider,&nav_wave,at);xz_rider_step(&nav_rider,at);xz_rider_render_targets(&nav_rider,out,800*480,800,&targets);}
 xz_game_nav_frame(&nav_game_nav,&targets);xz_game_nav_draw(&nav_game_nav,out,800,800,480);
}
static int nav_setup_fixture(int code){
 if(code>=400&&code<=421)nav_setup_mode=1;
 switch(code){
 case 410:sm.notice="DECK 1 DID NOT LOAD THE TRACK";sm.notice_at_ms=now;return 1;
 case 411:sm.notice="USB1 IS NOT CONNECTED";sm.notice_at_ms=now;return 1;
 case 412:sm.notice=NULL;return 1;
 case 413:um.deck[0].usb_reason=XZ_UI_USB_WHY_OFF;um.enabled=0;return 1;
 case 414:um.deck[0].usb_reason=XZ_UI_USB_WHY_NOT_PREPARED;return 1;
 case 415:um.deck[0].usb_reason=XZ_UI_USB_WHY_ERROR;return 1;
 case 416:um.deck[0].usb_reason=XZ_UI_USB_WHY_NONE;um.enabled=XZ_UI_STEM;return 1;
 case 417:sm.deck[0].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302","CANNOT PLAY TRACK"};sm.deck[0].known|=(uint64_t)1<<XZ_SHELL_F_ALERT;return 1;
 case 418:sm.deck[0].alert.kind=XZ_SHELL_ALERT_NONE;return 1;
 case 419:um.enabled=0;for(int d=0;d<2;d++){um.deck[d].usb_reason=XZ_UI_USB_WHY_ENGINE;um.deck[d].ready&=~XZ_UI_STEM;}return 1;
 case 420:um.sdk.host=XZ_SDK_UI_RUNNING;um.sdk.fresh=1;um.sdk.roots=2;um.sdk.snapshot.generation=1;um.sdk.snapshot.count=1;um.sdk.snapshot.items[0].kind=XZ_SDK_APPLICATION;um.sdk.snapshot.items[0].state=XZ_SDK_READY;snprintf(um.sdk.snapshot.items[0].id,XZ_SDK_ID,"%s",XZ_SDK_UI_TEST_ID);snprintf(um.sdk.snapshot.items[0].name,XZ_SDK_NAME,"OFFLINE TEST CARD");return 1;
 case 421:um.sdk.snapshot.items[0].state=XZ_SDK_RUNNING;return 1;
 case 400:my_settings.writable=0;my_settings.on_stick=0;my_settings.stored=NULL;return 1;
 case 401:nav_my_failure=1;return 1;
 case 402:nav_my_failure=0;my_settings.state=XZ_SHELL_MY_READY;my_settings.writable=my_settings.on_stick=1;my_settings.stored=&settings;return 1;
 case 403:um.settings_status="NOT SAVED TO USB 1: CHECK USB";return 1;
 case 404:um.settings_status="NO SETTINGS USB: CHANGES LAST UNTIL POWER OFF.";return 1;
 case 405:um.settings_status="USB 1 IS LOCKED: NOT SAVED";return 1;
 case 406:um.settings_status="THE STICK IS FULL: NOT SAVED";return 1;
 case 407:um.settings_status="SAVED TO USB 1";return 1;
 default:return 0;
 }
}

static void nav_setup_apply(const struct xz_ui_action *a){
 if(xz_sdk_ui_kind(a->kind)){struct xz_sdk_ui_plan plan;xz_sdk_ui_plan(&um.sdk,a,&plan);if(plan.submit){effect("sdk-plan",plan.command.kind,-1,(int)plan.command.index,(float)plan.command.value);if(plan.command.kind==XZ_SDK_AUTOLOAD)um.sdk.snapshot.autoload_enabled=plan.command.value;}}
}
