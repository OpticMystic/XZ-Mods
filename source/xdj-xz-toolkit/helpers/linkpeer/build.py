"""Build xz-link-peer (the Ableton Link helper) for the lab host and for the XDJ-XZ.

Run from Windows (Python 3) with WSL Ubuntu present:
    python build.py                  fetch or check the pinned sources, build every variant, record facts
    python build.py --mirror DIR     take the pinned archives (DIR/tools/) and a Link clone (DIR/third_party/link)
                                     from DIR instead of the network; the pins below are checked either way
    python build.py --check          only verify pins and hashes of what is already here

Pins (the build refuses anything else):
  Ableton Link     tag Link-4.1.1, commit 5ba907ae027a8c2ec8824463e37296dcefc5e9be (GPL-2.0-or-later)
  asio-standalone  commit 8806a6803cde7054c3049d3666d3ec36786568c5 (asio 1.38.2, Boost Software License 1.0)
  Zig              0.16.0 x86_64-linux archive (the compiler the repository uses; musl 1.2.5 and libc++ inside)

Outputs in out/: host/, arm-musl/, arm-glibc213/, arm-musl-qemushim/ (test only), licenses/,
xz-link-peer-source.tar.gz (the complete corresponding source, see LICENSING.md) and build-info.json.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
MIRROR: Path | None = None  # --mirror
LINK_URL = "https://github.com/Ableton/link.git"
LINK_TAG = "Link-4.1.1"
LINK_COMMIT = "5ba907ae027a8c2ec8824463e37296dcefc5e9be"
ASIO_COMMIT = "8806a6803cde7054c3049d3666d3ec36786568c5"
ASIO_TGZ = f"asio-{ASIO_COMMIT}.tar.gz"
ASIO_URL = f"https://codeload.github.com/chriskohlhoff/asio/tar.gz/{ASIO_COMMIT}"
ASIO_SHA256 = "ca7f6c14f2bf91e61c7e81fb693f2f8fc86f93e85520d5fc7fd035d0f666bb35"
ZIG_TXZ = "zig-x86_64-linux-0.16.0.tar.xz"
ZIG_URL = f"https://ziglang.org/download/0.16.0/{ZIG_TXZ}"
ZIG_SHA256 = "70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00"
ZIG_HOME = "$HOME/xz-linkpeer"


def linux(path: Path) -> str:
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]


def wsl(script: str, capture=False) -> str:
    r = subprocess.run(["wsl", "-d", "Ubuntu", "--exec", "bash", "-c", script], check=True,
                       stdout=subprocess.PIPE if capture else None, text=True)
    return r.stdout if capture else ""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def git(*args, cwd=None) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, stdout=subprocess.PIPE, text=True).stdout.strip()


def archive(name: str, url: str, digest: str) -> Path:
    """A pinned archive in tools/: copied from the spike when it has one, else downloaded."""
    tools = HERE / "tools"
    tools.mkdir(exist_ok=True)
    target = tools / name
    if not target.is_file():
        local = MIRROR / "tools" / name if MIRROR else None
        if local and local.is_file():
            shutil.copyfile(local, target)
        else:
            print(f"download {url}")
            urllib.request.urlretrieve(url, target)
    got = sha256(target)
    if got != digest:
        raise SystemExit(f"{name}: sha256 {got} is not the pinned {digest}")
    return target


def link_checkout(check_only: bool) -> str:
    """third_party/link at the pinned commit. Returns where it was fetched from."""
    link = HERE / "third_party" / "link"
    origin = "existing checkout"
    if not (link / ".git").exists():
        if check_only:
            raise SystemExit("third_party/link is missing; run without --check")
        link.parent.mkdir(parents=True, exist_ok=True)
        if MIRROR:
            # A local clone of the same repository. The commit hash below is the identity check.
            git("clone", "--quiet", "--no-checkout", str(MIRROR / "third_party" / "link"), str(link))
            git("remote", "set-url", "origin", LINK_URL, cwd=link)
            origin = "local mirror of " + LINK_URL
        else:
            git("clone", "--quiet", "--no-checkout", LINK_URL, str(link))
            origin = LINK_URL
        git("-c", "advice.detachedHead=false", "checkout", "--quiet", LINK_COMMIT, cwd=link)
    head = git("rev-parse", "HEAD", cwd=link)
    if head != LINK_COMMIT:
        raise SystemExit(f"third_party/link is at {head}, not the pinned {LINK_COMMIT}")
    submodule = git("ls-tree", "HEAD", "modules/asio-standalone", cwd=link).split()[2]
    if submodule != ASIO_COMMIT:
        raise SystemExit(f"Link pins asio {submodule}, expected {ASIO_COMMIT}")
    return origin


def readelf_facts(path: Path) -> dict:
    p = linux(path)
    header = wsl(f"arm-linux-gnueabi-readelf -h '{p}' || readelf -h '{p}'", capture=True)
    attrs = wsl(f"arm-linux-gnueabi-readelf -A '{p}' 2>/dev/null || true", capture=True)
    dyn = wsl(f"arm-linux-gnueabi-readelf -d '{p}' 2>/dev/null || true", capture=True)
    vers = wsl(f"arm-linux-gnueabi-readelf -V '{p}' 2>/dev/null || true", capture=True)
    progs = wsl(f"arm-linux-gnueabi-readelf -l '{p}' 2>/dev/null || true", capture=True)
    glibc = sorted(set(re.findall(r"GLIBC_([0-9.]+)", vers)), key=lambda v: tuple(int(x) for x in v.split(".")))
    flags = re.search(r"Flags:\s+(.*)", header)
    return {
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
        "machine": (re.search(r"Machine:\s+(.*)", header) or [None, None])[1],
        "flags": flags.group(1).strip() if flags else None,
        "cpu": (re.search(r"Tag_CPU_name: \"?([^\"\n]*)", attrs) or [None, None])[1],
        "fp": (re.search(r"Tag_FP_arch: (.*)", attrs) or [None, None])[1],
        "abi_vfp_args": (re.search(r"Tag_ABI_VFP_args: (.*)", attrs) or [None, None])[1],
        "needed": re.findall(r"\(NEEDED\)\s+Shared library: \[(.*)\]", dyn),
        "interpreter": (re.search(r"Requesting program interpreter: (.*)\]", progs) or [None, None])[1],
        "static": "INTERP" not in progs and "(NEEDED)" not in dyn,
        "glibc_versions": glibc,
    }


def licences(zig_dir: str) -> None:
    """licenses/ (in the repository) holds every notice the binaries need, byte for byte as upstream ships them; it is
    checked against the pinned Link commit, the asio archive and the pinned Zig, then copied to out/ beside each
    device build."""
    committed = HERE / "licenses"
    out = HERE / "out" / "licenses"
    link = HERE / "third_party" / "link"
    expected = {
        "Ableton-Link-LICENSE.md": git_bytes(link, "LICENSE.md"),
        "GNU-GPL-v2.0.md": git_bytes(link, "GNU-GPL-v2.0.md"),
        "asio-LICENSE_1_0.txt": wsl_bytes(f"tar -xzOf '{linux(HERE / 'tools' / ASIO_TGZ)}' --wildcards '*/LICENSE_1_0.txt'"),
        "asio-COPYING": wsl_bytes(f"tar -xzOf '{linux(HERE / 'tools' / ASIO_TGZ)}' --wildcards '*/COPYING'"),
        "musl-COPYRIGHT": wsl_bytes(f"cat {zig_dir}/lib/libc/musl/COPYRIGHT"),
        "libcxx-LICENSE.TXT": wsl_bytes(f"cat {zig_dir}/lib/libcxx/LICENSE.TXT"),
        "libcxxabi-LICENSE.TXT": wsl_bytes(f"cat {zig_dir}/lib/libcxxabi/LICENSE.TXT"),
        "libunwind-LICENSE.TXT": wsl_bytes(f"cat {zig_dir}/lib/libunwind/LICENSE.TXT"),
        "zig-compiler-rt-LICENSE.txt": wsl_bytes(f"cat {zig_dir}/LICENSE"),
    }
    for name, text in expected.items():
        data = text if isinstance(text, bytes) else text.encode("utf-8")
        if (committed / name).read_bytes() != data:
            raise SystemExit(f"licenses/{name} differs from the pinned upstream text")
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(committed, out)
    shutil.copyfile(HERE / "LICENSING.md", out / "LICENSING.md")
    for variant in ("arm-musl", "arm-glibc213"):
        dest = HERE / "out" / variant / "licenses"
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(out, dest)


def wsl_bytes(script: str) -> bytes:
    return subprocess.run(["wsl", "-d", "Ubuntu", "--exec", "bash", "-c", script], check=True, stdout=subprocess.PIPE).stdout


def git_bytes(repo: Path, path: str) -> bytes:
    """A file as the pinned commit holds it (a Windows checkout may have rewritten its line endings)."""
    return subprocess.run(["git", "show", f"{LINK_COMMIT}:{path}"], cwd=repo, check=True, stdout=subprocess.PIPE).stdout


def source_bundle(zig_dir: str) -> Path:
    """GPL section 3(a): Link at the pinned commit, asio, the helper source and the build scripts, and the sources of
    the toolchain runtime linked into the static binary (musl, libc++, libc++abi, libunwind, compiler-rt) exactly as
    the pinned Zig archive carries them. Byte-for-byte reproducible: fixed order, owner and time, gzip without a name
    or time stamp."""
    bundle = HERE / "out" / "xz-link-peer-source.tar.gz"
    stage = f"$HOME/xz-linkpeer/source-stage"
    link = linux(HERE / "third_party" / "link")
    asio_tgz = linux(HERE / "tools" / ASIO_TGZ)
    wsl(f"""set -e; rm -rf {stage}; mkdir -p {stage}/xz-link-peer/third_party/link/modules/asio-standalone {stage}/xz-link-peer/tools
      git -C '{link}' archive {LINK_COMMIT} | tar -x -C {stage}/xz-link-peer/third_party/link
      tar -xzf '{asio_tgz}' -C {stage}/xz-link-peer/third_party/link/modules/asio-standalone --strip-components=1
      cp '{asio_tgz}' {stage}/xz-link-peer/tools/
      cp -r '{linux(HERE / "src")}' '{linux(HERE / "build.py")}' '{linux(HERE / "build.sh")}' '{linux(HERE / "LICENSING.md")}' {stage}/xz-link-peer/
      mkdir -p {stage}/xz-link-peer/toolchain-runtime/lib/libc
      cp -r {zig_dir}/LICENSE {stage}/xz-link-peer/toolchain-runtime/zig-LICENSE
      cp -r {zig_dir}/lib/libc/musl {stage}/xz-link-peer/toolchain-runtime/lib/libc/
      cp -r {zig_dir}/lib/libcxx {zig_dir}/lib/libcxxabi {zig_dir}/lib/libunwind {zig_dir}/lib/compiler_rt {zig_dir}/lib/compiler_rt.zig {stage}/xz-link-peer/toolchain-runtime/lib/
      tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-11-15 --mode=go-w -cf - -C {stage} xz-link-peer | gzip -n -9 > '{linux(bundle)}' 
      rm -rf {stage}""")
    return bundle


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="verify pins only")
    ap.add_argument("--mirror", type=Path, help="local copies of the pinned archives and a Link clone")
    a = ap.parse_args()
    global MIRROR
    MIRROR = a.mirror.resolve() if a.mirror else None
    asio_tgz = archive(ASIO_TGZ, ASIO_URL, ASIO_SHA256)
    zig_txz = archive(ZIG_TXZ, ZIG_URL, ZIG_SHA256)
    origin = link_checkout(a.check)
    if a.check:
        print("pins OK")
        return 0
    asio_dir = HERE / "third_party" / "link" / "modules" / "asio-standalone"
    if not (asio_dir / "include" / "asio" / "version.hpp").is_file():
        wsl(f"mkdir -p '{linux(asio_dir)}' && tar -xzf '{linux(asio_tgz)}' -C '{linux(asio_dir)}' --strip-components=1")
    zig_dir = f"{ZIG_HOME}/zig-x86_64-linux-0.16.0"
    wsl(f"mkdir -p {ZIG_HOME} && (test -x {zig_dir}/zig || tar -xJf '{linux(zig_txz)}' -C {ZIG_HOME})")
    zig_version = wsl(f"{zig_dir}/zig version", capture=True).strip()
    if zig_version != "0.16.0":
        raise SystemExit(f"zig reports {zig_version}")
    wsl(f"bash '{linux(HERE / 'build.sh')}' {zig_dir}/zig")
    licences(zig_dir)
    bundle = source_bundle(zig_dir)
    out = HERE / "out"
    info = {
        "link": {"tag": LINK_TAG, "commit": LINK_COMMIT, "fetched_from": origin, "licence": "GPL-2.0-or-later"},
        "asio": {"commit": ASIO_COMMIT, "archive_sha256": ASIO_SHA256, "licence": "BSL-1.0"},
        "zig": {"archive": ZIG_TXZ, "sha256": ZIG_SHA256, "version": zig_version},
        "binaries": {},
        "source_bundle": {"path": str(bundle.relative_to(HERE)), "bytes": bundle.stat().st_size, "sha256": sha256(bundle)},
    }
    for rel in ("host/xz-link-peer", "arm-musl/xz-link-peer", "arm-musl/xz-link-probe", "arm-glibc213/xz-link-peer",
                "arm-musl-qemushim/xz-link-peer"):
        info["binaries"][rel] = readelf_facts(out / rel)
    gates = []
    for rel in ("arm-musl/xz-link-peer", "arm-glibc213/xz-link-peer"):
        f = info["binaries"][rel]
        gates.append((rel, "soft-float ABI" in (f["flags"] or ""), "flags soft-float"))
        gates.append((rel, f["cpu"] == "cortex-a9", "Tag_CPU_name cortex-a9"))
        gates.append((rel, f["abi_vfp_args"] is None or "VFP registers" not in f["abi_vfp_args"], "no VFP argument passing"))
    gates.append(("arm-musl/xz-link-peer", info["binaries"]["arm-musl/xz-link-peer"]["static"], "fully static"))
    g = info["binaries"]["arm-glibc213/xz-link-peer"]
    newest = max((tuple(int(x) for x in v.split(".")) for v in g["glibc_versions"]), default=(0,))
    gates.append(("arm-glibc213/xz-link-peer", newest <= (2, 13), f"GLIBC <= 2.13 (newest {'.'.join(map(str, newest))})"))
    gates.append(("arm-glibc213/xz-link-peer", not any("stdc++" in n for n in g["needed"]), "no libstdc++ needed"))
    info["gates"] = [{"binary": b, "check": c, "ok": ok} for b, ok, c in gates]
    (out / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
    for b, ok, c in gates:
        print(f"{'PASS' if ok else 'FAIL'} {b}: {c}")
    for rel, f in info["binaries"].items():
        print(f"{rel}: {f['bytes']} bytes, {f['flags']}, needed={f['needed'] or '-'}, glibc={f['glibc_versions'] or '-'}")
    print(f"source bundle {bundle.name}: {bundle.stat().st_size} bytes")
    return 0 if all(ok for _, ok, _ in gates) else 1


if __name__ == "__main__":
    sys.exit(main())
