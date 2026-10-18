"""Read the FLAGSHIP shell's frame and input counters (xz_mods_shell_proof_v1) without changing device memory.

Usage: python mods/read_shell_proof.py RECEIPT BINARY OUTPUT.json [--samples N]
RECEIPT is the deploy_development.py receipt, BINARY the deployed libxz-mods-development.so.
Each sample is one consistent copy (same sequence before and after); samples are about a second apart.
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
          'list_errors list_build_ms').split()
SIZE = 4 * len(FIELDS)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('receipt', type=Path)
parser.add_argument('binary', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--host', default='169.254.168.59')
parser.add_argument('--samples', type=int, default=5)
args = parser.parse_args()
if args.output.exists():
    raise ValueError('Choose a new evidence file')
receipt = json.loads(args.receipt.read_text())
if hashlib.sha256(args.binary.read_bytes()).hexdigest() != receipt['runtime_sha256']:
    raise ValueError('Binary differs from deployed receipt')
with args.binary.open('rb') as f:
    symbols = ELFFile(f).get_section_by_name('.dynsym').get_symbol_by_name('xz_mods_shell_proof_v1')
    if not symbols or symbols[0]['st_size'] != SIZE:
        raise ValueError('Unexpected shell proof ABI')
    offset = int(symbols[0]['st_value'])
pid = command(args.host, 'pidof rbp').strip()
if not re.fullmatch('[0-9]+', pid):
    raise ValueError('Expected one application process')
path = receipt['remote'] + '/mods.so'
maps = command(args.host, f'cat /proc/{pid}/maps').splitlines()
mapping = [line.split() for line in maps if line.split()[-1] == path and line.split()[2] == '00000000']
if len(mapping) != 1:
    raise ValueError('Deployed library is not mapped as expected')
address = int(mapping[0][0].split('-')[0], 16) + offset


def read(length):
    start = address // 4096
    count = (address % 4096 + length + 4095) // 4096
    blob = base64.b64decode(''.join(command(args.host, f'dd if=/proc/{pid}/mem bs=4096 skip={start} count={count} 2>/dev/null | base64').splitlines()), validate=True)
    if len(blob) != count * 4096:
        raise ValueError('Incomplete proof memory read')
    return blob[address % 4096:address % 4096 + length]


samples = []
for _ in range(args.samples):
    for attempt in range(5):
        first = read(SIZE)
        second = read(SIZE)
        if first == second:
            break
    else:
        raise ValueError('Counters kept changing during the read')
    values = dict(zip(FIELDS, struct.unpack(f'<{len(FIELDS)}I', first)))
    if values['version'] != 1 or values['size'] != SIZE:
        raise ValueError('Unexpected shell proof header')
    samples.append(values)
    time.sleep(1)
args.output.write_text(json.dumps({'pid': int(pid), 'address': hex(address), 'samples': samples}, indent=2) + '\n')
last = samples[-1]
print(f"owns={last['owns']} handed_off={last['handed_off']} caution=0x{last['caution']:x} frames/s={last['second_frames']} "
      f"render_avg_us={last['second_render_avg_us']} render_max_us={last['second_render_max_us']} "
      f"refresh_avg_us={last['second_refresh_avg_us']} stock_flips/s={last['second_stock_flips']} guards=0x{last['guards']:x}")
