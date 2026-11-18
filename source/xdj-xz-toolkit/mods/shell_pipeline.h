/* Internal, included after shell_runtime's model/source globals. */
#ifndef XZ_SHELL_PIPELINE_H
#define XZ_SHELL_PIPELINE_H
struct xp_geometry {
 int screen,depth,popup,panel,zoom,grid,deck,view,count,order,sides,stem_known,stem_layout,stem_order,hidden;
 int requested_count,wave_color,wave_override,wave_position,route_state,stems_known,stems_on;uint32_t display_hidden;uint64_t deck_known[4];
 uint32_t caps,native_id[2],native_kind[2],native_fence[2],extra_load[2],extra_route[2];
 int32_t length[4];uint32_t deck_caps[4];
};
struct xp_publication { __typeof__(shell.header_frame) header; struct xz_shell_key_frame keys; uint32_t painted; };
struct xp_stage {
 uint32_t token,epoch,revision,input_revision;
 __typeof__(position[0].display) phase[2];
 struct xp_publication publication;
 struct xp_geometry geometry;
 unsigned native_visible;
};
static struct xp_stage xp_stage;
static struct xp_geometry xp_presented_geometry;
static __thread struct xp_stage xp_serial;
static int xp_have_presented;
static uint32_t xp_epoch,xp_active,xp_allowed,xp_gate,xp_reset_requested;
enum { XP_EMPTY,XP_PENDING,XP_ACCEPTED,XP_CANCELLED,XP_APPLIED };
#ifndef XZ_XP_ACCEPT_HOOK
#define XZ_XP_ACCEPT_HOOK(point) ((void)0)
#endif
static void xp_cancel_pending(void){uint32_t pending=XP_PENDING;__atomic_compare_exchange_n(&xp_gate,&pending,XP_CANCELLED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
static uint64_t xp_ack_mono;
static __thread struct {uint32_t token,epoch;uint64_t target_wall;} xp_request;
static struct xp_geometry xp_geometry_now(void){
 struct xp_geometry k;memset(&k,0,sizeof k);
 k.screen=xz_shell_screen(&shell);k.depth=shell.depth;k.popup=shell.popup.kind;k.panel=shell.panel.kind;k.zoom=shell.zoom;k.grid=shell.grid_mode;k.deck=shell.deck;
 k.requested_count=shell.play_decks;k.wave_color=shell.display.wave_color;k.wave_override=shell.display.wave_color_override;k.wave_position=shell.display.wave_position;k.display_hidden=shell.display.hidden;k.route_state=model.route_state;k.stems_known=model.stems_known;k.stems_on=model.stems_on;
 k.view=model.layout_preference;k.count=model.deck_count;k.order=model.wave_order;k.sides=model.sides_extra;k.stem_known=model.stem_layout_known;k.stem_layout=model.stem_layout;k.stem_order=model.stem_order;k.hidden=model.four_stems_hidden;k.caps=model.capabilities;
 for(int d=0;d<2;d++){k.native_id[d]=loaded[d].id;k.native_kind[d]=loaded[d].device_kind;k.native_fence[d]=xp_source_fence[d];k.extra_load[d]=extra.deck[d].timeline.load_revision;k.extra_route[d]=extra.deck[d].timeline.route_epoch;}
 for(int d=0;d<4;d++){k.deck_known[d]=model.deck[d].known;k.length[d]=model.deck[d].total_ms;k.deck_caps[d]=model.deck[d].caps_off;}
 return k;
}
static struct xp_publication xp_publication_get(void){return (struct xp_publication){shell.header_frame,shell.key_frame,shell.deck_view.painted};}
static void xp_publication_set(const struct xp_publication *p){shell.header_frame=p->header;shell.key_frame=p->keys;shell.deck_view.painted=p->painted;}
static int xp_steady_locked(void){
 return owns_flag&&xz_shell_screen(&shell)==XZ_SHELL_PLAY&&shell.popup.kind==XZ_SHELL_POPUP_NONE&&shell.panel.kind==XZ_SHELL_PANEL_NONE&&!shell.down&&!shell.grab.active&&!shell.grid_mode&&!shell.deck_view.open&&!shell.deck_view.contact&&shell.deck_view.focus==XZ_SHELL_FOCUS_NONE&&!xz_shell_play_motion_ms(&shell,&model)&&!follow_bits&&!engine[0].manual&&!engine[1].manual&&xz_shrt_moving_decks(&model);
}
static void xp_update_allowed_locked(void){__atomic_store_n(&xp_allowed,xp_steady_locked(),__ATOMIC_RELEASE);}
int xz_shell_pipeline_allowed(void){return __atomic_load_n(&xp_allowed,__ATOMIC_ACQUIRE)&&xz_shell_runtime_owns()&&!native_input_mask();}
int xz_shell_pipeline_epoch(uint32_t epoch,int active,int visibility_lost){
 if(active){
  if(pthread_mutex_trylock(&mutex))return 0;
  if(!xp_serial.token||xp_serial.revision!=__atomic_load_n(&xp_revision,__ATOMIC_ACQUIRE)||!xp_steady_locked()){pthread_mutex_unlock(&mutex);return 0;}
  /* Caller has just copied its own last serial frame. Other render callers
     have separate TLS snapshots and cannot seed this presentation history. */
  for(int d=0;d<2;d++)position[d].display=xp_serial.phase[d];
  xp_publication_set(&xp_serial.publication);xp_presented_geometry=xp_serial.geometry;xp_have_presented=1;
  __atomic_store_n(&xp_reset_requested,0u,__ATOMIC_RELEASE);
 }
 xp_cancel_pending();
 __atomic_store_n(&xp_active,0u,__ATOMIC_RELEASE);__atomic_store_n(&xp_epoch,epoch,__ATOMIC_RELEASE);
 if(visibility_lost)__atomic_store_n(&xp_reset_requested,1u,__ATOMIC_RELEASE);

 __atomic_store_n(&xp_active,active!=0,__ATOMIC_RELEASE);
 if(active)pthread_mutex_unlock(&mutex);return 1;
}
void xz_shell_pipeline_begin(uint32_t token,uint32_t epoch,uint64_t target){xp_request.token=token;xp_request.epoch=epoch;xp_request.target_wall=target;}
int xz_shell_pipeline_end(void){
 int ok=xp_request.token&&__atomic_load_n(&xp_stage.token,__ATOMIC_ACQUIRE)==xp_request.token&&xp_stage.epoch==xp_request.epoch&&__atomic_load_n(&xp_epoch,__ATOMIC_ACQUIRE)==xp_request.epoch&&__atomic_load_n(&xp_active,__ATOMIC_ACQUIRE)&&xp_stage.revision==__atomic_load_n(&xp_revision,__ATOMIC_ACQUIRE);
 memset(&xp_request,0,sizeof xp_request);return ok;
}
/* Called immediately before an unconditional scanout copy. Never takes a mutex.
   One unconsumed READY frame ensures the mailbox cannot outrun its producer. */
int xz_shell_pipeline_accept(uint32_t token,uint32_t epoch,uint64_t blank_mono){
 if(!token||!blank_mono||__atomic_load_n(&xp_gate,__ATOMIC_ACQUIRE)!=XP_PENDING||xp_stage.input_revision!=__atomic_load_n(&native_input_revision,__ATOMIC_ACQUIRE)||!xz_shell_pipeline_allowed()||!__atomic_load_n(&xp_active,__ATOMIC_ACQUIRE)||__atomic_load_n(&xp_epoch,__ATOMIC_ACQUIRE)!=epoch||__atomic_load_n(&xp_stage.token,__ATOMIC_ACQUIRE)!=token||xp_stage.epoch!=epoch||xp_stage.revision!=__atomic_load_n(&xp_revision,__ATOMIC_ACQUIRE))return 0;
 xp_ack_mono=blank_mono+SCANOUT_MID_US;
 XZ_XP_ACCEPT_HOOK(0);
 /* This CAS is the decision: invalidation wins by cancelling PENDING, or
    acceptance wins and the consumer is irrevocably committed to its copy. */
 uint32_t pending=XP_PENDING;
 if(!__atomic_compare_exchange_n(&xp_gate,&pending,XP_ACCEPTED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
 XZ_XP_ACCEPT_HOOK(1);
 return 1;
}

static void xp_apply_locked(void){
 uint32_t accepted=XP_ACCEPTED;
 if(__atomic_compare_exchange_n(&xp_gate,&accepted,XP_APPLIED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  if(xp_stage.revision==__atomic_load_n(&xp_revision,__ATOMIC_ACQUIRE)){
   for(int d=0;d<2;d++){position[d].display=xp_stage.phase[d];if(position[d].display.valid)position[d].display.at=(uint32_t)(xp_ack_mono/1000u);if(!(xp_stage.native_visible>>d&1u))position[d].display.valid=0;}
   xp_publication_set(&xp_stage.publication);xp_presented_geometry=xp_stage.geometry;xp_have_presented=1;
  }else {shell.header_frame.valid=0;shell.key_frame.projection=0;shell.deck_view.painted=0;xp_have_presented=0;}
 }
 if(__atomic_exchange_n(&xp_reset_requested,0u,__ATOMIC_ACQ_REL)){for(int d=0;d<2;d++)position[d].display.valid=0;xp_have_presented=0;}

}
/* All source/input revision changes hold the shell mutex. Cancel before
   mutating source state; if acceptance already won, apply it before the event. */
static void xp_invalidate_locked(void){
 xp_cancel_pending();xp_apply_locked();__atomic_add_fetch(&xp_revision,1u,__ATOMIC_RELEASE);
}
static int xp_touch_allowed_locked(void){
 if(!__atomic_load_n(&xp_active,__ATOMIC_ACQUIRE))return 1;
 struct xp_geometry current=xp_geometry_now();return xp_have_presented&&!memcmp(&current,&xp_presented_geometry,sizeof current);
}
#endif
