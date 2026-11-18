#!/usr/bin/env python3
"""Check bounded parsing/map construction work for the interface's stem catalog."""
import argparse
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'audio/tests'))
from verify_index_parser import instrument_jsmn


def main():
    p=argparse.ArgumentParser(description=__doc__)
    compilers=p.add_mutually_exclusive_group()
    compilers.add_argument('--cc',default='cc')
    compilers.add_argument('--zig')
    args=p.parse_args()
    compiler=[str(Path(args.zig).resolve()),'cc'] if args.zig else shlex.split(args.cc)
    with tempfile.TemporaryDirectory(prefix='xz-shell-index-cost-') as temporary:
        root=Path(temporary)
        for name in ('shell_analysis.c','tests/test_shell_index_cost.c','audio/vendor/jsmn/jsmn.h'):
            target=root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,target)
        instrument_jsmn(root/'audio/vendor/jsmn/jsmn.h')
        source=root/'shell_analysis.c'
        text=source.read_text()
        # Count accesses to the map's entry array, including the old insertion
        # search. Instrumentation stays in the temporary source copy.
        text,changes=re.subn(r's->e\[([^\]]+)\]',r's->e[map_index(\1)]',text)
        if not changes:raise RuntimeError('Map changed; review entry-access instrumentation')
        source.write_text(text)
        binary=root/'cost.exe'
        subprocess.run(compiler+['-O2','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
            '-DXZ_INDEX_COST_INSTRUMENTED','-I',str(ROOT),str(root/'tests/test_shell_index_cost.c'),
            str(ROOT/'ui/layered_wave_source.c'),str(ROOT/'ui/layered_wave.c'),'-lm','-o',str(binary)],check=True)
        count=4096
        tracks={str(i):{'file_path':f'/Contents/Track {i:04d}.mp3','bundle':f'{i:016x}',
                       'source_sha256':'a'*64,'frames':12345678} for i in range(1,count+1)}
        usb=root/'usb';(usb/'CDJMODS').mkdir(parents=True)
        (usb/'CDJMODS/index.json').write_text(json.dumps({'schema':'overcue-index/1','tracks':tracks}))
        run=subprocess.run([str(binary),str(usb),str(count)],capture_output=True,text=True,timeout=15)
        print(run.stdout,end='')
        if run.returncode:raise SystemExit('FAIL: interface catalog build exceeded the token/map work budget')


if __name__=='__main__':main()
