"""Host acceptance and offline ARM ABI gate for the FLAGSHIP shell model."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def inspect(path: Path) -> None:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf["e_machine"] != "EM_ARM":
            raise ValueError("Expected ELF32 little-endian ARM")
        if elf["e_flags"] & 0x400:
            raise ValueError("Hard-float ABI does not match XZ")
        versions: set[str] = set()
        section = elf.get_section_by_name(".gnu.version_r")
        if section:
            for _, auxiliaries in section.iter_versions():
                versions.update(aux.name for aux in auxiliaries)
        for version in versions:
            if version.startswith("GLIBC_") and tuple(map(int, version[6:].split("."))) > (2, 4):
                raise ValueError(f"XZ ABI gate failed: {version} exceeds GLIBC_2.4")
        imports: list[str] = []
        text = elf.get_section_by_name(".text")
        if text is None or not text.data():
            raise ValueError("ABI inspection requires retained model machine code")
        symbols = elf.get_section_by_name(".dynsym") or elf.get_section_by_name(".symtab")
        if symbols:
            imports = sorted(s.name for s in symbols.iter_symbols() if s.name and s["st_shndx"] == "SHN_UNDEF")
        print(f"PASS {path.name}: ARM32 soft-float; versions={','.join(sorted(versions)) or 'none'}; imports={','.join(imports) or 'none'}", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default=shutil.which("zig"))
    args = parser.parse_args()
    if not args.zig:
        parser.error("Supply --zig with the repository Zig executable")
    compiler = shutil.which(args.zig) or str(Path(args.zig).resolve())
    flags = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG"]
    with tempfile.TemporaryDirectory(prefix="xz-shell-model-") as temporary:
        output = Path(temporary)
        host = output / ("test_shell_model.exe" if os.name == "nt" else "test_shell_model")
        run([compiler, "cc", *flags, str(ROOT / "shell_model.c"), str(ROOT / "test_shell_model.c"), "-o", str(host)])
        run([str(host)])
        target = ["-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9"]
        obj = output / "shell_model.o"
        run([compiler, "cc", *target, *flags, "-fPIC", "-fvisibility=hidden", "-c", str(ROOT / "shell_model.c"), "-o", str(obj)])
        inspect(obj)
        # An object alone cannot prove libc symbol versions. Link an isolated
        # shared artifact using the main mods/build.py target/link flags too.
        library = output / "libshell_model.so"
        run([compiler, "cc", *target, *flags, "-s", "-fPIC", "-shared", "-fvisibility=hidden",
             "-Wl,--no-undefined", "-Wl,--no-gc-sections", "-Wl,-z,nodelete", "-Wl,-T," + str(ROOT / "xz-relocations.ld"),
             str(obj), "-o", str(library)])
        inspect(library)
    print("shell_model verification passed: host -O2/C11/Werror/assertions + deck object and linked GLIBC_2.4 gate", flush=True)


if __name__ == "__main__":
    main()
