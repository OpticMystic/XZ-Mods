// Seed knowledge base for "Is this it?" in the report wizard. Public-safe answers taken from the manual of this
// release (chapters named in each entry's source). Matching runs on this computer only; typed text never leaves it.
// [[NAME]] in an answer draws that XDJ-XZ button (ui/xz-keys.js). Actions are fixed IDs the app maps to its own setup screens; an entry can never name a command or a URL.
'use strict';
window.XZ_SUPPORT_KB = Object.freeze({
  schema: 'xz-support-kb/1',
  revision: 1,
  release: '0.5.0-preview.22.6',
  entries: [
    {
      entry_id: 'four-decks-cable-setup', revision: 1, kind: 'NOT_A_BUG', component: 'four_decks',
      title: 'Decks 3 and 4 need the loopback cable',
      need: ['deck 3', 'deck 4', 'decks 3', '3 and 4', 'four deck', '4 deck', 'loopback', 'cable', 'ch3', 'ch4', 'no cable found', 'rescan'],
      terms: ['silent', 'no sound', 'not working', 'nothing', 'cable', 'loopback', 'usb 1', 'usb 2', 'rear', 'ch3', 'ch4', 'no cable found', 'not connected', 'start', 'set up', 'setup', 'connect'],
      not: ['e-8307', '8307', 'hub', 'sync', 'bpm', 'limit', 'master', 'ram'],
      answer: 'Plug the USB-B end of the loopback cable into the rear USB socket and the USB-A end into USB 1 or USB 2 on top. Put your music in the other top port and set [[CH 3 INPUT]] and [[CH 4 INPUT]] to USB. Decks 3 and 4 start by themselves while decks 1 and 2 are stopped or paused, even with no tracks loaded.',
      action: { id: 'OPEN_FOUR_DECK_SETUP', label: 'Set up four decks' },
      source: 'Manual: Four decks, Set up four decks'
    },
    {
      entry_id: 'e8307-loopback-hub', revision: 2, kind: 'NOT_A_BUG', component: 'loopback',
      title: 'A USB warning with the loopback cable in',
      need: ['e-8307', 'e8307', '8307', 'hub', 'hubs', 'unsupported device'],
      terms: ['e-8307', 'e8307', '8307', 'hub', 'hubs', 'not supported', 'unsupported', 'warning', 'error', 'loopback', 'cable', 'usb'],
      not: ['access error'],
      answer: 'This warning is expected while the loopback cable is in. Play on. If you see it without the loopback cable, a device the player does not support is in a top USB port: remove it.',
      action: null,
      source: 'Manual: Messages and troubleshooting, LOOPBACK CABLE CONNECTED'
    },
    {
      entry_id: 'sync-labels-decks-3-4', revision: 1, kind: 'QUESTION', component: 'sync_tempo',
      title: 'What SYNC 3, BPM 1, LIMIT and EXT MASTER mean',
      need: ['sync', 'bpm', 'limit', 'ext master', '3/4 master', 'master'],
      terms: ['sync', 'bpm', 'limit', 'ext master', 'external master', 'master', 'tempo', 'beat', 'label', 'means', 'what does', 'deck 3', 'deck 4', 'follow', 'drift', 'quantize'],
      not: ['key sync', 'key shift', 'key cpu', 'keys'],
      answer: 'SYNC 1 to SYNC 4: SYNC is on and follows that deck. BPM 1 to BPM 4: only the tempo follows, because QUANTIZE is off, a track has no beat grid, a loop is running, or the beat position is not available. LIMIT: the master tempo is out of reach, so the deck keeps its last tempo. EXT MASTER: a LINK player is master, which decks 3 and 4 cannot follow. NO BPM: analyse the track in your DJ software.',
      action: null,
      source: 'Manual: Four decks, SYNC on decks 3 and 4'
    },
    {
      entry_id: 'key-sync-cpu-limit', revision: 1, kind: 'QUESTION', component: 'sync_tempo',
      title: 'KEY SYNC and KEY CPU LIMIT',
      need: ['key sync', 'key shift', 'key cpu', 'master tempo'],
      terms: ['key sync', 'key shift', 'key cpu', 'cpu limit', 'reset', 'key', 'pitch', 'master tempo', 'deck 3', 'deck 4'],
      not: [],
      answer: 'KEY SYNC changes the key only; tempo, SYNC and the master deck stay as they were. Only one of decks 3 and 4 can hold a key shift at a time. If you see KEY CPU LIMIT, press RESET on the KEYS page for the deck that holds the shift, then try again. [[SHIFT]] + [[SYNC]] or [[MASTER TEMPO]] ends KEY SYNC.',
      action: null,
      source: 'Manual: Performance, Keys and KEY SYNC'
    },
    {
      entry_id: 'ram-tracks-cable', revision: 1, kind: 'NOT_A_BUG', component: 'four_decks',
      title: 'RAM tracks on decks 3 and 4 need the loopback cable',
      need: ['ram'],
      terms: ['ram', 'ram tracks', 'memory', 'copy', 'load track into ram', 'ram engine', 'deck 3', 'deck 4', 'cable', 'loopback', 'usb out'],
      not: [],
      answer: 'RAM TRACKS works on all four decks, but decks 3 and 4 need the loopback cable connected to start a copy. Keep the music USB in while it copies and wait for ORIGINAL AUDIO IN RAM. If you see RAM ENGINE NOT DETECTED, prepare the USB again with this app.',
      action: { id: 'OPEN_FOUR_DECK_SETUP', label: 'Set up four decks' },
      source: 'Manual: Performance, RAM tracks'
    },
    {
      entry_id: 'player-will-not-start', revision: 2, kind: 'NOT_A_BUG', component: 'runtime_stability',
      title: 'The player will not start with the USB',
      need: ['start', 'boot', 'starts', 'load', 'stuck', 'black screen', 'freeze'],
      terms: ['start', 'boot', 'usb', 'stuck', 'black', 'screen', 'logo', 'loader', 'autoexec', 'firmware', 'will not', "won't", 'does not', 'freeze', 'update'],
      not: ['deck 3', 'deck 4', 'stems', 'ram'],
      answer: 'Switch the player off, remove the XZ Mods USB and switch it on: it always starts stock. Check it runs firmware 1.26 and autoexec.bin is at the top of the USB. Then open the USB in this app and update the loader. The app keeps a backup of the old loader each time.',
      action: { id: 'OPEN_USB_SETUP', label: 'Open the USB page' },
      source: 'Manual: Messages and troubleshooting, If the player will not start with the USB'
    }
  ]
});
