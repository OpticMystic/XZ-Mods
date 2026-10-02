"""Run native game acceptance in isolated XZ RAM; leave the DJ app running."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import uuid

root = Path(__file__).resolve().parent
sys.path.insert(0, str(root.parent))
sys.path.insert(0, str(root.parent.parent / 'vendor'))
from device_smoke import command
from live_io import HOST
from doom.device_verify import transfer

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', required=True, type=Path)
parser.add_argument('--evidence', required=True, type=Path)
parser.add_argument('--helper', required=True, help='Existing bounded RAM transfer helper')
args = parser.parse_args()
if not re.fullmatch(r'/dev/shm/xz-(?:doom|arcade)-[0-9a-f]{12}/transfer', args.helper):
    raise ValueError('Expected an existing XZ RAM transfer helper')
before = command(HOST, 'pidof rbp').strip()
if not re.fullmatch('[0-9]+', before): raise RuntimeError('Expected one running DJ app')
files = ['arcade-test-arm', 'arcade-runtime-test-arm', 'native-clock-test-arm']
digest = hashlib.sha256((args.build / files[0]).read_bytes()).hexdigest()[:12]
remote = '/dev/shm/xz-arcade-test-' + digest + '-' + uuid.uuid4().hex[:8]
response = command(HOST, 'mkdir ' + remote + '; echo CREATED=$?')
if 'CREATED=0' not in response: raise RuntimeError('RAM directory already exists; choose a new build')
receipt = {'pid_before': before, 'remote': remote, 'tests': {}, 'flash_written': False, 'usb_written': False}
for name in files:
    data = (args.build / name).read_bytes()
    transfer(HOST, '169.254.168.58', args.helper, 'receive', remote + '/' + name, len(data), data)
    output = command(HOST, 'chmod 700 ' + remote + '/' + name + '; ' + remote + '/' + name + '; echo TEST_STATUS=$?', timeout=45)
    receipt['tests'][name] = output
    if 'TEST_STATUS=0' not in output:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(json.dumps(receipt, indent=2) + '\n')
        raise RuntimeError(output)
receipt['pid_after'] = command(HOST, 'pidof rbp').strip()
if receipt['pid_after'] != before: raise RuntimeError('DJ app changed during isolated tests')
args.evidence.parent.mkdir(parents=True, exist_ok=True)
args.evidence.write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
