"""Runtime Jog LCD bridge fixtures: host sanitizers and ARM32 execution."""
import argparse, subprocess
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument("--out", required=True, type=Path)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]
def linux(path):
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]
def run(*args):
    subprocess.run(["wsl", "-d", "Ubuntu", "--cd", linux(root), "--exec", *map(str, args)], check=True)
for arch, compiler in [("host", "gcc"), ("arm", "arm-linux-gnueabi-gcc")]:
    binary = a.out / f"test-jog-display-runtime-{arch}"
    flags = ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if arch == "host" else ["-march=armv7-a"]
    run(compiler, "-std=c11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", *flags,
        "tests/test_jog_display_runtime.c", "jog_display_runtime.c", "jog_display.c", "-o", linux(binary))
    run(*(["qemu-arm", "-L", "/usr/arm-linux-gnueabi"] if arch == "arm" else []), linux(binary))
print("PASS Jog LCD runtime bridge host/ARM fixtures; lock-free publish, EXTRA/STALE/REPLAY verified")
