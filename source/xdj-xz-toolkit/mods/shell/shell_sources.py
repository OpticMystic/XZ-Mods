"""FLAGSHIP shell renderer source closure. The drawing primitives come from ../ui."""
SHELL_SOURCES = ['shell.c', 'shell_draw.c', 'shell_play.c', 'shell_wave.c', 'shell_browse.c', 'shell_settings.c', 'shell_info.c', 'shell_source.c', 'shell_menu.c', '../ui/ui_draw.c', '../ui/layout_cycle.c', '../ui/deck_view.c', '../ui/route_card.c', '../ui/ui_about.c', '../ui/pictures.c']
SHELL_HEADERS = ['shell.h', 'shell_internal.h', 'shell_help.h', 'shell_font.h', 'fixtures.h', 'four_deck_fixtures.h', '../ui/ui_skin.h', '../ui/theme_flagship.h', '../ui/font_atlas.h', '../ui/layout_cycle.h', '../ui/deck_view.h', '../ui/route_card.h', '../ui/ui_about.h', '../ui/about_assets.h', '../ui/pictures.h', '../ui/picture_data.h']
# The MODS panel (../ui) for the MENU tab tests and previews: its sources but the drawing primitives above.
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent.parent / 'ui'))
from ui_sources import UI_SOURCES as _UI
MENU_SOURCES = ['../ui/' + name for name in _UI if '../ui/' + name not in SHELL_SOURCES]
