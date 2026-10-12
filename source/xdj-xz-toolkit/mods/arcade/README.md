# Beat Arcade

A native shared-screen rhythm arcade for XDJ-XZ 1.26. Open **MODS > Extras >
Games > Beat Arcade**. The game and its 800×480 RGB565 visuals run on the XZ.

- Solo Pong against a limited opponent.
- Pong Duel, one jog per player, first to seven.
- Solo Breaks with a generous paddle and five lives in Chill mode.
- Back2Back cooperative Breakout with shared lives and a shared groove meter.

## Play the groove

Turn the jog to catch the ball. Tap **CUE on each beat** as the moving light
meets the center target. Spatial catches work even when you miss a rhythm tap.
Good notes build charge. Eight consecutive good notes earn a shield. Sixteen
notes build an arena-wide Drop, launched on the next downbeat for eight beats.

The default Smooth jog gain is 0.55 pixels per pulse, down from 1.8. Fine and
Fast options remain available. Chill makes the ball easier to follow, gives
larger paddles and adds two lives. Flow and Club increase the challenge.
These choices last for the current application session.

| Control | Action |
|---|---|
| PLAY / PAUSE | Normal music transport, including press and release |
| Track-search / skip buttons | Normal music navigation and restart behavior |
| CUE / pad A | Rhythm strike; start, confirm resume or rematch |
| Jog | Move the paddle; turn to select a game in the menu |
| Pad B | Wide paddle for 16 beats, costs 3 charge |
| Pad C | Save one miss with a shield, costs 4 charge |
| Pad D | Queue an arena-wide Drop on the next downbeat, costs 6 charge |
| Pads E / F | Bias the next return's aim |
| Pad G | Cycle Tunnel, Grid and Orbit visuals |
| Pad H | Align mixer tempo, or tap a manual tempo |
| DJ View on screen | Save the round and return all game controls to the DJ |
| Browse encoder | Turn moves a focus ring over the on-screen controls, push taps it |
| BACK | Menu or paused screen: DJ View. Beat source: done. Results: menu |
| REKORDBOX | Save the round and open MODS |

The game captures platter touch as well as jog rotation. Touching the jog in
Vinyl mode therefore does not pause or scratch the music while playing.
Matching CUE/pad/jog-touch releases stay captured after leaving the game.
PLAY and track-search events pass through unchanged.

After DJ View, reopen Beat Arcade and confirm with CUE. Multiplayer waits for
both players. Four beats count in before the saved ball moves. A native USB
track pause freezes the round; resume and track changes give a fresh count-in.
An earned Drop remains queued for a future downbeat after a pause.

## Music sources

Tap the source display at the top of the screen to open beat setup.

| Source | Timing path |
|---|---|
| Analyzed USB track | Read-only native beat grid, position, pitch and playing state |
| Pro DJ Link | Validated beat packets already sent or received by the DJ app |
| Computer DJ software | Pro DJ Link where supplied; otherwise mixer tempo plus phase alignment |
| External audio | Mixer tempo plus phase alignment, or manual taps |
| Any source without usable timing | Four taps on pad H establish a manual clock |

Auto prefers a playing internal deck with a consistent grid, then Link, then
usable mixer tempo. You can explicitly choose deck 1, deck 2, Mixer or Tap.
For mixer timing, select the desired Beat FX channel, normally MASTER, and
use AUTO BPM on the XZ. The game reads that tempo without enabling an effect.
One CUE or H tap on the beat aligns its pulse. If the detector is out of range,
choose Tap and tap four beats. Mixer tempo does not contain a validated phase
or track transport signal; those are never presented as an automatic lock.

The source display distinguishes USB/Link sync, mixer tempo, aligned taps,
and a free-running clock. A stale or invalid grid cannot label itself LIVE.
A paused native track or a lost previously active grid holds the ball safely.
Tap clocks continue independently from external playback; DJ View pauses the
round when that source cannot report its transport state.

## Visuals and timing

Paddle arrivals land on whole beats; wall and brick contacts use half beats.
The engine fixes each geometric endpoint before choosing its deadline.
Clock corrections preserve the visible ball position, and track discontinuities
start a count-in. Cue timing uses a 48 ms Perfect window and a 115 ms Good
window. Repeated taps on an already scored beat do not earn extra charge.

A moving beat lane, 16-note groove meter, pad pulses, impact particles and
Drop transitions share the same musical clock. Three procedural scenes use
small lookup fields and bounded wireframe strokes. The ball is drawn last
and stays brighter than the effects. Calm removes the animated background,
trails and particles. The game produces no audio on the XZ.

## Play the C preview

From the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/arcade/preview.py --zig .tmp/cdj-zig/ziglang/zig.exe --output .tmp/xz-arcade-v2/preview --port 8772
```

Open `http://127.0.0.1:8772`. Player 1 uses WASD, Space and digits 1–8.
Player 2 uses arrows, Enter and numpad 1–8. Q pauses the game. The optional
**Play demo groove** button supplies a synthetic beat for testing the real C
engine. P plays/pauses that demo and R restarts it. It does not control the XZ.

## Verification

```powershell
python packages/xdj-xz-toolkit/mods/arcade/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe --output .tmp/xz-arcade-v2/verify
```

This exercises long rallies, beat timing, charge, groove rewards, transport
handoff, pause/resume/skip, drawing bounds and native clock data validation.
It builds the same checks for ARM. `device_verify.py` runs the ARM executables
in isolated RAM while confirming the DJ app PID stays unchanged.

Physical feel and alignment to real playing music require hands-on acceptance.
The current evidence does not claim that all three source types were played
and measured during verification.
