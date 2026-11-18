"""Run every host verify suite once: one line per suite, a total, a JSON summary.

    python run_all_host.py OUTDIR [--quick] [--only SUITE ...] [--zig ZIG] [--firmware RBP]

The suites are mods/ui/verify_*.py, mods/ram/verify.py, mods/shell/verify.py, mods/tests/verify_*.py and
mods/audio/tests/verify_*.py.
Each line reads PASS, FAIL (with the first error), or SKIP (with what the suite needs and does not have), and the
seconds it took. A suite says it is skipped by exiting with SKIP_EXIT (77) after printing a line that starts with
"SKIPPED:". OUTDIR receives summary.json, one log per suite (logs/) and every suite's own output folder (work/).
The exit code is 1 when any suite fails or times out, and 0 otherwise; skips do not fail the run.

On Windows, suites that drive zig or WSL themselves run under this Python; the rest are Linux host suites (gcc,
qemu-arm, sanitizers) and run inside the WSL distribution with python3. On Linux every suite runs directly.

--quick leaves out the long passes and says so on the suite's line: the ARM passes under qemu (the SYNC/MASTER
simulation's ARM pass alone runs for more than half an hour) run only without --quick.
"""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

SKIP_EXIT = 77
MODS = Path(__file__).resolve().parents[1]
GROUPS = ('ui/verify_*.py', 'ram/verify.py', 'shell/verify.py', 'tests/verify_*.py', 'audio/tests/verify_*.py')

# Runs of one suite: each entry is the argument list of one invocation. A suite not listed runs once without
# extra arguments. Output folders, --zig, --arch where required and --firmware are added by the runner.
VARIANTS = {
    'ram/verify.py': [[], ['--auto'], ['--stop-key'], ['--presentation'], ['--display-assets'], ['--scalars'], ['--attach-close']],
    'ui/verify_runtime_controls.py': [[], ['--ram-only']],
    'ui/verify_owner_refresh.py': [[], ['--ram-only']],
    'tests/verify_stock_completion.py': [['--suite', s] for s in (
        'sync', 'controls', 'extensions', 'tempo_grid', 'global_sync', 'authority', 'tempo_display',
        'pickup_route', 'rate_hold', 'rate_hold_extended')],
    'audio/tests/verify_overcue.py': [['--cc', 'gcc']],
    'audio/tests/verify_overcue_sample_rates.py': [['--cc', 'gcc']],
}
# Suites that must be told --arch: both, the ARM one under qemu. --quick keeps the host run only.
for _suite in ('tests/verify_completed_output.py', 'tests/verify_completed_phase.py', 'tests/verify_jog_calibration.py',
               'tests/verify_wave_cadence.py'):
    VARIANTS[_suite] = [['--arch', 'host'], ['--arch', 'arm']]
ARM_VARIANT = ['--arch', 'arm']
# Suites whose default includes an ARM pass under qemu that runs for many minutes: --quick passes these
# arguments instead and says so. (The short ARM passes, such as verify_sync_engine.py's and
# verify_global_master.py's at well under two minutes, always run.)
QUICK = {
    'tests/verify_sync_master_sim.py': ['--arch', 'host'],
}
TIMEOUT = {'tests/verify_sync_master_sim.py': 4 * 3600}
DEFAULT_TIMEOUT = 1800
FIRST_ERROR = re.compile(r'error:|Assertion .*failed|Assertion failed|CHECK failed|undefined reference|timed out after')
FAIL_LINE = re.compile(r'\bFAIL\b|^RED\b')  # after the above: suites also print expected RED lines for their mutants
PYTHON_EXCEPTION = re.compile(r'^[\w.]*(Error|Exception|Expired|Exit)\b')


def add_argument_flags(text):
    """The script's options: {flag: required}."""
    found = {}
    for call in re.finditer(r"add_argument\(\s*['\"](--[\w-]+)['\"]([^)]*)\)", text):
        found[call.group(1)] = bool(re.search(r'required\s*=\s*True', call.group(2)))
    return found


def windows_side(rel, text, flags):
    """True when the suite drives zig or WSL itself, so it runs under Windows Python on a Windows host."""
    return flags.get('--zig') is True or bool(re.search(r"['\"]wsl['\"]", text)) or rel == 'ram/verify.py'


def wsl_path(path):
    value = Path(path).resolve().as_posix()
    return '/mnt/' + value[0].lower() + value[2:] if re.match(r'^[A-Za-z]:/', value) else value


def find_zig(given):
    if given:
        return given
    if shutil.which('zig'):
        return shutil.which('zig')
    try:
        import ziglang
        candidate = Path(ziglang.__file__).parent / ('zig.exe' if os.name == 'nt' else 'zig')
        return str(candidate) if candidate.exists() else None
    except ImportError:
        return None


def suites():
    found = []
    for pattern in GROUPS:
        found += sorted(MODS.glob(pattern))
    return [p.relative_to(MODS).as_posix() for p in found]


def first_error(output):
    """The most telling line: a compiler/link error or a failed C assertion; else a FAIL/RED line; else the Python
    exception that ended the script; else the last line."""
    raw = [l for l in output.splitlines() if l.strip() and 'Failed to translate' not in l and not l.startswith('###')]
    lines = [l.strip() for l in raw]
    hits = [l for l in lines if FIRST_ERROR.search(l)]
    # A suite's own verdict starts its line; indented FAIL/RED lines are details (expected NOTE stories, mutants).
    hits = hits or [l.strip() for l in raw if FAIL_LINE.match(l)] or [l for l in lines if FAIL_LINE.search(l)]
    hits = hits or [l for l in lines if PYTHON_EXCEPTION.search(l)]
    if hits:
        return hits[0][:240]
    return lines[-1][:240] if lines else ''


def skip_reason(output):
    for line in output.splitlines():
        if line.startswith('SKIPPED:'):
            return line[len('SKIPPED:'):].strip()
    return 'the suite exited 77 without a SKIPPED: line'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('outdir', type=Path, help='new or empty folder for summary.json, logs and suite outputs')
    parser.add_argument('--quick', action='store_true', help='leave out the long ARM passes under qemu')
    parser.add_argument('--only', action='append', default=[], help='run only this suite (path under mods/)')
    parser.add_argument('--zig', help='zig executable (default: zig on PATH, else the ziglang Python package)')
    parser.add_argument('--firmware', help='stock rbp for ram/verify.py (default: $XZ_STOCK_RBP)')
    parser.add_argument('--wsl-distro', default='Ubuntu')
    parser.add_argument('--wsl-tmp', default='/root/xz-run-all-host',
                        help='TMPDIR for Linux suites inside WSL (WSL /tmp can vanish under long runs)')
    parser.add_argument('--suite-args', action='append', default=[], metavar='SUITE=ARGS',
                        help='extra arguments for one suite, e.g. ui/verify_key_strip.py="--frame f.png"')
    parser.add_argument('--env', action='append', default=[], metavar='NAME=VALUE',
                        help='environment for every suite, e.g. XZ_MODS=<main repository> for the DJ profile suites '
                             '(inside WSL give Linux paths)')
    args = parser.parse_args()
    if not args.wsl_tmp.startswith('/') or ':' in args.wsl_tmp:
        # Git Bash rewrites /root/... into C:/Program Files/Git/root/...; WSL would then take it as a relative path.
        parser.error(f'--wsl-tmp must be an absolute Linux path, got {args.wsl_tmp!r} '
                     '(from Git Bash, set MSYS_NO_PATHCONV=1)')

    out = args.outdir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'logs').mkdir(exist_ok=True)
    (out / 'work').mkdir(exist_ok=True)
    zig = find_zig(args.zig)
    firmware = args.firmware or os.environ.get('XZ_STOCK_RBP')
    extra = {}
    for item in args.suite_args:
        name, _, value = item.partition('=')
        extra[name] = shlex.split(value)
    selected = suites()
    unknown = [s for s in args.only if s not in selected]
    if unknown:
        parser.error('unknown suite: ' + ', '.join(unknown))
    if args.only:
        selected = [s for s in selected if s in args.only]
    on_windows = os.name == 'nt'
    run_id = f'{os.getpid()}-{int(time.time())}'
    wsl_tmp = f'{args.wsl_tmp.rstrip("/")}/{run_id}'
    extra_env = dict(item.partition('=')[::2] for item in args.env)
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1', **extra_env}
    try:
        head = subprocess.run(['git', '-C', str(MODS), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    except OSError:
        head = ''

    results = []
    width = max(len(s) for s in selected) if selected else 0
    began = time.monotonic()
    for rel in selected:
        script = MODS / rel
        text = script.read_text(encoding='utf-8', errors='replace')
        flags = add_argument_flags(text)
        windows = on_windows and windows_side(rel, text, flags)
        in_wsl = on_windows and not windows
        path_of = wsl_path if in_wsl else (lambda p: str(p))
        variants = VARIANTS.get(rel, [[]])
        note = ''
        if args.quick and rel in QUICK:
            variants = [v + QUICK[rel] for v in variants]
            note = 'ARM pass left out (--quick)'
        if args.quick and ARM_VARIANT in variants:
            variants = [v for v in variants if v != ARM_VARIANT]
            note = 'ARM run left out (--quick)'
        name = rel.replace('/', '_')[:-3]
        log = out / 'logs' / f'{name}.log'
        log.write_text('', encoding='utf-8')
        status, seconds, detail = 'PASS', 0.0, ''
        for index, variant in enumerate(variants):
            work = out / 'work' / f'{name}-{index}'
            argv = []
            if flags.get('--zig') and zig:
                argv += ['--zig', path_of(zig)]
            for flag in ('--output', '--out'):
                if flags.get(flag):
                    argv += [flag, path_of(work)]
            if flags.get('--arch') and '--arch' not in variant:
                argv += ['--arch', 'host']
            if rel == 'ram/verify.py' and firmware:
                argv += ['--firmware', path_of(firmware)]
            argv += variant + extra.get(rel, [])
            if in_wsl:
                inner = (f'mkdir -p {shlex.quote(wsl_tmp)} && cd {shlex.quote(wsl_path(MODS))} && '
                         f'TMPDIR={shlex.quote(wsl_tmp)} PYTHONDONTWRITEBYTECODE=1 '
                         + ''.join(f'{key}={shlex.quote(value)} ' for key, value in extra_env.items()) + 'python3 '
                         f'{shlex.quote(wsl_path(script))} ' + ' '.join(shlex.quote(a) for a in argv))
                # --exec: no login shell re-reads the line, so quoting and $ reach bash as written.
                command = ['wsl', '-d', args.wsl_distro, '-u', 'root', '--exec', 'bash', '-c', inner]
            else:
                command = [sys.executable, str(script), *argv]
            start = time.monotonic()
            try:
                done = subprocess.run(command, cwd=str(MODS), env=env, capture_output=True, text=True,
                                      errors='replace', timeout=TIMEOUT.get(rel, DEFAULT_TIMEOUT))
                code, output = done.returncode, done.stdout + done.stderr
            except subprocess.TimeoutExpired as expired:
                code = 'timeout'
                output = ''.join(x if isinstance(x, str) else (x or b'').decode(errors='replace')
                                 for x in (expired.stdout, expired.stderr)) + f'\ntimed out after {expired.timeout} s'
            seconds += time.monotonic() - start
            with log.open('a', encoding='utf-8') as stream:
                stream.write(f'### {command}\n{output}\n### exit {code}\n')
            label = ' '.join(variant) or 'default'
            if code == SKIP_EXIT:
                if status == 'PASS':
                    status, detail = 'SKIP', skip_reason(output)
                elif status == 'SKIP':
                    detail += '; ' + skip_reason(output)
            elif code != 0 and status != 'FAIL':
                status, detail = 'FAIL', f'{label}: exit {code}: {first_error(output)}'
        if note:
            detail = f'{detail}; {note}' if detail else note
        results.append({'suite': rel, 'status': status, 'seconds': round(seconds, 1), 'detail': detail,
                        'runs': len(variants), 'log': str(log)})
        print(f'{status:<4} {seconds:7.1f}s  {rel:<{width}}  {detail}'.rstrip(), flush=True)

    total = time.monotonic() - began
    counts = {k: sum(r['status'] == k for r in results) for k in ('PASS', 'FAIL', 'SKIP')}
    print(f'TOTAL {len(results)} suites: {counts["PASS"]} pass, {counts["FAIL"]} fail, {counts["SKIP"]} skipped, '
          f'{total:.0f} s' + (' (--quick: ARM passes left out)' if args.quick else ''), flush=True)
    summary = {'head': head, 'quick': args.quick, 'seconds': round(total, 1), 'counts': counts, 'suites': results}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    return 1 if counts['FAIL'] else 0


if __name__ == '__main__':
    sys.exit(main())
