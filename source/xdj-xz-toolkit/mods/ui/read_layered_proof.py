"""Read the running layered-waveform proof counters from rbp memory (read-only)."""
import argparse
import base64
import json
from pathlib import Path
import re
import struct
import sys
from elftools.elf.elffile import ELFFile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from device_smoke import command

NAMES = ['version', 'loads', 'columns_drawn', 'columns_rejected', 'failed_loads', 'mode', 'last_load_ms',
         'frames_timed', 'frame_us_total', 'frame_us_worst']

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('library', type=Path, help='The exact libxz-mods-development.so that is loaded')
p.add_argument('--host', default='169.254.168.59')
a = p.parse_args()
with a.library.open('rb') as f:
    elf = ELFFile(f)
    symbol = elf.get_section_by_name('.dynsym').get_symbol_by_name('xz_layered_wave_proof_v1')[0]
    offset = symbol['st_value']
    segment = next(s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and s['p_vaddr'] <= offset < s['p_vaddr'] + s['p_memsz'])
pid = command(a.host, 'pidof rbp').strip()
maps = command(a.host, f'grep mods.so /proc/{pid}/maps')
base = min(int(line.split('-')[0], 16) - int(line.split()[2], 16) for line in maps.splitlines() if re.match(r'[0-9a-f]+-', line))
address = base + offset
raw = base64.b64decode(''.join(command(a.host, f'dd if=/proc/{pid}/mem bs=1 skip={address} count={4 * len(NAMES)} 2>/dev/null | base64').split()))
values = dict(zip(NAMES, struct.unpack('<%dI' % len(NAMES), raw)))
if values['frames_timed']:
    values['frame_us_mean'] = round(values['frame_us_total'] / values['frames_timed'])
print(json.dumps(values))
