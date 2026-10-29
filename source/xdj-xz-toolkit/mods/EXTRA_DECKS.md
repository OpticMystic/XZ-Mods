# Software decks 3 and 4: completion candidate

This candidate extends the four-deck runtime from `6dc3230`. It supplies two
independent software players to the existing USB audio transport. Native decks
1 and 2 keep their existing players. The bridge and standalone POC application
must not own the audio interface at the same time as this runtime.

The changes have host and emulated ARM coverage. They still require an isolated
load check and a controller trial for this exact binary. Earlier clean CH3/CH4
tone tests on both USB ports qualify the transport, not these player features.

## Controls

The following controls apply when a side is assigned to software deck 3 or 4.
Existing native routing remains in use when that side selects deck 1 or 2.

| Control | Software-player behavior |
| --- | --- |
| SHIFT twice / existing deck selector | Switch the side between its native and software player; tempo pickup prevents an immediate fader jump. |
| LOAD | Existing Classic or FLAGSHIP browser path loads the selected track into the assigned software player. |
| PLAY / CUE | Existing independent transport, back cue and cue audition. PLAY starts at full speed at once. In VINYL mode PAUSE brakes to a stop at the VINYL SPEED ADJUST rate (manual p.45); PLAY during a brake returns to full speed at once. |
| HOT CUE / BEAT LOOP / SLIP LOOP / BEAT JUMP | Set the software deck's pad mode; the native deck's mode is unchanged. SHIFT + HOT CUE (CALL) makes the pads inert. |
| Pads, HOT CUE mode | Store, play and (SHIFT) clear hot cues. Set during loop playback, the pad stores the loop; calling it loops, and loop edits update the pad (p.53). rekordbox loop hot cues load as loops. With SLIP on, a held hot cue plays from the cue; its release, or four beats (p.59), returns to the background position. |
| Pads, BEAT LOOP mode | 1/4, 1/2, 1, 2 / 4, 8, 16, 32 beats from the press (p.56). The running loop's pad exits it; another pad resizes it from the same IN. |
| Pads, SLIP LOOP mode | 1/16, 1/8, 1/4, 1/2 / 1, 2, 1/3, 3/4 beats while held; release continues from the background position (p.62). |
| Pads, BEAT JUMP mode | A/B back/forward 1 beat, C/D 2, E/F 4, G/H 8 (pp.56-57). With a loop set the loop moves. The pair layout is assumed from Pioneer's usual order; confirm on a stock deck. |
| DIRECTION REV | Toggle reverse playback at the same rate; it stops at the track start (p.42). With SLIP on, held: reverse while down, then the background position on release or after eight beats (p.59). |
| SLIP | Toggle slip; a load turns it off (p.60). With VINYL on, PAUSE keeps playback running in the background and PLAY resumes from there (pp.58-59). Beat/slip loops, manual loops and held hot cues then continue in the background; RELOOP/EXIT returns to the background position (p.61). |
| VINYL SPEED ADJUST | Captured code `0x4123`, operation 5, value 0..1. Controls the pause brake from 30 ms counterclockwise to 3 s clockwise, about 300 ms at centre. PLAY remains instant. The timing curve still needs comparison with a stock deck. |
| TEMPO RANGE | Cycle ±6%, ±10%, ±16%, WIDE. WIDE covers stopped speed through +100%. The fader moves in the stock steps: 0.02 % (±6), 0.05 % (±10, ±16), 0.5 % (WIDE). |
| TEMPO RESET | Toggle original speed; retain the fader setting for release from reset. |
| SYNC | As on stock decks 1/2 (owner-observed, `stock-sync-spec.md` S1-S8). A press takes the master's tempo at once and the lamp lights steady; with QUANTIZE on it also snaps to the nearest beat and holds it. A side nudge on this deck, or on the master, leaves a beat offset that SYNC keeps: the tempo stays matched, the trim holds the offset instead of closing it, master tempo changes keep it, and the SYNC lamp (and jog SYNC indicator) flash while it lasts (S4, S7, S8). A nudge on a master bends only its own playback; followers, and what a DECK 3/4 master publishes to native followers, keep the tempo setting (S8). SYNC off keeps the phase and the synced tempo; the trim fades out over 250 ms, and the tempo fader picks up from there (S2). SYNC on again snaps to the nearest beat (S3, S5). A pause, jump, load, master change, clock gap, platter-top scratch here or hold on the master's platter top finds the beat again on its own (re-snapping at most once a second) and ends a kept offset; stock behaviour for these is unobserved. The follower's tempo fader is locked while synced. A press toggles once; held 1 s (stock's hold notification) it also runs Instant Doubles. |
| MASTER | Make this deck the master; it takes master at once and the MASTER lamps show it when it commits (no pending notice, M1). Stock confirming its own master is not a DJ choice and does not cancel the request. With no master yet, the first deck to play an analysed track becomes master, DECK 3/4 included (p.64). When the master pauses, master passes at once to the next playing deck: a playing deck with SYNC on first, then in order (from 3: 4, then the native decks; from 4: native, then 3) (M2). In VINYL mode it passes at the pause press, while the deck brakes. A paused native master hands over to a playing DECK 3/4 at once, unless the other native deck plays: then the firmware's own 1/2 handoff gets 300 ms to act. Holding the master's platter top (VINYL, native or DECK 3/4) for 300 ms hands master to the other synced, playing deck (M4). Loading a track on the master (a DECK 3/4, or a native master whose track changes) hands master to the other playing deck; the loader keeps SYNC and follows at the shared tempo (M5). A handoff never changes the tempo: while a loaded DECK 3/4 stays the chosen master (a seek, beat jump, reload, VINYL pause or hold renewing its lease), the engine names no master meanwhile instead of the old native one (M6). A LINK (external) master is never taken. Channel-fader-down handoff (M3) needs fader positions this runtime cannot read yet. Native decks 1/2 cannot follow a software master. |
| SEARCH | Paused: each press moves the position half a frame (CUE then sets it, p.45). During LOOP IN/OUT adjust: each press moves that edge half a frame (pp.47-48). Playing: held, scans forward or back at 4x (provisional; the manual gives no speed). |
| CUE/LOOP CALL | During loop playback: loop 1/2X and 2X. Otherwise: call the track's stored memory point before or after the position; the deck pauses there, and a memory loop is set so PLAY loops (p.51). |
| TRACK SEARCH | Back: the point the track cued to at load, play state kept; twice within 500 ms (provisional): the previous track in the browsed order (p.40). Forward: the next track, at its load cue. |
| TIME MODE / AUTO CUE | A short press switches elapsed/remaining time for every deck, held 1 s it switches AUTO CUE; with SHIFT only the side's deck. Stock still gets the key for the native decks. AUTO CUE off: a load lands at the track start, and loading while playing keeps playing. |
| QUANTIZE | DECK QUANTIZE switches quantize for every deck, the native decks through stock as before; SHIFT + DECK QUANTIZE switches only the deck of that side (manual, Quantize). With QUANTIZE off, SYNC follows tempo only. With QUANTIZE on, cue, LOOP IN/OUT, hot cue and beat/slip loop points land on the beat grid (1-beat value; QUANTIZE BEAT VALUE is not readable), and a hot cue played mid-beat keeps the beat position. |
| MASTER TEMPO | Preserve pitch while changing playback speed using stereo WSOLA. |
| LOOP IN / OUT | Set loop boundaries; LOOP OUT on its own takes the cue point as IN (p.47). During a loop, enter boundary adjustment and move the selected edge with the jog or SEARCH; press again or wait ten seconds to leave adjustment. |
| Hold LOOP IN | After one second while playing, a four-beat loop from the press (on the beat with QUANTIZE). Beat length from the grid, else the track BPM, else 120 BPM. |
| SHIFT + LOOP IN | Retrigger the active loop. |
| RELOOP / EXIT | Exit the active loop or return to its stored boundaries. |
| VINYL | On at start, as the stock decks. Enables touch hold and bidirectional scratching. In CDJ mode, jog touch is ignored and the existing jog bend remains. |

Loop adjustment uses half a CD frame per jog tick. IN adjustment is bounded to
thirty CD frames around the entry point. Loop wrapping does not flush the USB
queue on every wrap. Cue jumps cancel the active loop but retain its boundaries
for RELOOP.

Vinyl hold submits no new audio until the jog moves. Forward and reverse motion
produce actual PCM; release restores the preceding playing or paused state.
Direction changes flush once to the heard position. The current conversion uses
360 jog ticks per revolution and 1.8 seconds per revolution, inherited from the
existing input path. Platter calibration and scratch latency need physical
acceptance.

## Additional stock controls

- MEMORY stores the current cue or loop. DELETE removes the called memory point. Hot-cue edits and loop-hot-cue boundary edits also request a save.
- The deck snapshot publishes the working memory points, and FLAGSHIP maps them to its existing waveform markers.
- Software-deck cues persist in `xzmods-cues-TRACK_ID.bin` in that track's ANLZ directory. Rekordbox's DAT, EXT and database files remain unchanged; native decks and rekordbox do not read this sidecar. A fresh software-deck load restores the saved cues after reading the export.
- A separate writer thread serializes edits from both decks. The audio workers enqueue changes into a fixed 16-job queue and perform no filesystem writes. Each job rechecks the selected USB's media generation, opens the ANLZ directory through the allowed USB root, writes a bounded temporary file, then renames it. Concurrent edits merge per pad or memory point rather than replacing another deck's changes.
- `XZ_MODS_SETTINGS_READONLY=1` prevents every save request. Edits still work for the session. A full queue or failed save is reported without discarding the session cue. Saved files have a version, track identity, duration, bounds and checksum checks. Invalid saved data leaves the rekordbox export available.

- The dedicated 4/8BEAT button uses captured code `0x4118`. Press starts a four-beat loop, hold extends it to eight beats, and a press during loop playback halves the loop. SHIFT doubles the loop.
- Press HOT CUE again while already in Hot Cue mode to enter CALL. Choosing a saved pad recalls it without moving playback. Press HOT CUE again to finish; pads not selected are cleared from the working bank. The source export remains unchanged.
- Rekordbox active loops engage when forward playback reaches the stored start. The DAT PCPT status must explicitly be 4. The public [crate-digger ANLZ schema](https://github.com/Deep-Symmetry/crate-digger/blob/main/src/main/kaitai/rekordbox_anlz.ksy) documents this field. Malformed or out-of-track loop boundaries are rejected.
- Hold SYNC or double-press LOAD to duplicate the other software deck's track, current position, tempo, direction and loop. Native-deck sources are not supported by this path.
- After a read failure, Emergency Loop replays the last four beats of accepted PCM from RAM. It requires a full four-beat history in the current generation and uses an eight-second cache per deck, about 2.7 MiB each. Scratch, reverse, scan and braking audio are excluded. Without sufficient history the existing read-error pause remains. PLAY and CUE can stop playback; LOAD replaces the emergency loop.

## Display behavior

FLAGSHIP receives software tempo range, Master Tempo and vinyl state through the
existing deck model. Classic displays four waveform lanes, range/MT/V readouts,
loop tint and short notices for loop edits and transport problems. Notices remain
visible briefly after the audio route disappears.

The preview harness renders the actual C drawing code. Classic previews are
composites over a retained controller screenshot; they are not new device
captures. The two-deck shell fixtures remain pixel-identical to the baseline.

The candidate also integrates the separately reviewed 260-byte shell-proof
reader correction. Its offline fixtures check the exported ABI, exact ELF hash,
mapping and process identity. A read is explicitly non-atomic; stable routing
fields are compared while rendering counters may advance.

## Audio and formats

The existing WAV, FLAC, MP3 and Vorbis decoder paths remain. AIFF adds stereo PCM
8/16/24/32-bit and AIFF-C `NONE`, `twos`, `sowt`, `fl32` and `FL32`, at integer
sample rates from 32 to 96 kHz. Unsupported compression, malformed chunks,
non-finite float samples and invalid bounds are rejected.

The MP3 reader now seeks in logical, encoder-delay-trimmed PCM coordinates.
Forward seeks discard the required logical delta. Backward seeks restart the
decoder and discard to the target. Tests compare arbitrary reads, including
near EOF, with a sequential PCM reference. This fixes positioning but does not
provide an MP3 seek index; backward cue/scratch latency on long MP3 tracks needs
hardware qualification.

Master Tempo uses a worker-owned fixed-memory WSOLA implementation with one
alignment for both stereo channels. A source cache retains overlap and appends
the missing tail, avoiding repeated backward decoder seeks during ordinary
forward playback. It resets on load, jumps and mode changes. Scratch rendering
bypasses the stretch processor.

The method follows Roelands and Verhelst,
[Eurospeech 1993](https://www.isca-archive.org/eurospeech_1993/roelands93_eurospeech.html).
The AIFF parser follows Apple's
[AIFF/AIFF-C format specification](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/MacOS_Sound-extract.pdf).
Control behavior was checked against the public XDJ-XZ operating instructions,
pages 47–50, retained locally as `E:/Claude/xz-ui-manuals/XDJ-XZ_DRI1625B_manual.txt`.
No new native hooks, firmware analysis, vendor decoder changes or downloads were
used for these additions.

## Reproduce the offline checks

From this directory, substitute the local Zig path and fresh output directories:

```powershell
python verify_extra_player.py --zig <zig.exe>
python verify_extra_features.py --zig <zig.exe> --output <features-output>
python verify_deck_select.py --zig <zig.exe>
python verify_shell_runtime.py --zig <zig.exe>
python -m unittest discover -s . -p test_read_shell_proof.py
python verify_extra_arm.py --zig <zig.exe> --build <runtime-build> --output <arm-output>
python ui/preview_four_deck.py --zig <zig.exe> --screen <retained-screen.png> --output <preview-output>
```

The player suite runs assertions, a deliberately broken back-cue mutation,
UBSan, ASan, TSan, route-connector checks and the unchanged USB transport tests.
TSan retries only its startup address-mapping collision, at most three times;
an actual race fails immediately and failure to start is not reported as a pass.

The feature suite checks pitch, duration, stereo independence, reset/silence,
bounded reads, AIFF malformed inputs, notice geometry and MP3 seek parity.
It uses an already installed FFmpeg only to generate the test MP3.

The ARM verifier uses QEMU with the actual VFP player/media objects from the
runtime build. Its receipt labels execution as emulated and hardware performance
as unverified. Synthetic pitch tests do not establish subjective music quality.

## Remaining acceptance and scope

- Run the exact runtime's isolated device smoke check, then prepare a new trial
  image and kit version. `private_usb.py` requires that device receipt. Do not
  substitute an emulation receipt or overwrite frozen v2.
- Under the hardware-session owner's lease, verify the current PID, exact
  runtime, stopped decks, selected removable media and rollback before loading.
  A changed PID or USB volume invalidates an older acceptance session.
- Reproduce the owner-reported native physical LOAD issue separately. There is
  no new native LOAD fix in this candidate.
- Test four real tracks together, both interfaces, deck switching, pickup,
  loops, Master Tempo, scratch, queue underruns and CPU headroom. Start with
  PCM WAV/AIFF, then test long compressed tracks and repeated backward seeks.
- Qualify reconnect and route loss, including Classic notices. The independent
  POC application must remain closed while the runtime owns the interface.
- AAC/M4A remains unsupported: the local inventory found no portable AAC
  decoder plus MP4 demux/seek/edit-list implementation that could be qualified
  for ARM32 soft-float and GLIBC_2.4.
- Lamps follow the manual's states (`xz_extra_lamp`, ui/native_led.c): PLAY lit
  while playing and flashing when paused; CUE lit while playing or at the cue,
  flashing elsewhere; LOOP IN/OUT lit while playing or looping, with the adjusted
  end flashing and the other off (pp.47-48); RELOOP/EXIT, REV, SLIP, VINYL, SYNC
  (while on), MASTER, MASTER TEMPO, TEMPO RESET and the pad-mode buttons. Two
  limits remain. The hook only edits records the native packet already carries.
  Flashing alternates the qualified ON/OFF states every 250 ms; physical cadence
  remains unverified. `read_feedback_capture.py` reads the native
  status records, the native pad colours per pad page and the code table that
  qualifies both and finds the VINYL SPEED ADJUST code.
- Beat-loop/slip-loop pad colours are provisional (`XZ_EXTRA_PAD_*_RGB`) until
  the pad probe captures the stock values. Uncoloured rekordbox hot cues light
  green, loops orange (p.54).
- Jog-display output remains with the root thread. The stopped jog-display
  analysis has not been resumed here.
- Not yet stock: native sources for Instant Doubles; native/rekordbox visibility of software cue edits;
  waveform needle search
  is refused while playing (stock NEEDLE LOCK default) because the UTILITY setting is not readable.
- Touch deceleration in VINYL mode and slip during scratching belong to the
  jog-coast work. Quantize, memory-point recall and software following of native
  tempo and phase are covered by the offline checks. Physical acceptance remains open.

`audio/usb_transport` remains byte-identical to its recorded upstream source.
The player and new stretch module retain GPL-2.0-only notices; the AIFF reader
is MIT. The existing distribution/license question remains open. This is a
private development candidate, with no public push, release or firmware/storage
write.
