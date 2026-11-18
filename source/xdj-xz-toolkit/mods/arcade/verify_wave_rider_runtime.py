"""Run the real UI/input adapter on Linux with address and undefined-behavior checks."""
import argparse
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent
sys.path.insert(0, str(root.parent))
from ui.ui_sources import UI_SOURCES

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
sources = [root / name for name in (
    "test_wave_rider_runtime.c", "arcade.c", "render.c", "input.c", "clock.c",
    "native_clock.c", "wave_rider.c", "wave_rider_runtime.c", "wave_rider_render.c", "wave_rider_art.c", "game_motion_draw.c", "game_feel_art.c", "game_font.c",
)]
sources += [root.parent / "ui" / name for name in UI_SOURCES]
sources += [root.parent / "ui" / name for name in (
    "native_touch.c", "stem_pads.c", "beat_jump.c", "wave_viewport.c",
    # The included runtime-controls checks need the deck-detail notices, as verify_runtime_controls.py links them.
    "extra_detail.c", "extra_lane.c", "layered_wave.c",
)]
sources += [root.parent / name for name in (
    "key/music.c", "key/control.c", "key/browser.c", "key/catalog.c", "key/traffic.c", "settings.c", "usb_profile.c", "doom/native_bridge.c", "doom/wad_catalog.c",
    "ota/native_update.c", "audio/vendor/sha256/sha256.c","pc_control/stems.c",
)]
binary = args.output.resolve() / "wave-rider-runtime-linux"
subprocess.run(["gcc", "-O1", "-g", "-std=c11", "-UNDEBUG", "-Wall", "-Wextra",
    "-Werror", "-Wno-misleading-indentation", "-fsanitize=address,undefined",
    "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", *map(str, sources),
    "-pthread", "-ldl", "-lm", "-o", str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True)
(args.output / "runtime-linux.txt").write_text(result.stdout + result.stderr)
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
result.check_returncode()
