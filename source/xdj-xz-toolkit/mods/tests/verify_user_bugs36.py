"""Host transport regressions for reports 3/4/6; supports an exported beta tree.

--probe-missing-order also reproduces the unresolved CLASSIC list-context defect.
All generated media and logs stay under --out. No device or firmware access.
"""
import argparse
import ast
import importlib.util
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mods', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--zig', required=True)
parser.add_argument('--probe-missing-order', action='store_true')
args = parser.parse_args()
root, out = args.mods.resolve(), args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
data = out / 'data'
data.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location('fixtures', root / 'extra_player_fixtures.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
fixtures.fixtures(data)
tree = ast.parse((root / 'verify_extra_player.py').read_text())
sources = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'SOURCES' for t in n.targets))
binary = out / 'player.exe'
command = [args.zig, 'cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
           '-DXZ_XP_TEST_RENDER_HOOK', '-I' + str(root),
           str(Path(__file__).with_name('test_user_bugs36_player.c')),
           *[str(root / s) for s in sources], '-lm', '-o', str(binary)]
result = subprocess.run(command, capture_output=True)
(out / 'build.log').write_bytes(result.stdout + result.stderr)
result.check_returncode()
cases = ['load', 'tracks', 'track-auto-cue', 'jump', 'jump-no-bpm']
if args.probe_missing_order:
    cases.append('classic-tracks')
receipt = []
for case in cases:
    command = [str(binary), str(data), case]
    result = subprocess.run(command, capture_output=True, timeout=60)
    (out / (case + '.log')).write_bytes(result.stdout + result.stderr)
    receipt.append({'case': case, 'command': command, 'exit': result.returncode})
    print(case, result.returncode, (result.stdout + result.stderr).decode(errors='replace')[-500:], flush=True)
(out / 'receipt.json').write_text(json.dumps(receipt, indent=2))
raise SystemExit(any(row['exit'] for row in receipt))
