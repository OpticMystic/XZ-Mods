"""Build a private, exact-firmware loopback bridge; never contacts a controller."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from elftools.elf.elffile import ELFFile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def kernel_contract(module, rebuilt):
    with module.open("rb") as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf["e_machine"] == "EM_ARM"
        assert elf.get_section_by_name(".gnu.linkonce.this_module")["sh_size"] == 320
        raw = elf.get_section_by_name("__versions").data()
        expected = {}
        for offset in range(0, len(raw), 64):
            name = raw[offset + 4:offset + 64].split(b"\0", 1)[0].decode()
            expected[name] = struct.unpack_from("<I", raw, offset)[0]
        imports = {s.name for s in elf.get_section_by_name(".symtab").iter_symbols()
                   if s["st_shndx"] == "SHN_UNDEF" and s.name}
        assert not imports - expected.keys()
        magic, = [v.decode().removeprefix("vermagic=") for v in
                  elf.get_section_by_name(".modinfo").data().split(b"\0")
                  if v.startswith(b"vermagic=")]
    expected["usb_get_dev"] = int(rebuilt["usb_get_dev"], 16)
    assert all(int(rebuilt[name], 16) == value for name, value in expected.items())
    return magic.split()[0], expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--sha-source", type=Path, required=True)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).parent
    module = args.module / "xz_hub_depth.ko"
    release, crcs = kernel_contract(module, json.loads((args.module / "rebuilt-crcs.json").read_text()))
    pins = {"RBP_SHA": sha(args.payload / "rbp.patched"),
            "RUNTIME_SHA": sha(args.payload / "tools/libxz-mods.so"),
            "RECEIVER_SHA": sha(args.payload / "tools/libxz-directfb-hook.so"),
            "MODULE_SHA": sha(module), "KERNEL_RELEASE": release}
    header = "".join(f'#define {name} "{value}"\n' for name, value in pins.items())
    header += 'static const struct kernel_crc { const char *name; unsigned int value; } kernel_crcs[] = {\n'
    header += "".join(f'    {{"__kcrctab_{name}", 0x{value:08x}u}},\n' for name, value in sorted(crcs.items()))
    header += "};\n"
    (args.output / "contract.h").write_text(header)
    shutil.copyfile(module, args.output / module.name)
    # Quoted includes search the source directory first. Compile a staged copy
    # so the generated contract, rather than the retained example, is used.
    for name in ("bridge.c", "port_layout.h"):
        shutil.copyfile(source / name, args.output / name)
    command = [str(args.zig), "cc", "-target", "arm-linux-gnueabi.2.13", "-mcpu=cortex_a9",
               "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-pie", "-no-pie",
               "-I" + str(args.output), "-I" + str(args.sha_source),
               str(args.output / "bridge.c"), str(args.sha_source / "sha256.c")]
    binary = args.output / "xz-auto-bridge"
    subprocess.run([*command, "-s", "-o", str(binary)], check=True)
    binary_bytes = binary.read_bytes()
    for name, value in pins.items():
        assert value.encode("ascii") in binary_bytes, "Compiled bridge lost generated pin: " + name
    with binary.open("rb") as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf["e_machine"] == "EM_ARM" and not elf["e_flags"] & 0x400
        versions = {s.name for _, aux in elf.get_section_by_name(".gnu.version_r").iter_versions() for s in aux}
        assert all(not v.startswith("GLIBC_") or tuple(map(int, v[6:].split("."))) <= (2, 4) for v in versions)
    linux = "/mnt/" + str(binary)[0].lower() + "/" + str(binary)[3:].replace("\\", "/")
    subprocess.run(["wsl", "-d", "Ubuntu", "--", "qemu-arm", "-cpu", "cortex-a9",
                    "/usr/arm-linux-gnueabi/lib/ld-linux.so.3", "--library-path",
                    "/usr/arm-linux-gnueabi/lib", linux, "--self-test"], check=True)
    receipt = {"pins": pins, "kernel_crcs": crcs, "binary_sha256": sha(binary),
               "binary_bytes": binary.stat().st_size, "glibc_versions": sorted(versions),
               "arm_self_test_passed": True, "hardware_contacted": False,
               "compiled_pins_verified": True,
               "contract_sha256": sha(args.output / "contract.h"),
               "sources": {name: sha(source / name) for name in ("build_bridge.py", "bridge.c", "port_layout.h")}}
    (args.output / "build.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
