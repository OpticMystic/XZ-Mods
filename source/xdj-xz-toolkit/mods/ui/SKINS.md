# Pioneer play-screen themes

The primary target is the native Pioneer play screen. Themes 21 (LCARS), 22
(Matrix), and 23 (Analog) replace native artwork, theme the live native text,
and style the inline stem controls. Existing theme IDs 0 through 20 keep their
meanings. Select a theme in MODS > Appearance, then close MODS.

## Native screen

`native_main_theme.h` renders replacement artwork for exact XZ 1.26 asset IDs
and dimensions. `native_asset_theme.h` applies it while preparing a private
asset pack. The existing owner-thread snapshot swap installs the pack and
requests a native repaint. Changing to Original restores the original path.
No firmware or original GUI pack is modified.

| Region | Assets | LCARS | Matrix | Analog |
|---|---|---|---|---|
| Deck instrument panels, 400x172 | 649, 687 | Amber/violet swept elbows and protected readout wells | Terminal brackets, dotted rules | Brushed metal perimeter and dark display wells |
| Track headers, 400x30 | Even 650-666, 688-704 | Solid bands with black native title text | Phosphor header rule | Recessed glass label window |
| Compact deck panels, 400x70 | 725, 726 | Rounded frame | Corner brackets | Metal bevel |
| Deck selectors, 120x41 | 1026-1028 | Filled DECK capsules with connected source/key brackets | Terminal frame | Metal key with inset label |
| Beat FX header, 120x31 | 969 | Capsule and condensed lettering | Pixel lettering and brackets | Recessed label |
| Unloaded central panel, 560x92 | 1487 | Quiet XZ MODS wordmark | Terminal wordmark | Transport faceplate wordmark |

Native text, time counters, tempo, master/quantize indicators, scrolling waves,
summary waves, cue marks, and touch hit areas remain owned by Pioneer. Their
numeric positions and hit areas do not move. LCARS shifts the small source/key
icons within the left column and the Beat FX artwork within its existing native
window. Unclassified artwork retains its original transparency mask. Unclassified assets retain the existing color-map path. Matrix
also uses the guarded native pixel-glyph adapter where that adapter supports
the native font encoding. Other encodings retain their original glyphs.

This is a native artwork replacement, not an independent replacement DJ UI.
The current mod model does not expose enough native metadata to reconstruct
every play-screen state independently. Do not rebuild the screen from a
retained framebuffer: partial redraws can capture already themed pixels.

## MODS and inline controls

The secondary MODS pages use independent layout/render modules. `ui.c` owns
control identity, availability, selection and actions; each skin places and
draws those controls. The selector shows all 24 themes. Existing stock touch
ownership and gesture capture remain shared.

- LCARS uses sidebar navigation, swept panels, capsules and explicit state caps.
- Matrix uses a terminal form, inverse selections, segmented level bars and
  deterministic glyph trails restricted to the margins.
- Analog uses a metal faceplate, lamp keys, switches and a tuner-style X-PAD.
  Its receiver needle displays reported FPS only; it is not an audio meter.

Inline controls retain the native two-row geometry. Labels, state words and
stored gain values avoid the runtime waveform band. Muting suppresses the
level bar but retains the stored percentage. Active grooves have steady
selection feedback without depending on an unset blink field.

## Verification

Run `python mods/ui/verify.py --zig <zig>` for portable tests. The main-screen
test checks structural changes beyond the LUT, exact asset gates, preservation
of source/key/stride bytes, and dark wells under counters and summary waves.
The UI tests check every theme/page for control parity, nonoverlap, minimum
hit sizes and press/release behavior. Skin-state tests cover readiness, active
grooves with blink=0, muted gain, original IDs and deterministic rendering.

`python mods/ui/preview.py --zig <zig> --output <new-directory>` renders the
MODS and inline fixtures, including active, unavailable and external-deck
states. These images are not hardware screenshots.

`python mods/build.py --zig <zig> --output <directory>` builds the native ARM
pair and checks the XZ ABI. Device smoke tests load the exact pair in isolated
processes before a RAM trial. Live screenshots, source tests, package parity,
and playback acceptance are separate evidence layers.

## LCARS reference implementation

`native_lcars_assets.h` owns exact numeric/status sprite IDs and dimensions.
It replaces glyphs and their source-key masks together. The authoritative
native sprite choice still supplies each digit, quantize value and state.
Owned descriptors explicitly enable magenta source transparency, preventing a
new glyph's transparent pixels from erasing a neighboring digit. Unknown
assets, formats and ambiguous key contexts keep the stock path.

The qualified 1.26 DrawText hook at0x15a62c and the existing WString hook use
black ink on filled track-title bands. Body data for the second deck uses
violet. The core-to-deck cache is bounded, resets across theme snapshots, and
invalidates at native window destruction. The FillRectangle hook at0x1594d8
adds the left brackets only after a full clear of the verified128x295 column.
It does not repaint a retained full-screen capture. The20 allowed ARM hooks
all retain exact prologue checks.

`native_lcars_wave.h` builds two immutable RGB565 maps with each LCARS snapshot.
Live coloring is a row-selected table lookup. Cue bands keep their existing
mapping; the two native yellow playhead pixels at x268/269 become white only
where native pixels already exist. Sample positions and wave geometry do not
change. Scalar/lookup equivalence and guard rows are regression tested.

The native amber/violet palette follows the supplied concept. The existing
MODS menu palette and layout are retained. Loaded track titles and playback
still require physical visual acceptance, separate from pure tests, isolated
ARM loading, and an unloaded-screen capture.
