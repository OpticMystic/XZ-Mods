---
interface: both
settings: browse_preview, browse_1, browse_2, browse_3, browse_key_prefix, browse_traffic
---

This chapter covers finding a track and putting it on a deck. MODERN has its own browse screens, built for touch and the rotary encoder. CLASSIC uses the player's own browse screens and adds the key, colours for compatible keys and a few labels to them.

## Browsing in MODERN {modern}

Press [[BROWSE]], [[TAG LIST]], [[PLAYLIST]] or [[SEARCH]] to open that list. Press [[BACK]] to go up one level. At the top of a list [[BACK]] opens SOURCE, where you pick the USB to browse.

```figure modern:browse-touched-load BROWSE · TRACK {modern}
The TRACK category of USB1, sorted by title, with PREVIEW on and a row touched.
1 @ 64,46 Source | The USB you are browsing, in its own colour. Tap to open SOURCE.
2 @ 63,140 Category rail | ARTIST, ALBUM, TRACK, KEY, BPM, GENRE, RATING, COLOR, PLAYLIST, FOLDER, HISTORY and MATCHING. Tap one, or drag the rail to see the rest.
3 @ 88,40 BACK | Goes up one level.
4 @ 563,40 PREVIEW | Shows or hides the preview waveform column.
5 @ 635,40 INFO | Opens the INFO panel for the highlighted track.
6 @ 366,61 Column title | The arrow marks the column the list is sorted by. The number in brackets is how many items the list holds.
7 @ 120,245 Preview | A small waveform of each track, so you can see breaks and builds before you load.
8 @ 230,131 Played | A green note marks a track you have played.
9 @ 630,212 On a deck | A blue number says the track is loaded on that deck.
10 @ 600,306 LOAD | Appears on the row you touched. Tap 1 or 2 to load the track on that deck.
11 @ 785,306 Key colour | Green or amber means the key mixes with the master deck's key.
12 @ 110,418 Deck strip | Both decks' time, overview, tempo, key and BPM stay in view while you browse.
```

### Moving through a list {modern}

| Control | What it does |
|---|---|
| Turn the encoder | Moves the highlight one row |
| Push the encoder | Opens a folder. On a track, opens a choice of decks with the active deck highlighted, so a second push loads it there |
| Hold the encoder | Turns letter jump on. Turning then jumps by first letter, or by page where the list has no index. Push or hold again to stop |
| Drag or flick the list | Scrolls |
| Tap a row | Highlights it and shows LOAD |
| [[BACK]] | Up one level. At the top, opens SOURCE |
| [[MENU]] | Opens the list's MENU panel: sort, columns and the track menu |
| [[INFO]] | Opens or closes the INFO panel |

```gallery {modern}
modern:browse-full PREVIEW off: the PREVIEW OFF tile and the full width for the columns
modern:browse-preview-on PREVIEW on
modern:browse-jump-letter Letter jump: the letter shows large while you turn
modern:browse-jump-page Page jump in a list with no letter index
modern:browse-deep The highlight near the end of a long list
modern:browse-window-gap Rows still on their way from the USB stay blank for a moment
modern:browse-folders The PLAYLIST category: folders and playlists
modern:browse-rail-scrolled The category rail dragged down to HISTORY and MATCHING
modern:browse-long-strings Long names are cut to fit their column
modern:browse-unknown The first moment after opening, before the list has been read
modern:screen-browse BROWSE before the USB's library has been read: LIST NOT READ YET
modern:screen-playlist PLAYLIST before it has been read
modern:screen-tag-list TAG LIST before it has been read
modern:screen-search SEARCH before the library has been read
modern:browse-deck2-not-loaded Deck 2 empty in the deck strip
```

The preview column starts hidden. One tap on **PREVIEW** shows it, and XZ Mods remembers your choice on the USB (`browse_preview`). PREVIEW is touch only.

### Loading a track {modern}

There are three ways to load the highlighted track:

- Tap the row, then tap **1** or **2** on its LOAD chip.
- Push the encoder twice to load on the active deck, or push, turn to another deck and push.
- Press the [[LOAD]] button of the deck you want.

If no track is highlighted, [[LOAD]] says **SELECT A TRACK, THEN PRESS LOAD**. On a screen that is not a track list it says **OPEN A TRACK LIST TO LOAD**. To load onto decks 3 and 4, see [Four decks](four-decks).

```gallery {modern}
modern:browse-touched-load-deck2 The same row with deck 2 active: its 2 is highlighted
modern:browse-touched-load-off Loading not available on this list: the row highlights without LOAD
modern:browse-touched-load-off-notice SELECT A TRACK, THEN PRESS LOAD
```

### INFO panel {modern}

Press [[INFO]] or tap **INFO** to open a panel with the highlighted track's details beside the list. The list narrows to make room.

```gallery {modern}
modern:browse-info The INFO panel: BPM, KEY, TIME, RATING, COLOR, PLAYED and ON DECK
modern:r5-browse-info-panel The INFO panel opened with [[INFO]] while keys are colour coded
modern:r5-browse-traffic-keys Keys coloured against deck 1's Fm
```

### Sort, columns and the track menu {modern}

Press [[MENU]] or tap a column title. The MENU panel opens on the right with the sort order, the columns to show, and the actions for the highlighted track.

```figure modern:parity-track-menu BROWSE · MENU {modern}
The MENU panel on a track list, with three tracks already in the tag list.
1 @ 600,100 TRACK | Actions for the highlighted track.
2 @ 700,130 ADD TO TAG LIST | Adds the track to the tag list. On a track already in it this reads REMOVE FROM TAG LIST.
3 @ 690,164 CUE/LOOP LIST | Not on the player in this version.
4 @ 690,198 MARK PLAYED | Marks the track played. A played track offers MARK UNPLAYED.
5 @ 650,266 SEARCH | Opens SEARCH.
6 @ 395,150 Tag marks | Tracks in the tag list carry a check mark.
7 @ 776,40 Close | Closes the panel. [[MENU]] and [[BACK]] close it too.
```

```gallery {modern}
modern:r5-browse-menu-sort Sort by: one choice at a time
modern:r5-browse-menu-columns COLUMNS: tick the columns the list shows
modern:browse-preview-off-columns Six columns with PREVIEW off, sorted by BPM, highest first
modern:browse-preview-off-color-time COLOR and TIME columns
modern:parity-tag-marks Check marks on tracks in the tag list
modern:parity-history-menu HISTORY: ADD ALL TO TAG LIST, DELETE HISTORY and DELETE ALL HISTORIES
```

### Filter {modern}

Tap **FILTER** to narrow the list without leaving it. The filters stay on while you move between categories, and a chip in the header says which are on.

| Filter | What it keeps |
|---|---|
| BPM | Tracks within a range of the master deck's BPM, for example MASTER ±6 |
| MATCH MASTER KEY | Tracks whose key mixes with the master deck's key |
| AT LEAST | Tracks rated at least that many stars |
| COLOR | Tracks with any of the colours you tick |
| MY TAG | Not available yet |

**CLEAR ALL FILTERS** in the panel, or a tap on the chip, turns every filter off. Filters that compare with the master deck need one. Without it you see **NO MASTER DECK: BPM FILTER OFF** or **NO MASTER KEY: KEY FILTER OFF**.

```gallery {modern}
modern:r5-browse-filter-panel TRACK FILTER with BPM, key, rating and colour filters on
modern:r5-browse-filter-chip The filter chip in the header. Tap it to clear every filter
```

### Search {modern}

Press [[SEARCH]]. Type on the keyboard and the list updates as you type. **SEARCH** puts the keyboard away so you can scroll the results. **DELETE** removes a letter and **CLEAR** empties the field.

```figure modern:browse-search-typing SEARCH {modern}
Typing NEON. Eleven tracks match so far.
1 @ 500,18 Search field | What you have typed.
2 @ 225,61 Count | How many tracks match.
3 @ 720,326 SEARCH | Puts the keyboard away and moves to the results.
4 @ 726,232 DELETE | Removes the last letter.
5 @ 400,410 Key | The key you touched is outlined.
```

```gallery {modern}
modern:browse-search-empty SEARCH before you type
modern:browse-search-results The results with the keyboard put away
```

### Tag list {modern}

The tag list is a quick list of tracks you want to play next. It holds up to 100 tracks and shows how many it has in the header. XZ Mods keeps it on the USB in `XZMODS/playlists.txt`, with your playlist changes. To add a track, use ADD TO TAG LIST in the [[MENU]] panel. To add a whole folder, highlight it and use ADD ALL TO TAG LIST.

```gallery {modern}
modern:browse-tag-list The tag list with 12 of 100 tracks, one row touched
modern:browse-tag-list-full The tag list full at 100 tracks
modern:parity-tag-list-menu MENU in the tag list: REMOVE FROM TAG LIST, CREATE PLAYLIST and REMOVE ALL TRACKS
modern:parity-tag-to-playlist CREATE PLAYLIST: the tag list saved as a new playlist
modern:parity-tag-clear-armed REMOVE ALL TRACKS armed: push again to clear the tag list
```

### Playlists {modern}

Press [[PLAYLIST]] to open the USB's playlists. You can also make your own and change them on the player. Press [[MENU]] in the playlist list or inside a playlist.

| In the MENU panel | What it does |
|---|---|
| NEW PLAYLIST | Makes a playlist in the folder that is open. Type its name on the keyboard |
| RENAME | Renames the highlighted playlist |
| DELETE | Deletes the highlighted playlist. Push again to confirm |
| ADD TO PLAYLIST | Adds the highlighted track to a playlist you pick |
| REMOVE FROM PLAYLIST | Takes the highlighted track out of the playlist |
| MOVE TRACK | Picks up a track. Turn the encoder to move it, push to drop it |

Playlists, the tag list, played marks and histories can change only on a USB that is not read-only. Changes made on the player are saved on the same USB in `XZMODS/playlists.txt`. They never change your rekordbox export, so rekordbox and the CLASSIC browse screens do not show them. A playlist you changed on the player is marked in the list. In HISTORY the [[MENU]] panel offers DELETE HISTORY and DELETE ALL HISTORIES, each after a second push. If the USB is read-only you see **THIS STICK IS READ-ONLY: PLAYLISTS CANNOT CHANGE**. If it is full you see **THE STICK IS FULL: PLAYLIST NOT SAVED**.

```figure modern:r5-playlist-menu PLAYLIST · MENU {modern}
The playlist list with its MENU panel open and DELETE highlighted.
1 @ 612,302 PLAYLIST | Actions for the playlist list.
2 @ 690,334 NEW PLAYLIST | Makes a playlist here.
3 @ 640,368 RENAME | Renames the highlighted playlist.
4 @ 650,402 DELETE | Deletes it after a second push.
5 @ 545,180 Sort and columns | The same sort and column choices as in BROWSE.
```

```gallery {modern}
modern:browse-playlist A playlist's tracks
modern:browse-playlist-folders Playlist folders
modern:r5-playlist-delete-armed DELETE armed: push again to delete
modern:r5-playlist-new-name Naming a new playlist on the keyboard
modern:r5-playlist-add-to ADD TO PLAYLIST: pick the playlist
modern:r5-playlist-move-track A track picked up to move
modern:r5-playlist-edited-marks A playlist changed on the player, marked in the list
```

## SOURCE {modern}

SOURCE shows every place music can come from, one tile each. Press [[BACK]] at the top of a list, or tap the source tile at the top left of BROWSE.

```figure modern:source-full SOURCE {modern}
USB1 is being browsed and plays on both decks. USB2 is still loading.
1 @ 95,68 BROWSING | The source BROWSE shows now.
2 @ 100,238 Name and colour | The USB's name and the colour saved on it.
3 @ 310,250 Status | READY, LOADING, NOT CONNECTED, or DECK'S OWN SCREEN where the player's own screen handles that source.
4 @ 190,298 FREE | Free space on the USB.
5 @ 120,404 Decks | The decks playing a track from this source.
6 @ 565,250 LINK | Players on LINK. Browsing LINK from MODERN is not available yet.
7 @ 690,250 COMPUTER | rekordbox on a computer. Browsing it from MODERN is not available yet.
8 @ 446,22 Hint | Opening a source opens its library in BROWSE.
```

Turn the encoder to pick a tile and push to open it, or tap it. A source the player's own screen handles opens that screen. Touch cannot open it yet; push the encoder instead.

```gallery {modern}
modern:source-cursor-computer The highlight on COMPUTER
modern:source-loading-current USB2 being browsed while it loads
modern:source-none-connected Nothing connected
modern:source-stock-link LINK handled by the player's own screen
modern:source-stock-export COMPUTER handled by the player's own screen
modern:source-long-strings Long device names cut to fit
modern:source-unknown Before the sources have been read
modern:screen-source SOURCE opened before anything has been read
modern:r5-source-via-back SOURCE reached with [[BACK]] from the top of BROWSE
modern:r5-source-usb2-missing USB2 asked for with nothing in it
modern:r5-browse-absent-usb2 BROWSE on a USB that has been removed: no old rows remain
```

## INFO {modern}

Press [[INFO]] on the play screen, or tap the INFO icon in the top bar, to see everything about a loaded track.

```figure modern:info-deck1-full INFO {modern}
INFO for deck 1's track.
1 @ 258,22 Deck tabs | Switch between the decks' tracks.
2 @ 610,22 Close | Back to where you were. [[INFO]] and [[BACK]] close it too.
3 @ 225,150 Artwork | The track's artwork, when the USB has it.
4 @ 222,275 RATING | The track's stars.
5 @ 395,128 Linked field | A field with an arrow opens BROWSE with every track that shares it. ARTIST, ALBUM, KEY, BPM and GENRE are linked.
6 @ 379,230 KEY and BPM | Tap to open the KEY or BPM category at this value.
7 @ 540,340 COMMENT | The comment saved in rekordbox.
8 @ 110,370 SOURCE | Where the track came from.
9 @ 200,436 Deck strip | Both decks stay in view.
```

```gallery {modern}
modern:info-deck2-long-title Deck 2's track with a long title
modern:info-focus-key The highlight on KEY, ready to push
modern:info-focus-genre The highlight on GENRE
modern:info-list-highlight INFO for the track highlighted in a list, in its own tab
modern:info-not-loaded A deck with no track
modern:info-no-browse INFO when browsing is not available: fields are not linked
modern:info-long-strings Long names cut to fit
modern:info-unknown Before the track has been read
modern:screen-info INFO opened before anything has been read
```

## Browsing in CLASSIC {classic}

CLASSIC keeps the player's own browse screens: the same lists, keys and menus the XDJ-XZ operating instructions describe. XZ Mods adds three things to them.

- **The key in every row.** Each track row shows up to three fields from your rekordbox export inside the title cell. The default is the title, then the key, then the BPM.
- **Key before title.** With **KEY BEFORE TITLE** on, the key leads the row, so you can read keys down the list.
- **Key match colours.** With **KEY MATCH COLOURS** on, a leading key is green when it mixes with the master deck's key, amber when it is detuned against it, and grey otherwise.

The table header names what the player's own [[MENU]] click changes: **SORT** over the title column and **COLUMN** over the right-hand column. The STEMS and MODS buttons stay at the top right.

These additions only use tracks on a USB with a rekordbox export. A track XZ Mods cannot match in the export keeps the player's own row, and a field the export does not have is left out.

## Browse settings

| Setting | MODS | What it does | Default |
|---|---|---|---|
| `browse_preview` | PREVIEW tile in MODERN browse | Shows the preview waveform column in MODERN browse | Hidden |
| `browse_traffic` | **SCREEN**, KEY MATCH COLOURS | Colours keys that mix with the master deck's key, in both interfaces | On |
| `browse_key_prefix` | **SCREEN**, KEY BEFORE TITLE | Puts the key first in each CLASSIC browse row | On |
| `browse_1`, `browse_2`, `browse_3` | none | The three fields in each CLASSIC browse row, in order. Each is TITLE, ARTIST, KEY or BPM | TITLE, KEY, BPM |

MODS has no control for the three CLASSIC row fields in this version. They keep the values stored on the USB. In MODERN, choose columns from the [[MENU]] panel instead.

## If browsing goes wrong

| You see | What to do |
|---|---|
| USB1 IS NOT CONNECTED or USB2 IS NOT CONNECTED | Insert the USB, or pick another source |
| USB SOURCE CHANGED: SELECT TRACK AGAIN | The USB changed while you chose. Highlight the track again |
| LOAD FROM THIS LIST IS OFF | Load from a track list instead |
| LINK BROWSING IS NOT AVAILABLE YET or COMPUTER BROWSING IS NOT AVAILABLE YET | Switch to CLASSIC to browse that source |
| TOUCH CANNOT OPEN THIS YET: PUSH THE ENCODER | Highlight the tile and push the encoder |
| PLAYLIST CHANGE NOT APPLIED | Try again. If the USB is read-only or full, the notice says so |
