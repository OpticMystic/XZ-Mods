"""Run pinned XZ 1.26 play-state instructions in Unicorn, without device access.

Requires pyelftools and unicorn. Firmware remains an external private input.
Only isPlaying and the early pending-play branch execute; no callbacks are stubbed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rbp', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    expected = re.search(r'#define RBP_SHA "([0-9a-f]{64})"',
                         Path(__file__).with_name('contract.h').read_text())[1]
    digest = hashlib.sha256(args.rbp.read_bytes()).hexdigest()
    assert digest == expected, 'Firmware must match the helper RBP_SHA'
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    with args.rbp.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        for name in ('_ZNK10playengine10PlayEngine9isPlayingEN8djengine15EnPlayerChannelE',
                     '_ZN10playengine6Player4playEbbb'):
            symbol = symbols[name]
            section = elf.get_section(symbol['st_shndx'])
            address, size = symbol['st_value'], symbol['st_size']
            offset = address - section['sh_addr']
            uc.mem_map(address & ~4095, ((address & 4095) + size + 4095) & ~4095)
            uc.mem_write(address, section.data()[offset:offset + size])
    engine, vector, player, pending, stack, done = (0x2000000 + i * 0x10000 for i in range(6))
    for address in (engine, vector, player, pending, stack, done):
        uc.mem_map(address, 0x10000)

    def word(address, value):
        uc.mem_write(address, struct.pack('<I', value))

    def call(address, *values):
        for register, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), values):
            uc.reg_write(register, value)
        uc.reg_write(UC_ARM_REG_SP, stack + 0x8000)
        uc.reg_write(UC_ARM_REG_LR, done)
        uc.emu_start(address, done, count=100)
        assert uc.reg_read(UC_ARM_REG_PC) == done, 'Unexpected firmware branch or instruction limit'
        return uc.reg_read(UC_ARM_REG_R0)

    cases = 0
    for count in range(3):
        for present in (False, True):
            word(engine + 12, vector)
            word(engine + 16, vector + count * 4)
            word(vector, player if present else 0)
            word(vector + 4, player if present else 0)
            for adjacent in range(256):
                for playing in (0, 1, 0x80):
                    uc.mem_write(player + 0xe76, bytes((adjacent, playing)))
                    for deck in range(2):
                        actual = call(0x494ec, engine, deck)
                        assert actual == (playing if present and deck < count else 0)
                        cases += 1
    word(engine + 12, 0)
    word(engine + 16, 0)
    for deck in range(2):
        assert call(0x494ec, engine, deck) == 0
        cases += 1

    # The firmware itself can set +0xe76 while unloaded without setting +0xe77.
    # The early branch needs only a player and its pending-operation object.
    uc.mem_write(player, bytes(0x1000))
    uc.mem_write(player + 0xe64, b'\x01')
    word(player + 0xf1c, pending)
    uc.mem_write(pending + 12, b'\x01')
    call(0x6cb04, player, 0, 0, 0)
    flags = bytes(uc.mem_read(player + 0xe76, 2))
    assert flags == b'\x01\x00'
    assert bytes(uc.mem_read(player + 0xe66, 1)) == b'\x00'
    word(engine + 12, vector)
    word(engine + 16, vector + 4)
    word(vector, player)
    assert call(0x494ec, engine, 0) == 0
    receipt = {
        'rbp_sha256': digest, 'hardware': False, 'native_is_playing_cases': cases,
        'unloaded_pending_play': {'loaded_e66': 0, 'adjacent_e76': flags[0],
                                 'playing_e77': flags[1], 'native_is_playing': 0},
        'callbacks_stubbed': False,
    }
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS {cases} real ARM isPlaying cases; unloaded pending play sets e76=1, e77=0')


if __name__ == '__main__':
    main()
