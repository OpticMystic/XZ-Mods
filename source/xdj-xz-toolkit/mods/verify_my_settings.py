"""Offline settings codec, atomic-save failure checks and ARM32 ABI verification."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def linux_path(path: Path) -> str:
    path = path.resolve()
    return f"/mnt/{path.drive[0].lower()}/{path.as_posix()[3:]}"


def host_checks(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    sources = [ROOT / "my_settings.c", ROOT / "test_my_settings.c"]
    binary = output.resolve() / "my-settings-host-test"
    prefix = ["wsl", "-d", "Ubuntu", "--"] if os.name == "nt" else []
    path = linux_path if os.name == "nt" else str
    flags = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-DMS_TEST_FAULTS"]
    subprocess.run(prefix + ["gcc", *flags, *(path(p) for p in sources),
                            "-Wl,--wrap=write", "-Wl,--wrap=fsync", "-Wl,--wrap=rename",
                            "-o", path(binary)], check=True)
    subprocess.run(prefix + [path(binary)], check=True)


def arm_checks(output: Path, zig: str) -> None:
    from build import inspect

    compiler = shutil.which(zig) or str(Path(zig).resolve())
    flags = ["-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-O2",
             "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG"]
    library = output.resolve() / "libmy-settings.so"
    binary = output.resolve() / "my-settings-arm-test"
    subprocess.run([compiler, "cc", *flags, "-fPIC", "-shared", "-Wl,--no-undefined",
                    "-Wl,-z,nodelete", "-Wl,-T," + str(ROOT / "xz-relocations.ld"),
                    str(ROOT / "my_settings.c"), "-o", str(library)], check=True)
    subprocess.run([compiler, "cc", *flags, str(ROOT / "my_settings.c"),
                    str(ROOT / "test_my_settings.c"), "-o", str(binary)], check=True)
    for artifact in (library, binary):
        result = inspect(artifact)
        print(f"PASS {artifact.name}: {result['architecture']}; {result['symbol_versions']}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default=shutil.which("zig"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--host-only", action="store_true")
    args = parser.parse_args()
    if not args.host_only and not args.zig:
        parser.error("Supply --zig for the linked ARM32 GLIBC_2.4 gate, or --host-only")

    def verify(output: Path) -> None:
        host_checks(output)
        if not args.host_only:
            arm_checks(output, args.zig)

    if args.output:
        verify(args.output)
    else:
        with tempfile.TemporaryDirectory(prefix="my-settings-verify-") as folder:
            verify(Path(folder))


if __name__ == "__main__":
    main()
