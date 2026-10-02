# Wave Rider

Wave Rider turns the loaded track's waveform into a scrolling flight course.
It runs on the XDJ-XZ 1.26 without a laptop during play. Open **MODS > Extras >
Games > Wave Rider**, choose a deck, and follow the waveform and control checks.

Turn that deck's jog clockwise to rise and counterclockwise to descend. Tap
**CUE** or pads **A-D** on a spike to hop. Fly through the diamonds to collect
them. Follow the glowing line just above the terrain to double your points.

Pad **G** switches calm visuals. Pad **H** or **DECK** returns to the DJ screen.
Pads **E** and **F** also lower and raise the rider. **PLAY**, track selection,
tempo, and the other deck retain their music controls. The selected platter's
touch and rotation belong to the game until you leave.

The source label tells you what creates the level:

| Source | Spikes | Slipstream | Gems |
| --- | --- | --- | --- |
| Prepared stems | Drum onsets | Sustained harmonics | Vocal onsets |
| Rekordbox three-band analysis | Low-frequency onsets | Sustained mids | High-frequency onsets |

The ground follows the smoothed sum of the three amplitudes. The raw waveform
remains visible around its collision line. Silence stays flat and has no
objects. The game generates no random obstacles and never substitutes a timer
or BPM pattern for missing waveform data.

The native playhead drives the course. Pausing the track freezes it. Seeking
repositions it, and replayed sections become practice until you pass your
previous furthest point. Loading another track resets the course and score.
There are no lives, and a miss never stops the music. At the end of the track,
return to the deck to choose or restart music.

The game reads existing analysis from USB. Stem analysis takes priority when
the prepared bundle contains it. Otherwise it uses the track's Rekordbox PWV7
three-band analysis. The source worker operates independently of the selected
native waveform display style. Tracks without either analysis open the setup
flow. Unanalysed external audio has no full-track waveform to generate a course.

## Verify the native implementation

Run the engine, source, renderer, and ARM runtime checks:

```powershell
python packages/xdj-xz-toolkit/mods/arcade/verify_wave_rider.py --zig .tmp/cdj-zig/ziglang/zig.exe --output E:/Codex/xz-wave-rider-check
```

The checks cover waveform roles, silent sections, stable event positions,
pause and seek behavior, input ownership, rendering bounds, and moving-frame
cost. ARM executables also need execution on the XZ. Physical jog feel, audible
alignment, LCD presentation cadence, and a fresh USB boot are separate checks.

The source reader verifies the 1.26 getter instructions before reading the
stock player. Its position uses 44,100 ticks per second, converted to 150 Hz
waveform samples. It publishes a copied 1,800-sample window on a worker thread.
The renderer never opens a file, allocates a waveform, or borrows a retired
native waveform buffer. A stale source returns to setup.

## Preview an exported track on Windows

Pass one `.2EX` file for three-band data, or three files in drums, harmonics,
and vocal order for stems:

```powershell
python packages/xdj-xz-toolkit/mods/arcade/wave_rider_preview.py --zig .tmp/cdj-zig/ziglang/zig.exe --output E:/Codex/xz-wave-rider-preview --analysis path/to/ANLZ0000.2EX
```

Open `http://127.0.0.1:8776`. Arrow keys or the mouse wheel simulate jog input.
**Q** simulates CUE, and **Space** pauses the preview playhead. This uses the
actual C renderer and supplied waveform bytes, with a simulated playhead. It
does not play audio or control the XZ.

Wave Rider keeps its score and inputs in memory. It does not send analytics,
save a performance, modify analysis, or write to the music USB.
