"""Compare the packaged settings editor schema with its exact native C parser."""
from pathlib import Path
import argparse,json,subprocess,tempfile,sys
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--resources',type=Path,required=True);p.add_argument('--toolkit',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str(a.toolkit.resolve()));from builder import usb_settings
schema=usb_settings.contract(a.resources);native=a.resources.resolve()/'source/xdj-xz-toolkit/mods'
with tempfile.TemporaryDirectory(prefix='xz-settings-contract-') as folder:
 root=Path(folder);code=root/'contract.c';exe=root/'contract'
 code.write_text('''#include "settings.h"
#include "ui/themes.h"
#include <stdio.h>
int main(int argc,char **argv){struct xz_settings s;char text[512];
if(argc==1){xz_settings_default(&s);int n=xz_settings_format(&s,text,sizeof(text));if(n<0)return 3;fputs(text,stdout);return 0;}
FILE *f=fopen(argv[1],"rb");if(!f)return 2;size_t n=fread(text,1,511,f);fclose(f);text[n]=0;return xz_settings_parse(text,&s)?1:0;}
''')
 subprocess.run(['gcc','-std=c11','-O1','-I',str(native),str(code),str(native/'settings.c'),'-o',str(exe)],check=True)
 expected=usb_settings.serialize(usb_settings.defaults(schema),schema)
 assert subprocess.check_output([str(exe)])==expected,'Native defaults differ from editor schema'
 cases=[expected]
 lines=expected.splitlines(keepends=True)
 cases += [b''.join(lines[:n+1]) for n in range(8,24)]
 for field in schema['fields']:
  for value in [field['min'],field['max']]:
   values=usb_settings.defaults(schema);values[field['key']]=value;cases.append(usb_settings.serialize(values,schema))
  cases.append(expected.replace((field['key']+'='+str(field['default'])+'\n').encode(),(field['key']+'='+str(field['max']+1)+'\n').encode()))
 cases += [expected+b'future=1\n',expected.replace(b'\n',b'\r\n'),expected.replace(b'SETTINGS 1',b'SETTINGS 2')]
 for i,data in enumerate(cases):
  file=root/f'{i}.cfg';file.write_bytes(data)
  native_ok=subprocess.run([str(exe),str(file)]).returncode==0
  try:usb_settings.parse(data,schema);python_ok=True
  except ValueError:python_ok=False
  assert native_ok==python_ok,f'Native/editor disagreement in case {i}'
 print(f'PASS {len(cases)} native/editor schema cases and exact defaults')
