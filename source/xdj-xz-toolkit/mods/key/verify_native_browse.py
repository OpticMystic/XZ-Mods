from pathlib import Path
import argparse,subprocess,tempfile
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path);args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='xz-native-sort-') as tmp:
    target=Path(tmp)/('native-sort.exe' if args.zig else 'native-sort')
    compiler=[str(args.zig),'cc'] if args.zig else ['gcc']
    sanitizers=[] if args.zig else ['-fsanitize=address,undefined','-fno-sanitize-recover=all']
    subprocess.run(compiler+['-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
        *sanitizers,str(ROOT/'native_browse.c'),str(ROOT/'test_native_browse.c'),'-o',str(target)],check=True)
    subprocess.run([str(target)],check=True)
