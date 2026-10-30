"""Real all-locale source-change and zero-inference resume proof in isolated state."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from xz_i18n import LOCALES, ROOT, digest, protected
from xz_translation_quality import require_quality


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',type=Path,required=True)
    parser.add_argument('--state',type=Path,required=True)
    parser.add_argument('--proof',type=Path,required=True)
    args=parser.parse_args()
    proof=args.proof.resolve(); state=args.state.resolve()
    if proof==state or state in proof.parents or proof in state.parents:
        raise ValueError('Proof and production cache directories must be independent')
    if proof.exists(): raise ValueError('Choose a new proof directory; prior receipts are retained')
    proof.mkdir(parents=True); cache=proof/'cache'; cache.mkdir()
    canonical=ROOT/'ui/xz-l10n-data.js'
    file_hash=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    canonical_hash=file_hash(canonical)
    raw=canonical.read_text(encoding='utf-8')
    original=json.loads(raw.removeprefix('window.XZ_L10N_DATA=').strip().removesuffix(';'))
    require_quality(original)
    production_hashes={}
    for path in state.glob('*.json'):
        value=json.loads(path.read_text(encoding='utf-8'))
        if isinstance(value,dict) and isinstance(value.get('identity'),str) and isinstance(value.get('segments'),dict):
            shutil.copy2(path,cache/path.name)
            production_hashes[path]=file_hash(path)
    source=original['source']
    text='The offline language test is ready.'
    key='desktop:'+digest(text)[:20]
    if key in source['entries']: raise ValueError('Fixture must add a new authored source message')
    source['entries'][key]={'source':text,'source_sha256':digest(text),'surface':'desktop','placeholders':[],
                            'protected':protected(text),'immutable':False,'contexts':['proof:authored-fixture']}
    source['sources']['proof/authored-fixture.txt']=digest(text)
    source['source_revision']=digest(json.dumps(source['sources'],sort_keys=True))
    snapshot=proof/'source.json'; snapshot.write_text(json.dumps(source,ensure_ascii=False),encoding='utf-8')
    output=proof/'bundle.js'
    command=[sys.executable,str(ROOT/'tools/xz_translate.py'),'--source',str(snapshot),'--model',str(args.model.resolve()),
             '--state',str(cache),'--output',str(output),'--overrides',str(ROOT/'localization/authored-overrides.json')]
    with (proof/'generation.log').open('w',encoding='utf-8') as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    generated=json.loads(output.read_text(encoding='utf-8').removeprefix('window.XZ_L10N_DATA=').strip().removesuffix(';'))
    quality=require_quality(generated)
    for locale in LOCALES:
        translated=generated['catalogs'][locale]['entries'][key]['text']
        if translated.casefold()==text.casefold(): raise ValueError(locale+': new source remained English')
        if generated['catalogs'][locale]['source_revision']!=source['source_revision']: raise ValueError('Stale generation')
    before=hashlib.sha256(output.read_bytes()).hexdigest()
    with (proof/'resume.log').open('w',encoding='utf-8') as log:
        subprocess.run([*command,'--cached-only'],stdout=log,stderr=subprocess.STDOUT,check=True)
    resume=json.loads((cache/'cached-validation.json').read_text(encoding='utf-8'))
    if resume['inference_calls']!=0: raise ValueError('Resume unexpectedly performed inference')
    if hashlib.sha256(output.read_bytes()).hexdigest()!=before: raise ValueError('Validation changed the published proof bundle')
    if file_hash(canonical)!=canonical_hash or any(file_hash(path)!=expected for path,expected in production_hashes.items()):
        raise ValueError('Production source or cache changed during the isolated proof')
    receipt={'source_revision':source['source_revision'],'added_entry':key,'quality':quality,
             'resume':resume,'bundle_sha256':before,'production_source_and_cache_unchanged':True,
             'new_translations':{locale:generated['catalogs'][locale]['entries'][key]['text'] for locale in LOCALES}}
    (proof/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'proof':str(proof),'locales':len(quality),'inference_calls_on_resume':0}))


if __name__=='__main__': main()
