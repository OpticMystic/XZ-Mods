#ifdef NDEBUG
#error Physical LOAD tests require assertions
#endif
#include "shell/browse_fixtures.h"
#include "shell/shell_internal.h"
#include "shell_runtime.h"
#include <assert.h>
#include <stdio.h>
static struct bx_library library;
static struct fx_track track;
static struct xz_shell_action actions[XZ_SHELL_ACTIONS];
/* Actual key map, phase classification, contact ownership/duplicate gate and
 * shell action dispatcher; no native functions or controller access. */
static size_t physical(struct xz_shrt_contacts *contacts,struct xz_shell *u,const struct xz_shell_model *m,unsigned channel,unsigned op) {
 const struct xz_shrt_key_map *map=xz_shrt_key_lookup(0x430f,channel);assert(map);
 enum xz_shrt_phase phase=xz_shrt_phase_of(op);
 if(xz_shrt_load_duplicate(contacts,map,phase)>=0)return 0;
 int held=0;
 if(phase==XZ_SHRT_PRESS)xz_shrt_contact_press(contacts,0x430f,channel,1);
 else if(xz_shrt_contact_follow(contacts,0x430f,channel,phase,&held)<=0)return 0;
 int key=xz_shrt_key_for_phase(map,phase,held);
 struct xz_shell_event event={XZ_SHELL_KEY,0,0,0,key,map->deck};
 return key>=0?xz_shell_input(u,m,&event,actions):0;
}
int main(void) {
 (void)&bx_folders;fx_track_build(&track,1);bx_build(&library);
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_browse_model b;struct xz_shrt_contacts contacts={0};
 fx_model_full(&m,&track);bx_tracks(&b,&library,XZ_SHELL_BROWSE);b.media_generation=7;
 m.deck_count=4;m.capabilities|=XZ_SHELL_CAP_BROWSE|XZ_SHELL_CAP_LOAD;m.browse=&b;
 for(int d=0;d<4;d++)m.deck[d].caps_off=0;
 xz_shell_init(&u);xz_shell_browse_reset();
 struct xz_shell_event browse_event={XZ_SHELL_KEY,0,0,0,XZ_SHELL_KEY_BROWSE,-1};
 xz_shell_input(&u,&m,&browse_event,actions);u.stack[u.depth-1].focus=1;
 uint32_t id=b.rows[0].id;
 assert(!physical(&contacts,&u,&m,1,0)&&sh_browse_load_pending(&u));
 assert(!physical(&contacts,&u,&m,1,0)&&sh_browse_load_pending(&u));
 assert(!physical(&contacts,&u,&m,1,1)&&sh_browse_load_pending(&u));
 assert(!physical(&contacts,&u,&m,1,2)&&sh_browse_load_pending(&u));
 assert(!physical(&contacts,&u,&m,1,2)&&sh_browse_load_pending(&u));
 assert(physical(&contacts,&u,&m,2,0)==1&&actions[0].kind==XZ_SHELL_ACT_LOAD&&actions[0].deck==2&&actions[0].id==(int)id);
 assert(!physical(&contacts,&u,&m,2,0));assert(!physical(&contacts,&u,&m,2,2));
 const struct xz_shrt_key_map *browse=xz_shrt_key_lookup(0x209,0);
 xz_shrt_contact_press(&contacts,0x209,0,1);assert(xz_shrt_load_duplicate(&contacts,browse,XZ_SHRT_PRESS)==-1);
 puts("PASS physical LOAD phases: first press opens; duplicate/hold/release do not confirm; next numbered press selects; non-LOAD repeats unchanged");
 return 0;
}
