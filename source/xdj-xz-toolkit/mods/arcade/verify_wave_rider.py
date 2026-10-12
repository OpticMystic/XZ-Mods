"""Exercise the Wave Rider C engine and renderer, then build XZ ARM tests."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

from PIL import Image


ROOT = Path(__file__).resolve().parent


def run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, text=True, capture_output=capture)
    if capture:
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
    result.check_returncode()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compiler = args.zig.resolve()
    output = args.output.resolve()
    if not compiler.is_file():
        parser.error("--zig must name an existing Zig executable")
    output.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / name for name in (
        "wave_rider.c", "wave_rider_source.c", "wave_rider_render.c", "wave_rider_art.c", "game_motion_draw.c", "game_feel_art.c", "game_font.c", "test_wave_rider.c",
    )] + [ROOT.parent / "ui/ui_draw.c"]
    missing = [str(source) for source in sources if not source.is_file()]
    if missing:
        parser.error("Wave Rider sources are incomplete: " + ", ".join(missing))
    common = [str(compiler), "cc", "-O2", "-s", "-std=c11", "-Wall", "-Wextra",
              "-Werror", "-UNDEBUG", "-DXZ_WAVE_RIDER_SOURCE_PORTABLE"]
    focused = {}
    for name in ("angle", "expression", "difficulty"):
        binary = output / ("wave-rider-" + name + "-test.exe")
        inputs = [str(ROOT / "wave_rider.c"), str(ROOT / ("test_wave_rider_" + name + ".c"))]
        run(common + inputs + ["-o", str(binary)])
        checked = run([str(binary)], capture=True)
        (output / (name + "-tests.txt")).write_text(checked.stdout, encoding="utf8")
        focused[name] = True
        if name == "difficulty":
            arm_binary = output / "wave-rider-difficulty-test-arm"
            run(common + ["-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9"] + inputs + ["-lm", "-o", str(arm_binary)])
    host = output / "wave-rider-test.exe"
    run(common + list(map(str, sources)) + ["-o", str(host)])
    result = run([str(host), str(output)], capture=True)
    (output / "tests.txt").write_text(result.stdout, encoding="utf8")
    screenshots = []
    for path in sorted(output.glob("wave-rider-*.ppm")):
        destination = path.with_suffix(".png")
        with Image.open(path) as screenshot:
            screenshot.save(destination)
        screenshots.append(destination.name)
    if not screenshots:
        raise RuntimeError("The renderer fixture produced no screenshots")
    arm = output / "wave-rider-test-arm"
    target = ["-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9"]
    run(common + target + list(map(str, sources)) + ["-lm", "-o", str(arm)])
    sys.path.insert(0, str(ROOT.parent))
    from build import inspect
    arm_metadata = inspect(arm)
    arm_metadata.update({"executed_on_device": False,
                         "physical_jog_verified": False,
                         "track_position_verified_on_device": False})
    (output / "arm.json").write_text(json.dumps(arm_metadata, indent=2) + "\n", encoding="utf8")
    source_inputs = [str(ROOT / name) for name in (
        "wave_rider_source.c", "test_wave_rider_source.c",
    )]
    source_common = common + ["-DXZ_WAVE_RIDER_SOURCE_PORTABLE"]
    source_host = output / "wave-rider-source-test.exe"
    run(source_common + source_inputs + ["-o", str(source_host)])
    source_result = run([str(source_host)], capture=True)
    (output / "source-tests.txt").write_text(source_result.stdout, encoding="utf8")
    source_arm = output / "wave-rider-source-test-arm"
    run(source_common + target + source_inputs + ["-lm", "-o", str(source_arm)])
    source_metadata = inspect(source_arm)
    source_metadata["executed_on_device"] = False
    (output / "source-arm.json").write_text(json.dumps(source_metadata, indent=2) + "\n", encoding="utf8")
    from ui.ui_sources import UI_SOURCES
    runtime_sources = [ROOT / "test_wave_rider_runtime.c"]
    runtime_sources += [ROOT / name for name in (
        "arcade.c", "render.c", "input.c", "clock.c", "native_clock.c", "wave_rider.c",
        "wave_rider_runtime.c", "wave_rider_render.c", "wave_rider_art.c", "game_motion_draw.c", "game_feel_art.c", "game_font.c",
    )]
    runtime_sources += [ROOT.parent / "ui" / name for name in UI_SOURCES]
    runtime_sources += [ROOT.parent / "ui" / name for name in (
        "native_touch.c", "stem_pads.c", "beat_jump.c", "wave_viewport.c",
    )]
    runtime_sources += [ROOT.parent / name for name in (
        "key/music.c", "key/browser.c", "key/catalog.c", "key/traffic.c", "settings.c", "usb_profile.c", "doom/native_bridge.c", "doom/wad_catalog.c",
        "ota/native_update.c", "audio/vendor/sha256/sha256.c","pc_control/stems.c",
    )]
    runtime_arm = output / "wave-rider-runtime-test-arm"
    run(common + target + list(map(str, runtime_sources)) +
        ["-pthread", "-ldl", "-lm", "-o", str(runtime_arm)])
    runtime_metadata = inspect(runtime_arm)
    runtime_metadata.update({"executed_on_device": False, "source_is_fixture": True})
    (output / "runtime-arm.json").write_text(json.dumps(runtime_metadata, indent=2) + "\n", encoding="utf8")
    benchmarks = [json.loads(line.removeprefix("BENCH_JSON "))
                  for line in result.stdout.splitlines() if line.startswith("BENCH_JSON ")]
    if not benchmarks:
        raise RuntimeError("The renderer fixture did not measure frame cost")
    report = {"host_tests_passed": True, "source_tests_passed": True, "arm_abi_passed": True,
              "focused_tests": focused,
              "screenshots": screenshots, "benchmarks": benchmarks,
              "native_lcd_cadence_verified": False,
              "arm_executable": arm.name, "source_arm_executable": source_arm.name,
              "runtime_arm_executable": runtime_arm.name}
    (output / "verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("PASS Wave Rider host behavior/rendering and ARM32 soft-float ABI build")


if __name__ == "__main__":
    main()
