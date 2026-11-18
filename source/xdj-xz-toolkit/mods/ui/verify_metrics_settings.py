"""Compile the actual settings default/parse/format path without host filesystem I/O."""
import argparse
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
text=(root/'settings.c').read_text();start=text.index('int xz_settings_look(');end=text.index('static int paths(')
source='''#include "settings.h"
#include "ui/themes.h"
#include "ui/wave_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
'''+text[start:end]+'''
/* Since 19e22067 ("Remove redundant feature gates and keep additive controls available") metrics is no longer a
   preference: the top-bar readings fill added space and are always available. metrics_enabled keeps its version-1
   wire slot (field 37) so old records still parse, but it defaults to 1 and every parsed or overlaid record reads 1,
   whatever the record stored. */
int main(void){
 struct xz_settings a,b,c;char text[2048];unsigned present=0;xz_settings_default(&a);assert(a.metrics_enabled==1);
 for(int on=0;on<2;on++){a.metrics_enabled=on;assert(xz_settings_format(&a,text,sizeof(text))>0);assert(strstr(text,on?"metrics_enabled=1\\n":"metrics_enabled=0\\n"));
  assert(!xz_settings_parse(text,&b));assert(b.metrics_enabled==1);a.metrics_enabled=1;assert(!memcmp(&a,&b,sizeof(a)));}
 a.metrics_enabled=0;assert(xz_settings_format(&a,text,sizeof(text))>0);assert(!xz_settings_parse_present(text,&b,&present)&&present>37);
 xz_settings_default(&c);c.metrics_enabled=0;b.metrics_enabled=0;assert(!xz_settings_overlay(&c,&b,present)&&c.metrics_enabled==1);
 char *field=strstr(text,"metrics_enabled=");assert(field);*field=0;
 assert(!xz_settings_parse(text,&b)&&b.metrics_enabled==1);
 a.metrics_enabled=2;assert(xz_settings_format(&a,text,sizeof(text))<0);
 assert(!xz_settings_parse("XZ_MODS_SETTINGS 1\\nstems=0\\ngate=0\\nsmart=0\\ntheme=0\\nstem_page=0\\nshift_pages=0\\npad_feedback=1\\nshift_keysync=0\\n",&b)&&b.metrics_enabled==1);
 puts("PASS metrics always on: default ON, a stored 0 or 1 parses and overlays as ON, field still written in its slot, records cut before it and old 8-field records read ON, invalid value 2 refused");return 0;
}
'''
(out/'settings-test.c').write_text(source)
binary=out/'settings-test.exe'
subprocess.run([str(a.zig.resolve()),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(root),str(out/'settings-test.c'),str(root/'ui/beat_jump.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
schema=json.loads((root/'settings-schema.json').read_text())
# 19e22067 moved metrics_enabled from the writable fields to retired_fields (read-only, default 1); its wire slot stays.
assert 'metrics_enabled' not in [f['key'] for f in schema['fields']]
retired=schema['retired_fields']['metrics_enabled'];assert (retired['default'],retired['readonly'])==(1,True)
wire=schema['wire_order'];assert wire.index('metrics_enabled')==36 and wire[37]=='stem_order'
assert 38 in schema['accepted_field_counts'] and len(wire) in schema['accepted_field_counts']
stem=[f for f in schema['fields'] if f['key']=='stem_order'][0];assert (stem['default'],stem['max'])==(0,5)
print('PASS schema: metrics_enabled retired (read-only, default 1) in wire slot 37, stem_order after it, legacy 38-field and current',len(wire),'field records accepted')
