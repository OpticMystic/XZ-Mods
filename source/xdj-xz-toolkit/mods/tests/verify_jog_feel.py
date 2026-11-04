"""Check DECK 3/4 backspin travel and side-nudge bend through production decode and PCM (issue #5).

Only synthetic files and fake sinks are used. This is not a measurement of the physical platter.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

MODS=Path(__file__).resolve().parents[1]
SOURCES=['extra_player.c','key/keyshift.c','key/music.c','key/control.c','deck_select.c','anlz_extra.c','shell_analysis.c',
         'ui/layered_wave_source.c','ui/layered_wave.c',
         'audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c',
         'tests/test_jog_feel.c']


def linux(path):
 value=path.resolve().as_posix();return '/mnt/'+value[0].lower()+value[2:]


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--out',required=True,type=Path)
 parser.add_argument('--counts',type=int,default=6600,help='platter counts per revolution (build.py hardware estimate)')
 args=parser.parse_args()
 out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
 spec=importlib.util.spec_from_file_location('feel_fixtures',MODS/'extra_player_fixtures.py')
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 fixtures=out/'fixtures';fixtures.mkdir();module.fixtures(fixtures)
 binary=out/'test-jog-feel'
 flags=['-O1','-g','-UNDEBUG','-std=c11','-pthread','-Wall','-Wextra','-Werror',
        '-Wno-misleading-indentation','-DXZ_XP_TEST_RENDER_HOOK',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
        '-DXZ_XP_JOG_TICKS_PER_REV='+str(args.counts),'-I'+linux(MODS)]
 command=['wsl','-d','Ubuntu','--exec','gcc',*flags,*[linux(MODS/name) for name in SOURCES],'-lm','-o',linux(binary)]
 built=subprocess.run(command,capture_output=True,text=True)
 (out/'build.log').write_text(built.stdout+built.stderr)
 if built.returncode:raise RuntimeError('Jog feel test did not compile; see build.log')
 result=subprocess.run(['wsl','-d','Ubuntu','--exec',linux(binary),linux(fixtures)],capture_output=True,text=True)
 (out/'run.log').write_text(result.stdout+result.stderr)
 if result.returncode or 'RESULT jog-feel' not in result.stdout:raise RuntimeError('Jog feel test failed; see run.log')
 summary=result.stdout.splitlines()[-1]
 (out/'receipt.json').write_text(json.dumps({'command':command,'counts_per_rev':args.counts,'exit':result.returncode,
                                             'physical_feel':False,'summary':summary},indent=2)+'\n')
 print(summary)


if __name__=='__main__':main()
