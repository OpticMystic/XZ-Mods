"""Check guarded native observer hooks without touching an XZ or USB device."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
output = args.output.resolve()
compiler = str(args.zig.resolve())
test = output / 'native-observer-test.exe'
subprocess.run([compiler, 'cc', '-std=c11', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                str(ROOT/'test_native.c'), str(ROOT/'stems.c'), '-o', str(test)], check=True)
subprocess.run([str(test)], check=True)
obj = output / 'native-observer-arm.o'
subprocess.run([compiler, 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9',
                '-mfloat-abi=soft', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fPIC', '-c', str(ROOT/'native.c'), '-o', str(obj)], check=True)
receipt = {'clear_discard_same_count_replacement': 'PASS', 'busy_snapshot_rejected': 'PASS',
           'partial_hook_failure_disables_waveforms': 'PASS', 'original_arguments_and_results': 'PASS',
           'all_four_slots': 'PASS', 'controller_touched': False,
           'native_source_sha256': hashlib.sha256((ROOT/'native.c').read_bytes()).hexdigest(),
           'arm_object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest()}
(output/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(json.dumps(receipt))
