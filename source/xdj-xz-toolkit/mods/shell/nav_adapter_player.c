/* Offline player adapter. The included fixture supplies a fake sink, never starts workers. */
#define main nav_unused_player_test_main
#include "../test_extra_player.c"
#undef main
void nav_player_open(const char *directory){
 assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",directory);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",directory);build_analysis();
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,NULL,1,1};
 p=xz_xp_create(&env);assert(p);master=12800;fake_route(1);step();load(0,1);load(1,1);
}
const struct xz_extra_decks *nav_player_input(int kind,int side){
 assert(side>=0&&side<2);press(side,(enum xz_extra_input)kind);return &view;
}
