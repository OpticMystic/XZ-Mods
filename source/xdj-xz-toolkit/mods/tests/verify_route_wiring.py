"""Execute the actual additive route dispatcher cases and model publications from both runtimes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xz-route-wiring-') as temporary:
    temporary = Path(temporary)
    # The UI model mirrors struct xz_extra_route_status as model.route (route_refresh_locked); the shell model
    # keeps its flat route_* fields. RESCAN refreshes the UI copy at once, so its case calls route_refresh_locked.
    for area, source_name, refresh_name, refresh_end, field, model_type, action_type, action, following in (
        ('ui', 'ui_runtime.c', 'static void route_refresh_locked(void){', 'model.route.ready_revision=s.ready_revision;',
         'model.route.', 'xz_ui_model', 'xz_ui_action_kind', 'XZ_UI_ROUTE_RESCAN', 'XZ_UI_PC_SETUP'),
        ('shell', 'shell_runtime.c', 'static void extra_refresh_locked(uint32_t now) {',
         'model.route_ready_revision=route_status.ready_revision;', 'model.route_', 'xz_shell_model',
         'xz_shell_action_kind', 'XZ_SHELL_ACT_ROUTE_RESCAN', 'XZ_SHELL_ACT_DECK_LAYOUT'),
    ):
        source = (ROOT / source_name).read_text()
        case = source[source.index('case ' + action + ':'):source.index('case ' + following + ':')]
        refresh = source[source.index(refresh_name) + len(refresh_name):]
        refresh = refresh[:refresh.index(refresh_end) + len(refresh_end)]
        text = '''#include "extra_decks.h"
#include "ui/ui.h"
#include "shell/shell.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Route wiring requires live assertions
#endif
unsigned calls;
static int dirty_flag;
struct xz_extra_route_status fixture;
_Static_assert(XZ_UI_ROUTE_RESCAN==XZ_UI_HOME_SETUP+1,"XZ_UI_ROUTE_RESCAN is the last UI action");
_Static_assert(XZ_SHELL_ACT_ROUTE_RESCAN==XZ_SHELL_ACT_DECK_LAYOUT+1,"append shell actions");
'''
        text += f'static struct {model_type} model;\n'
        # Keep weak-symbol existence checks from the production snippets.
        text += '#pragma weak xz_extra_decks_rescan\n#pragma weak xz_extra_decks_route_status\n'
        text += 'static void route_refresh_locked(void){' + refresh + '}\n'
        text += 'static void refresh(void){route_refresh_locked();}\n'
        text += f'static void act(enum {action_type} kind){{switch(kind){{{case} default:break;}}}}\n'
        text += f'''int main(void){{
 for(int state=XZ_EXTRA_ROUTE_NO_ENGINE;state<=XZ_EXTRA_ROUTE_LOST;state++){{
  fixture=(struct xz_extra_route_status){{.state=state,.fail_part=XZ_EXTRA_ROUTE_PART_SOCKET,
   .checked_at_ms=1234,.revision=72,.ready_revision=41}};
  strcpy(fixture.reason,"The connection is being checked.");
  refresh();assert((int){field}state==state&&(int){field}fail_part==XZ_EXTRA_ROUTE_PART_SOCKET);
  assert(!strcmp({field}reason,fixture.reason));
  assert({field}checked_at_ms==1234&&{field}revision==72&&{field}ready_revision==41);
  act({action});assert(calls==(unsigned)state+1u);
 }}
 (void)dirty_flag;puts("PASS {area} route fields, all states and RESCAN dispatcher case");return 0;
}}
'''
        test = temporary / (area + '.c')
        test.write_text(text)
        stubs = temporary / (area + '-stubs.c')
        stubs.write_text('#include "extra_decks.h"\nextern unsigned calls;\nextern struct xz_extra_route_status fixture;\n'
                         'void xz_extra_decks_rescan(void){calls++;}\n'
                         'struct xz_extra_route_status xz_extra_decks_route_status(void){return fixture;}\n')
        binary = temporary / area
        subprocess.run(['gcc', '-std=c11', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-I' + str(ROOT), str(test), str(stubs), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
