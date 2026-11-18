# SPDX-License-Identifier: MIT
"""Builds and tests the Pro DJ Link core: host gcc with ASan/UBSan in WSL, the ARM object with Zig,
and the ARM test binary under qemu-arm. Then builds the xz-djlink helper and the lab stand-in for
the player: host x86_64 (normal and ASan/UBSan) in WSL, and the helper for the unit as a static
ARM musl binary. Exits non-zero on any failure."""
import argparse
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "out"
WARN = ["-std=c11", "-Wall", "-Wextra", "-Werror"]
ARM = ["-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-O2"]
DEVICE = ["-target", "arm-linux-musleabi", "-mcpu=cortex_a9", "-O2", "-static", "-s"]
SAN = "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
# Integer and memory helpers the ARM EABI compiler may call (bcmp is LLVM's memcmp for equality, the
# unwind personalities come from the exception index tables). Anything else, such as float
# helpers or libc beyond mem*, would break the purity rules.
ARM_ALLOWED_IMPORTS = {"memcpy", "memset", "memcmp", "memmove", "__aeabi_uldivmod", "__aeabi_ldivmod",
                       "__aeabi_uidiv", "__aeabi_uidivmod", "__aeabi_idiv", "__aeabi_idivmod", "__aeabi_memcpy",
                       "__aeabi_memcpy4", "__aeabi_memcpy8", "__aeabi_memset", "__aeabi_memset4", "__aeabi_memclr",
                       "__aeabi_memclr4", "__aeabi_memclr8", "__aeabi_memmove", "__aeabi_lmul", "__aeabi_llsl",
                       "__aeabi_llsr", "__aeabi_lasr", "bcmp",
                       "__aeabi_unwind_cpp_pr0", "__aeabi_unwind_cpp_pr1"}


def wsl_path(path: pathlib.Path) -> str:
    p = path.resolve()
    return "/mnt/" + p.drive[0].lower() + p.as_posix()[2:]


def wsl(distro: str, command: str) -> subprocess.CompletedProcess:
    return subprocess.run(["wsl.exe", "-d", distro, "--", "bash", "-lc", command], capture_output=True, text=True)


def find_zig(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    found = shutil.which("zig")
    if found:
        return found
    try:
        import ziglang
    except ImportError:
        return None
    candidate = pathlib.Path(ziglang.__file__).parent / ("zig.exe" if sys.platform == "win32" else "zig")
    return str(candidate) if candidate.exists() else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", help="Zig executable (default: PATH, then the ziglang Python package)")
    parser.add_argument("--wsl-distro", default="Ubuntu")
    parser.add_argument("--fuzz", type=int, default=1000000, help="fuzz datagrams on the host")
    parser.add_argument("--arm-fuzz", type=int, default=200000, help="fuzz datagrams under qemu-arm")
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    rows: list[tuple[str, bool, str]] = []

    def record(name: str, ok: bool, detail: str = "") -> bool:
        rows.append((name, ok, detail))
        return ok

    gen = subprocess.run([sys.executable, str(HERE / "tests" / "gen_fixtures.py")], capture_output=True, text=True)
    record("generate fixtures", gen.returncode == 0, gen.stderr.strip()[-200:])

    root = wsl_path(HERE)
    host = wsl(args.wsl_distro, f"cd '{root}' && gcc {' '.join(WARN)} -O1 -g -fno-omit-frame-pointer "
                                "-fsanitize=address,undefined -fno-sanitize-recover=all "
                                "djl.c tests/test_djl.c -o out/test_host")
    if record("host gcc build (ASan+UBSan)", host.returncode == 0, (host.stdout + host.stderr).strip()[-400:]):
        run = wsl(args.wsl_distro, f"cd '{root}' && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "
                                   f"UBSAN_OPTIONS=print_stacktrace=1 ./out/test_host {args.fuzz}")
        print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="", file=sys.stderr)
        record("host tests (ASan+UBSan)", run.returncode == 0 and "PASS:" in run.stdout,
               f"exit {run.returncode}")

    zig = find_zig(args.zig)
    if not record("find zig", zig is not None, zig or "not found"):
        zig = None
    if zig:
        obj = OUT / "djl-arm.o"
        lib = subprocess.run([zig, "cc", *ARM, *WARN, "-c", str(HERE / "djl.c"), "-o", str(obj)],
                             capture_output=True, text=True)
        if record("ARM library build (zig, -Werror)", lib.returncode == 0, lib.stderr.strip()[-400:]):
            nm = wsl(args.wsl_distro, f"arm-linux-gnueabi-nm '{wsl_path(obj)}'")
            if nm.returncode == 0:
                imports, data = set(), []
                for line in nm.stdout.splitlines():
                    parts = line.split()
                    if len(parts) == 2 and parts[0] == "U":
                        imports.add(parts[1])
                    elif len(parts) == 3 and parts[1] in "DdBbCcGgSs":
                        data.append(parts[2])
                extra = sorted(imports - ARM_ALLOWED_IMPORTS)
                record("ARM object: imports and globals", not extra and not data,
                       f"imports {sorted(imports)}; unexpected {extra}; writable data {data}")
            else:
                record("ARM object: imports and globals", False, nm.stderr.strip()[-200:])
        exe = OUT / "test_arm"
        build = subprocess.run([zig, "cc", *ARM, *WARN, str(HERE / "djl.c"), str(HERE / "tests" / "test_djl.c"),
                                "-o", str(exe)], capture_output=True, text=True)
        if record("ARM test build (zig)", build.returncode == 0, build.stderr.strip()[-400:]):
            run = wsl(args.wsl_distro, f"qemu-arm -L /usr/arm-linux-gnueabi '{wsl_path(exe)}' {args.arm_fuzz}")
            print(run.stdout, end="")
            if run.stderr:
                print(run.stderr, end="", file=sys.stderr)
            record("ARM tests under qemu-arm", run.returncode == 0 and "PASS:" in run.stdout, f"exit {run.returncode}")

    helper_builds(args, zig, record)

    print()
    width = max(len(r[0]) for r in rows)
    for name, ok, detail in rows:
        line = f"{name:<{width}}  {'PASS' if ok else 'FAIL'}"
        if detail and (not ok or name.startswith(("ARM object", "device helper", "qemu-arm helper"))):
            line += f"  {detail}"
        print(line)
    failed = [r for r in rows if not r[1]]
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


def helper_builds(args, zig, record) -> None:
    root = wsl_path(HERE)
    sources = {"xz-djlink": "djl.c helper/xz_djlink.c", "xz-djlink-sim": "djl.c sim/xz_djlink_sim.c"}
    for name, src in sources.items():
        for flavour, flags in (("", "-O2"), ("-asan", SAN)):
            out = f"out/{name}{flavour}"
            r = wsl(args.wsl_distro, f"cd '{root}' && gcc {' '.join(WARN)} {flags} {src} -o {out}")
            record(f"host build {name}{flavour}", r.returncode == 0, (r.stdout + r.stderr).strip()[-400:])
    help_run = wsl(args.wsl_distro, f"cd '{root}' && ./out/xz-djlink-asan --help")
    record("host helper --help", help_run.returncode == 0 and help_run.stdout.startswith("usage: xz-djlink"),
           help_run.stdout[:80])
    if not zig:
        record("device helper build", False, "zig not found")
        return
    player_abi = subprocess.run([zig, "cc", *ARM, *WARN, "-c", str(HERE / "sim" / "xz_djlink_sim.c"), "-o",
                                 str(OUT / "sim-arm-glibc.o")], capture_output=True, text=True)
    record("bus layout asserts on the player's ABI (arm-linux-gnueabi)", player_abi.returncode == 0,
           player_abi.stderr.strip()[-400:])
    exe = OUT / "xz-djlink-arm"
    build = subprocess.run([zig, "cc", *DEVICE, *WARN, str(HERE / "djl.c"), str(HERE / "helper" / "xz_djlink.c"),
                            "-o", str(exe)], capture_output=True, text=True)
    if not record("device helper build (zig, arm-linux-musleabi, static)", build.returncode == 0,
                  build.stderr.strip()[-400:]):
        return
    elf = wsl(args.wsl_distro, f"readelf -h -l -d '{wsl_path(exe)}'")
    text = elf.stdout
    static = "INTERP" not in text and "There is no dynamic section" in text
    arm = "Machine:                           ARM" in text and "ELF32" in text
    flags = next((line.split(":", 1)[1].strip() for line in text.splitlines() if line.strip().startswith("Flags:")), "")
    record("device helper is static 32-bit ARM", elf.returncode == 0 and static and arm,
           f"size {exe.stat().st_size} bytes; static {static}; ARM ELF32 {arm}; flags {flags}")
    q = wsl(args.wsl_distro, f"qemu-arm '{wsl_path(exe)}' --help")
    record("qemu-arm helper --help (only --help: qemu-user cannot run AF_PACKET meaningfully)",
           q.returncode == 0 and q.stdout.startswith("usage: xz-djlink"), f"exit {q.returncode}: {q.stdout[:40]!r}")
    # Second variant, linked the way every binary already on the unit is: against its own glibc 2.13. Which
    # of the two starts on kernel 3.0.101 is decided on the unit.
    dyn = OUT / "xz-djlink-arm-glibc"
    build = subprocess.run([zig, "cc", *ARM, *WARN, "-s", str(HERE / "djl.c"), str(HERE / "helper" / "xz_djlink.c"),
                            "-lrt", "-lpthread", "-o", str(dyn)], capture_output=True, text=True)
    if not record("device helper build (zig, arm-linux-gnueabi.2.13, dynamic)", build.returncode == 0,
                  build.stderr.strip()[-400:]):
        return
    ver = wsl(args.wsl_distro, f"readelf -h -d -V '{wsl_path(dyn)}'")
    versions = sorted({tuple(int(n) for n in v.split(".")) for v in re.findall(r"GLIBC_(\d+(?:\.\d+)+)", ver.stdout)})
    needed = sorted(set(re.findall(r"Shared library: \[([^\]]+)\]", ver.stdout)))
    soft = "soft-float ABI" in ver.stdout
    newest = ".".join(map(str, versions[-1])) if versions else "none"
    record("device helper (glibc) imports at most GLIBC_2.13, soft-float", ver.returncode == 0 and soft and versions
           and versions[-1] <= (2, 13), f"size {dyn.stat().st_size} bytes; newest GLIBC_{newest}; needs {needed}")
    q = wsl(args.wsl_distro, f"qemu-arm -L /usr/arm-linux-gnueabi '{wsl_path(dyn)}' --help")
    record("qemu-arm helper (glibc) --help", q.returncode == 0 and q.stdout.startswith("usage: xz-djlink"),
           f"exit {q.returncode}: {q.stdout[:40]!r}")


if __name__ == "__main__":
    sys.exit(main())
