"""Linux host route-status acceptance. No device access; all files/transport are fixtures."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tsan', action='store_true')
parser.add_argument('--tsan-no-aslr', action='store_true')
args = parser.parse_args()
bridge = (ROOT.parent / 'vendor/tools/xz_quad/bridge.c').read_text()
codes = sorted(set(re.findall(r'"((?:WAIT_|UNSUPPORTED_|RACE_|FATAL_|TERMINATED_|STOPPED|STOP_PENDING|CLEANUP_|BIND_FAILED|LOOPBACK_MOVED|ROUTE_DISCONNECTED|READY_CH3_CH4|MUSIC_READY|MUSIC_HUB_)[^"\n]*)"', bridge)))
codes = [code for code in codes if not code.endswith('_') and code != 'STOP_PENDING']
parts = {
    'WAIT_NATIVE_PAUSED': 'SELECTOR', 'WAIT_AUDIO_DEVICE': 'SOCKET',
    'WAIT_MUSIC_HUB_PAUSED': 'SELECTOR', 'MUSIC_HUB_BIND_FAILED': 'PORT',
    'TERMINATED_MUSIC_HUB_IDENTITY': 'PORT',
    'WAIT_LOOPBACK': 'CABLE', 'WAIT_SAFE_LOOPBACK_DISCONNECT': 'CABLE',
    'ROUTE_DISCONNECTED': 'CABLE', 'WAIT_LOOPBACK_SETTLING': 'CABLE', 'LOOPBACK_MOVED': 'CABLE',
    'TERMINATED_STOPPED_MEDIA_ADMISSION': 'PORT', 'TERMINATED_MUSIC_IDENTITY': 'PORT',
    'TERMINATED_ADMISSION_DEBOUNCE_FAILURE': 'CABLE', 'TERMINATED_SCAN_FAILURE': 'CABLE',
    'TERMINATED_BIND_DEBOUNCE_FAILURE': 'CABLE',
}
with tempfile.TemporaryDirectory(prefix='xz-route-status-') as tmp:
    tmp = Path(tmp)
    checks = []
    for code in codes:
        if code in ('WAIT_NATIVE_PAUSED', 'WAIT_MUSIC_HUB_PAUSED'):
            state = 'WAITING_NATIVE_STOP'
        elif code in ('WAIT_LOOPBACK', 'ROUTE_DISCONNECTED'):
            state = 'NOT_DETECTED'
        elif code.startswith(('WAIT_', 'RACE_')) or code in ('READY_CH3_CH4', 'MUSIC_READY', 'LOOPBACK_MOVED'):
            state = 'STARTING'
        else:
            state = 'FAILED'
        part = parts.get(code, 'PORT' if code.startswith(('WAIT_MUSIC_', 'UNSUPPORTED_', 'RACE_MUSIC_')) else
                         'CABLE' if code.startswith(('BIND_FAILED', 'STOP_PENDING_DISCONNECT')) else 'NONE')
        checks += [f'assert(route_reason_map("{code}",0).state==XZ_EXTRA_ROUTE_{state});',
                   f'assert(route_reason_map("{code}",0).fail_part==XZ_EXTRA_ROUTE_PART_{part});']
        # Unknown fallback must not silently cover a newly emitted bridge code.
        assert f'RR("{code}"' in (ROOT / 'extra_route_status.h').read_text(), code
    (tmp / 'route_reason_checks.h').write_text('\n'.join(checks) + '\n')
    binary = tmp / 'route-status'
    command = ['gcc', '-O1', '-g', '-UNDEBUG', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', '-DXZ_XP_TEST_RENDER_HOOK', '-pthread',
               '-fsanitize=' + ('thread' if args.tsan else 'address,undefined'),
               '-fno-sanitize-recover=all', '-fno-pie', '-no-pie',
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-I' + str(ROOT), '-I' + str(tmp),
               str(ROOT / 'tests/test_route_status.c'),
               '-Wl,--wrap=open,--wrap=read,--wrap=fopen,--wrap=stat,--wrap=pthread_mutex_lock',
               '-lm', '-o', str(binary)]
    subprocess.run(command, check=True)
    run = (['setarch', 'x86_64', '-R'] if args.tsan_no_aslr else []) + [str(binary)]
    for attempt in range(3 if args.tsan else 1):
        result = subprocess.run(run, cwd=tmp, capture_output=True, text=True)
        print(result.stdout, end='')
        print(result.stderr, end='')
        if result.returncode == 0 or 'ThreadSanitizer: unexpected memory mapping' not in result.stderr:
            break
        print(f'TSan startup mapping collision {attempt + 1}/3')
    result.check_returncode()
print(f'PASS all {len(codes)} bridge reason strings')
