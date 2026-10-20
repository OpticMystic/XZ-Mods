"""FLAGSHIP shell renderer source closure. The drawing primitives come from ../ui."""
SHELL_SOURCES = ['shell.c', 'shell_draw.c', 'shell_play.c', 'shell_wave.c', 'shell_browse.c', 'shell_settings.c', 'shell_info.c', 'shell_source.c', 'shell_menu.c', '../ui/ui_draw.c']
SHELL_HEADERS = ['shell.h', 'shell_internal.h', 'shell_help.h', 'shell_font.h', 'fixtures.h', '../ui/ui_skin.h', '../ui/theme_flagship.h', '../ui/font_atlas.h']
# The MODS panel (../ui) for the MENU tab tests and previews: its sources but the drawing primitives above.
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent.parent / 'ui'))
from ui_sources import UI_SOURCES as _UI
MENU_SOURCES = ['../ui/' + name for name in _UI if name != 'ui_draw.c']
