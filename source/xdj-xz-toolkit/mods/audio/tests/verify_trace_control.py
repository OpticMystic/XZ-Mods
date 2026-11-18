"""Test dormant capture and explicit RAM freeze/export on Linux."""
from pathlib import Path
import subprocess,tempfile

here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-trace-control-') as directory:
    binary=Path(directory)/'trace-control'
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
                    '-Wno-misleading-indentation','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',str(here/'test_trace_control.c'),
                    '-pthread','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)
