# Wave Rider

A game by VJ OpticMystic.

Wave Rider turns your song into smooth hills, then lets you surf them with
gravity, momentum, and ship rotation. It runs on the XDJ-XZ 1.26 without a
laptop during play. Open **MODS > Extras > Games > Wave Rider**.

Choose **Solo**, **Co-op**, or **Versus**, then select the music deck. In Solo,
that deck's jog controls your ship. In multiplayer, the left jog controls P1
and the right jog controls P2. Both riders share one song, course, and camera.

Choose **Easy**, **Normal**, or **Hard** on the control-check screen. Easy is
the default. Both riders use the same difficulty.

- **Easy:** the board keeps the air angle you set after its spin slows. The
  flight arc does not turn the board against your steering. Wider landing
  and gem windows help you keep moving. Rough landings cost one streak step
  and retain more speed.
- **Normal:** the original surfing physics. The board gradually follows its
  flight arc, and accurate landings keep your streak.
- **Hard:** stronger arc alignment, tighter landing and gem windows, and a
  larger speed penalty for a rough landing.

**Ride this track** uses your selected difficulty. **Ride Easy / Defaults**
starts with Easy and Spin Lift on. The choice stays set when you reopen the
game during this runtime session. The HUD and result screen show it.

## Ride the hills

1. Hold **CUE** or a top-row pad to add weight. Descend
   into a downhill to build speed.
2. Release the hold as the terrain rises. Momentum launches you from the
   crest; the button never creates an upward impulse.
3. Make small jog turns to match the next landing slope. Clockwise points the
   nose down, and counterclockwise points it up. Pitch stops at about 69 degrees
   in either direction on the ground. In the air, spins carry momentum and
   can make full flips. Turn the jog back to brake a spin. Easy holds the
   resulting angle. Normal and Hard gradually align the ship with its arc.
4. Land downhill with the slope to keep your speed. Accurate pitch and
   musical timing earn a Perfect. Chain clean landings for higher flights,
   more points, brighter trails, and an expanded view of the course. Each
   clean landing shows its awarded points beside the rider, including any
   banked air combo. Smooth and Perfect landing feedback glows gold.

Holding continuously keeps you on the ground. Poor uphill or misaligned
landings cost momentum and break the streak. There are no lives, and a miss
never stops the music.

Resting a hand on the platter does not add weight. CUE still controls dive and
release while you steer. The weight indicator responds immediately; the nearby
TRY hint suggests an action from the slope and flight path.

**Spin Lift** is an optional experiment on the control-check screen. Give the
jog a quick sweep near a rising crest, then release CUE. A gold indicator shows
when the move is ready; a successful crest launch gains extra lift. Slow turns,
flat ground, and spinning in midair do not trigger it. Turn Spin Lift off to
compare the basic surfing loop with the extra gesture.

Co-op adds shared bonuses when both riders land well together. Versus compares
individual scores. Each rider gets their own chance to collect a vocal gem;
the other player cannot take it away.

## Spend the charge you earn

| Control | Action |
| --- | --- |
| Pad E, 3 charge | Aurora gives softer landings and a longer glide for six song seconds. |
| Pad F, 5 charge | Comet amplifies your next crest launch. It expires after six song seconds if unused. |
| Pad G | Toggle calm visuals. |
| Pad H or DECK | Leave the game and return controls to the DJ. |

Good landings and gems earn charge. Powers replace the previous active power.
Comet still needs a hill and momentum; it cannot launch you from flat ground.
**PLAY**, tempo, load, and track selection remain native music controls.
In Solo, the other deck stays available for DJ use. Multiplayer captures both
jogs, platter touch, CUE, and pads while the game is open.

## How the song shapes the course

The source worker reads existing analysis from USB. Prepared drum, harmonic,
and vocal waveforms take priority. Other tracks use Rekordbox's low, mid, and
high three-band analysis. The game always labels which source it has.

The composer measures rhythm from the full track's drum or low-band onsets.
It turns those accents into broad, continuous hills, with heights derived from
the waveform's energy. Valleys snap toward nearby real accents. Tracks without
a reliable pulse use a smooth amplitude contour. Silence stays flat.
The displayed crest is the exact surface used by physics.

Harmonic energy creates gentle updrafts. Vocal or high-band onsets place gems.
Stem energy also drives the aurora, sky, and water. Flow streaks and earned
powers strengthen those effects; calm mode reduces motion and particles.
The rhythm estimate is derived from the waveform, not claimed as a verified
Rekordbox beat grid. Timing bonuses also look for nearby actual drum accents.

The native playhead advances the shared course. Pausing freezes physics.
Seeks move both riders safely, and replayed sections cannot award the same
progress again. Loading another track resets both riders.

## Verify or preview

The native source reader publishes a copied 2,400-sample window. Analysis stays
off the drawing thread. Physics uses fixed source-time steps, and the camera
expands to keep high flights and both riders visible.

From the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/arcade/verify_wave_rider.py --zig .tmp/cdj-zig/ziglang/zig.exe --output E:/Codex/xz-wave-rider-check
```

The Linux runtime verifier runs actual UI/input routing with address and
undefined-behavior sanitizers:

```sh
python3 packages/xdj-xz-toolkit/mods/arcade/verify_wave_rider_runtime.py --output /tmp/xz-wave-runtime
```

For a Windows preview, pass one three-band `.2EX` file, or three stem files
in drums, harmonics, and vocal order:

```powershell
python packages/xdj-xz-toolkit/mods/arcade/wave_rider_preview.py --zig .tmp/cdj-zig/ziglang/zig.exe --output E:/Codex/xz-wave-rider-preview --analysis path/to/ANLZ0000.2EX
```

Open `http://127.0.0.1:8776`. P1 uses A/D to tilt and Q to dive. P2 uses
left/right arrows to tilt and Enter to dive. R and / simulate each player's
quick jog sweep. Release the dive button to glide. Space pauses
the simulated song. This preview uses the actual C engine and renderer; it
does not play audio or control the XZ.

Tests distinguish source/ABI checks, desktop execution, and real hardware
acceptance. Physical jog feel, audible alignment, LCD cadence, and a fresh USB
boot require device checks. The game saves no performance data and makes no
USB, firmware, network, or account writes.
