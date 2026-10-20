"""Read the FLAGSHIP shell's frame and input counters (xz_mods_shell_proof_v1) without changing device memory.

Usage: python mods/read_shell_proof.py RECEIPT BINARY OUTPUT.json [--samples N]
RECEIPT is the deploy_development.py receipt, BINARY the deployed libxz-mods-development.so.
Each sample requires matching load/routing fields in two consecutive reads.
Rendering counters are observational and may advance; this is not an atomic snapshot.
Samples are about a second apart. Only the 260-byte proof layout is supported.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import time
from elftools.elf.elffile import ELFFile
from device_smoke import command

FIELDS = ('version size sequence enabled load_enabled owns handed_off caution_yield caution screen capabilities '
          'second_frames second_render_calls second_stock_flips second_skipped second_busy second_pump second_ticks '
          'second_render_avg_us second_render_max_us second_refresh_avg_us second_refresh_max_us '
          'frames render_calls stock_flips keys_swallowed keys_passed key_hand_offs touches encoder_events actions '
          'hand_offs resumes back_hold_resumes commands_run commands_refused commands_dropped command_latency_max_ms '
          'last_command last_command_result guards model_guard_r3 model_guard_r4 model_guard_r5 model_guard_r6 '
          'wave_loads wave_failures grid_loads library_tracks_usb1 library_tracks_usb2 library_errors list_builds '
          'list_errors list_build_ms second_refreshes second_gap_max_us second_late second_tick_busy '
          'second_clock_reads second_moving model_holds fx_guard_failures caution_filtered ui_requests menu_passes').split()
SIZE = 4 * len(FIELDS)
STABLE_FIELDS = ('enabled load_enabled owns handed_off caution_yield caution screen capabilities '
                 'commands_run commands_refused commands_dropped last_command last_command_result guards').split()

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('receipt', type=Path)
parser.add_argument('binary', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--host', default='169.254.168.59')
parser.add_argument('--samples', type=int, default=5)
args = parser.parse_args()
if args.samples < 1:
    raise ValueError('Choose at least one sample')
if args.output.exists():
    raise ValueError('Choose a new evidence file')
receipt = json.loads(args.receipt.read_text())
started = re.findall(r'^STARTED\r?\n([1-9][0-9]*)\r?$', receipt.get('after', ''), re.M)
if len(started) != 1:
    raise ValueError('Receipt has no unambiguous deployment PID')
expected_pid = started[0]
if hashlib.sha256(args.binary.read_bytes()).hexdigest() != receipt['runtime_sha256']:
    raise ValueError('Binary differs from deployed receipt')
with args.binary.open('rb') as f:
    dynsym = ELFFile(f).get_section_by_name('.dynsym')
    symbols = dynsym.get_symbol_by_name('xz_mods_shell_proof_v1') if dynsym else None
    if not symbols or len(symbols) != 1 or symbols[0]['st_size'] != SIZE:
        raise ValueError('Unexpected shell proof ABI')
    offset = int(symbols[0]['st_value'])
pid = command(args.host, 'pidof rbp').strip()
if pid != expected_pid:
    raise ValueError('Application process changed from the deployed receipt')
path = receipt['remote'] + '/mods.so'


def mapped_address():
    rows = [line.split() for line in command(args.host, f'cat /proc/{pid}/maps').splitlines()]
    mapping = [row for row in rows if len(row) >= 6 and row[-1] == path and row[2] == '00000000']
    if len(mapping) != 1:
        raise ValueError('Deployed library is not mapped as expected')
    return int(mapping[0][0].split('-')[0], 16) + offset


address = mapped_address()


def require_pid():
    if command(args.host, 'pidof rbp').strip() != pid:
        raise ValueError('Application process changed during the observation')


def read(length):
    start = address // 4096
    count = (address % 4096 + length + 4095) // 4096
    blob = base64.b64decode(''.join(command(args.host, f'dd if=/proc/{pid}/mem bs=4096 skip={start} count={count} 2>/dev/null | base64').splitlines()), validate=True)
    if len(blob) != count * 4096:
        raise ValueError('Incomplete proof memory read')
    return blob[address % 4096:address % 4096 + length]


def decode(blob):
    values = dict(zip(FIELDS, struct.unpack(f'<{len(FIELDS)}I', blob)))
    if values['version'] != 1 or values['size'] != SIZE:
        raise ValueError('Unexpected shell proof header')
    return values


samples = []
for sample_index in range(args.samples):
    require_pid()
    for attempt in range(5):
        first = decode(read(SIZE))
        second = decode(read(SIZE))
        if all(first[name] == second[name] for name in STABLE_FIELDS):
            break
    else:
        raise ValueError('Load counters kept changing during the read')
    if mapped_address() != address:
        raise ValueError('Deployed library mapping changed during the observation')
    require_pid()
    samples.append(second)
    if sample_index + 1 < args.samples:
        time.sleep(1)
with args.output.open('x', encoding='utf-8') as output:
    json.dump({'pid': int(pid), 'address': hex(address), 'runtime_sha256': receipt['runtime_sha256'],
               'proof_bytes': SIZE, 'stable_fields': STABLE_FIELDS,
               'atomic_snapshot': False, 'samples': samples}, output, indent=2)
    output.write('\n')
last = samples[-1]
print(f"owns={last['owns']} handed_off={last['handed_off']} caution=0x{last['caution']:x} frames/s={last['second_frames']} "
      f"render_avg_us={last['second_render_avg_us']} render_max_us={last['second_render_max_us']} "
      f"refresh_avg_us={last['second_refresh_avg_us']} stock_flips/s={last['second_stock_flips']} guards=0x{last['guards']:x}")
