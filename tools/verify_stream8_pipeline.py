"""Verify the real C sender -> Windows decoder -> OBS socket, without a controller."""
from pathlib import Path
import argparse
import json
import os
import shlex
import subprocess
import time

APP = Path(__file__).resolve().parents[1]
FLAGS = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}

def linux(path):
    path = path.resolve()
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]

def wait_file(path, process):
    until = time.monotonic() + 40
    while time.monotonic() < until:
        if path.is_file() and path.stat().st_size:
            return
        if process.poll() is not None:
            raise RuntimeError('Fixture exited before creating ' + str(path))
        time.sleep(.05)
    raise TimeoutError(str(path))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--test-exe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    output = a.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    port = output / 'source-port.txt'
    receipt = output / 'session.json'
    source_log = (output / 'source.log').open('w')
    native_log = (output / 'native.log').open('w')
    source = native = None
    try:
        code = APP / 'source/xdj-xz-toolkit/mods/tests/test_screen_server_wire.c'
        binary = '/dev/shm/xz-stream8-wire-' + str(os.getpid())
        command = '\n'.join([
            'set -eu',
            'export TMPDIR=/dev/shm',
            'gcc -O2 -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -rdynamic ' + shlex.quote(linux(code)) + ' -ldl -pthread -o ' + binary,
            'exec ' + binary + ' ' + shlex.quote(linux(port)),
        ])
        source = subprocess.Popen(['wsl.exe', '-d', 'Ubuntu', '-e', 'sh', '-lc', command], stdout=source_log, stderr=subprocess.STDOUT, **FLAGS)
        wait_file(port, source)
        env = os.environ.copy()
        env.update(XZ_SCREEN_FIXTURE_COLOR_BITS='8', XZ_SCREEN_FIXTURE_ADDRESS='127.0.0.1:' + port.read_text().strip(), XZ_SCREEN_FIXTURE_RECEIPT=str(receipt))
        native = subprocess.Popen([str(a.test_exe.resolve()), '--exact', 'screen_transport::tests::browser_fixture', '--ignored', '--nocapture'], env=env, stdout=native_log, stderr=subprocess.STDOUT, **FLAGS)
        wait_file(receipt, native)
        subprocess.run(['node', str(APP / 'tools/verify_stream8_loopback.mjs'), str(receipt)], check=True, **FLAGS)
        receipt.with_suffix('.stop').touch()
        native.wait(timeout=10)
        source.wait(timeout=10)
        assert native.returncode == 0 and source.returncode == 0
        print('PASS: real C RGB332 frames decoded and checked through the OBS socket')
    finally:
        if native and native.poll() is None:
            receipt.with_suffix('.stop').touch()
            try: native.wait(timeout=8)
            except subprocess.TimeoutExpired: native.terminate(); native.wait(timeout=5)
        if source and source.poll() is None:
            source.terminate()
            source.wait(timeout=5)
        source_log.close()
        native_log.close()

if __name__ == '__main__':
    main()
