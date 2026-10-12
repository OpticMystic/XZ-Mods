# Separation engine contract

The desktop backend (`service.py`) exposes the engine over JSON lines. Everything
heavy runs in the uv-managed `engine-python` venv under the data root
(`XZ_BUILDER_DATA`, default `%LOCALAPPDATA%\XZ Mods`) through
`resources/inference/inference.py`, which never downloads or installs anything.

## Presets (`builder/models.json`)

| id | parts | models | device |
| --- | --- | --- | --- |
| `balanced` (default) | vocals from Mel-Band RoFormer (KimberleyJSN, MIT), drums from `htdemucs_ft` drums model (MIT), harmonics = mix - vocals - drums | 913 MB + 84 MB | GPU recommended |
| `fast` | vocals and drums from `htdemucs` in one pass | 84 MB | GPU recommended |
| `umxhq` ("Light") | Open-Unmix HQ, four targets jointly | 4 x 36 MB | CPU, low RAM |

A preset is an ordered list of `passes`; each pass names a `separators` record
(model family, weights, chunk/hop, relative `cost`, GPU sizing) and the sources
it yields. Weights and vendored model code are pinned by SHA-256 and verified
before deserialisation. One venv serves every preset; only the torch flavour
(`cu128`, `cu126`, `cpu`, or the default macOS wheels) differs per machine.

## Service methods

- `plan_engine_setup {preset}` -> `{device, device_name, vram_bytes, torch_flavour,
  torch_installed_flavour, recreate_runtime, download_bytes_estimate,
  models_to_download, reason, estimate_note}`. Nothing is downloaded. Show the
  estimate and ask before `setup_engine`; `recreate_runtime` means the venv has
  the other torch flavour and will be replaced (never both flavours in one venv).
- `setup_engine {preset}` -> the plan fields plus `python`, `models`, `code`,
  `installed`. Progress stages: `runtime`, `download` (with `pct`, `bytes`,
  `total`), `models`.
- `separate {source, preset}` and `generate_library_stem_batch` run tracks
  through one worker per batch so models load once.
- `estimate_separation {preset, frames}` -> `{seconds, device}` from the
  per-machine store `<data>/separation-timings.json` (median of the last 10
  runs per preset and device, seconds per source second plus model-load
  seconds) or the registry priors before any run.

## Progress events

`{"event":"progress","stage":…,"message":…,"pct":0-100,"overall":0-100,"device":…}`.
Stages: `load-model`, `separate-vocals`, `separate-drums`, `separate` (one pass
yielding both), `postprocess`, and `device` once with `fallback: true` when the
GPU failed and the run continued on the CPU. `pct` restarts per stage;
`overall` is monotonic across the whole track. Batch items carry the current
`pct` too. Older events without `pct` are still valid.

## Worker protocol (`inference.py --serve`)

stdin: `{"op":"separate","input":wav,"output_dir":dir,"parts":["vocals","harmonics"]|["drums","harmonics"]}`
or `{"op":"quit"}`. stdout: progress events, then `{"event":"done","result":
result.json,"model_id","device","timing":{frames,load_seconds,separate_seconds,
postprocess_seconds,peak_device_bytes},"diagnostics"}` or
`{"event":"failed","error"}`. Library prints go to stderr. `result.json` keeps
the v1 part contract that `overcue_roles.py` consumes: 44.1 kHz 16-bit
`vocals.wav` and `harmonics.wav` with per-part `gain`. The 2-stem import path
(`engines.separate_drums`, caller holds the engine lock) asks for
`drums` + `harmonics` from an instrumental and gets both in the same convention.

## MP3 timeline

Players decode every MP3 frame. The audio helper does the same: for a
LAME-tagged file it reports `mp3.delay_frames` (encoder delay + 529) and
`mp3.padding_frames`, the frames a gapless decoder would drop, and keeps
them in the PCM. So the seven roles, the stem waveforms and the import
alignment all sit on the player's full-mix timeline, as OverCue Desktop 2.2.5
does by prepending silence. The manifest records `source.mp3` (with
`timeline: player`) and `runtime.latency_pad_frames` on the 96 kHz timeline.
An XZ Mods MP3 bundle whose manifest lacks `source.mp3` was written from a
trimmed decode; when `mp3_timing.probe` finds a LAME tag the library reports
`needs_regeneration` and asks for Prepare again. The XDJ-XZ runtime aligns
stems to its own decode live (search of 8192 frames), so playback there was
unaffected; the stem waveform files and fixed-offset players were 25 ms early.

## Memory

CPU runs keep the disk-backed buffers for long tracks. GPU runs batch chunks to
`(vram_bytes - gpu_reserve_bytes) / gpu_bytes_per_item` per separator, capped
by `max_batch`; the balanced preset peaks near 7.8 GB on a 16 GB card at batch
8 and fits 8 GB cards at the smaller batch. A CUDA failure at load or during a
pass is recorded in `diagnostics.device_error` and the pass reruns on the CPU.
