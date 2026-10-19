"""Host check for shell_mysettings.c: MY SETTINGS onto the stock UTILITY items, on the codec's public fixtures.

POSIX host (the codec writes with fsync and rename): gcc with ASan/UBSan, through WSL on Windows.
Also builds shell_mysettings.o for the deck (ARM32 soft-float) when --zig is given.
"""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def linux_path(path: Path) -> str:
    text = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + text[0].lower() + text[2:]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--zig")
args = parser.parse_args()
prefix = ["wsl", "-d", "Ubuntu", "--"] if os.name == "nt" else []
path = linux_path if os.name == "nt" else str
sources = [ROOT / name for name in ("my_settings.c", "shell_utility.c", "shell_mysettings.c", "test_shell_mysettings.c")]
binary = "/tmp/xz-test-shell-mysettings"
subprocess.run(prefix + ["gcc", "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-I" + path(ROOT),
                         *(path(p) for p in sources), "-o", binary], check=True)
subprocess.run(prefix + [binary], check=True)
negative = subprocess.run(prefix + ["gcc", "-std=c11", "-DNDEBUG", "-I" + path(ROOT), "-c", path(ROOT / "test_shell_mysettings.c"),
                                    "-o", "/tmp/xz-negative-shms.o"], capture_output=True, text=True)
if negative.returncode == 0 or "Shell MY SETTINGS acceptance requires active assertions" not in negative.stderr:
    raise RuntimeError("Missing assertion rejection")
if args.zig:
    with tempfile.TemporaryDirectory(prefix="xz-shms-arm-") as folder:
        subprocess.run([args.zig, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-O2", "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-c", str(ROOT / "shell_mysettings.c"), "-o",
                        str(Path(folder) / "shell_mysettings.o")], check=True)
    print("PASS shell_mysettings.o: ARM32 soft-float object")
print("PASS shell MY SETTINGS host check")
