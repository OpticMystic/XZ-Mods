"""Compile production parser/jump acceptance; audit independent enable gates."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent))
from builder import usb_settings

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--zig',required=True,type=Path)
 args=parser.parse_args()
 schema=json.loads((ROOT/'settings-schema.json').read_text())
 retired={'jump_shift_page','ram_debug','metrics_enabled','shift_pages','shift_keysync'}
 assert set(schema['retired_fields'])==retired
 assert not retired & {f['key'] for f in schema['fields']}
 # Every remaining saved boolean belongs to one distinct behavior. Adding a
 # second gate requires updating this audited inventory instead of slipping in.
 switches={f['key'] for f in schema['fields'] if f['kind']=='boolean' and not f['readonly']}
 assert switches=={'stems','gate','smart','pad_feedback',
   'fb_takeover','jump_enabled','browse_key_prefix','browse_traffic','key_feedback','rekordbox_menu'}

 values=usb_settings.defaults(schema)
 wire=usb_settings.serialize(values,schema)
 for key in retired:
  old=wire.replace((key+'=1\n').encode(),(key+'=0\n').encode())
  assert usb_settings.parse(old,schema)==values
  assert usb_settings.serialize({**values,key:0},schema)==wire
 for count in schema['accepted_field_counts']:
  old=b''.join(wire.splitlines(keepends=True)[:count+1])
  assert usb_settings.parse(old,schema)==values
 ui=(ROOT/'ui/ui.c').read_text()
 for action in ('JUMP_SHIFT','RAM_DEBUG','METRICS_TOGGLE','SHIFT_PAGES','SHIFT_KEYSYNC'):
  assert 'add(w,&n,XZ_UI_'+action+',' not in ui
 runtime=(ROOT/'runtime.c').read_text()+(ROOT/'ui_runtime.c').read_text()
 for flag in ('STEMS','STEMS_FORCE','GATE_CUE','SMART_CUE','KEYSHIFT','THEME','WAVE_MODE','NATIVE_VIEW'):
  assert '"XZ_MODS_'+flag+'"' not in runtime,flag
 assert 'XZ_MODS_LAYERED_WAVE' not in (ROOT/'ui/layered_wave_runtime.c').read_text()
 assert 'XZ_MODS_INLINE' not in (ROOT/'ui/native_wave_runtime.c').read_text()
 assert 'XZ_KART_CONTROLS' not in (ROOT/'kart/xz_kart_runtime.c').read_text()
 jump=(ROOT/'ui/beat_jump.c').read_text()
 assert 'cfg->shift_page' not in jump
 assert '(!page&&!cfg->enabled)' in jump
 # Windows cannot compile the Linux USB/mount implementation. Compile the exact
 # production definitions up to its filesystem boundary, without rewriting logic.
 source=(ROOT/'settings.c').read_text().split('static int paths(')[0]
 for header in ('<sys/stat.h>','<unistd.h>','<fcntl.h>','<mntent.h>'):
  source=source.replace('#include '+header,'')
 with tempfile.TemporaryDirectory(prefix='xz-toggle-') as folder:
  out=Path(folder);native=out/'settings-parser.c';native.write_text(source)
  exe=out/'toggles.exe'
  subprocess.run([str(args.zig.resolve()),'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
    '-I',str(ROOT),str(native),str(ROOT/'ui/beat_jump.c'),str(ROOT/'ui/stem_pads.c'),str(ROOT/'tests/test_feature_toggles.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('PASS active schema, legacy desktop forms, all historical record lengths and independent gate inventory')

if __name__=='__main__':main()
