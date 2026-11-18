"""Run the real VFP player object, stretch, and AIFF checks under ARM emulation."""
import argparse
import json
from pathlib import Path
import subprocess
from build import inspect
from extra_player_fixtures import fixtures as player_fixtures
from verify_extra_features import fixtures as format_fixtures, mp3_fixture

ROOT = Path(__file__).resolve().parent


def linux(path):
    text = Path(path).resolve().as_posix()
    return "/mnt/" + text[0].lower() + text[2:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    data = args.output / "fixtures"; data.mkdir()
    player_fixtures(data); format_fixtures(data)
    mp3_fixture(data / "seek.mp3")
    base = [args.zig, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
            "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-pthread",
            "-I" + str(ROOT), "-Wl,-T," + str(ROOT / "xz-relocations.ld")]
    binaries = {}
    # test_extra_player.c is written for the reference jog scale (XZ_XP_JOG_TICKS_PER_REV 360); the production object
    # (extra-player-vfp.o) is built for the hardware estimate (build.py --jog-counts-per-rev). Compile the same source
    # with the production VFP flags at the reference scale; the jog constant is the only difference.
    reference = args.output / "extra-player-vfp-reference.o"
    subprocess.run([args.zig, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-mfloat-abi=softfp", "-mfpu=vfpv3",
                    "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-fvisibility=hidden", "-c",
                    str(ROOT / "extra_player.c"), "-o", str(reference)], check=True)
    for name, sources, flags in (
        ("stretch", [ROOT / "test_extra_stretch.c"], ["-mfloat-abi=softfp", "-mfpu=vfpv3"]),
        ("aiff", [ROOT / "audio/tests/test_aiff.c"], []),
        ("seek", [args.build / "stem-media-vfp.o", ROOT / "audio/stem_mp3.c",
                  ROOT / "audio/stem_decode.c", ROOT / "audio/tests/test_media_seek.c"], []),
        ("player", [reference, args.build / "stem-media-vfp.o",
                    *[ROOT / name for name in ("key/keyshift.c", "key/music.c", "key/control.c", "deck_select.c", "anlz_extra.c", "shell_analysis.c", "ui/layered_wave_source.c",
                                               "ui/layered_wave.c", "audio/stem_mp3.c",
                                               "audio/stem_decode.c", "test_extra_player.c")]], []),
    ):
        binary = args.output / name
        subprocess.run([*base, *flags, *map(str, sources), "-lm", "-o", str(binary)], check=True)
        binaries[name] = inspect(binary)
        command = ["wsl", "-d", "Ubuntu", "--", "qemu-arm", "-cpu", "cortex-a9",
                   "/usr/arm-linux-gnueabi/lib/ld-linux.so.3", "--library-path",
                   "/usr/arm-linux-gnueabi/lib", linux(binary)]
        if name in ("aiff", "player"):
            command.append(linux(data))
        elif name == "seek":
            command.append(linux(data / "seek.mp3"))
        result = subprocess.run(command, capture_output=True, text=True, timeout=3600)  # the whole player suite under qemu
        (args.output / (name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        result.check_returncode()
        print(result.stdout.splitlines()[-1], flush=True)
    report = {"passed": True, "controller_access": False, "emulated": True,
              "hardware_performance_verified": False, "binaries": binaries}
    (args.output / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("PASS ARM32/GLIBC_2.4/VFP player, stretch and AIFF; hardware performance remains unmeasured")


if __name__ == "__main__":
    main()
