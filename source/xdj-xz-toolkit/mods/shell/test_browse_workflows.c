#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#error Workflow acceptance requires active assertions
#endif
#include "browse_fixtures.h"
#include "shell_source.h"
#ifdef XZ_BROWSE_WORKFLOW_RUNTIME_PROBE
#include "shell_runtime.h"
#endif
#include <inttypes.h>
#include <time.h>

static struct bx_library library;
static struct fx_track track;
static struct xz_shell u;
static struct xz_shell_model model;
static struct xz_shell_browse_model browse;
static struct xz_shell_source_model source;
static struct xz_shell_action actions[XZ_SHELL_ACTIONS];
static uint16_t pixels[XZ_SHELL_WIDTH*XZ_SHELL_HEIGHT];
static const char *story;
static unsigned failures,checks,steps,case_failures,verbose;
static size_t action_count;
#define CHECK(e) do { checks++;if(!(e)){failures++;case_failures++;printf("{\"failure\":\"%s\",\"line\":%d,\"check\":\"%s\"}\n",story,__LINE__,#e);} } while(0)
static struct xz_shell_view view(void) { return u.stack[u.depth-1]; }
static size_t send_event(struct xz_shell_event e) {
    struct xz_shell_view before=view();
    action_count=xz_shell_input(&u,&model,&e,actions);steps++;
    if(verbose) { struct xz_shell_view after=view();printf("{\"story\":\"%s\",\"step\":%u,\"kind\":%d,\"key\":%d,\"delta\":%d,\"before\":[%d,%d,%d,%d],\"after\":[%d,%d,%d,%d],\"modal\":%d,\"actions\":%zu,\"action_kind\":%d,\"action_id\":%d,\"action_value\":%d}\n",story,steps,e.kind,e.key,e.delta,before.screen,before.cursor,before.scroll,before.focus,after.screen,after.cursor,after.scroll,after.focus,u.load_choice.mode,action_count,action_count?actions[0].kind:0,action_count?actions[0].id:0,action_count?actions[0].value:0); }
    /* This is the model-provider boundary, not an input implementation. */
    for(size_t i=0;i<action_count;i++)if(actions[i].kind==XZ_SHELL_ACT_BROWSE&&actions[i].id==XZ_SHELL_BROWSE_REQUEST_WINDOW)bx_window(&browse,&library,actions[i].value);
    return action_count;
}
static size_t key(enum xz_shell_key k,int deck) { return send_event((struct xz_shell_event){XZ_SHELL_KEY,0,0,0,k,deck}); }
static size_t turn(int delta) { return send_event((struct xz_shell_event){XZ_SHELL_ENCODER_TURN,0,0,delta,0,-1}); }
static size_t push(void) { return send_event((struct xz_shell_event){XZ_SHELL_ENCODER_PUSH,0,0,0,0,-1}); }
static void start(const char *name,int decks,enum xz_shell_screen screen) {
    story=name;steps=case_failures=0;
    fx_model_full(&model,&track);bx_tracks(&browse,&library,screen);browse.media_generation=17;
    memset(&source,0,sizeof source);source.current_known=1;source.current=XZ_SHELL_SRC_USB1;
    for(int i=0;i<4;i++){source.device[i].known=1u<<XZ_SHELL_D_STATUS;source.device[i].status=XZ_SHELL_DEVICE_READY;}
    model.browse=&browse;model.source=&source;model.deck_count=decks;
    model.capabilities|=XZ_SHELL_CAP_LOAD|XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_SEARCH|XZ_SHELL_CAP_TAG_LIST|XZ_SHELL_CAP_SOURCE;
    for(int i=0;i<4;i++)model.deck[i].playing=0;
    xz_shell_init(&u);xz_shell_browse_reset();
    enum xz_shell_key k=screen==XZ_SHELL_SEARCH?XZ_SHELL_KEY_SEARCH:screen==XZ_SHELL_PLAYLIST?XZ_SHELL_KEY_PLAYLIST:screen==XZ_SHELL_TAG_LIST?XZ_SHELL_KEY_TAG_LIST:XZ_SHELL_KEY_BROWSE;
    key(k,-1);
    /* First real encoder event initializes the screen; BROWSE starts on its
     * current category rail, whose push enters the track list. */
    turn(0);if(screen==XZ_SHELL_BROWSE)push();
    /* These return/LOAD stories start on results. SEARCH initially focuses
     * its keyboard; BACK dismisses it in one press before browsing results. */
    if(screen==XZ_SHELL_SEARCH)key(XZ_SHELL_KEY_BACK,-1);
}
static void finish(void) { printf("{\"workflow\":\"%s\",\"steps\":%u,\"failures\":%u}\n",story,steps,case_failures); }
static int same_view(struct xz_shell_view a,struct xz_shell_view b) { return a.screen==b.screen&&a.cursor==b.cursor&&a.scroll==b.scroll&&a.focus==b.focus; }
static int is_action(enum xz_shell_action_kind kind,int id) { return action_count==1&&actions[0].kind==kind&&(id<0||actions[0].id==id); }
static void load_track(int decks,int physical_choice) {
    if(decks==4&&!physical_choice) { push();CHECK(u.load_choice.mode==2);turn(2);push(); }
    else key(XZ_SHELL_KEY_LOAD,decks==4?2:0);
    CHECK(is_action(XZ_SHELL_ACT_LOAD,-1));CHECK(xz_shell_screen(&u)==XZ_SHELL_PLAY);
    CHECK(actions[0].deck==(decks==4?2:0));
}
static void return_workflows(void) {
    for(int count=2;count<=4;count+=2)for(int screen=XZ_SHELL_BROWSE;screen<=XZ_SHELL_SEARCH;screen++)for(int back=0;back<2;back++)for(int physical=0;physical<2;physical++) {
        char name[100];snprintf(name,sizeof name,"%d_decks_screen%d_%s_%s",count,screen,back?"BACK":"BROWSE",physical?"physical_choice":"encoder_choice");
        start(name,count,(enum xz_shell_screen)screen);turn(18);struct xz_shell_view saved=view();int depth=u.depth;uint32_t list=browse.list_id;uint64_t generation=browse.media_generation;
        CHECK(saved.cursor==18&&saved.scroll>0&&saved.focus==1);
        load_track(count,physical);CHECK(actions[0].id==1018&&actions[0].value==19);
        key(back?XZ_SHELL_KEY_BACK:XZ_SHELL_KEY_BROWSE,-1);
        CHECK(u.depth==depth&&same_view(view(),saved));CHECK(browse.list_id==list&&browse.media_generation==generation);
        turn(1);CHECK(view().cursor==19&&view().focus==1);
        finish();
    }
}
static void cancel_and_isolation(void) {
    static const enum xz_shell_key isolated[]={XZ_SHELL_KEY_BACK,XZ_SHELL_KEY_BROWSE,XZ_SHELL_KEY_SOURCE,XZ_SHELL_KEY_MENU,XZ_SHELL_KEY_SEARCH};
    for(unsigned k=0;k<sizeof isolated/sizeof isolated[0];k++) {
        char name[60];snprintf(name,sizeof name,"modal_key_%d_isolated",isolated[k]);start(name,4,XZ_SHELL_BROWSE);turn(18);struct xz_shell_view saved=view();
        push();CHECK(u.load_choice.mode==2);turn(1000);CHECK(u.load_choice.cursor==4&&same_view(view(),saved));
        key(isolated[k],-1);CHECK(!u.load_choice.mode);
        if(isolated[k]==XZ_SHELL_KEY_BACK){CHECK(same_view(view(),saved));turn(1);CHECK(view().cursor==19);}
        else CHECK(xz_shell_screen(&u)==(isolated[k]==XZ_SHELL_KEY_BROWSE?XZ_SHELL_PLAY:isolated[k]==XZ_SHELL_KEY_SOURCE?XZ_SHELL_SOURCE:isolated[k]==XZ_SHELL_KEY_SEARCH?XZ_SHELL_SEARCH:XZ_SHELL_BROWSE));
        finish();
    }
    start("modal_encoder_CANCEL",4,XZ_SHELL_BROWSE);turn(18);struct xz_shell_view saved=view();push();turn(4);push();CHECK(!action_count&&!u.load_choice.mode&&same_view(view(),saved));finish();
}
static void source_hierarchy(void) {
    start("SOURCE_BACK_category_source_PLAY",4,XZ_SHELL_BROWSE);turn(7);key(XZ_SHELL_KEY_BACK,-1);CHECK(xz_shell_screen(&u)==XZ_SHELL_BROWSE&&view().focus!=1);
    key(XZ_SHELL_KEY_BACK,-1);CHECK(xz_shell_screen(&u)==XZ_SHELL_SOURCE);key(XZ_SHELL_KEY_BACK,-1);CHECK(xz_shell_screen(&u)==XZ_SHELL_PLAY);
    key(XZ_SHELL_KEY_SOURCE,-1);CHECK(xz_shell_screen(&u)==XZ_SHELL_SOURCE);turn(1);push();
    int set_usb2=0;for(size_t i=0;i<action_count;i++)if(actions[i].kind==XZ_SHELL_ACT_SET_SOURCE&&actions[i].id==XZ_SHELL_SRC_USB2)set_usb2=1;
    CHECK(set_usb2&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);finish();
}
static void playlist_hierarchy(void) {
    start("playlist_folder_LOAD_return_parent_source",4,XZ_SHELL_PLAYLIST);bx_folders(&browse,&library,XZ_SHELL_PLAYLIST);browse.media_generation=17;turn(4);struct xz_shell_view folder=view();push();CHECK(is_action(XZ_SHELL_ACT_BROWSE,XZ_SHELL_BROWSE_OPEN)&&actions[0].value==5);
    bx_tracks(&browse,&library,XZ_SHELL_PLAYLIST);browse.media_generation=17;browse.list_id=505;turn(18);struct xz_shell_view saved=view();
    load_track(4,0);key(XZ_SHELL_KEY_BACK,-1);CHECK(same_view(view(),saved));
    key(XZ_SHELL_KEY_BACK,-1);CHECK(is_action(XZ_SHELL_ACT_BROWSE,XZ_SHELL_BROWSE_UP));
    bx_folders(&browse,&library,XZ_SHELL_PLAYLIST);browse.media_generation=17;CHECK(same_view(view(),folder));
    key(XZ_SHELL_KEY_BACK,-1);CHECK(xz_shell_screen(&u)==XZ_SHELL_SOURCE);finish();
}
static int key_index(const char *label) { for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(!strcmp(xz_shell_browse_key_label(i),label))return i;return -1; }
static void tap_key(const char *label) {
    int index=key_index(label);CHECK(index>=0);struct xz_shell_rect r=xz_shell_browse_rect(&u,&model,XZ_SHELL_BR_KEY,index);
    send_event((struct xz_shell_event){XZ_SHELL_TOUCH_DOWN,r.x+r.w/2,r.y+r.h/2,0,0,-1});send_event((struct xz_shell_event){XZ_SHELL_TOUCH_UP,r.x+r.w/2,r.y+r.h/2,0,0,-1});
}
static void search_workflow(void) {
    start("SEARCH_query_LOAD_return_next",4,XZ_SHELL_SEARCH);turn(-1);push();tap_key("N");tap_key("E");CHECK(!strcmp(xz_shell_browse_query(),"NE"));tap_key("SEARCH");turn(12);struct xz_shell_view saved=view();
    load_track(4,0);key(XZ_SHELL_KEY_BACK,-1);CHECK(same_view(view(),saved));CHECK(!strcmp(xz_shell_browse_query(),"NE"));turn(1);CHECK(view().cursor==13);finish();
}
static void removal_workflows(void) {
    for(int why=0;why<5;why++) {
        char name[70];snprintf(name,sizeof name,"pending_LOAD_invalidation_%d",why);start(name,4,XZ_SHELL_BROWSE);turn(18);push();
        if(why==0)model.browse=NULL;else if(why==1)browse.media_generation++;else if(why==2)browse.list_id++;else if(why==3)browse.source_name="USB2";else{library.tracks[18].id=9999;}
        push();CHECK(!action_count&&!u.load_choice.mode&&xz_shell_screen(&u)==XZ_SHELL_BROWSE);library.tracks[18].id=1018;finish();
    }
    for(int why=0;why<4;why++) {
        char name[70];snprintf(name,sizeof name,"return_view_invalidation_%d",why);start(name,2,XZ_SHELL_BROWSE);turn(18);load_track(2,0);
        if(why==0)model.browse=NULL;else if(why==1)browse.media_generation++;else if(why==2)browse.list_id++;else browse.source_name="USB2";
        key(XZ_SHELL_KEY_BACK,-1);CHECK(xz_shell_screen(&u)==XZ_SHELL_PLAY);finish();
    }
    start("USB_replacement_active_list_encoder_and_LOAD",4,XZ_SHELL_BROWSE);turn(18);source.device[0].status=XZ_SHELL_DEVICE_NOT_CONNECTED;model.browse=NULL;
    turn(1);CHECK(action_count==1&&actions[0].kind==XZ_SHELL_ACT_HAND_OFF);key(XZ_SHELL_KEY_LOAD,0);CHECK(!action_count&&!u.load_choice.mode);
    bx_tracks(&browse,&library,XZ_SHELL_BROWSE);browse.media_generation=18;model.browse=&browse;source.device[0].status=XZ_SHELL_DEVICE_READY;
    turn(0);CHECK(view().cursor==0&&view().scroll==0);key(XZ_SHELL_KEY_LOAD,0);CHECK(is_action(XZ_SHELL_ACT_LOAD,1000));finish();
    start("USB_replacement_immediate_LOAD_no_encoder",4,XZ_SHELL_BROWSE);turn(18);model.browse=NULL;
    bx_tracks(&browse,&library,XZ_SHELL_BROWSE);browse.media_generation=18;model.browse=&browse;
    const struct xz_shell_browse_row *fresh=xz_shell_browse_highlight(&u,&model);CHECK(fresh&&fresh->id==1000);
    key(XZ_SHELL_KEY_LOAD,0);CHECK(!u.load_choice.mode);
    CHECK(is_action(XZ_SHELL_ACT_LOAD,1000)&&actions[0].value==1&&xz_shell_screen(&u)==XZ_SHELL_PLAY);finish();
}
static uint64_t render_hash(const struct xz_shell *shell) {
    if(!xz_shell_render(shell,&model,pixels,XZ_SHELL_WIDTH))return 0;
    uint64_t h=1469598103934665603ull;for(size_t i=0;i<sizeof pixels/sizeof pixels[0];i++)h=(h^pixels[i])*1099511628211ull;return h;
}
static void probe_purity(void) {
    start("BROWSE_verdict_pending_jump_then_dispatch",2,XZ_SHELL_BROWSE);turn(18);
    send_event((struct xz_shell_event){XZ_SHELL_ENCODER_HOLD,0,0,0,0,-1});struct xz_shell before_list=u;
    load_track(2,0);uint64_t before=render_hash(&before_list);struct xz_shell before_probe=u;
#ifdef XZ_BROWSE_WORKFLOW_RUNTIME_PROBE
    const struct xz_shrt_key_map *map=xz_shrt_key_lookup(0x209,0);CHECK(map!=NULL);
    CHECK(xz_shrt_key_verdict(&u,&model,map)==XZ_SHRT_SWALLOW);
#else
    struct xz_shell copy=u;struct xz_shell_action probe_actions[XZ_SHELL_ACTIONS];
    struct xz_shell_event probe_key={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};
    xz_shell_input(&copy,&model,&probe_key,probe_actions);
#endif
    CHECK(!memcmp(&u,&before_probe,sizeof u));
    CHECK(render_hash(&before_list)==before);
    key(XZ_SHELL_KEY_BROWSE,-1);CHECK(view().cursor==18&&view().focus==1);CHECK(render_hash(&u)!=before);
    turn(1);CHECK(view().cursor==19&&view().focus==1);finish();
}
static void fast_burst(void) {
    start("encoder_1000_forward_1000_reverse_no_loss",4,XZ_SHELL_BROWSE);
    for(int i=0;i<1000;i++){turn(1);}CHECK(view().cursor==BX_TRACKS-1);
    for(int i=0;i<1000;i++){turn(-1);}CHECK(view().cursor==0&&view().scroll==0&&view().focus==1);finish();
}
static uint64_t ns(void) {
#ifdef _WIN32
    return (uint64_t)clock()*1000000000ull/CLOCKS_PER_SEC;
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
#endif
}
static int compare64(const void *a,const void *b) { uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return x<y?-1:x>y; }
static int benchmark(void) {
    uint64_t batches[128],checksum=0;unsigned events=0;verbose=0;
    /* Identical productive state transitions on frozen/current: navigation and
     * cancelling a chooser. The changed return path is deliberately excluded. */
    start("common_dispatch_benchmark",4,XZ_SHELL_BROWSE);turn(12);
    for(unsigned b=0;b<128;b++) { uint64_t t=ns();for(unsigned j=0;j<256;j++) {
        turn(1);turn(-1);push();turn(1);key(XZ_SHELL_KEY_BACK,-1);
        checksum+=(uint64_t)view().cursor+(uint64_t)view().scroll+(uint64_t)u.load_choice.mode;events+=5;
    }batches[b]=ns()-t; }
    qsort(batches,128,sizeof batches[0],compare64);
    printf("{\"benchmark\":\"common_dispatch\",\"events\":%u,\"events_per_batch\":1280,\"p50_batch_ns\":%"PRIu64",\"p95_batch_ns\":%"PRIu64",\"checksum\":%"PRIu64"}\n",events,batches[64],batches[121],checksum);return 0;
}
int main(int argc,char **argv) {
    (void)&bx_panels;fx_track_build(&track,1);bx_build(&library);
    if(argc>1&&!strcmp(argv[1],"--bench"))return benchmark();
    verbose=argc>1&&!strcmp(argv[1],"--trace");
    return_workflows();cancel_and_isolation();source_hierarchy();playlist_hierarchy();search_workflow();removal_workflows();probe_purity();fast_burst();
    printf("{\"summary\":\"browse_workflows\",\"checks\":%u,\"failures\":%u}\n",checks,failures);return failures?1:0;
}
