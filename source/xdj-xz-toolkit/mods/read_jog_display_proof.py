"""Read private Jog LCD counters from a freshly identified, exact RAM runtime.

No controller writes or calls into its code. Global/publication counters are
independent observations. Only matching even per-side sequences qualify a side's
assembler state; the complete result is never an atomic snapshot.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct

GLOBAL = 'version start_attempts install_ok install_failed installed test_installs calls disabled_calls null_self invalid_channel last_invalid_channel snapshot_calls snapshot_no_players selections selection_control'.split()
SIDE = 'calls publications published_seq published_ms seq channel control desired snapshot_reason select_result step_result phase encoded_frame encoded_packet changed fallback read_seq'.split()
SIZE = 4 * (len(GLOBAL) + 2 * len(SIDE))
REASONS = 'NATIVE NO_PUBLICATION EMPTY LENGTH STALE FUTURE VALID'.split()


def decode(first, second):
    if len(first) != SIZE or len(second) != SIZE:
        raise ValueError('Incomplete Jog LCD proof')
    left = struct.unpack(f'<{SIZE // 4}I', first)
    right = struct.unpack(f'<{SIZE // 4}I', second)
    if left[0] != 1 or right[0] != 1:
        raise ValueError('Unsupported Jog LCD proof ABI')
    result = {'counters': dict(zip(GLOBAL, right)), 'sides': [], 'atomic_snapshot': False}
    for side in range(2):
        start = len(GLOBAL) + side * len(SIDE)
        a, b = left[start:start + len(SIDE)], right[start:start + len(SIDE)]
        valid = a[4] == b[4] and not (b[4] & 1) and a[5:] == b[5:]
        row = {'physical_side': side + 1, 'assembler_state_stable': valid,
               'counters': dict(zip(SIDE[:4], b[:4]))}
        if valid:
            row['state'] = dict(zip(SIDE[4:], b[4:]))
            why = row['state']['snapshot_reason']
            row['state']['snapshot_reason_name'] = REASONS[why] if why < len(REASONS) else 'UNKNOWN'
        result['sides'].append(row)
    return result



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
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--monitor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.pid < 1:
        raise ValueError('Choose a new output and a freshly verified positive PID')
    from elftools.elf.elffile import ELFFile
    data = args.binary.read_bytes()
    with io.BytesIO(data) as handle:
        elf = ELFFile(handle)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM':
            raise ValueError('Expected exact ARM32 runtime')
        table = elf.get_section_by_name('.dynsym')
        symbols = table.get_symbol_by_name('xz_mods_jog_proof_v1') if table else None
        if not symbols or len(symbols) != 1 or symbols[0]['st_size'] != SIZE or symbols[0]['st_info']['type'] != 'STT_OBJECT':
            raise ValueError('Build has no matching Jog LCD proof')
        offset = int(symbols[0]['st_value'])
        if not any(segment['p_type'] == 'PT_LOAD' and segment['p_vaddr'] <= offset and
                   offset + SIZE <= segment['p_vaddr'] + segment['p_memsz'] for segment in elf.iter_segments()):
            raise ValueError('Jog proof lies outside runtime load segments')
    spec = importlib.util.spec_from_file_location('xz_jog_monitor', args.monitor)
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
        if command('md5sum /dev/shm/libxz-mods.so').split()[0] != hashlib.md5(data).hexdigest():
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
        page, count = address // 4096, (address % 4096 + SIZE + 4095) // 4096

        def read():
            raw = command(f'dd if=/proc/{args.pid}/mem bs=4096 skip={page} count={count} 2>/dev/null | base64')
            value = base64.b64decode(''.join(raw.splitlines()), validate=True)
            if len(value) != count * 4096:
                raise ValueError('Incomplete proof memory read')
            return value[address % 4096:address % 4096 + SIZE]

        result = decode(read(), read())
        if identity() != binding:
            raise ValueError('Runtime mapping changed during observation')
        result.update(pid=args.pid, captured_at=datetime.now(timezone.utc).isoformat(),
                      runtime_sha256=hashlib.sha256(data).hexdigest(), controller_modified=False,
                      process_start_ticks=binding[1], mapping_identity=binding[2])
    finally:
        diagnostic.close()
    with args.output.open('x', encoding='utf-8', newline='\n') as handle:
        json.dump(result, handle, indent=2)
        handle.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':main()
