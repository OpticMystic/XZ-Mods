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
int main(void){
 struct xz_settings a,b;char text[1024];xz_settings_default(&a);assert(!a.metrics_enabled);
 for(int on=0;on<2;on++){a.metrics_enabled=on;assert(xz_settings_format(&a,text,sizeof(text))>0);assert(strstr(text,"metrics_enabled="));assert(!xz_settings_parse(text,&b));assert(!memcmp(&a,&b,sizeof(a)));}
 char *field=strstr(text,"metrics_enabled=");assert(field);*field=0;
 assert(!xz_settings_parse(text,&b)&&!b.metrics_enabled);
 a.metrics_enabled=2;assert(xz_settings_format(&a,text,sizeof(text))<0);
 assert(!xz_settings_parse("XZ_MODS_SETTINGS 1\\nstems=0\\ngate=0\\nsmart=0\\ntheme=0\\nstem_page=0\\nshift_pages=0\\npad_feedback=1\\nshift_keysync=0\\n",&b)&&!b.metrics_enabled);
 puts("PASS actual metrics preference ON/OFF roundtrip, defaultOFF, old36field and old8field records, invalid values");return 0;
}
'''
(out/'settings-test.c').write_text(source)
binary=out/'settings-test.exe'
subprocess.run([str(a.zig.resolve()),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(root),str(out/'settings-test.c'),str(root/'ui/beat_jump.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
schema=json.loads((root/'settings-schema.json').read_text());assert len(schema['fields'])==38 and 38 in schema['accepted_field_counts']
assert schema['fields'][-2]['key']=='metrics_enabled' and schema['fields'][-2]['default']==0
assert schema['fields'][-1]['key']=='stem_order' and (schema['fields'][-1]['default'],schema['fields'][-1]['max'])==(0,5)
print('PASS schema field37 and accepted legacy counts')
