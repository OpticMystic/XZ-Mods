"""Read the deck-code table and native LED probes from the exact supplied RAM runtime.

The controller is never modified. Four exported objects are read twice each:
xz_mods_deck_input_codes_v1 (every control code either deck hook saw, per side),
xz_mods_led_probe_v1 (latest native status-LED record words per channel and id),
xz_mods_led_pad_probe_v1 (native pad words and colour per channel, native pad page and pad) and
xz_mods_led_trace_v1 (LED hook counters). An entry counts only when both reads agree on an even
sequence. The objects are not one atomic snapshot.

Use: with a side on a native deck, operate the stock control (pause/play, a loop, VINYL SPEED ADJUST,
each pad mode) and read; the probes then hold the stock records and the code table holds the codes.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent
CODES = 128
CODES_SIZE = 32 + CODES * 48
LED_IDS = 64
LED_SIZE = 12 + 2 * LED_IDS * 48
PAD_SIZE = 4 + 2 * 4 * 8 * 44
TRACE_SIZE = 48
OBJECTS = {
    'xz_mods_deck_input_codes_v1': CODES_SIZE,
    'xz_mods_led_probe_v1': LED_SIZE,
    'xz_mods_led_pad_probe_v1': PAD_SIZE,
    'xz_mods_led_trace_v1': TRACE_SIZE,
}
TRACE_NAMES = ['version', 'calls', 'invalid_player_channel', 'invalid_innards', 'innards_channel_mismatch',
               'last_record_count', 'last_native_mode', 'pad_records', 'matching_channel_records',
               'forced_records', 'changed_records', 'reserved']
PATHS = {1: 'fallback', 2: 'global', 3: 'global_key'}
PAGES = ['HOT CUE', 'BEAT LOOP', 'SLIP LOOP', 'BEAT JUMP']


def _float(bits):
    value = struct.unpack('<f', struct.pack('<I', bits))[0]
    return value if math.isfinite(value) else str(value)


def _stable(first, second, start, size):
    a, b = first[start:start + size], second[start:start + size]
    sequence = struct.unpack_from('<I', b, 0)[0]
    return a == b and sequence and not sequence & 1


def decode_codes(first, second):
    if len(first) != CODES_SIZE or len(second) != CODES_SIZE:
        raise ValueError('Incomplete code table')
    head = struct.unpack_from('<8I', second)
    if struct.unpack_from('<4I', first) != head[:4] or head[:2] != (1, CODES_SIZE) or head[3] != CODES:
        raise ValueError('Unsupported code table ABI or changing configuration')
    rows, unstable = [], 0
    for slot in range(min(head[4], CODES)):
        start = 32 + slot * 48
        if not _stable(first, second, start, 48):
            unstable += 1
            continue
        w = struct.unpack_from('<12I', second, start)
        key = w[1]
        rows.append({'code_hex': hex(key & 0xffff), 'side': 'left' if not key >> 16 & 1 else 'right',
                     'count': w[2], 'operations': [op for op in range(16) if w[3] >> op & 1],
                     'paths': [PATHS.get(path, path) for path in range(32) if w[4] >> path & 1],
                     'first_ms': w[5], 'last_ms': w[6], 'last_operation': w[7],
                     'last_delta': struct.unpack('<i', struct.pack('<I', w[8]))[0],
                     'last_value': _float(w[9]), 'last_verdict': w[10],
                     'last_controls': w[11], 'decks_3_4_selected': {'left': bool(w[11] & 1), 'right': bool(w[11] & 2)}})
    return {'enabled': bool(head[2]), 'used': head[4], 'overflow': head[5], 'contention_drops': head[6],
            'unstable_entries': unstable, 'codes': rows}


def decode_led(first, second):
    if len(first) != LED_SIZE or len(second) != LED_SIZE:
        raise ValueError('Incomplete LED probe')
    version, *overflow = struct.unpack_from('<3I', second)
    if version != 1:
        raise ValueError('Unsupported LED probe ABI')
    channels, unstable = {}, 0
    for channel in range(2):
        rows = []
        for slot in range(LED_IDS):
            start = 12 + (channel * LED_IDS + slot) * 48
            w = struct.unpack_from('<12I', second, start)
            if not w[1]:
                continue
            if not _stable(first, second, start, 48):
                unstable += 1
                continue
            rows.append({'id': w[2], 'seen': w[1], 'changes': w[3], 'words': [hex(x) for x in w[4:12]],
                         'state_word_0x10': w[6]})
        channels[str(channel + 1)] = rows
    return {'overflow': overflow, 'unstable_entries': unstable, 'channels': channels}


def decode_pads(first, second):
    if len(first) != PAD_SIZE or len(second) != PAD_SIZE:
        raise ValueError('Incomplete pad probe')
    if struct.unpack_from('<I', second)[0] != 1:
        raise ValueError('Unsupported pad probe ABI')
    rows, unstable = [], 0
    for channel in range(2):
        for page in range(4):
            for pad in range(8):
                start = 4 + ((channel * 4 + page) * 8 + pad) * 44
                w = struct.unpack_from('<11I', second, start)
                if not w[1]:
                    continue
                if not _stable(first, second, start, 44):
                    unstable += 1
                    continue
                rows.append({'channel': channel + 1, 'native_page': PAGES[page], 'pad': 'ABCDEFGH'[pad],
                             'seen': w[1], 'words': [hex(x) for x in w[2:10]],
                             'rgb': '#%06x' % ((w[10] & 0xff) << 16 | (w[10] >> 8 & 0xff) << 8 | (w[10] >> 16 & 0xff))})
    return {'unstable_entries': unstable, 'pads': rows}


def decode_trace(first, second):
    if len(first) != TRACE_SIZE or len(second) != TRACE_SIZE:
        raise ValueError('Incomplete LED trace')
    return dict(zip(TRACE_NAMES, struct.unpack_from('<12I', second)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--host', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--monitor', required=True, type=Path, help='Local tools/xz-activity-monitor.py')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists() or args.pid < 1:
        raise ValueError('Choose a new output and a freshly verified positive native PID')
    trace_spec = importlib.util.spec_from_file_location('trace_reader', ROOT / 'read_deck_input_trace.py')
    trace_reader = importlib.util.module_from_spec(trace_spec)
    trace_spec.loader.exec_module(trace_reader)
    from elftools.elf.elffile import ELFFile
    binary = args.binary.read_bytes()
    offsets = {}
    with io.BytesIO(binary) as handle:
        elf = ELFFile(handle)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM' or elf['e_type'] != 'ET_DYN':
            raise ValueError('Expected the exact ARM32 runtime')
        table = elf.get_section_by_name('.dynsym')
        for name, size in OBJECTS.items():
            symbols = table.get_symbol_by_name(name) if table else None
            if not symbols or len(symbols) != 1 or symbols[0]['st_size'] != size or symbols[0]['st_info']['type'] != 'STT_OBJECT' or \
                    symbols[0]['st_info']['bind'] != 'STB_GLOBAL' or \
                    symbols[0]['st_other']['visibility'] != 'STV_DEFAULT' or symbols[0]['st_shndx'] == 'SHN_UNDEF':
                raise ValueError('Runtime has no matching ' + name)
            offset = int(symbols[0]['st_value'])
            loads = [segment for segment in elf.iter_segments() if segment['p_type'] == 'PT_LOAD']
            if not loads or min(segment['p_vaddr'] for segment in loads) != 0:
                raise ValueError('Unsupported runtime load origin')
            if offset < 0 or offset + size > 0x100000000 or not any(segment['p_flags'] & 4 and
                    segment['p_vaddr'] <= offset and offset + size <= segment['p_vaddr'] + segment['p_memsz']
                    for segment in loads):
                raise ValueError('Diagnostic object lies outside runtime load segments')
            offsets[name] = offset
    spec = importlib.util.spec_from_file_location('xz_trace_monitor', args.monitor)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    diagnostic = module.Diagnostic(args.host, args.source)

    def command(value):
        return diagnostic.command(value + '; echo').strip()

    def identity():
        if command('cat /sys/class/net/eth0/address').lower() != 'c8:3d:fc:0c:a8:3a':
            raise ValueError('Controller identity changed')
        if command('pidof rbp') != str(args.pid):
            raise ValueError('Native process changed')
        start = trace_reader.process_start(command(f'cat /proc/{args.pid}/stat'), args.pid)
        if command('md5sum /dev/shm/libxz-mods.so').split()[0] != hashlib.md5(binary).hexdigest():
            raise ValueError('RAM runtime differs from supplied binary')
        rows = [row.split() for row in command(f'cat /proc/{args.pid}/maps').splitlines()]
        maps = [row for row in rows if len(row) == 6 and row[-1] == '/dev/shm/libxz-mods.so' and row[2] == '00000000']
        if len(maps) != 1:
            raise ValueError('Expected one unchanged RAM runtime mapping')
        base = int(maps[0][0].split('-')[0], 16)
        if trace_reader.process_start(command(f'cat /proc/{args.pid}/stat'), args.pid) != start:
            raise ValueError('Process changed during identity read')
        return base, start, tuple(trace_reader.mapping_identity(rows, base + offsets[name], size)
                                  for name, size in OBJECTS.items())

    def read(address, size):
        page = address // 4096
        count = (address % 4096 + size + 4095) // 4096
        raw = command(f'dd if=/proc/{args.pid}/mem bs=4096 skip={page} count={count} 2>/dev/null | base64')
        data = base64.b64decode(''.join(raw.splitlines()), validate=True)
        if len(data) != count * 4096:
            raise ValueError('Incomplete memory read')
        return data[address % 4096:address % 4096 + size]

    try:
        binding = identity()
        base = binding[0]
        pairs = {name: (read(base + offsets[name], size), read(base + offsets[name], size))
                 for name, size in OBJECTS.items()}
        if identity() != binding:
            raise ValueError('Runtime mapping changed during observation')
    finally:
        diagnostic.close()
    report = {
        'codes': decode_codes(*pairs['xz_mods_deck_input_codes_v1']),
        'led_probe': decode_led(*pairs['xz_mods_led_probe_v1']),
        'pad_probe': decode_pads(*pairs['xz_mods_led_pad_probe_v1']),
        'led_trace': decode_trace(*pairs['xz_mods_led_trace_v1']),
        'captured_at': datetime.now(timezone.utc).isoformat(), 'pid': args.pid,
        'runtime_sha256': hashlib.sha256(binary).hexdigest(), 'controller_modified': False,
        'process_start_ticks': binding[1], 'atomic_snapshot': False,
    }
    with args.output.open('x', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=2, allow_nan=False)
        handle.write('\n')
    print(f"codes={len(report['codes']['codes'])} led_ids={sum(len(v) for v in report['led_probe']['channels'].values())} "
          f"pads={len(report['pad_probe']['pads'])}")


if __name__ == '__main__':
    main()
