"""Linux-only fake-tree connector acceptance. No transport/device/helper actions."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tsan', action='store_true')
parser.add_argument('--tsan-no-aslr', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='xz-route-events-') as tmp:
    binary = Path(tmp) / 'route-events'
    subprocess.run(['gcc', '-O1', '-g', '-UNDEBUG', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-pthread', '-DXZ_XP_TEST_RENDER_HOOK',
                    '-fsanitize=' + ('thread' if args.tsan else 'address,undefined'),
                    '-fno-sanitize-recover=all', '-fno-pie', '-no-pie',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    str(ROOT / 'tests/test_route_events.c'),
                    '-Wl,--wrap=inotify_init,--wrap=inotify_add_watch', '-lm', '-o', str(binary)], check=True)
    run = (['setarch', 'x86_64', '-R'] if args.tsan_no_aslr else []) + [str(binary)]
    for attempt in range(3 if args.tsan else 1):
        result = subprocess.run(run, cwd=tmp, capture_output=True, text=True, timeout=40)
        print(result.stdout, end='')
        print(result.stderr, end='')
        if result.returncode == 0 or 'ThreadSanitizer: unexpected memory mapping' not in result.stderr:
            break
        print(f'TSan startup mapping collision {attempt + 1}/3')
    result.check_returncode()
