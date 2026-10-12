"""Checks export_mod_changes.py against real VJToolsLibrary commits: a change before a build is included, a change
after it is not, and a sha that does not resolve fails the export. Run: python tools/test_export_mod_changes.py"""
import json,subprocess,sys,tempfile
from pathlib import Path

APP=Path(__file__).resolve().parents[1]
MONOREPO=APP.parent/'VJToolsLibrary'
BEFORE='9a444b4ae8c9c2bf01453b5d868faf7b9a859872'
BUILD='999d15b42170ab15c7f47f4e9c1545b2b043860e'
AFTER='b5a6a26a2bffe015c43da6386526831a64d9dd9e'

def export(folder,data):
    source,output=folder/'mod-changes.json',folder/'mod-changes.js'
    source.write_text(json.dumps(data),encoding='utf-8')
    run=subprocess.run([sys.executable,str(APP/'tools/export_mod_changes.py'),'--input',str(source),'--monorepo',str(MONOREPO),
        '--runtime-manifest',str(folder/'none.json'),'--output',str(output)],capture_output=True,text=True)
    if run.returncode:return run,None
    text=output.read_text(encoding='utf-8')
    return run,json.loads(text[text.index('=')+1:].rstrip().rstrip(';'))

def change(id,commit,kind='new'):return {'id':id,'commit':commit,'kind':kind,'area':'Games','text':'Change '+id}

with tempfile.TemporaryDirectory(prefix='xz-mod-changes-') as temp:
    folder=Path(temp)
    run,result=export(folder,{'format':'xz-mod-changes/1','builds':[{'label':'Build','source_commit':BUILD[:10]},{'label':'Later','source_commit':AFTER}],
        'changes':[change('before',BEFORE[:8]),change('after',AFTER,'fixed')]})
    assert run.returncode==0,run.stderr
    assert result['builds'][BUILD]==['before'],result['builds']
    print('ok a change committed before the build is included')
    assert 'after' not in result['builds'][BUILD] and result['builds'][AFTER]==['after','before'],result['builds']
    print('ok a later change is not in the earlier build, and the later build lists newest first')
    run,_=export(folder,{'format':'xz-mod-changes/1','builds':[{'label':'Build','source_commit':BUILD}],'changes':[change('ghost','deadbeefdeadbeef')]})
    assert run.returncode!=0 and 'deadbeefdeadbeef' in run.stderr+run.stdout,(run.returncode,run.stderr)
    print('ok an unresolvable sha fails and names the sha')
    run,_=export(folder,{'format':'xz-mod-changes/1','builds':[{'label':'Build','source_commit':BUILD}],'changes':[change('twice',BEFORE),change('twice',BEFORE)]})
    assert run.returncode!=0 and 'Duplicate' in run.stderr,run.stderr
    print('ok a duplicate change id fails')
print('4 checks passed')
