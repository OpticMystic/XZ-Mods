"""Run a bounded GZDoom scene in private XZ RAM without restarting rbp."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import sys
import time
import uuid

from PIL import Image

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from device_smoke import command, upload
from doom.device_verify import transfer

HOST = "169.254.168.59"
CLIENT = "169.254.168.58"
HELPER = "/dev/shm/xz-doom-5a156a2ef034/transfer"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def put(remote: str, path: Path) -> None:
    data = path.read_bytes()
    existing = command(HOST, f"if [ -f {shlex.quote(remote)} ]; then md5sum {shlex.quote(remote)}; fi")
    if existing:
        if existing.split()[0] == hashlib.md5(data).hexdigest():
            return
        raise RuntimeError("Refusing to replace different diagnostic input: " + remote)
    parts = []
    for index, offset in enumerate(range(0, len(data), 32 * 1024 * 1024)):
        chunk = data[offset:offset + 32 * 1024 * 1024]
        part = remote + f".part{index}"
        transfer(HOST, CLIENT, HELPER, "receive", part, len(chunk), chunk)
        parts.append(part)
    quoted = " ".join(shlex.quote(part) for part in parts)
    result = command(HOST, f"set -C; cat {quoted} > {shlex.quote(remote)}; md5sum {shlex.quote(remote)}")
    if result.split()[0] != hashlib.md5(data).hexdigest():
        raise RuntimeError("Game file transfer readback failed")
    command(HOST, "rm " + quoted)


def main() -> None:
    global HELPER
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--iwad", type=Path, required=True)
    parser.add_argument("--addon", type=Path)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--remote", help="Reuse only a previous directory created by this verifier")
    parser.add_argument("--seconds", type=int, default=8)
    parser.add_argument("--display", action="store_true")
    parser.add_argument("--trace-library", type=Path, help="Private fault tracer for an isolated diagnostic")
    parser.add_argument("--device-iwad", help="Verified base data already stored on mounted USB")
    parser.add_argument("--device-addon", help="Verified MyHouse already stored on mounted USB")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 30:
        parser.error("Scene duration must be between 1 and 30 seconds")
    for path in (args.device_iwad, args.device_addon):
        if path and (not path.startswith("/media/usb") or ".." in Path(path).parts or "\n" in path):
            parser.error("Device game data must be under a mounted USB directory")
    if args.device_addon and not args.addon:
        parser.error("Provide the local addon for USB integrity verification")
    watchdog = args.build / "gz-watchdog"
    if not watchdog.is_file():
        raise ValueError("Build the native diagnostic watchdog before running scenes")
    helper = args.build / "gz-transfer"
    if not helper.is_file():
        raise ValueError("Build the private transfer helper before running scenes")
    before = command(HOST, "pidof rbp").strip()
    if not before.isdigit():
        raise RuntimeError("Expected one running DJ application")
    firmware = command(HOST, f"md5sum /proc/{before}/exe").split()[0]
    if firmware != "6a7ccb454e52afa26a73f3380706c9ca":
        raise RuntimeError("Expected the qualified XZ 1.26 application")
    remote = args.remote or "/dev/shm/xz-gz-test-" + uuid.uuid4().hex
    if not remote.startswith("/dev/shm/xz-gz-test-") or not remote.rsplit("-", 1)[-1].isalnum():
        raise ValueError("Unrecognized diagnostic RAM directory")
    args.evidence.mkdir(parents=True, exist_ok=True)
    if not args.remote:
        result = command(HOST, f"mkdir {remote} && mount -t tmpfs -o size=256m tmpfs {remote}; echo RAM_STATUS=$?")
        if "RAM_STATUS=0" not in result:
            raise RuntimeError(result)
        HELPER = remote + "/gz-transfer"
        upload(HOST, HELPER, helper.read_bytes())
        command(HOST, f"chmod 700 {HELPER}")
        for name in ["gzdoom", "libzmusiclite.so.1", "gzdoom.pk3", "game_support.pk3",
                     "brightmaps.pk3", "lights.pk3", "game_widescreen_gfx.pk3"]:
            print("Staging " + name, flush=True)
            put(remote + "/" + name, args.build / name)
        print("Staging base data", flush=True)
        if not args.device_iwad:
            put(remote + "/base.wad", args.iwad)
        if args.addon and not args.device_addon:
            print("Staging MyHouse", flush=True)
            put(remote + "/myhouse.pk3", args.addon)
    else:
        HELPER = remote + "/gz-transfer"
        put(remote + "/gzdoom-" + digest(args.build / "gzdoom")[:12], args.build / "gzdoom")
        if args.addon and not args.device_addon:
            put(remote + "/myhouse.pk3", args.addon)
    for device_path, local_path in ((args.device_iwad, args.iwad), (args.device_addon, args.addon)):
        if device_path:
            observed = command(HOST, "md5sum " + shlex.quote(device_path)).split()[0]
            if observed != hashlib.md5(local_path.read_bytes()).hexdigest():
                raise ValueError("USB game data differs from the reviewed local file")
    watchname = "watchdog-" + digest(watchdog)[:12]
    put(remote + "/" + watchname, watchdog)
    preload = ""
    if args.trace_library:
        trace_name = "trace-" + digest(args.trace_library)[:12] + ".so"
        put(remote + "/" + trace_name, args.trace_library)
        preload = remote + "/" + trace_name
    binary = "gzdoom" if not args.remote else "gzdoom-" + digest(args.build / "gzdoom")[:12]
    tag = uuid.uuid4().hex[:8]
    snapshot = "scene-" + tag + ".rgba"
    logfile = "run-" + tag + ".log"
    config = "config-" + tag + ".ini"
    invocation = (f"chmod 700 {binary} {watchname} && "
                  f"LD_PRELOAD={preload} SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy XZ_GZ_OFFSCREEN=1 "
                  f"XZ_GZ_DISPLAY={int(args.display)} "
                  f"XZ_GZ_SECONDS={args.seconds} XZ_GZ_SNAPSHOT={snapshot} ./{watchname} 180 {remote}/{binary} "
                  f"-nojoy -nosound -nomusic -iwad {shlex.quote(args.device_iwad or 'base.wad')} -config {config} "
                  + ("-file " + shlex.quote(args.device_addon or 'myhouse.pk3') + " " if args.addon else "")
                  + f"-width 320 -height 200 +vid_preferbackend 2 +gl_es 1 +vid_fullscreen 0 "
                  + "-warp 1")
    script = args.evidence / "run.sh"
    script.write_text(f"#!/bin/sh\ncd {remote} || exit 1\n{invocation} >{logfile} 2>&1 &\n"
                      + f"game=$!\necho $game >pid-{tag}\nwait $game\necho $? >status-{tag}\n", newline="\n")
    put(remote + f"/run-{tag}.sh", script)
    print("Running native engine in " + remote, flush=True)
    try:
        # Keep one shell for the scene. Repeated status connections allocate
        # extra PTYs on the old daemon and are unnecessary with a local guard.
        command(HOST, f"sh {remote}/run-{tag}.sh", timeout=190)
        status = command(HOST, f"cat {remote}/status-{tag}").strip()
        run = "GAME_STATUS=" + status
        log = command(HOST, f"cat {remote}/{logfile}")
        after = command(HOST, "pidof rbp").strip()
    except (ValueError, OSError) as error:
        run = "GAME_STATUS=control_connection_lost"
        log = type(error).__name__ + ": " + str(error)
        after = None
    ok = "GAME_STATUS=0" in run and "XZ_GZ_EXIT" in log and "XZ_GZ_SNAPSHOT" in log and before == after
    report = {"format": "xz-gzdoom-native-scene/1", "remote": remote, "run": run,
              "log": log, "rbp_before": before, "rbp_after": after,
              "engine_sha256": digest(args.build / "gzdoom"), "iwad_sha256": digest(args.iwad),
              "addon_sha256": digest(args.addon) if args.addon else None,
              "game_logic_on_xz": ok, "display_verified": False,
              "physical_controls_verified": False, "audio_verified": False,
              "usb_boot_verified": False, "pass": ok}
    if "XZ_GZ_SNAPSHOT" in log:
        pixels = transfer(HOST, CLIENT, HELPER, "send", remote + "/" + snapshot, 320 * 200 * 4)
        Image.frombytes("RGBA", (320, 200), pixels).transpose(Image.Transpose.FLIP_TOP_BOTTOM).save(args.evidence / "scene.png")
    (args.evidence / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
