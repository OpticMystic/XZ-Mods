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
| Pads, HOT CUE mode | Store, play and (SHIFT) clear hot cues. With SLIP on, a held hot cue plays from the cue and its release returns to the background position. |
| Pads, BEAT LOOP mode | 1/4, 1/2, 1, 2 / 4, 8, 16, 32 beats from the press (p.56). The running loop's pad exits it; another pad resizes it from the same IN. |
| Pads, SLIP LOOP mode | 1/16, 1/8, 1/4, 1/2 / 1, 2, 1/3, 3/4 beats while held; release continues from the background position (p.62). |
| Pads, BEAT JUMP mode | A/B back/forward 1 beat, C/D 2, E/F 4, G/H 8 (pp.56-57). With a loop set the loop moves. The pair layout is assumed from Pioneer's usual order; confirm on a stock deck. |
| DIRECTION REV | Toggle reverse playback at the same rate; it stops at the track start (p.42). With SLIP on, held: reverse while down, then the background position on release or after eight beats (p.59). |
| SLIP | Toggle slip. Beat/slip loops, manual loops and held hot cues then continue in the background; RELOOP/EXIT returns to the background position (p.61). |
| VINYL SPEED ADJUST | Brake/start time from 30 ms (counterclockwise) to 3 s (clockwise), ~300 ms at the centre. The player accepts the knob, but its key code is unknown, so no input reaches it yet and the centre value is used. |
| TEMPO RANGE | Cycle ±6%, ±10%, ±16%, WIDE. WIDE covers stopped speed through +100%. |
| TEMPO RESET | Toggle original speed; retain the fader setting for release from reset. |
| SYNC | Follow the master deck's tempo until SYNC is pressed off. With QUANTIZE on, SYNC also snaps to the master's beat and holds it. A pause, jump, master change or clock gap re-acquires on its own (re-snapping at most once a second); no second press. The follower's tempo fader is locked while synced. |
| MASTER | Make this deck the master. When the master stops, master passes to the next playing deck in order (3, 4, then the native decks' own master; from 4: native, then 3). A stopped native master hands over to a playing DECK 3/4 after 300 ms, so the firmware's own 1/2 handoff wins; a LINK (external) master is never taken. Channel-fader-down handoff needs fader positions this runtime cannot read yet. Native decks 1/2 cannot follow a software master. |
| QUANTIZE | DECK QUANTIZE switches quantize for every deck, the native decks through stock as before; SHIFT + DECK QUANTIZE switches only the deck of that side (manual, Quantize). With QUANTIZE off, SYNC follows tempo only. |
| MASTER TEMPO | Preserve pitch while changing playback speed using stereo WSOLA. |
| LOOP IN / OUT | Set loop boundaries. During a loop, enter boundary adjustment and move the selected edge with the jog; press again or wait ten seconds to leave adjustment. |
| Hold LOOP IN | After one second while playing, create a four-beat loop. Use the track BPM, or 120 BPM when it is unknown. |
| SHIFT + LOOP IN | Retrigger the active loop. |
| RELOOP / EXIT | Exit the active loop or return to its stored boundaries. |
| CALL PREV / NEXT | Halve / double the active loop length. |
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
  limits remain. The hook only edits records the native packet already carries,
  and a flash is written as OFF until `XZ_LED_BLINK_QUALIFIED` is set from a
  captured native flashing record. `read_feedback_capture.py` reads the native
  status records, the native pad colours per pad page and the code table that
  qualifies both and finds the VINYL SPEED ADJUST code.
- Beat-loop/slip-loop pad colours are provisional (`XZ_EXTRA_PAD_*_RGB`) until
  the pad probe captures the stock values. Uncoloured rekordbox hot cues light
  green, loops orange (p.54).
- Jog-display output remains with the root thread. The stopped jog-display
  analysis has not been resumed here.
- Touch deceleration in VINYL mode and slip during scratching belong to the
  jog-coast work and are not changed here. Quantize, memory cues and
  native-equivalent beat-phase sync are not provided by this candidate.

`audio/usb_transport` remains byte-identical to its recorded upstream source.
The player and new stretch module retain GPL-2.0-only notices; the AIFF reader
is MIT. The existing distribution/license question remains open. This is a
private development candidate, with no public push, release or firmware/storage
write.
