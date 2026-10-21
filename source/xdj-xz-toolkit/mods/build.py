"""Build and inspect the XZ ARM development runtime without deploying it."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

from ui.ui_sources import UI_SOURCES
from sdk.sdk_sources import SDK_SOURCES
from shell.shell_sources import SHELL_SOURCES

ROOT = pathlib.Path(__file__).resolve().parent


def build_mp3_vfp(output: pathlib.Path, arm_gcc: str | None, wsl_distro: str) -> dict:
    """Keep the base ARM ABI while compiling MP3 arithmetic to VFP instructions."""
    source = ROOT / "audio/stem_mp3.c"
    target = output / "stem-mp3-vfp.o"
    compiler = arm_gcc or shutil.which("arm-linux-gnueabi-gcc")
    if compiler:
        command = [compiler]
        source_arg, target_arg = str(source), str(target)
    elif os.name == "nt":
        command = ["wsl", "-d", wsl_distro, "--", "arm-linux-gnueabi-gcc"]
        def linux(path: pathlib.Path) -> str:
            value = str(path.resolve()).replace("\\", "/")
            if len(value) < 3 or value[1:3] != ":/":
                raise ValueError("The WSL MP3 compiler requires a local drive path")
            return "/mnt/" + value[0].lower() + value[2:]
        source_arg, target_arg = linux(source), linux(target)
    else:
        raise ValueError("MP3 needs arm-linux-gnueabi-gcc; pass --arm-gcc with its executable")
    version = subprocess.run(command + ["--version"], check=True, capture_output=True,
                             text=True).stdout.splitlines()[0]
    subprocess.run(command + [
        "-mcpu=cortex-a9", "-marm", "-mfpu=vfpv3", "-mfloat-abi=softfp",
        "-O2", "-std=c11", "-fPIC", "-fvisibility=hidden", "-ffp-contract=off",
        "-c", source_arg, "-o", target_arg], check=True)
    with target.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or elf["e_machine"] != "EM_ARM" or elf["e_flags"] & 0x400:
            raise ValueError("MP3 object must use the ARM32 base calling convention")
        symbols = elf.get_section_by_name(".symtab")
        undefined = {s.name for s in symbols.iter_symbols() if s["st_shndx"] == "SHN_UNDEF"}
        if undefined & {"__aeabi_fadd", "__aeabi_fsub", "__aeabi_fmul", "__aeabi_fdiv"}:
            raise ValueError("MP3 arithmetic fell back to software floating point")
        decoder = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        floating = 0
        for symbol in symbols.iter_symbols():
            if symbol["st_info"]["type"] != "STT_FUNC" or not isinstance(symbol["st_shndx"], int):
                continue
            section = elf.get_section(symbol["st_shndx"])
            start = symbol["st_value"] - section["sh_addr"]
            for instruction in decoder.disasm(section.data()[start:start + symbol["st_size"]], symbol["st_value"]):
                floating += instruction.mnemonic.startswith(("vadd.f32", "vsub.f32", "vmul.f32", "vmla.f32"))
        if not floating:
            raise ValueError("MP3 object contains no verified VFP arithmetic")
    return {"object": target.name, "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
            "compiler": version, "vfp_arithmetic_instructions": floating,
            "calling_convention": "ARM base ABI", "floating_point": "VFPv3",
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "decoder_header_sha256": hashlib.sha256((ROOT / "audio/vendor/dr_libs/dr_mp3.h").read_bytes()).hexdigest()}


def inspect(path: pathlib.Path) -> dict:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf["e_machine"] != "EM_ARM":
            raise ValueError("Expected ELF32 little-endian ARM")
        if elf["e_flags"] & 0x400:
            raise ValueError("Hard-float calling convention does not match the XZ application")
        versions = set()
        section = elf.get_section_by_name(".gnu.version_r")
        if section:
            for _, auxiliaries in section.iter_versions():
                versions.update(aux.name for aux in auxiliaries)
        for version in versions:
            if version.startswith("GLIBC_"):
                if tuple(map(int, version[6:].split("."))) > (2, 4):
                    raise ValueError(f"XZ ABI gate failed: {version} exceeds verified GLIBC_2.4 imports")
        libraries = []
        dynamic = elf.get_section_by_name(".dynamic")
        if dynamic:
            libraries = [tag.needed for tag in dynamic.iter_tags() if tag.entry.d_tag == "DT_NEEDED"]
            tags = {tag.entry.d_tag: tag.entry.d_val for tag in dynamic.iter_tags()}
            if tags.get("DT_RELCOUNT", 0) and tags.get("DT_JMPREL"):
                if tags.get("DT_REL", 0) + tags.get("DT_RELSZ", 0) != tags["DT_JMPREL"]:
                    raise ValueError("XZ glibc 2.13 needs contiguous REL and PLT relocation tables")
        nodelete = bool(dynamic and any(tag.entry.d_tag == "DT_FLAGS_1" and tag.entry.d_val & 8
                                       for tag in dynamic.iter_tags()))
        if elf["e_type"] == "ET_DYN" and not nodelete:
            raise ValueError("Hook library must remain mapped after dlclose")
        allowed = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "ld-linux.so.3"}
        if set(libraries) - allowed:
            raise ValueError(f"Unexpected runtime dependencies: {libraries}")
        imports = [s.name for s in elf.get_section_by_name(".dynsym").iter_symbols()
                   if s.name and s["st_shndx"] == "SHN_UNDEF"]
        return {"architecture": "ARM32 soft-float ABI", "libraries": libraries,
                "symbol_versions": sorted(versions), "imports": sorted(imports),
                "nodelete": nodelete, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default=shutil.which("zig"))
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--arm-gcc", help="ARM EABI GCC executable for the VFP MP3 decoder")
    parser.add_argument("--wsl-distro", default="Ubuntu", help="WSL distribution used for ARM GCC on Windows")
    parser.add_argument("--host-checks-only", action="store_true",
                        help="Run standalone settings host checks without building or deploying the runtime")
    args = parser.parse_args()
    if args.host_checks_only:
        from verify_my_settings import host_checks
        host_checks(args.output)
        return
    if not args.zig:
        parser.error("Supply --zig with a Zig executable supporting arm-linux-gnueabi.2.13")
    args.output.mkdir(parents=True, exist_ok=True)
    destination = args.output.resolve() / "libxz-mods-development.so"
    compiler = shutil.which(args.zig) or str(pathlib.Path(args.zig).resolve())
    mp3_build = build_mp3_vfp(args.output.resolve(), args.arm_gcc, args.wsl_distro)
    mp3_object = args.output.resolve() / mp3_build["object"]
    # Keep the qualified legacy decoder compilation unchanged. LLVM 21 crashes
    # when compiling its dr_libs paths with softfp. Only the new streaming math
    # needs VFP; softfp retains the same function-call ABI as the legacy objects.
    stream_object = args.output.resolve() / "overcue-stream.o"
    subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                    "-mfloat-abi=softfp", "-mfpu=neon", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fPIC", "-fvisibility=hidden", "-c", str(ROOT / "audio/overcue_stream.c"),
                    "-o", str(stream_object)], check=True)
    audio_runtime_object=args.output.resolve()/"audio-runtime-vfp.o"
    subprocess.run([compiler,"cc","-target","arm-linux-gnueabi.2.13","-mcpu=cortex_a9",
                    "-mfloat-abi=softfp","-mfpu=vfpv3","-O2","-std=c11","-Wall","-Wextra","-Werror",
                    "-fPIC","-fvisibility=hidden","-c",str(ROOT/"audio/runtime.c"),
                    "-o",str(audio_runtime_object)],check=True)
    media_object=args.output.resolve()/"stem-media-vfp.o"
    subprocess.run([compiler,"cc","-target","arm-linux-gnueabi.2.13","-mcpu=cortex_a9",
                    "-mfloat-abi=softfp","-mfpu=vfpv3","-O2","-std=c11","-Wall","-Wextra","-Werror",
                    "-fPIC","-fvisibility=hidden","-c",str(ROOT/"audio/stem_media.c"),
                    "-o",str(media_object)],check=True)
    # DECK 3/4: the resampler runs per output frame, so VFP like the stem decoder. The transport copy stays
    # byte-identical; glibc 2.13 keeps clock_gettime in librt, so it calls extra_decks.c's syscall instead.
    player_object=args.output.resolve()/"extra-player-vfp.o"
    subprocess.run([compiler,"cc","-target","arm-linux-gnueabi.2.13","-mcpu=cortex_a9",
                    "-mfloat-abi=softfp","-mfpu=vfpv3","-O2","-std=c11","-Wall","-Wextra","-Werror",
                    "-fPIC","-fvisibility=hidden","-c",str(ROOT/"extra_player.c"),
                    "-o",str(player_object)],check=True)
    transport_object=args.output.resolve()/"usb-transport.o"
    subprocess.run([compiler,"cc","-target","arm-linux-gnueabi.2.13","-mcpu=cortex_a9",
                    "-O2","-std=c11","-Wall","-Wextra","-Werror","-fPIC","-fvisibility=hidden",
                    "-Dclock_gettime=xz_usb_clock_gettime","-c",str(ROOT/"audio/usb_transport/usb_transport.c"),
                    "-o",str(transport_object)],check=True)
    # The native column drawer passes its zoom float in s0. Enable VFP so the
    # per-function aapcs-vfp attribute is honored; the library stays soft-float.
    layered_object = args.output.resolve() / "layered-wave-runtime.o"
    subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                    "-mfloat-abi=softfp", "-mfpu=neon", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fPIC", "-fvisibility=hidden", "-c", str(ROOT / "ui/layered_wave_runtime.c"),
                    "-o", str(layered_object)], check=True)
    # The XZ has VFPv3. Software floating point exceeded a 512-frame audio
    # deadline on the controller; softfp keeps the stock register-call ABI.
    key_object = args.output.resolve() / "keyshift-native.o"
    subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                    "-mfloat-abi=softfp", "-mfpu=vfpv3", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fPIC", "-fvisibility=hidden", "-c", str(ROOT / "key/keyshift.c"),
                    "-o", str(key_object)], check=True)
    with key_object.open("rb") as stream:
        key_elf = ELFFile(stream)
        key_symbol = next(s for s in key_elf.get_section_by_name(".symtab").iter_symbols()
                          if s.name == "xz_key_shift_process")
        key_section = key_elf.get_section(key_symbol["st_shndx"])
        key_code = key_section.data()[key_symbol["st_value"]:key_symbol["st_value"] + key_symbol["st_size"]]
        if key_elf["e_flags"] & 0x400 or not any(i.mnemonic.startswith("v")
                for i in Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(key_code, 0)):
            raise ValueError("Key DSP requires VFP instructions with the stock soft-float calling convention")
    lab_objects = []
    for name in ("rhythm_lab", "rhythm_lab_render", "glyph_run", "glyph_run_render", "game_motion_draw", "game_feel_art", "game_font", "wave_rider", "wave_rider_render", "wave_rider_art"):
        target = args.output.resolve() / (name + "-native.o")
        subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                        "-mfloat-abi=softfp", "-mfpu=vfpv3", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fPIC", "-fvisibility=hidden", "-c", str(ROOT / "arcade" / (name + ".c")),
                        "-o", str(target)], check=True)
        lab_objects.append(str(target))
    command = [compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
               "-O2", "-s", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
               "-fvisibility=hidden", "-Wl,--no-undefined", "-Wl,-z,nodelete",
               "-Wl,-T," + str(ROOT / "xz-relocations.ld"),
               str(ROOT / "runtime.c"), str(ROOT / "cue/cue.c"), str(ROOT / "cue/native.c"),
               str(ROOT / "deck_select.c"), str(ROOT / "extra_decks.c"), str(ROOT / "classic_load.c"), str(ROOT / "classic_popup.c"),
               str(player_object), str(ROOT / "anlz_extra.c"), str(transport_object),
               str(ROOT / "doom/native_bridge.c"),
               str(ROOT / "kart/xz_kart_input.c"), str(ROOT / "kart/xz_kart_runtime.c"),
               *(str(ROOT / "sdk" / name) for name in SDK_SOURCES),
               str(ROOT / "arcade/arcade.c"),str(ROOT / "arcade/render.c"),
               str(ROOT / "arcade/input.c"),str(ROOT / "arcade/clock.c"),str(ROOT / "arcade/native_clock.c"),
               str(ROOT / "arcade/wave_rider_source.c"),str(ROOT / "arcade/wave_rider_runtime.c"),
               *lab_objects, str(ROOT / "arcade/rhythm_lab_runtime.c"),
               str(ROOT / "doom/wad_catalog.c"),
               str(ROOT / "ota/native_update.c"),
               str(ROOT / "ui_runtime.c"), str(ROOT / "metrics_runtime.c"), str(ROOT / "pc_control/stems.c"), str(ROOT / "pc_control/native.c"), str(ROOT / "pc_control/wave.c"), *(str(ROOT / "ui" / name) for name in UI_SOURCES), str(ROOT / "ui/native_touch.c"),
               str(ROOT / "ui/stem_pads.c"), str(ROOT / "ui/beat_jump.c"),
               str(ROOT / "ui/native_led.c"),
               str(ROOT / "ui/native_wave.c"),
               str(ROOT / "ui/native_wave_runtime.c"),
               str(ROOT / "ui/extra_lane.c"),
               str(ROOT / "ui/layered_wave.c"), str(ROOT / "ui/layered_wave_source.c"), str(layered_object),
               str(ROOT / "ui/native_skin_runtime.c"),
               str(ROOT / "ui/native_raw_skin.c"),
               str(ROOT / "ui/native_window_keys.c"),
               # FLAGSHIP shell; ui_draw.c already comes with UI_SOURCES.
               *(str(ROOT / "shell" / name) for name in SHELL_SOURCES if name != "../ui/ui_draw.c"),
               str(ROOT / "shell_model.c"), str(ROOT / "shell_runtime.c"), str(ROOT / "shell_runtime_core.c"), str(ROOT / "sync_native.c"),
               str(ROOT / "shell_library.c"), str(ROOT / "shell_analysis.c"), str(ROOT / "shell_playlists.c"), str(ROOT / "shell_utility.c"), str(ROOT / "shell_mysettings.c"), str(ROOT / "my_settings.c"), str(ROOT / "pdb/pdb.c"),
               str(ROOT / "settings.c"), str(ROOT / "settings_internal.c"), str(ROOT / "usb_profile.c"),
               str(ROOT / "dj_profile.c"), str(ROOT / "dj_runtime.c"), str(ROOT / "dj_profile_write.c"), str(ROOT / "dj_folders.c"),
               str(ROOT / "dj_json.c"), str(ROOT / "dj_name_key.c"),
               str(ROOT / "ui/wave_viewport.c"),
               str(key_object), str(ROOT / "key/runtime.c"),
               str(ROOT / "key/music.c"), str(ROOT / "key/native.c"), str(ROOT / "key/native_browse.c"), str(ROOT / "key/traffic.c"),
               str(ROOT / "key/catalog.c"), str(ROOT / "key/browser.c"),
               str(audio_runtime_object), str(ROOT / "audio/native_reader.c"),
               str(ROOT / "audio/trace.c"),
               str(ROOT / "ram/cache.c"), str(ROOT / "ram/runtime.c"), str(ROOT / "ram/removal.c"), str(ROOT / "ram/presentation.c"), str(ROOT / "ram/pin.c"),
               str(ROOT / "audio/stem_cache.c"), str(ROOT / "audio/stem_mix.c"), str(ROOT / "audio/stem_decode.c"),
               str(ROOT / "audio/overcue_file.c"), str(ROOT / "audio/foreign_stems.c"),
               str(mp3_object), str(media_object), str(stream_object),
               str(ROOT / "audio/vendor/miniz/miniz_tinfl.c"), str(ROOT / "audio/vendor/sha256/sha256.c"),
               "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_DEFLATE_APIS",
               "-pthread", "-lm", "-ldl",
               "-o", str(destination)]
    subprocess.run(command, check=True)
    result = inspect(destination)
    result["mp3_decoder"] = mp3_build
    vendor = ROOT.parent / "vendor"
    receiver = args.output.resolve() / "libxz-directfb-mods-test.so"
    subprocess.run([compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
                    "-O2", "-s", "-Wall", "-Wextra", "-Werror", "-std=gnu99", "-fPIC", "-shared",
                    "-Wl,-z,nodelete", "-Wl,-T," + str(ROOT / "xz-relocations.ld"),
                    "-I" + str(vendor / "build/dfb-generated"),
                    "-I" + str(vendor / "build/directfb-1.4-src/include"),
                    "-I" + str(vendor / "build/directfb-1.4-src/lib"),
                    str(vendor / "tools/xz_runtime/xz_directfb_hook.c"), "-ldl", "-pthread",
                    "-o", str(receiver)], check=True)
    result["display_bridge"] = inspect(receiver)
    common = [compiler, "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
              "-O2", "-UNDEBUG", "-s", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    tests = {
        "runtime-smoke": [str(ROOT / "runtime_smoke.c"), "-ldl"],
        "beat-jump-test": [str(ROOT / "ui/beat_jump.c"),str(ROOT / "ui/test_beat_jump.c")],
        "cue-test": [str(ROOT / "cue/cue.c"), str(ROOT / "cue/test_cue.c")],
        "deck-select-test": [str(ROOT / "deck_select.c"), str(ROOT / "test_deck_select.c")],
        "extra-player-test": ["-pthread", *(str(ROOT / name) for name in (
            "extra_player.c", "anlz_extra.c", "shell_analysis.c", "ui/layered_wave_source.c", "ui/layered_wave.c",
            "audio/stem_media.c", "audio/stem_decode.c", "test_extra_player.c")), str(mp3_object), "-lm"],
        "settings-test": [str(ROOT / "settings.c"), str(ROOT / "tests/test_settings.c")],
        "my-settings-test": [str(ROOT / "my_settings.c"), str(ROOT / "test_my_settings.c")],
        "native-decode-test": [str(ROOT / "cue/native.c"), str(ROOT / "cue/test_native_decode.c")],
        "stem-test": ["-I" + str(ROOT / "audio"), str(ROOT / "audio/stem_cache.c"),
                      str(ROOT / "audio/stem_mix.c"), str(ROOT / "audio/tests/test_stems.c"), "-lm"],
        "decode-test": ["-I" + str(ROOT / "audio"), str(ROOT / "audio/stem_decode.c"),
                        str(ROOT / "audio/tests/test_decode.c"), "-lm"],
        "shell-runtime-test": [*(str(ROOT / "shell" / name) for name in SHELL_SOURCES), str(ROOT / "shell_runtime_core.c"),
                               str(ROOT / "shell_library.c"), str(ROOT / "shell_playlists.c"), str(ROOT / "pdb/pdb.c"), str(ROOT / "test_shell_runtime.c")],
    }
    test_evidence = {}
    for name, sources in tests.items():
        binary = args.output.resolve() / name
        subprocess.run(common + sources + ["-o", str(binary)], check=True)
        test_evidence[name] = inspect(binary)
    result.update({"artifact": destination.name, "development_only": True,
                   "default_enabled": False, "hardware_verified": False,
                   "features": {"gate_cue": "native adapter, unverified",
                                "smart_cue": "native adapter, unverified",
                                "stems": "OverCue default; AZ four-part Vorbis and verified RX3 PCM packages/sidecars; legacy stemd retained; physical acceptance pending",
                                "ui": "render and touch adapters, physical acceptance pending",
                                "ram_tracks": "bounded complete compressed-track RAM; USB-removal adapter; physical eject acceptance pending",
                                "vj_tools": "optional display bridge; original receiver separately packaged"}})
    destination.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    (args.output / "tests-build.json").write_text(json.dumps(test_evidence, indent=2) + "\n", encoding="utf8")
    print(f"Built {destination.name}: {result['sha256']}")
    print(f"Display bridge: {result['display_bridge']['sha256']}; ARM32 / GLIBC_2.4 / NODELETE checks passed")
    print(f"Metadata: {destination.with_suffix('.json')}")


if __name__ == "__main__":
    main()
