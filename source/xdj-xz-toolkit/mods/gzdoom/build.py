"""Build the ARM32 port from the pinned archive and prepared research dependencies.

The dependency cache is produced by the GZDoom cross-build investigation.
This command modifies only its new output directory and never installs on the XZ.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

from elftools.elf.elffile import ELFFile
from elf_compat import normalize

ROOT = Path(__file__).resolve().parent
PIN = "75da94aecd9b1c4c2e16b449dea1b9f9838c1af2"
ARCHIVE_SHA = "6e45726e7655252ae5511c324ebb01eabe734a89c50e43ba571b2f997119b9b1"
RESOURCES = ("gzdoom.pk3", "game_support.pk3", "brightmaps.pk3", "lights.pk3", "game_widescreen_gfx.pk3")
FLAGS = "-target arm-linux-gnueabi.2.13 -mcpu=cortex_a9 -mfloat-abi=softfp -mfpu=neon"


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inspect(path: Path) -> dict:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf["e_machine"] != "EM_ARM" or elf["e_flags"] & 0x400:
            raise ValueError("Expected ARM32 with the firmware's soft-float calling convention")
        dynamic = elf.get_section_by_name(".dynamic")
        needed = [tag.needed for tag in dynamic.iter_tags() if tag.entry.d_tag == "DT_NEEDED"]
        allowed = {"libzmusiclite.so.1", "libm.so.6", "libc.so.6", "ld-linux.so.3", "libpthread.so.0", "libdl.so.2", "librt.so.1"}
        if not set(needed) <= allowed:
            raise ValueError("Unpackaged runtime dependencies: " + str(needed))
        versions = elf.get_section_by_name(".gnu.version_r")
        names = sorted({item.name for _, entries in versions.iter_versions() for item in entries}) if versions else []
        if any(name.startswith("GLIBC_") and tuple(map(int, name[6:].split("."))) > (2, 13) for name in names):
            raise ValueError("Engine requires newer firmware libraries")
        tags = {tag.entry.d_tag: tag.entry.d_val for tag in dynamic.iter_tags()}
        if "DT_JMPREL" in tags and tags["DT_REL"] + tags["DT_RELSZ"] != tags["DT_JMPREL"]:
            raise ValueError("ARM loader requires adjacent, disjoint relocation tables")
        return {"sha256": sha(path), "bytes": path.stat().st_size, "machine": "ARM32",
                "flags": hex(elf["e_flags"]), "needed": needed, "symbol_versions": names}


def run(arguments: list[str], log: Path) -> None:
    with log.open("w", encoding="utf8") as stream:
        subprocess.run(arguments, check=True, stdout=stream, stderr=subprocess.STDOUT)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory for a reproducible candidate")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    cache, output, zig = args.cache.resolve(), args.output.resolve(), args.zig.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("Choose a new output directory; existing candidates are preserved")
    archive = cache / "gzdoom-source.zip"
    if sha(archive) != ARCHIVE_SHA:
        raise ValueError("GZDoom source archive changed")
    dependencies = [cache / "arm-prefix/lib/libSDL2.a", cache / "libvpx-build/libvpx.a",
                    cache / "zmusic-build/source/libzmusiclite.so.1.1.9"]
    for path in dependencies:
        if not path.is_file():
            raise ValueError("Prepare the private dependency cache first: " + str(path))
    output.mkdir(parents=True, exist_ok=True)
    source = output / ("gzdoom-" + PIN)
    with zipfile.ZipFile(archive) as bundle:
        for member in bundle.infolist():
            target = (output / member.filename).resolve()
            if not target.is_relative_to(source) or ".." in Path(member.filename).parts:
                raise ValueError("Source archive path leaves its pinned directory")
        bundle.extractall(output)
    subprocess.run(["git", "apply", str(ROOT / "port.patch")], cwd=source, check=True)
    sdl = source / "src/common/platform/posix/sdl"
    for original, name in ((ROOT / "offscreen.inc", "xz_offscreen.inc"), (ROOT / "stream.inc", "stream.inc"),
                           (ROOT.parent / "doom/xz_controls.h", "xz_controls.h"), (ROOT.parent / "doom/native_bridge.h", "native_bridge.h")):
        shutil.copy2(original, sdl / name)
    for relative in ("doom/xz_controls.c", "doom/xz_controls.h", "doom/vendor/doomkeys.h", "ui/pixel_font.h"):
        destination = sdl / "xz" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT.parent / relative, destination)
    toolchain = output / "arm.cmake"
    toolchain.write_text(f'set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR arm)\n'
                         + f'set(CMAKE_C_COMPILER "{zig.as_posix()}")\nset(CMAKE_C_COMPILER_ARG1 cc)\n'
                         + f'set(CMAKE_CXX_COMPILER "{zig.as_posix()}")\nset(CMAKE_CXX_COMPILER_ARG1 c++)\n', newline="\n")
    cmake = shutil.which("cmake") or "C:/Program Files/CMake/bin/cmake.exe"
    build = output / "build"
    definitions = {
        "CMAKE_MAKE_PROGRAM": cache / "ninja/ninja-1.13.2.data/scripts/ninja.exe",
        "CMAKE_TOOLCHAIN_FILE": toolchain,
        "CMAKE_PROJECT_INCLUDE": cache / "compiler-project.cmake",
        "CMAKE_POLICY_VERSION_MINIMUM": "3.5", "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_C_FLAGS": FLAGS, "CMAKE_CXX_FLAGS": FLAGS,
        "CMAKE_EXE_LINKER_FLAGS": "-Wl,--no-gc-sections,-T," + (ROOT / "xz-relocations.ld").as_posix(),
        "IMPORT_EXECUTABLES": cache / "ImportXzExecutables.cmake",
        "HAVE_VULKAN": "OFF", "HAVE_GLES2": "ON", "HAVE_VM_JIT": "OFF",
        "NO_OPENAL": "ON", "NO_OPENMP": "ON", "NO_GTK": "ON", "NO_SDL_JOYSTICK": "ON",
        "SEND_ANON_STATS": "OFF", "ZDOOM_ENABLE_SWR": "ON",
        "FORCE_INTERNAL_ZLIB": "ON", "FORCE_INTERNAL_JPEG": "ON", "FORCE_INTERNAL_BZIP2": "ON",
        "SDL2_INCLUDE_DIR": cache / "arm-prefix/include/SDL2", "SDL2_LIBRARY": dependencies[0],
        "ZMUSIC_INCLUDE_DIR": cache / "ZMusic-1.1.9/include", "ZMUSIC_LIBRARIES": dependencies[2],
        "VPX_INCLUDE_DIR": cache / "libvpx-1.12.0", "VPX_LIBRARIES": dependencies[1],
    }
    command = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja"]
    command += ["-D" + key + "=" + (value.as_posix() if isinstance(value, Path) else value) for key, value in definitions.items()]
    run(command, output / "configure.log")
    run([cmake, "--build", str(build), "--parallel", str(args.jobs)], output / "build.log")
    runtime = output / "runtime"
    runtime.mkdir()
    (runtime / "gzdoom").write_bytes(normalize((build / "gzdoom").read_bytes()))
    (runtime / "libzmusiclite.so.1").write_bytes(normalize(dependencies[2].read_bytes()))
    for name in RESOURCES:
        shutil.copy2(build / name, runtime / name)
    run([str(zig), "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-O2", "-s",
         str(ROOT / "watchdog.c"), "-o", str(runtime / "gz-watchdog")], output / "watchdog-build.log")
    run([str(zig), "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9", "-O2", "-s",
         str(ROOT.parent / "transfer.c"), "-o", str(runtime / "gz-transfer")], output / "transfer-build.log")
    report = {"format": "xz-gzdoom-candidate/1", "upstream_commit": PIN, "source_archive_sha256": ARCHIVE_SHA,
              "port_patch_sha256": sha(ROOT / "port.patch"), "compiler_flags": FLAGS,
              "engine": inspect(runtime / "gzdoom"), "music": inspect(runtime / "libzmusiclite.so.1"),
              "dependency_sha256": {path.name: sha(path) for path in dependencies},
              "files": {path.name: sha(path) for path in runtime.iterdir()},
              "hardware_verified": False, "myhouse_verified": False, "audio": False,
              "port": "Unofficial ARM32 GZDoom 4.10; qualification required"}
    (runtime / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
