# Beat Arcade

A native, shared-screen rhythm arcade for the XDJ-XZ 1.26. All simulation and
800×480 RGB565 drawing run on the XZ. The current implementation adds four
games under MODS → Extras → Beat Arcade:

- Solo Pong against a speed-limited opponent.
- Pong Duel, one jog wheel per player, first to seven.
- Solo Breaks, three lives and repeating equalizer patterns.
- Back2Back, cooperative Breakout with two bottom paddles and shared lives.

Turn your deck's jog to move. Ordinary paddle saves always work. Tap PLAY or
pad A near a return to earn a Perfect and more charge. A landing ring shows
where and when to meet the ball. Paddle offset controls the outgoing angle.

| Control | Action |
|---|---|
| PLAY / pad A | Strike, confirm resume, or rematch |
| CUE / DJ Pause | Freeze the round and return controls to the DJ |
| Pad B | Wide paddle for 16 beats, costs 3 charge |
| Pad C | Shield saves one miss, costs 4 charge |
| Pad D | Drop for 8 beats, costs 6 charge; faster Pong shots or brick piercing |
| Pads E / F | Bias the next shot left or right |
| Pad G | Cycle three geometric visual styles |
| Pad H | Tap tempo |

CUE changes only the game. It does not pause, cue, seek, or load the track.
After changing music, reopen Beat Arcade from Extras. Each present player
confirms with PLAY, then a four-beat countdown resumes the saved ball.
Releases from game-owned buttons and touchscreen contacts remain captured
through the handoff, so they cannot reach stock controls as orphan releases.

Beat Sync setup chooses deck 1 or 2, detects an existing beat packet, offers
tap tempo, and shows a four-beat pulse for live verification. The passive
`sendto`/`recvfrom` observers retain only validated beat timing. They forward
the original call and preserve its return and errno; they create no socket
and send no extra packets. When beat packets stop, the game keeps its last
tempo and labels the clock TAP after four seconds. The default is 120 BPM.

The engine finds the next physical contact before choosing its duration.
Every newly planned arrival uses the next feasible quarter-beat boundary.
The stored endpoint stays fixed while the paddle moves. Tempo updates change
the elapsed beat rate, with a bounded phase correction applied to the music
clock. Contact planning aligns subsequent flights to that corrected clock.
Live phase changes can require several packets to settle. This is musical
contact timing, with exact deck phase subject to physical verification.

One ball, 48 bricks, twelve trail samples, and a bounded impact animation keep
the court readable. There is no game audio, persistent score storage, account
connection, or game input recording. Calm visuals remove trails, particles,
background pulses and drop borders.

## Run the real C preview

From the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/arcade/preview.py --zig .tmp/cdj-zig/ziglang/zig.exe --output .tmp/xz-arcade/preview --port 8769
```

Open `http://127.0.0.1:8769`. Player 1 uses WASD, Space and digits 1–8.
Player 2 uses arrows, Enter and numpad 1–8. Q pauses. Click the native game
cards to begin. This Windows preview runs the same C engine and RGB565
renderer as the XZ; it does not connect to hardware.

## Verify

```powershell
python packages/xdj-xz-toolkit/mods/arcade/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe --output .tmp/xz-arcade/verify
python packages/xdj-xz-toolkit/mods/ui/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
```

The first command executes six-minute simulated rallies in the actual C
engine, checks powers, tempo, pause, input ownership and drawing bounds, then
builds ARM game and UI-runtime acceptance executables. The second command
runs existing UI regressions. `device_verify.py` runs those ARM executables
in isolated XZ RAM while checking that the DJ app PID stays unchanged.

Physical input feel, audible playback continuity, live track phase, actual
presented frame cadence and a fresh USB boot are separate acceptance gates.
