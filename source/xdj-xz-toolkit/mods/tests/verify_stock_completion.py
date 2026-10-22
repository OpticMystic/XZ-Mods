"""Replay independent stock-behavior regressions against this checkout's C code."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, subprocess, wave

M = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--arch', choices=['host', 'arm'], default='host')
ap.add_argument('--sanitizer', choices=['address', 'thread'], default='address')
ap.add_argument('--suite', choices=['sync', 'controls', 'extensions', 'tempo_grid', 'global_sync', 'authority', 'tempo_display'], required=True)
ap.add_argument('--modes')
ap.add_argument('--source', type=Path, help='Frozen mods directory to compare using this regression fixture')
a = ap.parse_args()
test_root = M / 'tests'
if a.source:
    M = a.source.resolve()
O = a.output.resolve()
O.mkdir(parents=True, exist_ok=False)

def linux(p):
    s = p.resolve().as_posix()
    return '/mnt/' + s[0].lower() + s[2:]

spec = importlib.util.spec_from_file_location('fixtures', M / 'extra_player_fixtures.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
data = O / 'data'
data.mkdir()
fixtures.fixtures(data)
with wave.open(str(data / '44100.wav'), 'rb') as src:
    params, pcm = src.getparams(), src.readframes(2205)
with wave.open(str(data / 'short44100.wav'), 'wb') as dst:
    dst.setparams(params)
    dst.writeframes(pcm)
with wave.open(str(data / '44100.wav'), 'rb') as src:
    long_params, long_pcm = src.getparams(), src.readframes(src.getnframes())
with wave.open(str(data / 'long44100.wav'), 'wb') as dst:
    dst.setparams(long_params)
    dst.writeframes(long_pcm * 3)
runtime = (M / 'runtime.c').read_text()
begin = runtime.index('static int deck_outcome(')
end = runtime.index('\nstatic int deck_route(', begin)
(O / 'runtime-outcome.inc').write_text(runtime[begin:end] + '\n')
names = ['extra_player.c', 'deck_select.c', 'anlz_extra.c', 'shell_analysis.c',
         'ui/layered_wave_source.c', 'ui/layered_wave.c', 'audio/stem_media.c',
         'audio/stem_mp3.c', 'audio/stem_decode.c', 'shell_runtime_core.c']
sources = [M / n for n in names] + [test_root / ('test_stock_' + a.suite + '_review.c')]
flags = ['-std=c11', '-g', '-UNDEBUG', '-pthread', '-Wall', '-Wextra', '-Werror',
         '-Wno-unused-function', '-Wno-misleading-indentation', '-DXZ_XP_TEST_RENDER_HOOK',
         '-I' + linux(M), '-I' + linux(O), '-ffunction-sections', '-fdata-sections']
if a.suite == 'controls':
    flags += ['-DXZ_XP_SERVICE_PROOF', '-DXZ_XP_JOG_TICKS_PER_REV=6600']
if a.arch == 'host':
    cc = 'gcc'
    flags += ['-O1', '-fsanitize=' + ('thread' if a.sanitizer == 'thread' else 'address,undefined'), '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie']
    runner = ['env', 'TSAN_OPTIONS=halt_on_error=1', 'setarch', 'x86_64', '-R'] if a.sanitizer == 'thread' else ['env', 'ASAN_OPTIONS=detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS=halt_on_error=1']
else:
    cc = 'arm-linux-gnueabi-gcc'
    flags += ['-O2', '-mcpu=cortex-a9', '-mfpu=vfpv3', '-mfloat-abi=softfp']
    runner = ['qemu-arm', '-cpu', 'cortex-a9', '-L', '/usr/arm-linux-gnueabi']
receipt = {'arch': a.arch, 'sanitizer': a.sanitizer if a.arch == 'host' else None, 'suite': a.suite, 'hardware_verified': False, 'inputs': {}, 'runs': []}
for p in sources + list(M.glob('*.h')) + list(M.glob('test_extra_player*.h')):
    receipt['inputs'][str(p)] = hashlib.sha256(p.read_bytes()).hexdigest()

def run(name, command):
    r = subprocess.run(['wsl', '-d', 'Ubuntu', '--cd', linux(O), '--exec', *command], capture_output=True, timeout=300)
    (O / (name + '.stdout')).write_bytes(r.stdout)
    (O / (name + '.stderr')).write_bytes(r.stderr)
    receipt['runs'].append({'name': name, 'command': command, 'exit': r.returncode})
    (O / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(name, 'exit', r.returncode, r.stdout.decode(errors='replace')[-1200:], r.stderr.decode(errors='replace')[-1200:], flush=True)
    return r.returncode

binary = O / 'regression'
result = run('compile', [cc, *flags, *map(linux, sources), '-lm', '-Wl,--gc-sections', '-o', linux(binary)])
if result:
    raise SystemExit(result)
modes = a.modes or ('identity,incoherent,debounce-zero,debounce-wrap,debounce-rollback,debounce-identity,negative,tempo-only,failed-master,retained' if a.suite == 'sync' else 'all')
for mode in modes.split(','):
    result |= bool(run(mode, [*runner, linux(binary), linux(data), mode]))
raise SystemExit(int(result))
