"""Verify RAM playback with real file copies, sanitizers, and the ARM runtime under QEMU.

Windows requires the Ubuntu WSL distribution with cc and qemu-arm, plus Zig.
No controller, physical USB, playback commands, or firmware writes are used.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--auto',action='store_true',help='Exercise automatic playing-track retention')
parser.add_argument('--stop-key',action='store_true',help='Exercise safe USB STOP before removal dispatch')
parser.add_argument('--presentation',action='store_true',help='Exercise detached native track identity')
parser.add_argument('--display-assets',action='store_true',help='Exercise raw ejection and retained native display assets')
parser.add_argument('--scalars',action='store_true',help='Exercise native title/wave/control identity getters')
parser.add_argument('--firmware', type=Path, default=HERE.parents[1] / 'vendor/decrypted_iso/pdj/extracted/pdj/rbp')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
output = args.output.resolve()
firmware = args.firmware.resolve()
if hashlib.sha256(firmware.read_bytes()).hexdigest() != '6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2':
    raise ValueError('Unknown firmware')
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
guards = re.findall(r'\{(0x[0-9a-f]+),\{([^}]+)\}\}', (HERE / 'abi.h').read_text())
with firmware.open('rb') as stream:
    elf = ELFFile(stream)
    text = elf.get_section_by_name('.text')
    data, base = text.data(), text['sh_addr']
    for address, guard in guards:
        address = int(address, 16)
        expected = bytes(int(byte, 16) for byte in guard.split(','))
        if data[address-base:address-base+8] != expected:
            raise ValueError(f'Guard mismatch at {address:#x}')
        instructions = list(Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(expected, address))
        if len(instructions) != 2 or any('pc' in instruction.op_str or instruction.mnemonic.startswith('b') for instruction in instructions):
            raise ValueError('RAM hook prologue cannot be relocated verbatim')
    for site in (0xefef8,0xeec14,0xec728,0xf11f4,0x1425c8):
        call=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(data[site-4-base:site-base],site-4))
        if len(call)!=1 or call[0].mnemonic!='bl' or call[0].op_str!='#0xa9374':
            raise ValueError('Unqualified source-selector display caller')
    display_sites=((0xf5be0,0xd1d3c),(0xa4404,0xa761c),(0xec440,0xa904c),(0xec48c,0xa904c),(0xec52c,0xa904c),(0xeec2c,0xa904c),(0x1425e0,0xa904c),(0xf56a8,0xd174c),(0xb2c48,0x13a470),(0xa24c8,0x13a7f4),(0xa33e4,0x13a7f4),(0xa4744,0x13a7f4),(0xa4af0,0x13a7f4),
                   (0xb09f8,0xa02dc),(0xb0af0,0xa02dc),(0xb4c24,0xa02dc),(0xb4c80,0xa02dc))
    for site,target in display_sites:
        call=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(data[site-4-base:site-base],site-4))
        if len(call)!=1 or call[0].mnemonic!='bl' or call[0].op_str!=hex(target).replace('0x','#0x'):
            raise ValueError('Unqualified retained display or raw-eject caller')
    tail=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(data[0xa0314-base:0xa0318-base],0xa0314))
    if len(tail)!=1 or tail[0].mnemonic!='b' or tail[0].op_str!='#0x2547f8':
        raise ValueError('Raw USB sentinel no longer preserves its caller at emergency dispatch')
    for address in (0x13a528,0x13a52c,0x13a880,0x13a884):
        op=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(data[address-base:address-base+4],address))
        if len(op)!=1 or not op[0].op_str.endswith('#4'):
            raise ValueError('Native wave ownership mask differs from bit mask4')
    # The retained USB selector uses the exact native setter's kind2/3 mapping.
    import struct
    if struct.unpack_from('<5I', data, 0xa93e8-base) != (0xa9444,0xa943c,0xa9434,0xa93fc,0xa93b8):
        raise ValueError('Native device-to-source selector mapping differs')
    if data[0xa9434-base:0xa9444-base] != bytes.fromhex('0230a0e3dfffffea0330a0e3ddffffea'):
        raise ValueError('Native USB source selector values differ')
    local_vtable = next(symbol['st_value'] for symbol in elf.get_section_by_name('.symtab').iter_symbols()
                        if symbol.name == '_ZTVN6common16LocalInputStreamE')
    if local_vtable + 8 != 0x3b7bc8:
        raise ValueError('Local stream ABI differs')

def linux(path: Path) -> str:
    path = path.resolve()
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:] if os.name == 'nt' else str(path)

prefix = ['wsl', '-d', 'Ubuntu', '--'] if os.name == 'nt' else []
sources = [HERE / 'test_runtime.c', HERE / 'cache.c']
warnings = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation']
host = output / 'ram-sanitized-test'
subprocess.run(prefix + ['cc', *warnings, '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                         *map(linux, sources), '-pthread', '-o', linux(host)], check=True)
assertion = subprocess.run(prefix + [linux(host), '--prove-assertions'], capture_output=True)
if assertion.returncode == 0:
    raise ValueError('Disabled assertion acceptance')
mode=['--display-assets'] if args.display_assets else ['--scalars'] if args.scalars else ['--presentation'] if args.presentation else ['--stop-key'] if args.stop_key else ['--auto'] if args.auto else []
host_result = subprocess.run(prefix + [linux(host)]+mode, check=True, capture_output=True, text=True, timeout=60)
print(host_result.stdout.strip())
arm = output / 'ram-arm-test'
subprocess.run([str(args.zig.resolve()), 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9',
                *warnings, *map(str, sources), '-pthread', '-o', str(arm)], check=True)
arm_result = subprocess.run(prefix + ['qemu-arm', '-cpu', 'cortex-a9', '-L', '/usr/arm-linux-gnueabi', linux(arm)]+mode,
                            check=True, capture_output=True, text=True, timeout=60)
print(arm_result.stdout.strip())
report = {'firmware_sha256': hashlib.sha256(firmware.read_bytes()).hexdigest(),
          'qualified_hook_guards': len(guards), 'assertion_failure_exit': assertion.returncode,
          'host_sanitizers_passed': True, 'arm_qemu_passed': True,
          'arm_test_sha256': hashlib.sha256(arm.read_bytes()).hexdigest(),
          'source_hashes': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                            for path in sorted(HERE.glob('*')) if path.is_file()},
          'hardware_touched': False, 'physical_usb_eject_verified': False,
          'host_output': host_result.stdout.strip(), 'arm_output': arm_result.stdout.strip()}
(output / 'ram-verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'PASS {len(guards)} firmware guards, active assertions, ASan/UBSan and actual ARM cache/decoder/removal adapters')
