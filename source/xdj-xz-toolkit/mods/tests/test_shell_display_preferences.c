/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Display preference acceptance requires active assertions
#endif
#include "../shell_runtime.c"
#include <assert.h>
int main(void){
 xz_shell_init(&shell);display_input_known=0;wave_color_explicit=0;ui_action_count=0;
 struct xz_shell_runtime_frame_input in={.look_known=1,.modern_wave_color=2,.wave_zoom=3};
 assert(apply_display_input(&in));
 assert(shell.display.wave_color==XZ_SHELL_WAVE_3BAND&&shell.zoom==3);
 assert(!apply_display_input(&in));
 assert(look_apply_locked(XZ_SHELL_SET_WAVE_COLOR,XZ_SHELL_WAVE_RGB));
 assert(wave_color_explicit&&shell.display.wave_color==XZ_SHELL_WAVE_RGB&&shell.display.usb_colors==XZ_SHELL_USB_COLORS_TILE);
 assert(ui_action_count==2&&ui_actions[0].kind==XZ_UI_SHELL_DISPLAY&&ui_actions[0].index==XZ_SHELL_SET_WAVE_COLOR&&ui_actions[0].value==1);
 /* Repeated or cached previous inputs cannot undo the live choice. */
 assert(!apply_display_input(&in)&&shell.display.wave_color==XZ_SHELL_WAVE_RGB&&shell.display.usb_colors==XZ_SHELL_USB_COLORS_TILE);
 in.cached=1;assert(!apply_display_input(&in)&&shell.display.wave_color==XZ_SHELL_WAVE_RGB);
 /* The worker's acknowledgment changes only its changed values. */
 in.modern_wave_color=1;in.usb_colors=1;assert(apply_display_input(&in));
 assert(xz_shell_restore_setting(&shell,XZ_SHELL_ITEM_WAVE_ZOOM,6));save_display(XZ_SHELL_ITEM_WAVE_ZOOM,6);
 assert(!apply_display_input(&in)&&shell.zoom==6);
 save_display(XZ_SHELL_ITEM_WAVE_ZOOM,5);assert(ui_action_count==3&&ui_actions[2].value==5);
 in.wave_position=1;in.key_display=2;in.wave_zoom=5;in.usb_colors=2;in.hidden_parts=15;in.modern_browse_columns=2047;
 assert(apply_display_input(&in));
 assert(shell.display.wave_position==XZ_SHELL_POSITION_LEFT&&shell.display.key_display==XZ_SHELL_KEY_ALPHANUMERIC&&shell.zoom==5&&shell.display.usb_colors==XZ_SHELL_USB_COLORS_OFF&&shell.display.hidden==15&&column_mask==2047);
 assert(look_apply_locked(XZ_SHELL_ITEM_BYPASS_BUTTON,0));assert(shell.display.hidden==11);
 assert(!apply_display_input(&in)&&shell.display.hidden==11);
 in.hidden_parts=11;assert(apply_display_input(&in)&&shell.display.hidden==11);
 for(unsigned i=0;i<sizeof display_ids/sizeof *display_ids;i++)assert(xz_shrt_shell_owned_setting(display_ids[i]));
 /* Start/restart applies every saved field again, without queueing a write. */
 int queued=ui_action_count;xz_shell_init(&shell);display_input_known=0;assert(apply_display_input(&in));
 assert(shell.display.wave_color==XZ_SHELL_WAVE_RGB&&shell.display.hidden==11&&shell.zoom==5&&ui_action_count==queued);
 puts("PASS shell display input edges, stale/cached frames, local colour precedence, all fields, queue coalescing and restart restore");
 return 0;
}
