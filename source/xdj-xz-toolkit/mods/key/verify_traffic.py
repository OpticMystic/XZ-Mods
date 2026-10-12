from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-native-traffic-') as tmp:
    target=Path(tmp)/'traffic'
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(ROOT/'traffic.c'),str(ROOT/'test_traffic.c'),str(ROOT/'music.c'),'-lm','-o',str(target)],check=True)
    subprocess.run([str(target)],check=True)
