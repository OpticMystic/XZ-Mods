"""Read an opt-in selector-input trace from the exact supplied RAM runtime.

The controller is never modified. The trace observes selector outcomes, not
player queue admission or audio completion. Two matching per-record reads do
not make the entire ring an atomic snapshot. Native and fallback paths may
observe the same physical event; keep their streams separate for calibration.
logical_deck is selection after routing, not the recipient of an owned release.
Ordinals give trace-write order, which may differ from captured input-clock order.
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

CAPACITY = 1024
HEADER_BYTES = 32
RECORD_BYTES = 52
SIZE = HEADER_BYTES + CAPACITY * RECORD_BYTES
FIELDS = 'sequence ordinal at_ms path channel code operation delta_bits value_bits shift verdict logical_deck controls'.split()


def decode(first, second):
    if len(first) != SIZE or len(second) != SIZE:
        raise ValueError('Incomplete input trace')
    a = struct.unpack_from('<8I', first)
    b = struct.unpack_from('<8I', second)
    if a[:4] != b[:4] or b[0:2] != (1, SIZE) or b[3] != CAPACITY or b[2] not in (0, 1):
        raise ValueError('Unsupported input trace ABI or changing configuration')
    rows = []
    unstable = 0
    for slot in range(CAPACITY):
        start = HEADER_BYTES + slot * RECORD_BYTES
        left = first[start:start + RECORD_BYTES]
        right = second[start:start + RECORD_BYTES]
        values = struct.unpack('<13I', right)
        if not values[0]:
            continue
        if left != right or values[0] & 1:
            unstable += 1
            continue
        distance = (b[4] - values[1]) & 0xffffffff
        if not 1 <= distance <= CAPACITY:
            continue
        row = dict(zip(FIELDS, values))
        row['delta'] = struct.unpack('<i', struct.pack('<I', row['delta_bits']))[0]
        value = struct.unpack('<f', struct.pack('<I', row['value_bits']))[0]
        row['value'] = value if math.isfinite(value) else str(value)
        row['code_hex'] = hex(row['code'])
        row['_distance'] = distance
        rows.append(row)
    rows.sort(key=lambda row: -row['_distance'])
    for row in rows:
        del row['_distance']
    return {'version': b[0], 'bytes': b[1], 'enabled': bool(b[2]), 'capacity': b[3],
            'next': b[4], 'contention_drops': b[5], 'idle_updates': b[6],
            'writer_active_at_second_read': bool(b[7]), 'unstable_records': unstable,
            'atomic_snapshot': False, 'records': rows}



def process_start(stat, pid):
    end = stat.rfind(')')
    if not stat.startswith(str(pid) + ' (') or end < 0:
        raise ValueError('Malformed process identity')
    fields = stat[end + 1:].split()
    if len(fields) < 20 or not fields[19].isdecimal():
        raise ValueError('Missing process start time')
    return int(fields[19])


def mapping_identity(rows, address, size):
    owned = tuple(tuple(row) for row in rows if len(row) == 6 and row[-1] == '/dev/shm/libxz-mods.so')
    coverage = []
    for row in rows:
        start, end = (int(value, 16) for value in row[0].split('-'))
        if start < address + size and end > address:
            if 'r' not in row[1]:
                raise ValueError('Proof mapping is not readable')
            coverage.append(tuple(row))
    if not coverage:
        raise ValueError('Proof object is not mapped')
    return owned, tuple(coverage)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--host', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--monitor', required=True, type=Path,
                        help='Local tools/xz-activity-monitor.py')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists() or args.pid < 1:
        raise ValueError('Choose a new output and a freshly verified positive native PID')
    from elftools.elf.elffile import ELFFile
    binary = args.binary.read_bytes()
    with io.BytesIO(binary) as handle:
        elf = ELFFile(handle)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM':
            raise ValueError('Expected the exact ARM32 runtime')
        table = elf.get_section_by_name('.dynsym')
        symbols = table.get_symbol_by_name('xz_mods_deck_input_trace_v1') if table else None
        if not symbols or len(symbols) != 1 or symbols[0]['st_size'] != SIZE or symbols[0]['st_info']['type'] != 'STT_OBJECT':
            raise ValueError('Runtime has no matching input trace ABI')
        offset = int(symbols[0]['st_value'])
        if not any(segment['p_type'] == 'PT_LOAD' and segment['p_vaddr'] <= offset and
                   offset + SIZE <= segment['p_vaddr'] + segment['p_memsz'] for segment in elf.iter_segments()):
            raise ValueError('Input trace is outside runtime load segments')
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
        start = process_start(command(f'cat /proc/{args.pid}/stat'), args.pid)
        if command('md5sum /dev/shm/libxz-mods.so').split()[0] != hashlib.md5(binary).hexdigest():
            raise ValueError('RAM runtime differs from supplied binary')
        rows = [row.split() for row in command(f'cat /proc/{args.pid}/maps').splitlines()]
        maps = [row for row in rows if len(row) == 6 and row[-1] == '/dev/shm/libxz-mods.so' and row[2] == '00000000']
        if len(maps) != 1:
            raise ValueError('Expected one unchanged RAM runtime mapping')
        address = int(maps[0][0].split('-')[0], 16) + offset
        if process_start(command(f'cat /proc/{args.pid}/stat'), args.pid) != start:
            raise ValueError('Process changed during identity read')
        return address, start, mapping_identity(rows, address, SIZE)

    try:
        binding = identity()
        address = binding[0]
        page = address // 4096
        count = (address % 4096 + SIZE + 4095) // 4096

        def read():
            raw = command(f'dd if=/proc/{args.pid}/mem bs=4096 skip={page} count={count} 2>/dev/null | base64')
            data = base64.b64decode(''.join(raw.splitlines()), validate=True)
            if len(data) != count * 4096:
                raise ValueError('Incomplete memory read')
            return data[address % 4096:address % 4096 + SIZE]

        report = decode(read(), read())
        if identity() != binding:
            raise ValueError('Runtime mapping changed during observation')
        report.update(captured_at=datetime.now(timezone.utc).isoformat(), pid=args.pid,
                      runtime_sha256=hashlib.sha256(binary).hexdigest(), controller_modified=False,
                      process_start_ticks=binding[1], mapping_identity=binding[2])
    finally:
        diagnostic.close()
    with args.output.open('x', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=2, allow_nan=False)
        handle.write('\n')
    print(f"input trace enabled={report['enabled']} retained={len(report['records'])} contention_drops={report['contention_drops']}")


if __name__ == '__main__':
    main()
