/* The browse tile's queued action uses the existing shell -> UI -> settings worker path. */
static int preview_action_pending,preview_action_value;
int xz_shell_runtime_take_ui_action(int *kind,int *deck,int *index,float *value) {
 if(!preview_action_pending)return 0;
 preview_action_pending=0;*kind=XZ_UI_BROWSE_PREVIEW;*deck=-1;*index=0;*value=(float)preview_action_value;return 1;
}
static void browse_preview_runtime_check(void) {
 struct xz_settings fresh; xz_settings_default(&fresh);
 xz_ui_runtime_profile_apply(&fresh);assert(!model.browse_preview&&!settings_snapshot().browse_preview);
 uint64_t revision=settings_revision;
 int readonly=settings_readonly;settings_readonly=1;
 for(int shown=1;shown>=0;shown--){
  preview_action_pending=1;preview_action_value=shown;fake_requests|=XZ_SHELL_UI_ACTION;
  shell_ui_requests_locked();
  assert(!preview_action_pending&&model.browse_preview==shown&&settings_revision==++revision);
  struct xz_shell_runtime_frame_input in;shell_frame_input_locked(&in);assert(in.browse_preview==shown);
  struct xz_settings saved=settings_snapshot(),loaded;char text[1024];
  assert(xz_settings_format(&saved,text,sizeof text)>0&&!xz_settings_parse(text,&loaded)&&loaded.browse_preview==shown);
  model.browse_preview=!shown;xz_ui_runtime_profile_apply(&loaded);++revision;
  assert(model.browse_preview==shown&&settings_revision==revision);
  shell_ui_requests_locked();assert(settings_revision==revision);
 }
 struct xz_ui_action invalid={.kind=XZ_UI_BROWSE_PREVIEW,.value=2};
 apply(NULL,&invalid,1);assert(!model.browse_preview&&settings_revision==revision);
 settings_readonly=readonly;
 puts("PASS browse preview runtime: default hidden, shell action queue, settings revisions and round-trip, profile restore, frame publication, one action per tap");
}
