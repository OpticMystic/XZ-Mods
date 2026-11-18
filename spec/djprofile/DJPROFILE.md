# DJ Profile 1.0

Status: **draft 3** (2026-10-19). Open for comment. The format string is
`djprofile/1`. Once published it will not change in a way that breaks a
conforming reader.

Licence: the text of this document is CC BY 4.0. The schemas, examples and the
checker in this folder are CC0 1.0. See [LICENSE.md](LICENSE.md). Anyone may
implement DJ Profile, in free or commercial software or hardware, with no fee,
no registration and no permission.

The key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in
RFC 2119. Sections marked *informative* explain; they set no rules.

## 1. What it is

A DJ Profile is a DJ's name and logo, plus optional tagline, colour and links.
It lives on the DJ's USB stick or SD card, next to their music. Whatever the
DJ plugs the stick into can show who is playing: a player, a mixer, a lighting
desk, VJ software, a stream overlay. Nothing is hosted anywhere. No account is
needed.

Design goals, in priority order:

1. **A folder and a logo is a profile.** A DJ with no special software makes a
   folder named after themselves and drops a logo in it.
2. **It never interferes.** DJ Profile writers never touch the stick's DJ
   libraries (`PIONEER/`, `Engine Library/`, `_Serato_` or any other). Players
   that know nothing about DJ Profile behave exactly as before.
3. **It reaches players that do not share files.** A profile track (section 9)
   carries the profile through the DJ software's own export, so a network
   reader that can see track metadata can see the profile.
4. **Small enough for firmware.** Fixed paths, flat JSON, baseline images,
   every limit in bytes.

Out of scope: hosting, accounts, payments, analytics, and anything private.
Everything on the stick can be read by anyone on the same DJ network.

## 2. Terms

| Term | Meaning |
|---|---|
| stick | a removable volume: USB stick, SD card, external drive |
| profile folder | one folder inside `DJPROFILE/` holding one profile (section 3) |
| profile document | a profile folder's `profile.json` (section 5) |
| stick file | the optional `DJPROFILE/DJPROFILE.json` (section 6) |
| active profile | the profile the stick says is playing now (section 6) |
| profile track | a silent audio file whose tags carry the profile (section 9) |
| name key | the form names are compared and sorted in (section 3.1) |
| reader | software or firmware that shows profiles |
| writer | software or firmware that creates or edits them |

## 3. Layout

```
<stick root>/
  DJPROFILE/
    DJPROFILE.json                 stick file, optional (section 6)
    DJ Kai/                        a profile folder; the name is the DJ's name
      logo.png                     level 0: this alone is a profile
      profile.json                 level 1: the profile document (section 5)
      profile.previous.json        the document it replaced, recommended
      logo-light.png               any other file the document lists
      DJ PROFILE - DJ Kai.mp3      a copy of the profile track (section 9)
    Lena B/
      logo.jpg
  PIONEER/                         a DJ library; DJ Profile never writes here
  Contents/                        music
```

Rules:

- The top folder is `DJPROFILE` at the root of the volume. Writers MUST create
  it with exactly that name. Readers MUST also accept `DJPROFILES`. When both
  exist, a reader reads `DJPROFILE` first, and a profile folder whose name key
  is in both counts once, from `DJPROFILE`.
- Every fixed name in this specification (`DJPROFILE`, `DJPROFILES`,
  `DJPROFILE.json`, `profile.json`, `profile.previous.json`, `logo.png`,
  `logo.jpg`, `logo.jpeg`) is matched with ASCII case folding:
  `A` to `Z` equal `a` to `z`, every other character must match exactly.
- Readers MUST skip every file and folder whose name starts with `.` at every
  level. That covers temporary files and the `._` files macOS writes on FAT
  sticks.
- Every other directory directly inside the top folder is a candidate profile
  folder. Readers MUST ignore symbolic links and anything that is not a
  directory.
- A candidate is a profile only if it has a usable logo (section 4 or 5).
  A stick shows at most 16 profiles: the first 16 profiles in profile order
  (section 6), counted after candidates without a logo are dropped.
- A profile folder name is at most 100 UTF-16 code units and 160 UTF-8 bytes,
  is not blank, contains no control characters (U+0000 to U+001F,
  U+007F to U+009F) and none of `< > : " / \ | ? *`, and does not end in a space
  or a dot. These rules keep it valid on FAT32 (with long file names), exFAT
  and HFS+, and short enough to survive inside DJ software's export paths.
- Files a profile lists sit inside its profile folder, at most one subfolder
  deep. Readers MUST NOT follow a path out of the profile folder.
- Readers need long file name support on FAT. `DJPROFILE` is nine characters.

### 3.1 Name keys

Names are compared and sorted by their **name key**: the name converted to
Unicode Normalization Form C, then with ASCII case folding. Sort order is the
byte order of the name keys' UTF-8 encoding. macOS writes names in NFD; the
key makes `Beyoncé` written on a Mac equal `Beyoncé` typed on a player.

## 4. Level 0: a folder and a logo

A profile folder with no usable `profile.json` is a level 0 profile.

- Its name is the folder name.
- Its logo candidates are the PNG and JPEG files directly inside the folder
  (by extension `.png`, `.jpg`, `.jpeg`), in this order: `logo.png`,
  `logo.jpg`, `logo.jpeg`, then the rest by name key. The logo is the first
  candidate that passes the image rules of section 5.2. If none passes, the
  folder is not a profile.
- It has no id, tagline or links. Its colour is the stick colour (section 11).

## 5. Level 1: `profile.json`

UTF-8 JSON, no byte-order mark, at most 64 KiB, nested at most 8 levels, no
duplicate member names, member names in ASCII, and no numbers other than whole
numbers between -2^53 and 2^53 (write anything else as a string). Schema:
[`schema/profile.schema.json`](schema/profile.schema.json).

```json
{
  "format": "djprofile/1",
  "id": "5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21",
  "revision": "2026-10-19T15-02-11Z-3f1c",
  "name": "DJ Kai",
  "tagline": "House and garage, Leeds",
  "accent": "#ffa064",
  "aliases": ["Kai"],
  "logos": [
    { "file": "logo.png", "bytes": 48213, "sha256": "…64 hex…", "role": "primary", "background": "dark" },
    { "file": "logo-light.png", "bytes": 51002, "sha256": "…", "role": "alternate", "background": "light" }
  ],
  "links": [
    { "kind": "website", "url": "https://djkai.example" },
    { "kind": "instagram", "url": "https://instagram.com/djkai.example" }
  ],
  "track": { "file": "DJ PROFILE - DJ Kai.mp3", "bytes": 168044, "sha256": "…", "marker": "djprofile:1:5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21:3c9a0b7e11d2" }
}
```

All text limits are in UTF-8 bytes.

| Member | Required | Rule |
|---|---|---|
| `format` | yes | `"djprofile/1"`. A reader that finds any other value treats the folder as level 0 |
| `id` | yes | 1 to 128 bytes from `A-Z a-z 0-9 _ -`. Chosen once, at random, when the profile is created, and kept for life on every stick. Writers SHOULD use a lowercase UUID. It is the DJ's identity |
| `revision` | yes | 1 to 64 printable ASCII characters. Changes on every edit. Readers compare it for equality only |
| `name` | yes | 1 to 160 bytes, not blank, no control characters |
| `tagline` | no | at most 240 bytes |
| `accent` | no | `#rrggbb`. Without it the profile shows in the stick colour |
| `aliases` | no | up to 8 other names, each 1 to 160 bytes, for exact name matching |
| `logos` | no | 1 to 8 entries (section 5.1). Without it, the level 0 logo rule applies |
| `links` | no | up to 12 `{kind, url}` (section 5.3) |
| `track` | no | the profile track record (section 9) |
| `note` | no | plain text, at most 8192 bytes |
| `renditions` | no | up to 8 device-specific images (section 5.4) |
| `ext` | no | vendor extensions (section 10) |

If `profile.json` cannot be read or breaks a MUST of this section, a reader
tries `profile.previous.json`, then treats the folder as level 0.

**Duplicate ids.** If two profile folders on one stick hold the same `id`, the
first in profile order is the profile and the later ones are skipped.

### 5.1 Logos

A logo entry is `{file, bytes, sha256, role, background, label}`.

- `file` is a path relative to the profile folder, `/` separators, at most one
  subfolder, no `.` or `..` segment, no leading `/`, no drive letter, no
  backslash, no segment starting with `.`.
- `bytes` is the exact size. `sha256` is the file's SHA-256 in 64 hex digits.
- `role` is `primary` or `alternate`. Exactly one entry is `primary`, and its
  file is `logo.png` or `logo.jpg` at the top of the profile folder, so a
  level 0 reader shows the same logo.
- `background` is `dark`, `light` or `any` and says what the logo was drawn to
  sit on. Default `any`.
- `label` is at most 160 bytes. `note` is at most 1024 bytes.
- All logo files together are at most 24 MiB.

### 5.2 Images

A logo is a PNG or JPEG of 8 bytes to 8 MiB, at most 4096 pixels a side and
4 megapixels, so it decodes in 16 MiB of RGBA.

- Writers SHOULD write baseline JPEG or 8-bit non-interlaced PNG. Readers MUST
  read those and MAY reject progressive JPEG, interlaced PNG and 16-bit PNG.
  A rejected image is treated like a missing one.
- Readers ignore EXIF orientation. Writers store pixels upright.
- Transparent PNG is allowed. Readers draw it over the logo's `background`
  colour: black for `dark` and `any`, white for `light`.

### 5.3 Links

`url` starts with `https://` and is at most 2048 bytes. `kind` is 1 to 40
bytes. Readers MUST accept any `kind`. These have fixed meanings: `website`,
`page` (the DJ's own profile page), `booking`, `media-kit`, `files`,
`instagram`, `tiktok`, `youtube`, `soundcloud`, `mixcloud`, `bandcamp`,
`beatport`, `spotify`, `link`.

### 5.4 Renditions

Optional pre-scaled copies of the primary logo for one device family, so a
small device does not have to decode a large image. Each is
`{file, bytes, sha256, width, height, format, for}`, at most 4 MiB.

- `format` names the pixel layout: `png`, `jpeg`, or a raw layout such as
  `rgb565le` (row-major, little-endian, no header). For a raw layout, `bytes`
  equals `width * height * bytes per pixel`.
- `for` names the device family, free text up to 80 bytes.
- Readers MUST ignore renditions whose `format` or `for` they do not know, and
  renditions whose `sha256` does not match. A rendition never replaces the
  logo.

### 5.5 Unknown members

Readers MUST ignore members they do not know. Writers MUST keep members they do
not know when they rewrite a document, at every level, including inside `ext`,
logo entries and links.

## 6. The stick file, profile order and the active profile

`DJPROFILE/DJPROFILE.json` is optional, UTF-8 JSON under the same rules as
`profile.json`, at most 16 KiB. Schema:
[`schema/stick.schema.json`](schema/stick.schema.json).

```json
{
  "format": "djprofile-stick/1",
  "revision": "2026-10-19T15-02-11Z-3f1c",
  "active": "5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21",
  "profiles": [
    { "folder": "DJ Kai", "id": "5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21" },
    { "folder": "Lena B" }
  ]
}
```

`profiles` lists profile folders in the order the DJ wants them shown, with
each one's `id` when it has a `profile.json`. It is at most 16 entries. It
lets a reader that cannot list folders, such as a network reader with only
fixed-path file access, open `DJPROFILE/<folder>/profile.json` directly.

A **profile reference** is a profile's `id` when it has one, and its folder
name otherwise. `active` is a profile reference. A reader resolves a reference
by matching it against `id`s exactly first, then against folder name keys.
Writers write the `id` whenever the profile has one.

- An entry of `profiles` resolves through its `id` if it has one, else through
  its `folder`. Entries that do not resolve are skipped. If a folder was
  renamed by hand, a reader that can list folders still finds it through its
  `id`.
- **Profile order** is the profiles named in `profiles`, in that order, then
  the rest by folder name key.
- **The active profile** is the one `active` resolves to, else the first in
  profile order.
- A device that lets the DJ switch profiles rewrites only `active` and
  `revision`, keeps every other member, and replaces the file as in section 7.
- Writers that add, rename or remove a profile folder keep `profiles` in step,
  and set `active` only when there was none.

**Who is playing.** On a DJ network, the current DJ is the active profile of
the stick that the master deck's track was loaded from. A reader that cannot
see the master deck uses the on-air deck.

## 7. Writing

1. Write every new or changed asset first. Give a changed asset other than the
   primary logo a new file name, for example `logo-light-3c9a0b7e.png`, so a
   reader halfway through never pairs a new document with an old file. The
   primary logo is replaced in place (section 5.1); readers catch the brief
   mismatch through its `sha256` (section 13).
2. If `profile.json` exists, copy it to `profile.previous.json`.
3. Write the new `profile.json` to a temporary file in the same folder whose
   name starts with `.`, flush it, rename it over the old one, and flush the
   folder.
4. Replace `DJPROFILE.json` last, the same way, if it changes.
5. Then delete the files the previous document listed that the new one does
   not. Delete nothing else.

When the DJ name changes, a writer SHOULD rename the profile folder to match,
so level 0 readers show the new name, then update `DJPROFILE.json`.

FAT has no atomic rename. If the stick is pulled mid-write, readers recover
through `profile.previous.json` and level 0 (section 5), and ignore the
dot-named temporary file.

Writers MUST NOT write anywhere outside `DJPROFILE/` on the stick, and MUST NOT
store a profile in a device's internal memory. A profile belongs to the stick.

## 8. Conformance levels

| Level | The stick has | A reader at this level |
|---|---|---|
| 0 | a profile folder with a logo | lists profile folders, shows name and logo |
| 1 | a valid `profile.json` | also reads the document, `DJPROFILE.json` and the active profile |
| 2 | the profile track in a DJ library on the stick | also finds profiles from track metadata |

[`tools/check_djprofile.py`](tools/check_djprofile.py) checks a stick (or a
folder laid out like one) and reports each profile's level and every rule it
breaks. It finds level 2 by looking for the marker in `export.pdb`. It cannot
open OneLibrary `exportLibrary.db`, which is encrypted. The folders in
[`examples/`](examples/) are known-good sticks.

## 9. The profile track

Status: **provisional.** The parts of this section that depend on how DJ
software exports a track (which tag frames survive, how artwork is resized)
have not been measured yet. The open checks are listed at the end of the
section. A reader can rely on title, artist, comment and cover art today.

Some players refuse to share arbitrary files on the network, but every player
shares its library's tracks. The profile track is a short silent song the DJ
adds to their DJ software and exports like any other track. Its title,
artist, comment and cover art then appear in each library the software writes
to the stick (`export.pdb`, OneLibrary `exportLibrary.db` or others), and in
network queries for track metadata.

**The trade-off.** The profile track sits in the DJ's collection, so it can
turn up in search, history and auto-play. If it is played on air it is silence,
which is still dead air. Keep it short and in its own playlist. It is optional:
levels 0 and 1 work without it.

**Audio.** MPEG-1 Layer III, 44.1 kHz, constant 128 kbit/s, stereo or joint
stereo, 10 to 60 seconds; 10 is recommended. Every decoded sample MUST be zero.
Writers MUST decode their output and check that. The profile is never encoded
in the audio.

**Tags.** One ID3v2.4 tag at the start of the file. Every text frame uses
encoding 3 (UTF-8) and has no terminating zero byte.

| Frame | Value | Seen by |
|---|---|---|
| `TIT2` title | the DJ name | every library and network peer |
| `TPE1` artist, `TPE2` album artist | the DJ name | every library and network peer |
| `TALB` album | `DJ PROFILE` | every library and network peer |
| `TCON` genre | `DJ Profile` | every library and network peer |
| `COMM` comment, language `eng`, empty description, exactly one | the marker, then optionally one space and one `https://` URL | every library and network peer |
| `TPUB` label | the tagline, optional | most libraries; some peers |
| `APIC` type 3, front cover, exactly one | the primary logo as a square baseline JPEG, 240 to 800 pixels a side, padded on its background colour (section 5.2) | every library; peers get it at 80 or 240 px |
| `APIC` type 0x13, band logotype | full-resolution logos, optional | the audio file only |
| `TXXX` description `DJPROFILE` | the profile document without its `track` member, as canonical JSON (below) | the audio file only |
| `WOAR`, `WXXX` | links, optional | the audio file only |

**The marker** names the profile and the revision the track was made from:

```
djprofile:1:<id>:<hash12>
```

`<id>` is the profile's `id`. `<hash12>` is the first 12 lowercase hex digits
of the SHA-256 of the canonical JSON of the profile document without its
`track` member.

**Canonical JSON** is RFC 8785 (JSON Canonicalization Scheme). Because section
5 restricts member names to ASCII and numbers to whole numbers, it is exactly
what most JSON libraries write with sorted keys, compact separators and no
ASCII escaping, for example Python's `json.dumps(value, sort_keys=True,
separators=(',', ':'), ensure_ascii=False)`. The hash is computed over the
parsed document, so tag rewriting by DJ software does not change it.

- A reader that has the current `profile.json` recomputes the hash. If it
  differs from the marker, the track was made before the last edit. The reader
  still shows what the track carries, and a writer SHOULD offer to make a new
  track.
- **Finding the marker.** A track is a profile track when its comment contains
  the token `djprofile:1:<id>:<hash12>`, at the start or after a space. Some
  tools prepend text to comments, so readers MUST NOT require it at the start.

**File name.** `DJ PROFILE - <name>.mp3`, with characters invalid on FAT32
removed.

**Getting it onto the stick.** The writer saves the track to the DJ's computer.
The DJ imports that copy (never the stick copy, which DJ software would then
retag in place) into their DJ software, puts it in a top-level playlist named
`DJ PROFILE`, and exports or syncs the playlist to the stick. A writer MAY also
keep a copy in the profile folder (`track` in the document) so readers that
read files get it directly.

Writers MUST NOT insert the profile track into a DJ library on the stick
themselves. DJ software owns those files. A conversion or sync can rewrite them
without warning and drop what it did not create, and an insert into one library
but not another leaves the stick inconsistent. Export tools that build the
library themselves MAY add the profile track as an ordinary track during
export.

**Which libraries get it** depends on the DJ software (informative). rekordbox
7 writes `export.pdb` and, once enabled, OneLibrary. Software that writes only
OneLibrary leaves players that read only `export.pdb` without the track.

**Reading it.** Readers SHOULD look in a top-level playlist named `DJ PROFILE`
(name key comparison) before scanning all tracks. Over a network, a reader
that can browse a player's playlists lists the root, opens `DJ PROFILE`, and
reads each track's title, comment and artwork without loading it to a deck.

**The active DJ over the track path.** Track metadata tells a reader which DJs
the stick carries, not which one is active. If `DJ PROFILE` holds one profile
track, that DJ is the stick's DJ. If it holds several, a reader SHOULD take the
first in the playlist and show it as a guess. DJs who share a stick put the
usual DJ first.

**Open checks before this section becomes final:**

1. Export a profile track through rekordbox 7 to a FAT32 stick. Compare the
   source and stick copies, and confirm title, comment, label and cover art in
   both libraries.
2. Repeat after editing the track in rekordbox and syncing again.
3. Browse `DJ PROFILE` over the network on a CDJ-3000 and an XDJ-XZ without
   loading the track, and fetch the 240 px cover art.
4. Repeat on a OneLibrary player, and record which path answers.

## 10. Extensions

Vendor data goes in `ext`, keyed by a name the vendor controls (a domain or a
project name):

```json
"ext": { "example.org": { "visual_pack": "visuals/pack.json" } }
```

An extension MUST NOT change the meaning of a standard member. Files an
extension names follow the path rules of section 5.1. A reader that does not
know an extension ignores it. A writer keeps it (section 5.5).

Proposals for new standard members go to the issue tracker of the repository
that hosts this document. A new member is optional, so adding one never breaks
an existing reader.

## 11. Stick colour

DJ software lets the DJ give each stick a colour, and players paint the stick's
name with it. A profile with no `accent` shows in that colour. A writer that
creates a profile on a device SHOULD store the stick colour as `accent`.

Values 1 to 8 are Pink `#f870f8`, Red `#f87070`, Orange `#ffa064`, Yellow
`#f8e550`, Green `#1ee12b`, Aqua `#10e4dc`, Blue `#1e50f0`, Purple `#9828f0`.
Any other value means no colour.

Where to find the value (informative, not yet confirmed against the DJ
software's own colour names):

- In `PIONEER/rekordbox/export.pdb`, byte +9 of the `djdbProperty` row (table
  type 19) whose present bit is set. Older rows can remain in the heap, so the
  first row is not always the live one.
- In OneLibrary `exportLibrary.db`, the `property` table's
  `backGroundColorType` column, whose numbering has not been checked.

## 12. Profiles stored before 1.0

Software that kept profiles somewhere else on the stick before 1.0 MAY go on
reading that store, and then follows three rules. It maps what it reads into
the model of section 5, keeping each DJ's `id`. On its next write it moves
those profiles into profile folders, so the stick holds only the 1.0 layout.
Its old store never outranks a profile folder with the same `id`.

## 13. Reading safely

Everything on a stick is untrusted input.

- Check each file's size before reading it, and enforce every limit before
  parsing.
- Reject paths that break section 5.1.
- Decode images with a bounded decoder and the limits of section 5.2.
- Show text as text. Never render it as HTML or Markdown.
- Never open a link without a person choosing it.
- Read a profile once per stick mount and cache it by `revision`. On a network,
  that keeps booth traffic low.
- If a logo's SHA-256 does not match, read `profile.json` again once. If it
  still does not match, show the profile without that logo.

## 14. Reading over a DJ network

A network reader tries, in order:

1. **Files**, where the player shares them (on Pro DJ Link players, the NFS
   export of the stick). Read `DJPROFILE/DJPROFILE.json`, then
   `DJPROFILE/<folder>/profile.json` for each entry of its `profiles`. Only
   profile folders it does not list, such as hand-made level 0 ones, need a
   directory listing. Player makers have said file sharing is being restricted
   for security, so this path may close on some firmware.
2. **The profile track through the player's database server.** Browse the
   `DJ PROFILE` playlist and read title, comment and artwork.
3. **The profile track through the stick's library file**, on players with no
   database server: fetch the library the same way the player shares it with
   other players (`PIONEER/rekordbox/export.pdb`, or
   `PIONEER/rekordbox/exportLibrary.db` together with its `-wal` file) and
   find the marker there.

## 15. Versioning

New members in `djprofile/1` are optional, so a 1.0 reader keeps working.
A change that would make a 1.0 reader misread a stick gets a new format string
and a new file name (for example `profile.v2.json`), and writers keep writing
`profile.json` beside it until readers catch up. A reader that finds a
`format` it does not know treats the folder as level 0.

## 16. How this relates to OneLibrary

See [ONELIBRARY.md](ONELIBRARY.md).
