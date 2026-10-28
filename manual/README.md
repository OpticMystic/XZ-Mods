# The XZ Mods manual

The user manual published at xzmods.com/manual. It lives beside the firmware source so that a change to
the interface and the change to its documentation land in the same commit.

The screens in the manual are not hand-made screenshots. `tools/build_manual.py render` draws them with
the firmware's own display code (`mods/shell/preview.py` for MODERN, `mods/ui/preview.py` for the MODS
menu and CLASSIC), using fixture data. When the interface changes, the pictures change with it, and
`check` fails until somebody has read the affected chapter again.

## The rule

**Any change to what a player sees or presses updates this manual in the same change.** That covers
screens, MODS pages, settings, button and pad behaviour, notices, and the desktop app.
`tools/build_manual.py check` enforces the part a machine can see:

| `check` fails when | Fix |
|---|---|
| A screen a figure shows changed | Read the figure's text and callouts against the new screen (`proof`), fix them, then `accept` |
| A screen a figure shows no longer renders | Point the figure at the screen that replaced it |
| A new screen renders that no chapter shows | Show it in the chapter it belongs to, or add it to `coverage.json` with a reason |
| A MODS page has no figure | Show it, usually in `mods-menu.md` or the chapter of its feature |
| A setting in `settings-schema.json` is not listed under `settings:` in any chapter | Explain it and list it in that chapter's front matter |
| A watched behaviour file changed (`coverage.json` `watch`) | Re-read the chapter named in the entry, fix it, then `accept` |

`accept` only records that a person reviewed the chapter. Never run it to make `check` pass without
reading the figures it lists. Releases run `check` and refuse to package a stale manual.

## Commands

```
python tools/build_manual.py render --zig <zig.exe> --out <render-dir>   # a few minutes; never touches an XZ
python tools/build_manual.py check  --render <render-dir>
python tools/build_manual.py proof  --render <render-dir> --out <dir>     # every figure with its callouts drawn on
python tools/build_manual.py accept --render <render-dir> [shot or path ...]
python tools/build_manual.py site   --render <render-dir> --out <dir> [--draft] [--base /manual/]
python tools/build_manual.py app    --render <render-dir> [--draft] [--check]  # the copy inside the desktop app
```

`site` refuses to build while `check` fails; `--draft` builds anyway for local review. Never publish a draft.

The desktop app shows this manual on its Manual page from `ui/manual/`, which `app` writes from the same chapters
and screens: one script per chapter, a search index, and every picture as a lossless WebP named by its source's hash.
Never edit `ui/manual/` by hand. After you change a chapter or accept new screens, run `app` and commit `ui/manual/`
with the chapter. `build_manual.py gate` (run by `tools/build_release.py`) fails when `ui/manual/` is a draft or does
not match what `app` makes from the source being released. `node tools/verify_manual_tab.mjs` checks the page.
The monorepo's `tools/sync-xz-manual.mjs` copies a built site into `website/xz-manual/` for xzmods.com.

## Files

- `manual.json`: chapter order, groups, titles and one-line summaries.
- `chapters/<slug>.md`: one chapter each.
- `coverage.json`: screens deliberately left out (each with a reason), settings deliberately left out, and
  behaviour files whose changes must trigger a review (each naming its chapter).
- `shots.lock.json`: written by `accept`. The screen and file hashes the chapters were last reviewed against.
- `app-shots/`, `captures/`, `hardware/`: curated pictures (desktop app screenshots, photos of the player's
  screen, the top-panel drawing). Watch the sources they depict in `coverage.json`. The top-panel
  drawings come from `tools/draw_xz_panel.py manual/hardware`; edit that, not the SVGs.
- `template/`: the page, its stylesheet and script.

## Writing a chapter

Front matter, then a controlled Markdown subset:

```
---
interface: both            # both | modern | classic | computer
settings: stems, stem_layout   # every settings-schema key this chapter explains
---
```

- `## Heading` and `### Heading`. Add ` {modern}` or ` {classic}` to a heading when that section only
  applies to one interface; readers who chose the other interface do not see it.
- Paragraphs, `-` lists, `1.` lists (drawn as numbered steps), `| tables |`, `**bold**`, `*italic*`, `` `code` ``.
- `[[MENU]]` draws a hardware key. Use the name printed on the player.
- `{experimental}` and `{new}` draw a small tag.
- Links: `[text](stems)` to a chapter, `[text](stems#bypass)` to a section, or a full https URL.
- Notes: a line starting `> **Tip.**`, `> **Note.**`, `> **Caution.**` or `> **Experimental.**`.

Figures put the screen first and explain it with numbered callouts:

````
```figure classic:theme-0-home MODS · HOME
The HOME page with stems ready and four decks not connected yet.
1 @ 30,20 BACK | Goes up one level. On HOME it closes MODS.
2 @ 62,215 Section rail | Every section of MODS, top to bottom.
```
````

The fence line is the shot and an optional label for the frame. Lines `N @ x,y Name | What it does` are
callouts, in the picture's own pixels (800 x 480 for the player's screen). Put the marker beside the thing
it names, not on top of its text. Every other line is the caption. Add `{modern}` or `{classic}` after the
label to hide the figure from readers of the other interface.

Shot ids: `modern:<name>` (render/shell), `classic:<name>` (render/ui; MODS pages are
`classic:theme-<n>-<page>`), `app:<name>`, `capture:<name>`, `hw:<name>` (files in the folders above).

The MODS menu is drawn in FLAGSHIP (theme 24) under MODERN and in the chosen classic theme under CLASSIC.
Write a MODS page as `classic:theme-{look}-<page>`: the figure then shows theme 24 to MODERN readers and
theme 0 to CLASSIC readers, with the same callouts. Both pictures are checked.

Several variants of one screen go in a gallery, one shot and caption per line:

````
```gallery {modern}
modern:stems-layout-below-full Below the waveform, full height
modern:stems-layout-outside-half Outside the waveform, half height
```
````

Animated explainers draw how a feature works, step by step, in the app's Manual page. The website shows the
text around them and skips the explainer itself, so never put a fact only in an explainer.

````
```explainer beat-jump Beat jump, page 1 and page 2
Page 1 | 1 2 4 8 | Press [[BEAT JUMP]]. With the default sizes the pairs jump 1, 2, 4 and 8 beats.
Page 2 | 16 32 64 128 | shift | Hold [[SHIFT]], press [[BEAT JUMP]], let go of [[SHIFT]].
```
````

The fence names the explainer and its title. Each line is one scene: its title, any data the picture uses, then
the caption. `four-deck-setup` takes no lines: it steps through the numbered list that follows it. The pictures
live in `ui/manual-art.js` (`loopback-signal`, `loopback-cable`, `four-deck-setup`, `sync-master`, `beat-jump`,
`three-band`, `ram-usb`, `usb-diary`). They draw ports, deck order, labels and colours from the firmware; when a
watched source of that chapter changes, check its explainer as well as its figures.

Tables generated from source keep themselves current. Use them instead of copying the facts:

- ```` ```generated menu-key ```` (with its closing fence on the next line): what a short press of MENU does on each screen, from `ui/menu_key.c`.
- ```` ```generated themes ````: every theme's HOME page.
- The settings reference chapter appends the full table from `settings-schema.json`.

## Voice

Write for a DJ standing at the player. Say what a control does and when you would use it, in short
declarative sentences, second person, present tense. Name controls exactly as the screen prints them. Say
plainly when something is experimental or not available on decks 3 and 4. Do not describe internals,
file formats or build history unless the reader has to act on them.
