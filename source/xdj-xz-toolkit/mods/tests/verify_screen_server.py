"""Read-only screen endpoint bounds on a Linux host; never opens a framebuffer."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-screen-test-') as folder:
    for name in ('test_screen_server', 'test_screen_server_wire'):
        binary = Path(folder) / name
        subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-rdynamic',
                        str(root / (name + '.c')), '-pthread', '-ldl', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
