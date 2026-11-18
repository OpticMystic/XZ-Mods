# Working in XZ-Mods

## The manual follows the interface

The user manual at xzmods.com/manual is built from `manual/` in this repo. Its screens are drawn by the
firmware's own preview renderers, so it can be checked against the source.

**When you change anything a player sees or presses, update the manual in the same change.** That means
screens, MODS pages, settings in `settings-schema.json`, button and pad behaviour, notices, and the desktop
app UI. New screens need a preview fixture (`mods/shell/preview*.c`, `mods/ui/preview.c`), which the manual
then has to show or explicitly exclude.

```
python tools/build_manual.py render --zig <zig.exe> --out <render-dir>
python tools/build_manual.py check  --render <render-dir>
python tools/build_manual.py proof  --render <render-dir> --out <dir>   # look at every figure you touched
python tools/build_manual.py accept --render <render-dir> <shot or path ...>
```

- Fix every `check` line your change causes before you finish. Edit the chapter in `manual/chapters/`,
  look at its proof sheets, then `accept` only the items you reviewed.
- Never run `accept` without reading the figures it records. It is the record that a person checked them.
- `tools/build_release.py` runs `build_manual.py gate` and refuses to release a stale manual. Do not bypass it.
- A new behaviour that no screen shows (a button combination, a pad mode) goes into the chapter that covers
  it, and its source file into `manual/coverage.json` `watch`.
- After a release, the owner publishes the manual with the site; see `manual/README.md`.

Authoring syntax, voice and file layout: `manual/README.md`.
