"""Current playback must not wait behind an unrelated or borrowed loop buffer."""
from pathlib import Path
import subprocess,tempfile

here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-refill-priority-') as directory:
    for name in ['test_refill_priority','test_starve_signal']:
        binary=Path(directory)/name
        subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
                        '-Wno-misleading-indentation','-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all',str(here/(name+'.c')),
                        '-pthread','-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=25)
