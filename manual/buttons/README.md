# Button glyphs

A picture of every control on the XDJ-XZ, so the manual, the website and the desktop app show a button the way it
looks on the player: its shape, cap colour, lamp colour and the name printed on or beside it. Wherever the manual or
the app names a button, it shows this glyph, with the name kept as text for screen readers, search and copying.

- `tools/xz_buttons.py` draws every glyph from one table, `GLYPHS`. Edit that, never the SVG files.
- `python tools/xz_buttons.py write` writes `ui/assets/buttons/` (the SVGs and `index.js`, which the app reads) and
  [`glyphs.md`](glyphs.md), the list of every glyph and the names it answers to.
- `python tools/build_manual.py check` fails when those files are not what `tools/xz_buttons.py` draws now, and when a
  chapter writes `[[NAME]]` for a name with no glyph.
- [`sheet.png`](sheet.png) shows every glyph as it is now. `python tools/xz_buttons.py sheet --out <png>` draws every glyph, lit and unlit, at text size and large. Add
  `--photo <top-panel photo>` to put the reference photograph's crop beside each one; keep that sheet out of the repo.
- `python tools/xz_buttons.py firmware` writes [`firmware-spec.md`](firmware-spec.md) and its previews, for a later
  firmware pass that draws the same glyphs on the XZ's own MODS screens.

## The rule

**Every place that names a control of the XDJ-XZ shows its glyph.**

- In a chapter, write `[[NAME]]` with the name printed on the player: `[[SHIFT]]`, `[[PLAY/PAUSE]]`, `[[CH 3 INPUT]]`.
  [`glyphs.md`](glyphs.md) lists each glyph's name and the other spellings it accepts (`[[LOOP IN]]` draws IN/CUE).
  `[[NAME:off]]` draws the lamp off; by default a control with a lamp is drawn lit, because its colour is how a DJ
  finds it.
- In the app, write the same `[[NAME]]` in the text and pass it through `xzText()` (`ui/xz-keys.js`):
  `el('li', {}, ...xzText('Press [[SHIFT]] twice'))`. `xzKey('SHIFT')` is a single glyph. The answers in
  `ui/support-kb.js` use `[[NAME]]` too.
- A control that has no glyph yet gets one in `GLYPHS` (or an alias on the glyph it is), in the same change.
- No glyph is named after a brand or prints one. The media button directly above LINK prints a logo on the unit; its
  glyph is a plain cap drawn over a dimmed LINK, and it is written `[[BUTTON ABOVE LINK]]`. `check` fails on a brand
  name in `[[...]]` (`tools/check_trademarks.py`).
- Only buttons, keys, switches, pads and the rotary selector get glyphs. Ports (USB 1 and USB 2 as sockets), screens and
  on-screen buttons stay text.

## What a glyph shows

Each glyph comes in two layouts, both on a piece of the dark top panel so they read the same on any page:

- **In text** (`<id>.svg`, 32 units tall, shown 1.4em tall in a line and 1.7em in tables): the control, then the name
  printed for it on the panel, to its right so it stays legible at text size. On the unit most of these names are
  printed above the control. Controls whose cap carries a name too small to read at that size (CUE, SYNC, MASTER, TAP)
  also get the name beside them.
- **With the panel print** (`<id>.panel.svg`): the control with the print round it where the unit has it: above,
  below, beside, and the SHIFT functions in their grey boxes (IN ADJUST, LOOP CUTTER, CALL). For figures and
  reference sheets.

Controls with a lamp come lit and unlit (`.off`). Printed white text stays white when the lamp is off; text that is
part of the lamp (the PAD MODE buttons, VINYL, FX FREQUENCY, TAP) goes grey.

Every glyph has the same height, so a large cap (PLAY/PAUSE) is drawn smaller next to a small one (MASTER TEMPO)
than on the unit. Within one control the proportions follow the unit, except that print on a rectangular cap
(MENU, SHIFT, HOT CUE) is drawn about 1.3 times its size on the unit so it reads at text size. The pads and the four keys round the rotary
selector are drawn as their group, with the one a glyph names lit or light, because a single pad or key has nothing
printed on it.

Text is outlined from the firmware's own font, Barlow Semi Condensed (SIL Open Font License,
`source/xdj-xz-toolkit/mods/ui/fonts/OFL.txt`), slightly emboldened like the panel print. The glyphs need no font and
look the same in every browser.

## Sources

- The maker's product photograph of the XDJ-XZ top panel (2000 x 1333) and close-ups of SYNC, MASTER, MASTER TEMPO,
  SHIFT, PLAY/PAUSE and CUE, kept outside the repo (`E:/Claude/xz-beta-rc-scratch/button-ref/`). The glyphs are drawn
  from them; the photographs are not copied into the product. `GLYPHS` records where each control is in the top-panel
  photograph (`photo`), for the comparison sheet.
- The true-scale Blender model of the XZ and the drawings in the player's operating instructions it is built from (pads 23 x 23 mm, CUE and
  PLAY/PAUSE rings 32 mm, CH3/CH4 input selectors 17.0 x 7.8 mm).
- Lamp colours the firmware sets: `source/xdj-xz-toolkit/mods/ui/native_led.h`. BEAT JUMP pads use
  `xz_jump_page2_color` (red outer pads, amber inner). The BEAT LOOP and SLIP LOOP pad colours there are provisional
  until the pad probe captures the stock values; when they change, change `GLYPHS` with them.

## Where glyphs are used

- The manual, on the website and in the app: every `[[NAME]]` in `manual/chapters/` and `manual/manual.json`.
- The app's Manual page: the "Keys on the player" note on the start page.
- The app's USB setup: the "On the XZ" steps after writing a USB, and the four-deck steps and checks.
- The app's report wizard: the "Is this it?" answers (`ui/support-kb.js`).
