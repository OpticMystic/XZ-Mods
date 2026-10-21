#include "../extra_player.c"
static struct xz_xp_players test_players;
static uint32_t test_link(void *ctx){(void)ctx;return 1;}
static uint32_t test_now(void){return 0;}
void test_player_init(void){memset(&test_players,0,sizeof test_players);test_players.env.sink.link=test_link;test_players.env.now_ms=test_now;pthread_mutex_init(&test_players.publish_lock,NULL);for(int i=0;i<2;i++){pthread_mutex_init(&test_players.deck[i].queue_lock,NULL);atomic_init(&test_players.deck[i].published_state,XZ_XP_CUED);}}
int test_player_input(const struct xz_extra_deck_event *e){return xz_xp_input(&test_players,e);}
unsigned test_player_count(int side){return test_players.deck[side].count;}
int test_player_delta(int side){return test_players.deck[side].queue[test_players.deck[side].head].event.jog.delta;}

void test_player_clear(int side){struct deck *d=&test_players.deck[side];pthread_mutex_lock(&d->queue_lock);d->head=d->count=0;pthread_mutex_unlock(&d->queue_lock);}
int test_player_kind(int side,unsigned n){struct deck *d=&test_players.deck[side];return d->queue[(d->head+n)%QUEUE_SLOTS].event.kind;}
int test_player_pressed(int side,unsigned n){struct deck *d=&test_players.deck[side];return d->queue[(d->head+n)%QUEUE_SLOTS].event.pressed;}
