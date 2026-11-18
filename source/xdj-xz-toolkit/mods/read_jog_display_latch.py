"""Read HISTORICAL own-code Jog evidence bound to a supplied runtime identity.

No native calls or writes. Counters are independent lifetime modulo32 values.
Bins retain their first outcome per captured context until that bin is replaced.
Unrelated old-context/session bins remain HISTORICAL, never current.
The successful-copy bin requires a normal loaded EXTRA INFO frame.
They do not describe the latest frame, a whole atomic snapshot or MCU acceptance.
The live entry point is never used by the offline test suite.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct

HEADER = (2, 4184, 2, 15, 132, 25)
MAX_PAGES = 3
SIZE = HEADER[1]
SYMBOL = 'xz_mods_jog_latch_v2'
MODULE = '/dev/shm/libxz-mods.so'
REASONS = 'NATIVE NO_PUBLICATION EMPTY LENGTH STALE FUTURE VALID'.split()
RESULTS = 'OK NOT_READY BAD_INPUT UNSUPPORTED RESTORING STALE'.split()
FIELDS = '''seq valid context session control route_epoch at_ms_lo at_ms_hi
source_present source_session source_route_epoch source_revision source_timeline_route
source_load_revision source_media_lo source_media_hi source_transport_generation
source_device publication_seq publication_ms snapshot_reason select_result step_result
phase_before phase desired encoded_frame encoded_packet packet_command candidate_changed
final_copied fallback read_result'''.split()
assert len(FIELDS) == 33
COUNTERS = ['callbacks', 'contention', 'context_changes', 'session'] + \
    ['reason_' + n for n in REASONS] + ['select_' + n for n in RESULTS] + \
    ['step_' + n for n in RESULTS] + ['guard_skipped', 'memcpy_completed']
assert len(COUNTERS) == 25


def valid_record(record, slot, side):
    if record['valid'] != 1 or not record['context']:
        return False
    if any(record[k] not in (0, 1) for k in ('source_present', 'candidate_changed', 'final_copied', 'fallback')):
        return False
    if record['snapshot_reason'] >= 7 or record['select_result'] >= 6 or record['step_result'] >= 6 or record['phase_before'] >= 5 or record['phase'] >= 5:
        return False
    if record['control'] > 15 or record['encoded_frame'] > 15 or record['encoded_packet'] > 15 or record['packet_command'] > 255:
        return False
    desired = side + (3 if record['control'] & 4 and record['control'] & (1 << side) else 1)
    if record['desired'] != desired or record['read_result'] not in (0, 1, 0xffffffff):
        return False
    if slot < 7 and record['snapshot_reason'] != slot:
        return False
    if 7 <= slot < 13 and record['step_result'] != slot - 7:
        return False
    if slot >= 13 and record['final_copied'] != slot - 13:
        return False
    if slot == 14 and not (record['snapshot_reason'] == 6 and record['source_present'] and
            record['phase_before'] == record['phase'] == 2 and record['step_result'] == 0 and
            record['packet_command'] == 0x21 and record['desired'] == side + 3 and
            record['encoded_frame'] == record['encoded_packet'] == record['desired']):
        return False
    if record['publication_seq'] > 0x3fffffff:
        return False
    if record['source_present'] and (record['source_session'] != record['session'] or not record['route_epoch'] or
            record['source_route_epoch'] != record['route_epoch'] or
            record['source_timeline_route'] not in (0, record['route_epoch']) or not record['publication_seq']):
        return False
    return True


def decode(first, second):
    if len(first) != SIZE or len(second) != SIZE:
        raise ValueError('Incomplete Jog historical latch')
    left, right = [struct.unpack('<1046I', blob) for blob in (first, second)]
    if tuple(left[:6]) != HEADER or tuple(right[:6]) != HEADER:
        raise ValueError('Unsupported Jog historical latch metadata ABI')
    report = {'version': 2, 'size': SIZE, 'evidence': 'HISTORICAL', 'latest_frame': False,
              'atomic_snapshot': False, 'counter_scope': 'independent lifetime modulo32 observations', 'sides': []}
    for side in range(2):
        start = 6 + side * 520
        row = {'physical_side': side + 1, 'counters': dict(zip(COUNTERS, right[start:start+25])), 'slots': []}
        for slot in range(15):
            at = start + 25 + slot * 33
            a, b = left[at:at+33], right[at:at+33]
            stable = a == b and not b[0] & 1
            item = {'slot': slot, 'bin': 'reason' if slot < 7 else 'step_result' if slot < 13 else 'final_copy',
                    'event_stable': stable, 'latest_frame': False, 'current_context_claim': False}
            if slot == 14:
                item['bin'] = 'normal_loaded_extra_info_memcpy'
            if stable and b[1]:
                record = dict(zip(FIELDS, b))
                if b[0] and valid_record(record, slot, side):
                    record.update(evidence='HISTORICAL', latest_frame=False, current_context_claim=False,
                                  copy_outcome='ACTUAL_MEMCPY' if record['final_copied'] else 'GUARD_SKIPPED',
                                  at_monotonic_ms=(record.pop('at_ms_hi') << 32) | record.pop('at_ms_lo'),
                                  snapshot_reason_name=REASONS[record['snapshot_reason']],
                                  select_result_name=RESULTS[record['select_result']],
                                  step_result_name=RESULTS[record['step_result']],
                                  copy_scope='assembler memcpy only; transport and MCU acceptance unobserved')
                    if not record['source_present']:
                        for key in list(record):
                            if key.startswith('source_') and key != 'source_present' or key in ('publication_seq', 'publication_ms'):
                                del record[key]
                    item['record'] = record
                else:
                    item['event_stable'] = False
                    item['rejection'] = 'invalid historical record'
            row['slots'].append(item)
        report['sides'].append(row)
    return report


def process_start(stat, pid):
    end = stat.rfind(')')
    if not stat.startswith(str(pid) + ' (') or end < 0:
        raise ValueError('Malformed process identity')
    fields = stat[end+1:].split()
    if len(fields) < 20 or not fields[19].isdecimal():
        raise ValueError('Missing process start time')
    return int(fields[19])


def mapping_identity(rows, address, size):
    if size != SIZE or address < 0 or address + size > 0x100000000:
        raise ValueError('Invalid proof memory bounds')
    owned, coverage, spans = [], [], []
    for row in rows:
        if len(row) < 5 or not re.fullmatch(r'[0-9a-fA-F]+-[0-9a-fA-F]+', row[0]):
            raise ValueError('Malformed process mapping')
        start, end = (int(v, 16) for v in row[0].split('-'))
        if start >= end or end > 0x100000000:
            raise ValueError('Invalid process mapping range')
        if len(row) == 6 and row[-1] == MODULE:
            owned.append(row)
        if start < address + size and end > address:
            if not row[1].startswith('r'):
                raise ValueError('Proof mapping is not readable')
            coverage.append(row)
            spans.append((max(start, address), min(end, address+size)))
    cursor = address
    for start, end in sorted(spans):
        if start != cursor:
            raise ValueError('Proof mapping has a gap or overlap')
        cursor = end
    if not owned or cursor != address + size:
        raise ValueError('Proof object is not fully mapped')
    return [owned, coverage]


def expected_identity(path):
    if path.stat().st_size > 2*1024*1024:
        raise ValueError('Oversized identity input')
    identity = json.loads(path.read_text(encoding='utf-8-sig'))
    if not re.fullmatch('[0-9a-f]{64}', str(identity.get('runtime_sha256', ''))) or \
            type(identity.get('pid')) is not int or not 0 < identity['pid'] < 0x80000000 or \
            type(identity.get('process_start_ticks')) is not int or identity['process_start_ticks'] < 1 or \
            not re.fullmatch(r'(?:[0-9a-f]{2}:){5}[0-9a-f]{2}', str(identity.get('controller_mac', ''))):
        raise ValueError('Supply exact runtime hash, PID/start and MAC identity')
    mapping = identity.get('mapping_identity')
    if not isinstance(mapping, list) or len(mapping) != 2 or not all(isinstance(v, list) and v for v in mapping):
        raise ValueError('Supply exact module and proof mapping identity')
    if any(not isinstance(row, list) or not 5 <= len(row) <= 6 or not all(isinstance(v, str) for v in row)
           for group in mapping for row in group):
        raise ValueError('Malformed expected mapping identity')
    return identity


def runtime_symbol(data):
    # ELF metadata of the supplied own-code runtime only. No native code inspection.
    from elftools.elf.elffile import ELFFile
    with io.BytesIO(data) as handle:
        elf = ELFFile(handle)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM' or elf['e_type'] != 'ET_DYN':
            raise ValueError('Expected exact ARM32 own-code runtime')
        table = elf.get_section_by_name('.dynsym')
        symbols = table.get_symbol_by_name(SYMBOL) if table else None
        if not symbols or len(symbols) != 1 or symbols[0]['st_size'] != SIZE or \
                symbols[0]['st_info']['type'] != 'STT_OBJECT' or symbols[0]['st_info']['bind'] != 'STB_GLOBAL' or \
                symbols[0]['st_other']['visibility'] != 'STV_DEFAULT' or symbols[0]['st_shndx'] == 'SHN_UNDEF':
            raise ValueError('Build has no matching historical latch object')
        offset = int(symbols[0]['st_value'])
        loads = [segment for segment in elf.iter_segments() if segment['p_type'] == 'PT_LOAD']
        if not loads or min(segment['p_vaddr'] for segment in loads) != 0:
            raise ValueError('Unsupported runtime load origin')
        if offset < 0 or offset + SIZE > 0x100000000 or not any(segment['p_flags'] & 4 and
                segment['p_vaddr'] <= offset and offset + SIZE <= segment['p_vaddr'] + segment['p_memsz']
                for segment in loads):
            raise ValueError('Latch lies outside runtime load segments')
        return offset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--identity', required=True, type=Path,
                        help='Previously supplied exact runtime/process/MAC/module and proof mapping identity JSON')
    parser.add_argument('--host', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--monitor', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Choose a new output file')
    expected = expected_identity(args.identity)
    if not 0 < args.binary.stat().st_size <= 64*1024*1024:
        raise ValueError('Runtime input exceeds size bound')
    data = args.binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected['runtime_sha256']:
        raise ValueError('Supplied runtime hash mismatch')
    offset = runtime_symbol(data)
    spec = importlib.util.spec_from_file_location('xz_jog_latch_monitor', args.monitor)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    diagnostic = module.Diagnostic(args.host, args.source)
    pid = expected['pid']

    def command(value, limit=2*1024*1024):
        raw = diagnostic.command(value + '; echo')
        if not isinstance(raw, str) or len(raw) > limit:
            raise ValueError('Diagnostic response exceeds bound')
        return raw.strip()

    def identity():
        if command('cat /sys/class/net/eth0/address', 256).lower() != expected['controller_mac']:
            raise ValueError('Controller MAC identity mismatch')
        if command('pidof rbp', 256) != str(pid):
            raise ValueError('Native process PID mismatch')
        start = process_start(command(f'cat /proc/{pid}/stat', 8192), pid)
        if start != expected['process_start_ticks']:
            raise ValueError('Native process start mismatch')
        digest = command('sha256sum ' + MODULE, 1024).split()
        if len(digest) != 2 or digest != [expected['runtime_sha256'], MODULE]:
            raise ValueError('RAM runtime differs from supplied hash')
        rows = [row.split() for row in command(f'cat /proc/{pid}/maps').splitlines()]
        bases = [row for row in rows if len(row) == 6 and row[-1] == MODULE and row[2] == '00000000']
        if len(bases) != 1:
            raise ValueError('Expected one own-code RAM runtime base mapping')
        address = int(bases[0][0].split('-')[0], 16) + offset
        binding = mapping_identity(rows, address, SIZE)
        if binding != expected['mapping_identity']:
            raise ValueError('Supplied module/proof mapping identity mismatch')
        if process_start(command(f'cat /proc/{pid}/stat', 8192), pid) != start:
            raise ValueError('Process changed during identity read')
        return address, binding

    try:
        address, binding = identity()
        page, count = address // 4096, (address % 4096 + SIZE + 4095) // 4096
        if not 1 <= count <= MAX_PAGES:
            raise ValueError('Proof read exceeds three pages')

        def read():
            raw = command(f'dd if=/proc/{pid}/mem bs=4096 skip={page} count={count} 2>/dev/null | base64', 24576)
            encoded = ''.join(raw.split())
            if len(encoded) != 4*((count*4096+2)//3):
                raise ValueError('Invalid bounded proof memory length')
            value = base64.b64decode(encoded, validate=True)
            if len(value) != count*4096:
                raise ValueError('Incomplete proof memory read')
            return value[address%4096:address%4096+SIZE]

        report = decode(read(), read())
        if identity() != (address, binding):
            raise ValueError('Runtime mapping changed during observation')
        report.update(pid=pid, process_start_ticks=expected['process_start_ticks'],
                      controller_mac=expected['controller_mac'], runtime_sha256=expected['runtime_sha256'],
                      mapping_identity=binding, captured_at=datetime.now(timezone.utc).isoformat(), controller_modified=False)
    finally:
        diagnostic.close()
    with args.output.open('x', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=2)
        handle.write('\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
