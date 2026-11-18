"""MASTER REC recorder seam and recording tracklist: host sanitizers and ARM32 execution under qemu, then the
mutation check (each rule broken once in a copy; the tests must fail). Runs in WSL; writes only to --out."""
import argparse, subprocess, sys
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument("--out", required=True, type=Path)
p.add_argument("--no-mutations", action="store_true", help="skip the mutation check (about two minutes)")
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]
def linux(path):
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]
def run(*args):
    subprocess.run(["wsl", "-d", "Ubuntu", "--cd", linux(root), "--exec", *map(str, args)], check=True)
tests = {
    "recorder-state": ["-DXZ_RECORDER_STATE_NO_RUNTIME", "recorder_state.c", "tests/test_recorder_state.c"],
    "tracklist": ["-D_GNU_SOURCE", "tracklist.c", "tests/tracklist/test_tracklist.c"],
    "tracklist-file": ["-D_GNU_SOURCE", "-Wno-format-truncation", "tracklist.c", "tracklist_file.c", "tests/tracklist/test_tracklist_file.c"],
    "tracklist-runtime": ["-D_GNU_SOURCE", "-pthread", "-Wno-unused-function", "-I.", "tracklist.c", "tracklist_file.c",
                          "tests/tracklist/test_tracklist_runtime.c"],
    # USB STOP: the tracklist as a holder of its stick (real threads, the board of usb_release.c, a folder as the stick).
    "tracklist-stop": ["-pthread", "-Wno-format-truncation", "-Wno-unused-function", "-I.", "-DXZ_USB_RELEASE_NO_RUNTIME", "tracklist.c",
                       "tracklist_file.c", "usb_release.c", "tests/tracklist/test_tracklist_stop.c"],
}
stick = {"tracklist-stop"}   # these take a folder to stand in for the stick
for name, sources in tests.items():
    for arch, compiler in [("host", "gcc"), ("arm", "arm-linux-gnueabi-gcc")]:
        binary = a.out / f"test-{name}-{arch}"
        flags = ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"] if arch == "host" else ["-march=armv7-a"]
        run(compiler, "-std=c11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", *flags, *sources, "-o", linux(binary))
        run(*(["qemu-arm", "-L", "/usr/arm-linux-gnueabi"] if arch == "arm" else []), linux(binary),
            *([linux(a.out / f"stick-{arch}")] if name in stick else []))
if not a.no_mutations:
    run("python3", "tests/tracklist/mutate.py", linux(a.out / "mutations"))
print("PASS recorder seam (windows, guards, path text) and tracklist core, writer, runtime, USB STOP: host sanitizers and ARM32"
      + ("" if a.no_mutations else "; every mutation caught"))
