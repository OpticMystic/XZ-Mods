# Native text rendering investigation, 2026-10-28

The checked source is `source/xdj-xz-toolkit/mods/`. No device was accessed.

`ui/ui_draw.c` decodes UTF-8 but replaces every decoded value above 255 with `?`.
Both proportional and pixel rendering call this decoder. Removing the clamp alone
would index beyond `xz_font_glyphs[face][ch-32]`; it is not a Unicode fix.
Arabic also needs shaping and bidirectional layout, and CJK needs new glyph data.

`ui/NATIVE_STOCK_DIALOG.md` records NS_GetString at 0x182310 as a current-language
UTF-16 lookup. It records stock caution window ownership and guarded descriptor
reads, not a callable general-purpose text-rendering contract. The popup row ABI
is explicitly different from caution layout. There is insufficient evidence to
call a guessed stock drawing function with translated mod text safely. This
change therefore adds no firmware ABI calls.

A self-contained route would shape authored translations on the host using
HarfBuzz plus a bidi implementation, bake positioned glyph runs using licensed
Noto fonts, and render coverage masks through xz_plot. The runtime would select
pre-shaped runs by locale and stable authored string ID. Variable numbers need
separate directional runs. User track metadata must never enter the translation
catalog. This route requires licensed font assets, atlas bounds/overflow tests,
all-script host previews, build integration and hardware acceptance. None of
those renderer assets or runtime integration are implemented by this change.

Printed controls and exact stock alerts remain governed by xz_i18n.py. The
stock 18-value my_settings enum does not include Polish, Ukrainian or Arabic;
Portuguese is not evidence of a Brazilian regional variant. An independent mod
locale choice will need an explicit mapping and cannot overwrite stock enum
values with the desktop locale index.

Hardware acceptance remains pending. No firmware, internal storage, USB or
controller operation was performed.

## Follow-up inspection

The existing `xz_plot(struct xz_canvas, x, y, color, alpha)` in `ui/ui_draw.c`
clips coordinates to the canvas and blends 8-bit coverage into RGB565. Fixed
translated labels can therefore use pre-shaped coverage masks through this
existing renderer without calling a stock text ABI or extending the 224-glyph
font table. The host must finish bidi ordering, Arabic shaping and CJK glyph
selection before producing the mask. Runtime code would only select a label ID,
validate its rectangle and byte range, and paint coverage pixels.

A suitable atlas manifest must bind the locale, exact authored text hash,
font file hashes and redistribution licenses, shaping-library versions, glyph
coverage, width/height, baseline, byte offsets and atlas checksum. Reject a
missing glyph, clipped label or oversized rectangle during generation. Keep
variable numbers and metadata out of fixed label IDs. Printed control names and
exact stock alerts remain unchanged input runs. Preview every fixed label at
its real target size, including Arabic and both Chinese scripts, before runtime
integration. Measure atlas memory against the actual controller build; do not
assume that bundling all 14 locales fits its RAM budget.

`my_settings.h` still defines the 18 stock language values from 0x81 through
0x92. Desktop locale indexes cannot be written into that enum. A future atlas
selector needs its own append-only mod setting and an explicit fallback to
English, while stock language selection continues unchanged.

This follow-up only inspected source. It adds no fonts, masks, controller code,
ABI calls or hardware acceptance. Licensed font acquisition, atlas generation,
host visual proof, memory measurements and controller testing remain separate
work. They do not gate the independent desktop/manual corpus generation.
