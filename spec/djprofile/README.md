# DJ Profile

An open, free standard for putting a DJ's name and logo on their USB stick, so
whatever the stick is plugged into can show who is playing.

- **For DJs.** Make a folder called `DJPROFILE` on your stick. Inside it, make a
  folder with your DJ name. Put your logo in it as `logo.png` or `logo.jpg`.
  That's it.
- **For players, mixers, VJ software, lighting desks and overlays.** List the
  folders in `DJPROFILE/` and show the logo. Read `profile.json` if it is
  there for the tagline, colour and links.
- **For mod authors and DJ software.** Write `profile.json` and the optional
  silent profile track, and never touch anything else on the stick.

Status: **draft 2, 2026-10-19.** Levels 0 and 1 are ready to implement. The
profile track (level 2) is provisional until the export checks in section 9
of the specification have been run. Comments and proposals are welcome as
issues.

## Files

| Path | What it is |
|---|---|
| [DJPROFILE.md](DJPROFILE.md) | the specification |
| [ONELIBRARY.md](ONELIBRARY.md) | how DJ Profile sits beside OneLibrary, and a proposal for a future OneLibrary |
| [schema/profile.schema.json](schema/profile.schema.json) | JSON Schema for `profile.json` |
| [schema/stick.schema.json](schema/stick.schema.json) | JSON Schema for `DJPROFILE.json` |
| [examples/](examples/) | known-good sticks: `level0`, `level1`, and `level1-track` (level 1 with a profile track copy) |
| [tools/check_djprofile.py](tools/check_djprofile.py) | checks a stick and reports each profile's level and every rule it breaks |
| [tools/make_examples.py](tools/make_examples.py) | regenerates `examples/` |
| [LICENSE.md](LICENSE.md) | CC BY 4.0 for the text, CC0 for schemas, examples and tools |

Run the checker on a stick with `python tools/check_djprofile.py E:\`, and its
tests with `python -m unittest discover -s tools -v`.

## A level 0 reader in a few lines

```python
import unicodedata
from pathlib import Path

def key(name):  # section 3.1: NFC, then ASCII case folding
    return ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in unicodedata.normalize('NFC', name))

def visible(folder):
    return [p for p in folder.iterdir() if not p.name.startswith('.') and not p.is_symlink()]

def profiles(stick: Path):
    tops = sorted((p for p in visible(stick) if p.is_dir() and key(p.name) in ('djprofile', 'djprofiles')),
                  key=lambda p: key(p.name) != 'djprofile')
    folders = {}
    for top in tops:
        for folder in visible(top):
            if folder.is_dir() and key(folder.name) != 'vjtools':
                folders.setdefault(key(folder.name), folder)
    found = []
    for name_key in sorted(folders, key=lambda k: k.encode('utf-8')):
        folder = folders[name_key]
        images = [p for p in visible(folder) if p.is_file() and key(p.suffix) in ('.png', '.jpg', '.jpeg')]
        first = ('logo.png', 'logo.jpg', 'logo.jpeg')
        images.sort(key=lambda p: (first.index(key(p.name)) if key(p.name) in first else 3, key(p.name).encode()))
        if images:  # a real reader decodes each in turn and takes the first that works
            found.append({'name': folder.name, 'logo': images[0]})
    return found[:16]
```

A level 1 reader adds `profile.json`, `DJPROFILE.json`, the active profile, the
legacy stores and the size, path and image limits of the specification.
`tools/check_djprofile.py` does all of that and is public domain, so it can be
copied.

## Implementations

| Project | Reads | Writes |
|---|---|---|
| XZ Mods (desktop app and XDJ-XZ mods) | legacy layout; 1.0 in progress | legacy layout; 1.0 in progress |
| VJ.Tools Wingman and Library | legacy layout | legacy layout |

To add yours, open an issue or a pull request.
