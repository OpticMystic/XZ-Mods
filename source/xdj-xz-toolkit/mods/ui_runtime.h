#ifndef XZ_UI_RUNTIME_H
#define XZ_UI_RUNTIME_H
struct xz_cue_event;
struct xz_settings;
int xz_ui_runtime_pad_color(int deck,int pad,unsigned *rgb,int *enabled);
void xz_ui_runtime_pad_native_page(int deck,int page);
int xz_ui_runtime_pad(const struct xz_cue_event *event,unsigned *trace_flags);
int xz_ui_runtime_key_synced(int deck);
int xz_ui_runtime_start(int audio_ready, int key_ready, int stems_enabled);
void xz_ui_runtime_stop(void);
/* Async LOAD failure: bounded text plus a finite MODS-button alert.
   Its existing touch target opens the full message; no automatic panel takeover. */
void xz_ui_runtime_notice(const char *text);
void xz_ui_runtime_on_source_key(int source);
/* The stock LINK (0) or REKORDBOX (1) key handler, through its hook's trampoline so no assignment runs.
   run 0: 1 when the handler can be called. run 1: calls it with a press-phase entry ({flags 1, 0, state 2},
   as BrowseKeyProcessing passes it) and returns 0, or -1 when its hook is not armed. Call run 1 only on the
   native key task (Ui_EventTask, where stock's own key handlers run), with no lock held. */
int xz_ui_runtime_stock_source_key(int which, int run);
int xz_ui_runtime_arcade_key(void *self,const void *input);
/* Profile worker contract. profile_usb is called from the native list text hook for
   the selected local USB (0/1) only; it stores a value and never does I/O. */
void xz_ui_runtime_profile_usb(unsigned slot);
int xz_ui_runtime_profile_selected(void); /* -1 until a local USB is browsed */
void xz_ui_runtime_profile_snapshot(struct xz_settings *out);
/* Replace live preferences with a validated profile; does not queue a save. */
void xz_ui_runtime_profile_apply(const struct xz_settings *saved);
#endif
