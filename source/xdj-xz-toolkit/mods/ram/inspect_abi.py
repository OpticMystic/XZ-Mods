"""Read-only disassembly of the qualified XZ track-stream/removal functions."""
import argparse
import hashlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('firmware', type=Path)
parser.add_argument('patterns', nargs='+')
args = parser.parse_args()
if hashlib.sha256(args.firmware.read_bytes()).hexdigest() != '6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2':
    raise ValueError('Unknown firmware')
with args.firmware.open('rb') as stream:
    elf = ELFFile(stream)
    symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
    names = {s['st_value']: s.name for s in symbols if s['st_size']}
    sections = {i: (section['sh_addr'], section.data())
                for i, section in enumerate(elf.iter_sections()) if section['sh_flags'] & 4}
    for symbol in sorted(symbols, key=lambda s: s['st_value']):
        if not symbol['st_size'] or not isinstance(symbol['st_shndx'], int) or not any(p in symbol.name for p in args.patterns):
            continue
        if symbol['st_shndx'] not in sections:
            continue
        base, data = sections[symbol['st_shndx']]
        offset = symbol['st_value'] - base
        code = data[offset:offset + symbol['st_size']]
        print(f'\n{symbol.name} {symbol["st_value"]:#x} size={symbol["st_size"]} guard={code[:8].hex()}')
        for instruction in Cs(CS_ARCH_ARM, CS_MODE_ARM).disasm(code, symbol['st_value']):
            target = instruction.op_str.removeprefix('#')
            label = ''
            if instruction.mnemonic in ('bl', 'b') and target.startswith('0x'):
                label = names.get(int(target, 16), '')
            print(f'{instruction.address:08x}: {instruction.mnemonic:8} {instruction.op_str:36} {label}')
