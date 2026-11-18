"""Compare ARM objects of the device handlers against the capture branch baseline."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MODS=ROOT/'source/xdj-xz-toolkit/mods'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 results={}
 with tempfile.TemporaryDirectory(prefix='xz-nav-arm-') as tmp:
  tmp=Path(tmp)
  for name in ['deck_select.c', 'ui/stem_pads.c', 'ui/beat_jump.c', 'ui_runtime.c', 'doom/native_bridge.c', 'ui/deck_view.c', 'ui/key_strip.c', 'ui/ui_profile.c', 'ui/ui_support.c', 'ui/ui_network.c', 'shell/shell.c', 'shell/shell_browse.c', 'shell/shell_info.c', 'cue/cue.c', 'ui/ui.c', 'ui/extra_detail.c', 'ui/extra_lane.c', 'shell/shell_play.c', 'shell/shell_settings.c', 'ui/sdk_ui.c', 'arcade/arcade.c', 'arcade/render.c', 'arcade/wave_rider.c', 'arcade/wave_rider_render.c', 'arcade/wave_rider_source.c', 'extra_player.c', 'ui/classic_column.c', 'ui/classic_mixer.c', 'anlz_extra.c', 'shell_analysis.c', 'key/keyshift.c', 'ui/layered_wave_source.c', 'audio/stem_media.c', 'audio/stem_decode.c']:
   source=MODS/name; relative=source.relative_to(ROOT).as_posix()
   baseline=subprocess.check_output(['git','show','00f16052:'+relative],cwd=ROOT)
   if source.read_bytes()!=baseline:raise SystemExit('device source changed: '+name)
   hashes=[]
   for label in ['before','after']:
    directory=tmp/label; copy=directory/name;copy.parent.mkdir(parents=True,exist_ok=True);copy.write_bytes(baseline if label=='before' else source.read_bytes())
    obj=directory/'handler.o'
    subprocess.run([a.zig,'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-O2','-std=c11','-fPIC','-fvisibility=hidden','-iquote',str(source.parent),'-I',str(MODS),'-ffile-prefix-map='+str(directory)+'='+str(MODS),'-c',str(copy),'-o',str(obj)],check=True)
    hashes.append(hashlib.sha256(obj.read_bytes()).hexdigest())
   if hashes[0]!=hashes[1]:raise SystemExit('ARM object mismatch: '+name)
   results[name]={'before':hashes[0],'after':hashes[1],'byte_identical':True}
 a.out.write_text(json.dumps({'baseline':'00f16052','target':'arm-linux-gnueabi.2.13 cortex_a9','objects':results},indent=2)+'\n')
 print('ARM handler objects byte-identical:',len(results))
if __name__=='__main__':main()
