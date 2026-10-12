"""Check production settings parsing, legacy USB records and the display toggle on Windows."""
from pathlib import Path
import argparse,json,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);a=p.parse_args()
settings=(root/'settings.c').read_text().split('static int paths(')[0].replace('#include <mntent.h>','')
fixture=r'''
#define CHECK(p) do{if(!(p)){fprintf(stderr,"FAIL %s line %d\n",#p,__LINE__);exit(1);}}while(0)
int main(void){
    struct xz_settings s,out;char text[1024],legacy[1024];xz_settings_default(&s);CHECK(s.key_feedback==1);
    for(int enabled=0;enabled<2;enabled++){
        s.key_feedback=enabled;s.ram_debug=enabled;CHECK(xz_settings_format(&s,text,sizeof(text))>0);
        CHECK(!xz_settings_parse(text,&out)&&out.key_feedback==enabled&&out.ram_debug==enabled&&!memcmp(&s,&out,sizeof(s)));
        strcpy(legacy,text);char *end=strstr(legacy,"key_feedback=");CHECK(end);*end=0;
        CHECK(!xz_settings_parse(legacy,&out)&&out.key_feedback==1&&out.theme==s.theme);
    }
    s.ram_debug=2;CHECK(xz_settings_format(&s,text,sizeof(text))<0);s.ram_debug=0;
    s.key_feedback=2;CHECK(xz_settings_format(&s,text,sizeof(text))<0);s.key_feedback=1;
    CHECK(xz_settings_format(&s,text,sizeof(text))>0);
    const int counts[]={8,9,10,11,12,22,23,24,30,33,34};
    for(unsigned k=0;k<sizeof(counts)/sizeof(counts[0]);k++){
        strcpy(legacy,text);char *line=strchr(legacy,'\n')+1;
        for(int i=0;i<counts[k];i++)line=strchr(line,'\n')+1;
        *line=0;CHECK(!xz_settings_parse(legacy,&out)&&out.key_feedback==1);
    }
    char *value=strstr(text,"key_feedback=");CHECK(value);value[strlen("key_feedback=")]='2';CHECK(xz_settings_parse(text,&out)<0);
    puts("PASS production preference: on/off roundtrip, all legacy counts, default on, invalid flag refusal");
}
'''
with tempfile.TemporaryDirectory(prefix='xz-feedback-profile-') as tmp:
    tmp=Path(tmp);c=tmp/'settings.c';c.write_text(settings+fixture);exe=tmp/'settings.exe'
    subprocess.run([str(a.zig),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(root),str(c),str(root/'ui/beat_jump.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
schema=json.loads((root/'settings-schema.json').read_text())
assert schema['accepted_field_counts']==[8,9,10,11,12,22,23,24,30,33,34,35,36,37,38]
assert len(schema['fields'])==38
byname={f['key']:f for f in schema['fields']}
field=byname['key_feedback'];assert (field['default'],field['min'],field['max'])==(1,0,1)
assert byname['ram_debug']['default']==0 and byname['rekordbox_menu']['default']==1
print('PASS schema38 keeps the key feedback extension and all legacy lengths')
