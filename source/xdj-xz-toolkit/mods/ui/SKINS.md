# Pioneer play-screen themes

The primary target is the native Pioneer play screen. Themes 21 (LCARS), 22
(Matrix), 23 (Analog) and 24 (FLAGSHIP, see below) replace native artwork, theme the live native text,
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
draws those controls. The selector shows all 25 themes. Existing stock touch
ownership and gesture capture remain shared.

- LCARS uses sidebar navigation, swept panels, capsules and explicit state caps.
- Matrix uses a terminal form, inverse selections, segmented level bars and
  deterministic glyph trails restricted to the margins.
- Analog uses a metal faceplate, lamp keys, switches and a tuner-style X-PAD.
  Its receiver needle displays reported FPS only; it is not an audio meter.
- Flagship (`skin_flagship.c`) is the club-standard UTILITY / browse chrome at
  800x480: `#191919` rows with 1 px black separators, a `#3c3c3c` rail with the
  open section in `#646465`, `#007de1` selected rows and primary buttons,
  outlined ON/OFF chips, hatched `#313131` launch tiles, outlined caption boxes
  with big values, HOT CUE pad chips (outlined when ready, filled with a white
  edge when playing), a radio-circle theme list, and the overview waveform in
  blue with white caps on the STEMS and X-PAD deck header. The focus ring is
  white there because selection already owns blue. Stock-screen chips are
  white outlines that fill blue when lit; inline stem rows sit on black.

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

## FLAGSHIP (theme 24)

FLAGSHIP restyles the play screen toward the current club-standard flagship
players, within the same hooks: artwork assets, the palette LUT, the waveform
colour map and the text maps. Pioneer still owns geometry, text content,
numeric positions and touch. Measured colours and the reference layout are in
`docs/research/2026-10-18-cdj3000-xdj-az-ui.md` (sections 2, 4 and 5).

| Region | Assets | FLAGSHIP |
|---|---|---|
| Deck instrument panels, 400x172 | 649, 687 | Pure black, untagged title band with a 4 px grey bar, `#3c3c3c` hairlines between deck/track, time and tempo fields; time, overview, tempo well and cue gutters stay black |
| Track headers, 400x30 | Even 650-666, 688-704 | Black band, 4 px left bar in the strip's own LUT colour (the track colour), hairline below |
| Compact deck panels, 400x70 | 725, 726 | Black with the same title band |
| Deck selectors, 120x41 | 1026-1028 | Tag: 1027 solid `#007de1` with a 2 px white top edge, 1026 `#191919` with a white outline, 1028 dim with a `#3c3c3c` outline |
| DECK labels, 120x41 | 972-995 | White `DECK n` over the paired tag, transparent ground |
| Beat FX header and panel | 969, 970 | `#191919` panel, white `BEAT FX` caption, `#a4a4a4` sub-captions, `#3c3c3c` rules |
| Unloaded central panel, 560x92 | 1487 | Thin `#3c3c3c` rules around an `XZ MODS` wordmark in `#a4a4a4` |
| Numerals and marks | 734-848, 639, 727-733, 764, 815, 826, 837, 849-852 | Regenerated glyphs on a transparent ground |
| Captions and status | 640-647, 879-885, 887-902, 904-908, 955-959 | DECK caption and cue modes as 1 px outlined boxes; BPM box white outline, MASTER box and tag `#ff4619`; QUANTIZE `#ff4619` on, `#a4a4a4` off; tempo range white on a solid `#ff4619` box |
| Source and FX names | 996-1025, 1044-1071 | Source on a `#3c3c3c` plate; FX name on the panel chrome |
| Track-colour edges | Odd 651-667, 689-705 | Fully transparent, as LCARS |
| Scrolling and summary waves | 536x268, 300x34 | Three bands by dominant source channel: blue `#0055e1` (`#003fa8` when dim), warm `#ffa028`, green/white `#f5ebd7`, scaled by the strongest channel; the grey lane ground becomes black |

`native_flagship_assets.h` keys its sprite restyle by a table of (ID range,
width, height, kind) entries. The inventory is the LCARS inventory, and the
test asserts both lists agree for every ID. `xz_native_asset_owned` in
`native_asset_theme.h` is the one ownership gate the asset renderer and the
run-time image hook share; owned sprites use the same magenta mask descriptor
as LCARS. The LCARS deck tint, sidebar FillRectangle drawing and sprite
placement shifts remain LCARS only.

Digits, marks and status words take their ink from the stock sprite they
replace: a grey stock sprite stays `#a4a4a4`, a saturated warm one (the master
deck's BPM) becomes `#ff4619`, anything else is white. The sprite's own native
state therefore still shows. Track titles keep the stock black paper and white
ink through the LUT; FLAGSHIP adds no title override.

`native_flagship_wave.h` builds one 65536-entry RGB565 map per snapshot. The
lookup, the cue bands kept on the standard LUT (rows below 10 and from 122 of
each 136-row lane) and the white two-pixel playhead at x 268/269 follow the
LCARS wave. Scalar and lookup equivalence is regression tested.

Not reachable with these hooks: the MT and QUANTIZE outlined boxes (QUANTIZE
is a 88x10 text sprite and MT has no XZ asset), red playhead and beat-grid
colours outside the protected bands, the header-row layout and tile hatching.
Physical visual acceptance on the XZ is still required.
