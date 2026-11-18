# XZ key-shift native adapter

`runtime.c` hooks the exact 1.26
`dsp::TimeStretchManager::operate(long, Float2*, long, long)` entry at `0x76284`.
The first eight bytes are `f0412de90040a0e1`. The wrapper preserves all five
ARM32 arguments and returns the original source-position result unchanged.

The manager's active DSP pointer is at `+0x10`. The stock branch at
`0x76314..0x7631c` returns the input source position when that pointer is null
and does not write the output buffer. The adapter captures the active pointer
and published deck/rate/epoch before the original call, then requires the same
nonzero pointer and identity after it. It skips null-to-active, active-to-null,
engine-swap, and epoch-change transition blocks. Skipping a transition block is
safe; processing an uninitialized or ambiguously owned block is not.

`xz_audio_output_deck` recognizes only a manager captured by a successful
native load snapshot. It publishes manager, 44.1 kHz rate, and output epoch with
32-bit atomics. Load and unload increment the epoch before the original
operation, so lookup fails until the next complete snapshot. Runtime stop also
invalidates it. Stem enable and disable use a separate decode generation and do
not reset Key Shift. The key adapter reinitializes that deck's history when the
published output epoch changes.

Within one output epoch, the previous original return is the next expected
foreground source position. A mismatch resets that deck's history before
processing. Background/slip position changes alone do not reset it.

Control threads publish only an integer desired semitone. The audio callback
reads it once at the block boundary. Both desired values start at zero, and the
portable shifter keeps neutral output byte-identical. Start installs the hook;
stop disables processing and resets both desired values to zero. The code hook
stays mapped for the process lifetime under the parent runtime's nodelete rule.

`native.c` reads the original analyzed key, master flags, Master Tempo, and the
stock effective tempo ratio with exact 1.26 instruction guards and repeated
snapshot checks. `music.c` calculates the playing key and residual cents.
Shift+Sync matches the other deck's playing pitch, using the relative key when
the two tracks have different major/minor modes. A pitch shifter cannot change
a track's major/minor mode. The two local decks always target each other,
including when the source is master or neither deck is master. More than two
decks would need master selection; network selection is outside this adapter.

Shift+Sync toggles per deck. Engagement remembers Master Tempo, turns it on
with the guarded native setter, and computes the source correction without
source tempo transposition. The other deck contributes its original key,
applied shift and tempo transposition when its Master Tempo is off. A second
press requests zero cents and restores the remembered Master Tempo state.
MASTER TEMPO while synced consumes the entire press/repeat/release gesture,
requests zero cents and restores that same state. Later presses are stock;
only Shift+Sync engages KEY SYNC again. A new load ends sync and restores
Master Tempo without issuing a previous-epoch reset to the new track.

The 1.26 setter at `0x289790` accepts both boolean values. Its guard is
`70402de90150a0e1`. Offline disassembly of the local 1.26 ELF shows:
`0x289794` saves argument r1 in r5; `0x2897b8..0x2897c0` returns if unchanged;
`0x2897cc` copies r5 to r2 and `0x2897d0` calls the engine setter at `0x40c98`.
`0x2897fc..0x289804` forwards the same boolean to native settings. There is no
ON-only branch. This proves the native OFF call path, not physical button or
audio acceptance. `key_player()` resolves the UI manager at `0x1c8a308`, array
at +0x40/count +0x48, then the local player's +0x138 innards and verifies its
channel byte at +0x26, as the existing RAM adapter does.

FLAGSHIP receives playing key, analysed key, residual cents and sync state on
each publication. Both keys use the selected display format. Frame input owns
its arrays; the shell model copies them into persistent runtime storage and
publishes KEY, KEY_ORIGINAL and KEY_SYNC known bits only for valid data.
The playing display follows applied audio cents and live tempo, including
while Master Tempo is off. Pending cents remain separate from applied audio.

The MT LED mapping is disabled pending the capture described in
`MASTER_TEMPO_LED_PROBE.md`. Portable hypothetical packet tests do not qualify
a native LED id or a blink encoding.

UI requests use integer cents bound to the native output epoch. Loading another
track invalidates the request before its first output block. The post-stretch
adapter applies the cents ratio at a block boundary. Master Tempo holds the
original key against tempo changes; an explicit key shift still changes it.
With Master Tempo off, the display also includes the native tempo/nudge ratio.

The USB browser reads the exported DeviceSQL database on a separate worker.
Only valid live rows contribute keys. Native title rendering receives a
prefixed display copy in the USB's Classic or Camelot preference. A second
glyph pass colors the prefix and restores the first pass's native bounds.
Track metadata, title strings, and the USB database stay unchanged. Conflicting
or unknown keys for the same displayed title on the selected USB are left
unannotated. The other USB's metadata is never borrowed. Remote library views are excluded. The index
accepts at most 131,072 tracks per USB and rejects an incomplete/malformed index.

Green means an equal, adjacent-wheel, or relative key within 25 cents of the
master. Amber means a harmonically related key outside that tuning tolerance.
Other keys retain neutral coloring. Missing metadata or an ambiguous master
reports the missing prerequisite. Detect verifies the two tracks. The shortcut
is always available on the standalone deck; old `shift_keysync=0` values are
ignored on read. Computer-controlled sources retain their own stock mappings.

Host tones, native-shaped input/control replays, export parsing, text-wrapper
tests, and ARM ABI builds verify software behavior. Controller key metadata,
the physical chord, real-song audio quality, browser scrolling, timing under
load, and a fresh USB boot still require live acceptance of this exact bundle.
