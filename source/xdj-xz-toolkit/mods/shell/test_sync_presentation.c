#ifdef NDEBUG
#error SYNC presentation requires active assertions
#endif
#include "four_deck_fixtures.h"
#include <assert.h>
#include <stdio.h>
static uint16_t frame[800*480],reference[800*480];
int main(void){
 static struct fx_track t;fx_track_build(&t,7);
 struct xz_shell_model m;fx_model_four(&m,&t);struct xz_shell u;xz_shell_init(&u);
 static const struct {int status;const char *text;} rows[]={
  {XZ_SHELL_SYNC_OFF,"SYNC"},{XZ_SHELL_SYNC_LOCKED,"SYNC 1"},{XZ_SHELL_SYNC_WAITING,"SYNC"},
  {XZ_SHELL_SYNC_TEMPO_ONLY,"BPM 1"},{XZ_SHELL_SYNC_MASTER,""},{XZ_SHELL_SYNC_RATE_LIMIT,"LIMIT"},
  {XZ_SHELL_SYNC_ENABLED,"SYNC 1"},{XZ_SHELL_SYNC_EXTERNAL_MASTER,"EXT MASTER"},{XZ_SHELL_SYNC_NO_BPM,"NO BPM"},{999,"SYNC"}};
 for(int deck=0;deck<4;deck++){
  struct xz_shell_deck *d=&m.deck[deck];d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC)|XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC_STATUS);
  d->sync=1;d->master=0;d->sync_master_deck=-1;d->sync_status=XZ_SHELL_SYNC_ENABLED;
  assert(xz_shell_render(&u,&m,reference,800));
  d->sync_master_deck=0;
  for(unsigned i=0;i<sizeof rows/sizeof *rows;i++){
   d->sync_status=rows[i].status;
   const char *label=xz_shell_sync_label(d->sync_status,d->sync_master_deck);
   assert(!strcmp(label,rows[i].text)&&!strstr(label,"WAIT"));
   assert(xz_shell_render(&u,&m,frame,800));
   if(rows[i].status==XZ_SHELL_SYNC_WAITING||rows[i].status==999||rows[i].status==XZ_SHELL_SYNC_OFF)
    assert(!memcmp(frame,reference,sizeof frame));
  }
 }
 puts("PASS all shell SYNC labels and actual legacy/unknown enabled rendering on decks 1-4");
 return 0;
}
