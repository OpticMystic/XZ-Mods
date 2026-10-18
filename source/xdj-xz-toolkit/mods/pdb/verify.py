"""Build and verify the arena reader without device access or external writes."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from make_fixture import make
from cross_check import check, DEFAULT_REFERENCE

ROOT = Path(__file__).resolve().parent
SNAPSHOTS = [
    Path(r"E:\Codex\xz-library-repair-20261014\original\PIONEER\rekordbox\export.pdb"),
    Path(r"E:\Codex\xz-live-stems-20261014\opticmystic-before\PIONEER\rekordbox\export.pdb"),
]

def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)

def inspect_arm(path):
    # Same ELF architecture, ABI and GLIBC-version gate as mods/build.py.
    from elftools.elf.elffile import ELFFile
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf["e_machine"] == "EM_ARM"
        assert not elf["e_flags"] & 0x400, "hard-float ABI"
        versions = set()
        section = elf.get_section_by_name(".gnu.version_r")
        if section:
            for _, aux in section.iter_versions():
                versions.update(a.name for a in aux)
        for version in versions:
            if version.startswith("GLIBC_"):
                assert tuple(map(int, version[6:].split("."))) <= (2, 4), version
        imports = sorted(s.name for s in elf.get_section_by_name(".dynsym").iter_symbols()
                         if s.name and s["st_shndx"] == "SHN_UNDEF")
        assert not any(s in imports for s in ("malloc", "calloc", "realloc", "free", "qsort")), imports
        dynamic = elf.get_section_by_name(".dynamic")
        libraries = [t.needed for t in dynamic.iter_tags() if t.entry.d_tag == "DT_NEEDED"]
        assert set(libraries) <= {"libc.so.6", "ld-linux.so.3"}, libraries
    return dict(architecture="ELF32 little-endian ARM / soft-float", versions=sorted(versions),
                imports=imports, libraries=libraries, sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    if not __debug__:
        raise RuntimeError("Verification requires Python assertions; omit -O")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", required=True, type=Path)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--pdb", type=Path, action="append", default=[], help="additional read-only parity input")
    args = parser.parse_args()
    build = ROOT / ".build"
    build.mkdir(exist_ok=True)
    os.environ["ZIG_GLOBAL_CACHE_DIR"] = str(build / "zig-global-cache")
    os.environ["ZIG_LOCAL_CACHE_DIR"] = str(build / "zig-local-cache")
    os.environ["TMP"] = os.environ["TEMP"] = str(build)
    common = [args.zig.resolve(), "cc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    suffix = ".exe" if os.name == "nt" else ""
    clean, corrupt, large = (build / name for name in ("fixture.pdb", "corrupt.pdb", "large-page.pdb"))
    clean.write_bytes(make())
    corrupt.write_bytes(make(True))
    large.write_bytes(make(False, 16384))
    binary = build / ("test-pdb" + suffix)
    dump = build / ("pdb-dump" + suffix)
    run(common + ["-UNDEBUG", ROOT / "pdb.c", ROOT / "test_pdb.c", "-o", binary])
    run([binary, clean, "fuzz"])
    run([binary, corrupt, "corrupt"])
    run([binary, large])
    negative = subprocess.run(list(map(str, common + ["-DNDEBUG", "-c", ROOT / "test_pdb.c", "-o", build / "ndebug.o"])),
                              capture_output=True, text=True)
    assert negative.returncode and "PDB acceptance requires active assertions" in negative.stderr
    print("PASS NDEBUG build rejected", flush=True)
    run(common + [ROOT / "pdb.c", ROOT / "pdb_dump.c", "-o", dump])
    check(dump, args.reference, clean)
    check(dump, args.reference, large)
    for path in [*SNAPSHOTS, *args.pdb]:
        if path.is_file():
            check(dump, args.reference, path)
            print(f"  read-only source: {path}; sha256={hashlib.sha256(path.read_bytes()).hexdigest()}", flush=True)
        elif path in args.pdb:
            raise FileNotFoundError(path)

    # Probe actual instrumentation, not just whether zig accepts the option.
    probe = build / "sanitizer-probe.c"
    probe.write_text("int main(void) { volatile unsigned char a[8] = {0}; volatile unsigned char *p = a; "
                     "volatile int i = 8; volatile unsigned char value = p[i]; (void)value; return 0; }\n", encoding="ascii")
    probe_exe = build / ("sanitizer-probe" + suffix)
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    compiled = subprocess.run(list(map(str, common + flags + [probe, "-o", probe_exe])), capture_output=True, text=True)
    (build / "sanitizer-build.log").write_text(f"exit={compiled.returncode}\n" + compiled.stdout + compiled.stderr, encoding="utf-8")
    instrumented = False
    if not compiled.returncode:
        probed = subprocess.run([str(probe_exe)], capture_output=True, text=True)
        (build / "sanitizer-probe.log").write_text(f"exit={probed.returncode}\n" + probed.stdout + probed.stderr, encoding="utf-8")
        instrumented = probed.returncode != 0 and any(word in probed.stderr for word in
            ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:"))
    if instrumented:
        sanitized = build / ("test-pdb-sanitized" + suffix)
        run(common + flags + ["-UNDEBUG", ROOT / "pdb.c", ROOT / "test_pdb.c", "-o", sanitized])
        run([sanitized, corrupt, "corrupt", "fuzz"])
        print("PASS ASan/UBSan host instrumentation + 2000 corrupt-fixture mutations", flush=True)
    else:
        print("ASan/UBSan unavailable in this Zig host runtime (probe evidence in .build); using checked bounds", flush=True)
        checked = build / ("test-pdb-checked" + suffix)
        run(common + ["-UNDEBUG", "-DPDB_BOUNDS_CHECK", ROOT / "pdb.c", ROOT / "test_pdb.c", "-o", checked])
        run([checked, clean, "fuzz"])
        run([checked, corrupt, "corrupt", "fuzz"])
        print("PASS manual file/page/string bounds, arena guards, UTF-8 validation + 2000 corrupt-fixture mutations", flush=True)

    target = ["-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9"]
    obj = build / "pdb-arm.o"
    library = build / "libpdb-arm.so"
    run(common + target + ["-fPIC", "-c", ROOT / "pdb.c", "-o", obj])
    run(common + target + ["-fPIC", "-shared", "-Wl,--no-undefined", "-Wl,-z,nodelete", ROOT / "pdb.c", "-o", library])
    evidence = inspect_arm(library)
    (build / "arm-symbols.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(f"PASS ARM Cortex-A9 soft-float object + linked library; GLIBC <= 2.4: {evidence['versions']}", flush=True)
    print(f"ARM imports: {', '.join(evidence['imports']) or '(none)'}", flush=True)
    print("PASS verify.py: host, assertion rejection, Python parity, fuzz, ARM ABI/symbol gates", flush=True)

if __name__ == "__main__":
    main()
