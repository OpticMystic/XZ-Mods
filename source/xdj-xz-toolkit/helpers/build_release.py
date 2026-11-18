# SPDX-License-Identifier: MIT
"""Builds the two network helpers for a release and lays out what the payload needs, with hashes.

    python helpers/build_release.py --out <dir> [--mirror <dir with tools/ and third_party/link>] [--zig <zig.exe>]

Runs djlink/build.py (wire core tests on host and ARM, the static musl helper) and linkpeer/build.py (Ableton
Link 4.1.1 at its pinned commit, the static musl helper, licence check, source bundle), then writes:

  <out>/runtime/quad/xz-djlink                 static ARM (musl) Pro DJ Link helper        -> USB tools/xz-djlink
  <out>/runtime/quad/xz-link-peer              static ARM (musl) Ableton Link helper       -> USB tools/xz-link-peer
  <out>/licenses/xz-djlink/...                 notices for what is linked into xz-djlink
  <out>/licenses/xz-link-peer/...              GPL v2 text, Link, asio, musl, libc++ (3), compiler-rt, LICENSING.md
  <out>/licenses/source/xz-link-peer-source.tar.gz   complete corresponding source (GPL v2 section 3(a))
  <out>/helpers-manifest.json                  bytes, sha256 and md5 of every file above, and the build inputs

The layout mirrors the app's resources/ folder (see link/PACKAGING.md of the 2026-11-15 handoff). Both binaries are
reproducible: built from the same source they are byte-identical; the manifest records the hashes of the builds
that ran on the unit, and the run fails if a binary differs from them unless --allow-new is given (a source change).
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
UNIT_TESTED = {  # md5 of the builds that ran on the unit (r29, 2026-11-15)
    "runtime/quad/xz-djlink": "1f521e16a20c575b6b78776573786ca7",
    "runtime/quad/xz-link-peer": "a7c084688c9590871cb18410d7749e34",
}


def digest(path: Path, algo: str) -> str:
    h = hashlib.new(algo)
    h.update(path.read_bytes())
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--mirror", type=Path, help="passed to linkpeer/build.py")
    ap.add_argument("--zig", help="passed to djlink/build.py (default: the ziglang Python package)")
    ap.add_argument("--allow-new", action="store_true", help="accept binaries that differ from the unit-tested ones")
    a = ap.parse_args()
    if a.out.exists():
        raise SystemExit(f"{a.out} exists; the output folder must be new")
    dj = [sys.executable, str(HERE / "djlink" / "build.py"), "--fuzz", "200000", "--arm-fuzz", "50000"]
    if a.zig:
        dj += ["--zig", a.zig]
    subprocess.run(dj, check=True)
    lp = [sys.executable, str(HERE / "linkpeer" / "build.py")]
    if a.mirror:
        lp += ["--mirror", str(a.mirror)]
    subprocess.run(lp, check=True)

    files = {
        "runtime/quad/xz-djlink": HERE / "djlink" / "out" / "xz-djlink-arm",
        "runtime/quad/xz-link-peer": HERE / "linkpeer" / "out" / "arm-musl" / "xz-link-peer",
        "licenses/source/xz-link-peer-source.tar.gz": HERE / "linkpeer" / "out" / "xz-link-peer-source.tar.gz",
    }
    for p in sorted((HERE / "linkpeer" / "out" / "licenses").iterdir()):
        files["licenses/xz-link-peer/" + p.name] = p
    for p in sorted((HERE / "djlink" / "licenses").iterdir()):
        files["licenses/xz-djlink/" + p.name] = p
    files["licenses/xz-djlink/XZ-Mods-MIT.txt"] = HERE.parent / "LICENSE"
    manifest = {"files": {}, "inputs": json.loads((HERE / "linkpeer" / "out" / "build-info.json").read_text())}
    manifest["inputs"].pop("binaries", None)
    manifest["inputs"]["source_commit"] = subprocess.run(["git", "-C", str(HERE), "rev-parse", "HEAD"], check=True,
                                                         capture_output=True, text=True).stdout.strip()
    for name, src in files.items():
        dest = a.out / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        manifest["files"][name] = {"bytes": dest.stat().st_size, "sha256": digest(dest, "sha256"), "md5": digest(dest, "md5")}
    drift = {n: manifest["files"][n]["md5"] for n, want in UNIT_TESTED.items() if manifest["files"][n]["md5"] != want}
    manifest["unit_tested"] = {n: manifest["files"][n]["md5"] == want for n, want in UNIT_TESTED.items()}
    (a.out / "helpers-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for name, f in manifest["files"].items():
        print(f"{f['sha256']}  {f['bytes']:>9}  {name}")
    if drift and not a.allow_new:
        print(f"FAIL: differs from the unit-tested build: {drift} (pass --allow-new after a source change)")
        return 1
    print("PASS: both helpers built; " + ("identical to the unit-tested builds" if not drift else "NEW builds (--allow-new)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
