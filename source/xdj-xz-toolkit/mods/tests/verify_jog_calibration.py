"""Check the private 6600-count travel model through production decode and PCM.

Only synthetic files and fake sinks are used. This is not encoder calibration.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

MODS=Path(__file__).resolve().parents[1]
SOURCES=['extra_player.c','deck_select.c','anlz_extra.c','shell_analysis.c',
         'ui/layered_wave_source.c','ui/layered_wave.c',
         'audio/stem_media.c','audio/stem_mp3.c','audio/stem_decode.c',
         'tests/test_jog_calibration.c']


def linux(path):
 value=path.resolve().as_posix();return '/mnt/'+value[0].lower()+value[2:]


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--out',required=True,type=Path)
 parser.add_argument('--arch',choices=['host','arm'],required=True)
 parser.add_argument('--counts',type=int,default=6600)
 parser.add_argument('--player-object',type=Path,
                     help='Link the exact production ARM player object instead of recompiling the player')
 parser.add_argument('--expect-red',action='store_true')
 args=parser.parse_args()
 if args.player_object and args.arch!='arm':parser.error('--player-object requires --arch arm')
 player_object=args.player_object.resolve() if args.player_object else None
 object_sha256=hashlib.sha256(player_object.read_bytes()).hexdigest() if player_object else None
 out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
 spec=importlib.util.spec_from_file_location('calibration_fixtures',MODS/'extra_player_fixtures.py')
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 fixtures=out/'fixtures';fixtures.mkdir();module.fixtures(fixtures)
 binary=out/'test-jog-calibration'
 flags=['-O1','-g','-UNDEBUG','-std=c11','-pthread','-Wall','-Wextra','-Werror',
        '-Wno-misleading-indentation','-DXZ_XP_TEST_RENDER_HOOK',
        '-DXZ_XP_JOG_TICKS_PER_REV='+str(args.counts),'-I'+linux(MODS)]
 cc='gcc' if args.arch=='host' else 'arm-linux-gnueabi-gcc'
 if args.arch=='host':flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie']
 source_names=[name for name in SOURCES if not(player_object and name=='extra_player.c')]
 command=['wsl','-d','Ubuntu','--exec',cc,*flags,*[linux(MODS/name) for name in source_names],
          *([linux(player_object)] if player_object else []),'-lm','-o',linux(binary)]
 built=subprocess.run(command,capture_output=True,text=True)
 (out/'build.log').write_text(built.stdout+built.stderr)
 if built.returncode:raise RuntimeError('Calibration model did not compile; see build.log')
 run=['wsl','-d','Ubuntu','--exec',*(['qemu-arm','-L','/usr/arm-linux-gnueabi'] if args.arch=='arm' else []),linux(binary),linux(fixtures)]
 result=subprocess.run(run,capture_output=True,text=True)
 (out/'run.log').write_text(result.stdout+result.stderr)
 expected=1 if args.expect_red else 0
 if result.returncode!=expected or 'RESULT configured-jog-model' not in result.stdout:
  raise RuntimeError('Calibration model failed; see run.log')
 if player_object and hashlib.sha256(player_object.read_bytes()).hexdigest()!=object_sha256:
  raise ValueError('Production player object changed during the model test')
 sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
 receipt={'command':command,'run_command':run,'exit':result.returncode,'expected_exit':expected,
          'counts_per_rev':args.counts,'target_source_travel_ms':1800,
          'player_object':str(player_object) if player_object else None,'player_object_sha256':object_sha256,
          'binary_sha256':sha(binary),'source_sha256':{name:sha(MODS/name) for name in SOURCES},
          'header_sha256':sha(MODS/'extra_player.h'),'physical_calibration':False,
          'summary':result.stdout.splitlines()[-1]}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(receipt['summary'])


if __name__=='__main__':main()
