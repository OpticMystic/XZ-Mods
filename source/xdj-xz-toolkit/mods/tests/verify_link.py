"""Ableton Link policy, master shape and runtime: host sanitizers and ARM32 execution, plus the header check."""
import argparse, subprocess, sys
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument("--out", required=True, type=Path)
p.add_argument("--no-header-check", action="store_true", help="skip the comparison with the xz-link-peer workspace header")
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]
if not a.no_header_check:
    subprocess.run([sys.executable, str(root / "link/sync_from_workspace.py"), "--check"], check=True)
def linux(path):
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]
def run(*args):
    subprocess.run(["wsl", "-d", "Ubuntu", "--cd", linux(root), "--exec", *map(str, args)], check=True)
tests = {
    "link-policy": ["tests/test_link_policy.c", "link_policy.c", "link_clock.c"],
    "link-runtime": ["-pthread", '-DXZ_LINK_DIR="/tmp/xz-link-test"', "tests/test_link_runtime.c", "link_runtime.c",
                     "link_policy.c", "link_clock.c", "djlink_facts.c"],
}
for name, sources in tests.items():
    for arch, compiler in [("host", "gcc"), ("arm", "arm-linux-gnueabi-gcc")]:
        binary = a.out / f"test-{name}-{arch}"
        flags = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"] if arch == "host" else ["-march=armv7-a", "-mfpu=vfpv3", "-mfloat-abi=softfp"]
        run(compiler, "-std=c11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", *flags, *sources, "-lm", "-o", linux(binary))
        run(*(["qemu-arm", "-L", "/usr/arm-linux-gnueabi"] if arch == "arm" else []), linux(binary))
print("PASS Ableton Link policy simulation and runtime, host and ARM; header matches the workspace")
