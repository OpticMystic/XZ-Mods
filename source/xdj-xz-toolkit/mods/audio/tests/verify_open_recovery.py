"""Exercise typed preparation failures, bounded recovery, and cancellation."""
from pathlib import Path
import subprocess,tempfile

here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-open-recovery-') as directory:
    for name in ['test_stream_open_failure','test_stream_open_retry']:
        binary=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
                        '-Wno-misleading-indentation','-Wno-unused-function',
                        '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                        str(here/(name+'.c')),'-pthread','-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=30)
