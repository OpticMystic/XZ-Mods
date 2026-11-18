# DJ Profile and OneLibrary

OneLibrary is the newer DJ library format on USB sticks, made by AlphaTheta
with Algoriddim and Native Instruments. rekordbox 7 and djay write it. The
CDJ-3000X, XDJ-AZ, OPUS-QUAD, OMNIS-DUO and newer players read it. It is
`PIONEER/rekordbox/exportLibrary.db`, an encrypted SQLite (SQLCipher 4)
database, written beside the older `export.pdb`, not instead of it.

No official OneLibrary specification is published. What follows rests on the
community specification at
[v1vendi/onelibrary](https://github.com/v1vendi/onelibrary/tree/master/spec)
and on reading real exports.

## Living side by side today

DJ Profile was shaped so that a OneLibrary stick needs nothing new:

- `DJPROFILE/` sits at the stick root, outside `PIONEER/`. OneLibrary
  software does not look there, and DJ Profile writers never write into
  `PIONEER/`.
- The profile track (DJ Profile section 9) is an ordinary MP3. When the DJ
  exports it, it lands in each library the DJ software writes, like any song:
  title, artist, comment (`djComment` in OneLibrary) and cover art (`image`).
  rekordbox 7 writes `export.pdb` and, once enabled, OneLibrary. Software that
  writes only OneLibrary reaches OneLibrary players but not players that read
  only `export.pdb`, such as the XDJ-XZ.
- Nothing is added to the OneLibrary database by DJ Profile tools. rekordbox's
  "Convert" overwrites an existing `exportLibrary.db`, and a sync can replace a
  playlist the DJ edited on the stick, so anything inserted behind the DJ
  software's back can vanish without notice. The export is the only safe way
  in.

A reader that finds a profile through OneLibrary looks for `content` rows
whose `djComment` contains the `djprofile:1:` marker, preferably among the members of a
root `playlist` (`playlist_id_parent = 0`, `attribute = 0`) named
`DJ PROFILE`. The cover art path is `image.path` for the row's `image_id`.
Readers must apply `exportLibrary.db-wal` before reading: rekordbox leaves most
of a fresh export in the WAL file, and the main file alone looks almost empty.

## A proposal for a future OneLibrary

If the OneLibrary partners want DJ identity in the format itself, these are
the smallest steps, in order. Each one keeps every current stick and player
working.

1. **Reserve the folder.** State that `DJPROFILE/` at the stick root belongs
   to the DJ, that library software leaves it alone during export, conversion
   and formatting tools' "clean up", and that players may list it.
2. **Read the profile on the player.** A player that finds `DJPROFILE/` (or the
   profile track) shows the active DJ's name and logo while the stick is
   loaded, in the place it shows the stick name today.
3. **Name it in the database.** A one-row table, for example
   `djProfile(path varchar, revision varchar)`, pointing at the active profile
   folder, would let network peers that only fetch `exportLibrary.db` find the
   profile without listing folders. Until such a table exists, the profile
   track does that job.
4. **Carry it over the network.** Players already answer metadata queries for
   tracks. Answering one more query with the active profile document, or
   simply serving `DJPROFILE/` alongside the library files after any network
   security fix, keeps VJ software, lighting desks and stream overlays able to
   credit the DJ.

DJ Profile is free to adopt in whole or in part, with no permission needed
(see [LICENSE.md](LICENSE.md)). If a future OneLibrary specification covers DJ
identity, DJ Profile will follow it and keep reading what is on sticks today.
