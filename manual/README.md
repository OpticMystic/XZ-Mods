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
| A firmware source the menu map was captured from changed, or the map is missing | Crawl the menus again and import the tree (see [The menu map](#the-menu-map)) |
| A menu map screen points at a chapter or heading that does not exist | Fix the crawler's section for it, or the heading |

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
python tools/build_manual.py menu-tree --from <tree-dir>                    # import a menu map tree into manual/menu-tree/
```

`site` refuses to build while `check` fails; `--draft` builds anyway for local review. Never publish a draft.

The desktop app shows this manual on its Manual page from `ui/manual/`, which `app` writes from the same chapters
and screens: one script per chapter, a search index, and every picture as a lossless WebP named by its source's hash.
Never edit `ui/manual/` by hand. After you change a chapter or accept new screens, run `app` and commit `ui/manual/`
with the chapter. `build_manual.py gate` (run by `tools/build_release.py`) fails when `ui/manual/` is a draft or does
not match what `app` makes from the source being released. `node tools/verify_manual_tab.mjs` checks the page.
The monorepo's `tools/sync-xz-manual.mjs` copies a built site into `website/xz-manual/` for xzmods.com.

## The menu map

The app's Manual page has a **Menu map**: every screen the player can reach, laid out as a flow chart from the play
screen, each branch labelled with the control that opens it. It is not drawn by hand. Every screen on it is a frame the
firmware's own C drew in the offline capture harness, and the moves between them are the inputs that harness sent.

The map comes from one file, `manual/menu-tree/menu-tree.json` (format `xz-menu-tree/1`), and its screens,
`manual/menu-tree/nodes/<id>.webp`:

```
{ "format": "xz-menu-tree/1", "interfaces": ["modern", "classic"],
  "nodes": [{ "id", "interface", "title", "path": ["MODS", "PLAY", ...], "kind": "page|picker|keyboard|dialog|list|play|browse",
              "image": "nodes/<id>.webp", "focusables": [{ "label", "rect": [x, y, w, h] or null }], "chapter": "slug#anchor" or null }],
  "edges": [{ "from", "to", "control", "action": "turn+|turn-|push|press|hold|tap", "target", "rect": [x, y, w, h] or null }],
  "roots": [{ "id", "label" }],
  "sources": { "<repo path>": "<sha256 the screens were captured from>" } }
```

- `path` places a screen on the map: frames with the same interface and path are one screen, and its frames are its
  states (the highlight moved, a value changed). The map lays screens out breadth first from `roots`.
- `chapter` is the section of the manual that explains the screen. That chapter's heading gets **On the menu map**, and
  the screen's panel gets **Read in the manual**.
- `sources` lists the firmware sources the screens came from, with their hashes. `check` and the release `gate` fail when
  one of them changed or is gone: the screens may no longer be what the player shows. The tree is then captured again;
  nobody edits it by hand.

To put a new tree in, one command checks it (format, pictures, moves, chapter links) and replaces `manual/menu-tree/`:

```
python tools/build_manual.py menu-tree --from <folder with menu-tree.json and nodes/>
python tools/build_manual.py check --render <render-dir>
python tools/build_manual.py app --render <render-dir>        # writes ui/manual/map.js and ui/manual/map/
```

The tree in the repo now is **provisional**: `tools/build_menu_tree.py` made it from the navigation captures (one folder
per flow with `frame-NNNN.png`, `frames.json`, `timeline.json` and the capture's `manifest.json`):

```
python tools/build_menu_tree.py --captures <nav-capture-dir> --flows <flow specs: manual/nav/*.json> --out <tree-dir>
```

It joins identical frames into one node, joins each settled frame to the next by the control between them, and takes each
flow's manual section from its spec. The menu crawler writes the same format directly from every reachable screen;
when its tree lands, import it with `menu-tree --from` and commit `manual/menu-tree/` and `ui/manual/` together.

A chapter can open the map at a screen with a link to `menu-map#<screen>`, where `<screen>` is the screen's path
slugified: `[the BEAT JUMP PADS page](menu-map#mods-play-beat-jump-pads)`. `app` fails when no screen has that path. The
website has no map, so the site shows the link's words only. The desktop app also opens it with
`openManual('menu-map', '<screen>')` or `data-manual-open="menu-map#<screen>"`.

`node tools/verify_manual_tab.mjs` drives the map under the app's CSP: the tree, pan and zoom, keys, find, the screen
panel, links both ways, every state, CLASSIC, window sizes and reduced motion.

## Files

- `manual.json`: chapter order, groups, titles and one-line summaries.
- `chapters/<slug>.md`: one chapter each.
- `coverage.json`: screens deliberately left out (each with a reason), settings deliberately left out, and
  behaviour files whose changes must trigger a review (each naming its chapter).
- `shots.lock.json`: written by `accept`. The screen and file hashes the chapters were last reviewed against.
- `menu-tree/`: the menu map's tree and screens (see [The menu map](#the-menu-map)). Replace it with `menu-tree --from`.
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
- `[[MENU]]` draws that control of the XDJ-XZ as it looks on the player (its glyph from `tools/xz_buttons.py`), with
  its name kept as text for screen readers and search. Use the name printed on the player; `manual/buttons/glyphs.md`
  lists every glyph and the spellings it accepts. `[[SYNC:off]]` draws the lamp off. `check` fails on a name with no
  glyph: add the glyph or an alias. See `manual/buttons/README.md`.
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

### Pictures of the player: physically accurate only

Every picture or moving picture of the XDJ-XZ, and of what its screen shows, is a real render:

- **Hardware** (the player, its controls, ports, the loopback cable, a USB stick) is shown only by renders of the
  true-to-scale Blender model. The loopback-cable clip `ui/assets/wizard/loopback-cable.mp4` is the bar.
- **The player's screen** is shown only by the firmware's own preview renders: figure shots (`modern:`, `classic:`)
  or, inside a Blender clip, those same screens on the model.
- **A concept with no physical subject** (waveform layering, a file on a stick, a signal path drawn as boxes) gets no
  picture of its own. The chapter text carries it, and points at the firmware screen that shows it when one exists.
- No hand-drawn SVG or CSS drawing of the XZ, its controls, ports, sticks, screens or signal flow anywhere on the
  Manual page. `tools/verify_manual_tab.mjs` fails on one in an explainer, the start page or a tour tile.
- Known exception, still to replace: the `hw:` top-panel schematics in `hardware.md` (`manual/hardware/*.svg`). They
  go once the Blender model has top-down stills; move their callouts onto the stills then.

### Explainers: one rendered clip per scene

An explainer shows a feature in the app's Manual page as a short sequence of clips with a caption under each. The
website shows the text around it and skips the explainer, so never put a fact only in an explainer.

````
```explainer beat-jump Beat jump, page 1 and page 2
Page 1 | Press [[BEAT JUMP]]. With the default sizes the pairs jump 1, 2, 4 and 8 beats.
Page 2 | Hold [[SHIFT]], press [[BEAT JUMP]], let go of [[SHIFT]].
```
````

The fence names the explainer and its title. Each line is one scene, `Title | caption` (a single scene may be just a
caption). `four-deck-setup` takes no lines: it steps through the numbered list that follows it. Scene N plays clip N.
The kinds are `EXPLAINERS` in `tools/build_manual.py`: `four-deck-setup`, `loopback-cable`, `loopback-signal`,
`sync-master`, `beat-jump`, `ram-usb`. Add one only for a subject the Blender model can show.

The clips are rendered from the Blender model into a folder with a `manifest.json`:

```
{ "<kind>": { "w": 1280, "h": 480, "scenes": [ { "video": "<kind>/scene-1.mp4", "poster": "<kind>/scene-1.webp",
                                                 "seconds": 6.5, "loop": false, "screen": ... }, ... ] },
  "hero": { "w": 1280, "h": 480, "video": "hero/hero.mp4", "poster": "hero/hero.webp", "seconds": 12, "loop": true } }
```

`python tools/import_manual_media.py <render-folder>` checks it and copies it into the app: one clip per scene of the
chapter (it refuses another count), paths inside the folder, H.264 yuv420p MP4 of the stated size and length (checked
with ffprobe when it is on PATH), WebP posters. It writes `ui/assets/manual/<kind>/scene-<n>.mp4` and `.webp` and
`ui/assets/manual/media.js`, which the Manual page loads; `screen` is carried through as given. Re-run it whenever the
renders change, then commit `ui/assets/manual/`. `--out <dir>` stages a test copy instead; `--none` writes the
built-in clips only (the cable clip, and the cable render as the start page picture until a `hero` is rendered).

In the app, a scene shows its poster at once and plays its clip, muted, when the explainer is in view or the reader
picks the scene. A scene lasts as long as its clip or as long as its caption takes to read, whichever is longer.
With reduced motion nothing plays by itself: each scene shows its poster until the reader presses Play. An explainer
with no clips yet is hidden; its text is still in the chapter. `build_manual.py check` fails when a chapter's scene
count no longer matches its clips: render the scenes again or fix the chapter. When the clips change, check each one
against its scene lines and capture `app:manual` again (`coverage.json` watches `media.js`).

The start page's tour tiles are pictures too: each `manual.json` tour item names a `shot` a chapter figure already
shows (or `asset:<path>` under `ui/assets`, such as a poster of the model) and an optional `crop` `[x, y, w, h]` in
that picture's pixels. `app` writes them with the figures.

Tables generated from source keep themselves current. Use them instead of copying the facts:

- ```` ```generated menu-key ```` (with its closing fence on the next line): what a short press of MENU does on each screen, from `ui/menu_key.c`.
- ```` ```generated themes ````: every theme's HOME page.
- The settings reference chapter appends the full table from `settings-schema.json`.

## Voice

Write for a DJ standing at the player. Say what a control does and when you would use it, in short
declarative sentences, second person, present tense. Name controls exactly as the screen prints them. Say
plainly when something is experimental or not available on decks 3 and 4. Do not describe internals,
file formats or build history unless the reader has to act on them.
