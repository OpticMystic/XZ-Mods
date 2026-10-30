"""XZ-only resumable local translation. Publish one complete bundle with os.replace."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import time
from xz_translation_quality import LiteralPolicy, validate_quality, require_quality
from xz_i18n import ROOT, LOCALES, STOCK_ALERTS, CONTROL_RE, BRAND_RE, extract, validate, script, digest, placeholders, protected_valid, normalize, translate_html

LANG = {locale: {'zh-Hans':'zh', 'zh-Hant':'zh'}.get(locale,locale) for locale in LOCALES}
POLICY = LiteralPolicy()

def file_hash(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()

def replace_file(source, target):
    # Windows readers and antivirus may briefly omit FILE_SHARE_DELETE.
    deadline=time.monotonic()+5
    delay=0.05
    while True:
        try:
            os.replace(source,target)
            return
        except PermissionError as error:
            if getattr(error,'winerror',None) not in (5,32,33) or time.monotonic()>=deadline: raise
            time.sleep(delay)
            delay=min(delay*2,0.5)

def atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + '.tmp')
    with tmp.open('w', encoding='utf-8', newline='\n') as f:
        f.write(value); f.flush(); os.fsync(f.fileno())
    replace_file(tmp, path)

def publish_reserved(path, value, reserve):
    """Consume preallocated same-volume space without exposing a partial bundle."""
    data=value.encode('utf-8')
    if reserve.resolve()==path.resolve(): raise ValueError('Reserve must not be the published bundle')
    if reserve.stat().st_dev!=path.parent.stat().st_dev:
        raise ValueError('Publication reserve must be on the output volume')
    with reserve.open('r+b') as stream:
        if len(data)>os.fstat(stream.fileno()).st_size:
            raise ValueError('Publication reserve is too small; prior bundle retained')
        stream.write(data); stream.flush(); os.fsync(stream.fileno())
        stream.truncate(len(data)); stream.flush(); os.fsync(stream.fileno())
    replace_file(reserve,path)

def pieces(text, terms=()):
    result=[]; start=0
    spans=[(left,right,text[left:right]) for left,right in POLICY.spans(text)]
    spans.extend((left,right,target) for left,right,target,_ in term_matches(text,terms))
    for left,right,target in sorted(spans):
        if left>start: result.append((False,text[start:left]))
        result.append((True,target)); start=right
    if start<len(text): result.append((False,text[start:]))
    bounded=[]
    for keep,value in result:
        if keep: bounded.append((True,value)); continue
        match=re.match(r'^(\W*)(.*?)(\W*)$',value,flags=re.S)
        prefix,core,suffix=match.groups()
        if prefix: bounded.append((True,prefix))
        if core: bounded.append((False,core))
        if suffix: bounded.append((True,suffix))
    return bounded

def term_matches(text, terms):
    protected=POLICY.spans(text); found=[]
    for term in terms:
        pattern=re.compile(r'(?<!\w)(?:'+'|'.join(re.escape(word) for word in term['sources'])+r')(?!\w)',re.I|re.ASCII)
        for match in pattern.finditer(text):
            if not any(match.start()<right and match.end()>left for left,right in protected):
                found.append((match.start(),match.end(),term['target'],term))
    return found

def mask_text(text, terms=()):
    spans=[(left,right,text[left:right]) for left,right in POLICY.spans(text)]
    spans.extend((left,right,target) for left,right,target,_ in term_matches(text,terms))
    spans.sort(); replacements=[]; output=[]; start=0
    for left,right,target in spans:
        if left<start: raise ValueError('Overlapping authored terminology rules')
        number=9001+len(replacements); marker=str(number)
        while marker in text or any(marker==used for used,_ in replacements):
            number+=1; marker=str(number)
        output.extend((text[start:left],marker)); replacements.append((marker,target)); start=right
    output.append(text[start:]); return ''.join(output),replacements

def restore_text(text, replacements):
    for marker, original in replacements:
        if text.count(marker)!=1: raise ValueError('Model changed protected marker '+marker)
        text=text.replace(marker,original)
    return text

def restore_checked(text, replacements, entry):
    restored=restore_text(text,replacements)
    if placeholders(restored)!=entry['placeholders'] or not protected_valid(entry['protected'],restored):
        raise ValueError('Restored output changed a placeholder or printed-control boundary')
    errors=POLICY.errors(entry['source'],restored)
    if errors: raise ValueError('; '.join(errors))
    if len(entry['source'].split())>1 and any(c.isalpha() for c in POLICY.prose(entry['source'])) and not any(c.isalpha() for c in POLICY.prose(restored)):
        raise ValueError('Provider dropped all prose and returned only protected tokens')
    return restored

class Provider:
    def __init__(self, model, threads):
        import ctranslate2
        import sentencepiece
        from opencc import OpenCC
        self.inference_calls=0
        self.model=model.resolve()
        self.sp=sentencepiece.SentencePieceProcessor(model_proto=(model/'sentencepiece.bpe.model').read_bytes())
        vocab=json.loads((model/'shared_vocabulary.json').read_text(encoding='utf-8'))
        for lang in set(LANG.values()):
            if f'__{lang}__' not in vocab: raise ValueError('Missing model language '+lang)
        for dll_dir in (Path(os.environ.get('LOCALAPPDATA', '')) / 'Programs/Ollama/lib/ollama/cuda_v12',):
            if dll_dir.is_dir():
                os.environ['PATH'] = str(dll_dir) + os.pathsep + os.environ.get('PATH', '')
                if hasattr(os, 'add_dll_directory'): os.add_dll_directory(str(dll_dir))
        self.translator = None
        if getattr(ctranslate2, 'get_cuda_device_count', lambda: 0)() > 0:
            try:
                cand = ctranslate2.Translator(str(self.model), device='cuda', compute_type='int8_float16')
                cand.translate_batch([['__es__', '</s>']], beam_size=1, max_decoding_length=4)
                self.translator = cand
            except Exception:
                self.translator = None
        if self.translator is None:
            self.translator=ctranslate2.Translator(str(self.model),device='cpu',compute_type='int8_float32',intra_threads=threads)
        self.hant=OpenCC('s2t'); self.hans=OpenCC('t2s')
        self.identity={'provider':'xz-small100-ct2','review':'machine-unreviewed',
                       'model_sha256':file_hash(model/'model.bin'),
                       'tokenizer_sha256':hashlib.sha256((model/'sentencepiece.bpe.model').read_bytes()).hexdigest(),
                       'vocabulary_sha256':hashlib.sha256((model/'shared_vocabulary.json').read_bytes()).hexdigest(),
                       'ctranslate2':ctranslate2.__version__, 'recipe':2, 'beam':1, 'no_repeat_ngram_size':4, 'script_conversion':'opencc-0.1.7'}
    def translate(self, texts, locale, beam=1, bounded=False):
        # Only over-context passages are split; normal cached input recipes are unchanged.
        if any(len(self.sp.encode(text))>1000 for text in texts):
            result=[]
            for text in texts:
                if len(self.sp.encode(text))<=1000:
                    result.extend(self.translate([text],locale,beam,bounded)); continue
                sentences=re.split(r'(?<=[.!?])\s+',text)
                chunks=[]; current=''
                for sentence in sentences:
                    if len(self.sp.encode(sentence))>1000:
                        raise ValueError('Single sentence exceeds model context; explicit source segmentation required')
                    candidate=(current+' '+sentence).strip()
                    if len(self.sp.encode(candidate))>1000:
                        chunks.append(current); current=sentence
                    else: current=candidate
                if current: chunks.append(current)
                result.append(' '.join(self.translate(chunks,locale,beam,bounded)))
            return result
        self.inference_calls+=1
        encoded=[]
        for text in texts:
            tokens=self.sp.encode(text,out_type=str)
            if len(tokens)>1000: raise ValueError('Input exceeds model context; split source explicitly')
            encoded.append([f'__{LANG[locale]}__',*tokens,'</s>'])
        limit=min(1024,max(64,4*max(map(len,encoded),default=0)+32)) if bounded else 1024
        outputs=self.translator.translate_batch(encoded,beam_size=beam,no_repeat_ngram_size=4,max_decoding_length=limit,max_batch_size=max(32, len(encoded)))
        result=[]
        for index,output in enumerate(outputs):
            tokens=output.hypotheses[0]
            if len(tokens)>=limit and beam!=4:
                tokens=self.translator.translate_batch([encoded[index]],beam_size=4,no_repeat_ngram_size=4,max_decoding_length=limit)[0].hypotheses[0]
            if len(tokens)>=limit:
                if not bounded:
                    parts = [p.strip() for p in re.split(r'(?<=[.!?;:])\s+|\n+', texts[index]) if p.strip()]
                    if len(parts) > 1:
                        result.append(' '.join(self.translate(parts, locale, beam=4, bounded=True)))
                        continue
                raise ValueError('Translation reached decoding limit')
            text=self.sp.decode([x for x in tokens if not x.startswith('__') and x not in ('<s>','</s>','<pad>')]).strip()
            if not text: raise ValueError('Empty model output')
            if locale=='zh-Hant': text=self.hant.convert(text)
            if locale=='zh-Hans': text=self.hans.convert(text)
            result.append(text)
        return result

def derive_search_entries(source, catalog):
    from xz_manual_search import section_text
    sections=source.get('manual_search_sections',[])
    derived={row['id'] for row in sections}
    missing=[key for key,entry in source['entries'].items() if entry['surface']=='manual' and key not in derived and key not in catalog['entries']]
    if missing and sections: raise ValueError('Cannot derive search from an incomplete translated manual')
    rows={}; originals={}; result={}
    for slug,document in source.get('manual_documents',{}).items():
        for row in section_text(slug,document['title'],document['html']): originals[(slug,row['a'])]=row['x']
        rendered=translate_html(document['html'],source,catalog)
        for row in section_text(slug,document['title'],rendered): rows[(slug,row['a'])]=row['x']
    for section in sections:
        key=section['id']; location=(section['chapter'],section['anchor']); entry=source['entries'][key]
        if normalize(originals.get(location,''))!=entry['source']:
            raise ValueError('Manual/search source mismatch: '+key)
        value=normalize(rows[location])
        result[key]={'source_sha256':entry['source_sha256'],'text':value,
                     'provenance':{'review':'machine-unreviewed','context':'derived-manual-search-v1',
                                   'protection':'rendered-manual','manual_html_sha256':digest(source['manual_documents'][section['chapter']]['html'])}}
    return result

def case_variants(masked):
    if '_' in masked or len(masked)>140 or len(masked.split())>10: return []
    if not re.search('[A-Za-z]',masked): return []
    variants=[]
    for value in (masked.lower(),masked.capitalize()):
        if value!=masked and value not in variants: variants.append(value)
    return variants

def reject_english_repair(entry,value):
    if len(POLICY.prose(entry['source']).split())>1 and normalize(value).casefold()==normalize(entry['source']).casefold():
        raise ValueError('Repair returned unchanged English prose')
    return value

def repair_entry(provider,locale,entry,masked,replacements,segments,terms=()):
    errors=[]
    for candidate,method in [(masked,'beam-retry-unreviewed'),*((v,'prose-case-normalized-unreviewed') for v in case_variants(masked))]:
        try:
            result=provider.translate([candidate],locale,beam=4,bounded=True)[0]
            value=reject_english_repair(entry,restore_checked(result,replacements,entry))
            segments[digest(candidate)]=result
            return value,{'method':method,'input':candidate,'input_sha256':digest(candidate),'beam':4,'decoding_bound':'min(1024,max(64,4*input_tokens+32)); truncation rejected'}
        except ValueError as error: errors.append(str(error))
    parts=pieces(entry['source'],terms)
    prose=[text for keep,text in parts if not keep and any(c.isalpha() for c in text)]
    results=iter(provider.translate(prose,locale,bounded=True))
    value=''.join(text if keep or not any(c.isalpha() for c in text) else next(results) for keep,text in parts)
    value=reject_english_repair(entry,restore_checked(value,[],entry))
    return value,{'method':'split-fallback-unreviewed','inputs_sha256':[digest(text) for text in prose],'earlier_errors':errors}

def load_authored(path,source):
    if not path.exists(): return {'entries':{},'terminology':{}}
    value=json.loads(path.read_text(encoding='utf-8'))
    if value.get('schema')!=1 or value.get('product')!='xz-mods': raise ValueError('Invalid authored override file')
    for locale,entries in value.get('entries',{}).items():
        if locale not in LOCALES: raise ValueError('Unknown override locale')
        for key,entry in entries.items():
            original=source['entries'].get(key)
            if original and (entry.get('source_sha256')!=original['source_sha256'] or entry.get('source')!=original['source']):
                raise ValueError('Stale authored override: '+key)
            if not entry.get('provenance',{}).get('reason'): raise ValueError('Override needs recorded provenance')
    return value

def run(args):
    print('Extracting XZ sources',flush=True)
    if getattr(args,'source',None):
        frozen=args.source.read_bytes(); source=json.loads(frozen)
        snapshot_hash=hashlib.sha256(frozen).hexdigest()
    else:
        source=extract(); snapshot_hash=None
    global POLICY
    POLICY=LiteralPolicy(source)
    authored=load_authored(getattr(args,'overrides',None) or ROOT/'localization/authored-overrides.json',source)
    derived={row['id'] for row in source.get('manual_search_sections',[])}
    print('Loading independent local model',flush=True)
    provider=Provider(args.model,args.threads)
    print('Provider ready; checking retained segments',flush=True)
    identity=digest(json.dumps(provider.identity,sort_keys=True))
    state=args.state.resolve(); state.mkdir(parents=True,exist_ok=True)
    lock=state/'run.lock'
    # OS-held lock is released on exit/crash; a leftover file never blocks resume.
    lockfile=lock.open('a+b')
    if lockfile.seek(0, os.SEEK_END) == 0:
        lockfile.write(b'0'); lockfile.flush()
    lockfile.seek(0)
    try:
        if os.name=='nt':
            import msvcrt
            msvcrt.locking(lockfile.fileno(),msvcrt.LK_NBLCK,1)
        else:
            import fcntl
            fcntl.flock(lockfile,fcntl.LOCK_EX|fcntl.LOCK_NB)
        started=time.monotonic(); catalogs={}; incomplete=False; failures=[]
        for locale in LOCALES:
            cache_path=state/(locale+'-'+identity[:12]+'.json')
            legacy_path=state/('pt-BR-'+identity[:12]+'.json')
            read_path=legacy_path if locale=='pt' and not cache_path.exists() and legacy_path.exists() else cache_path
            cache=json.loads(read_path.read_text(encoding='utf-8')) if read_path.exists() else {}
            if cache.get('identity')!=identity: cache={'identity':identity,'segments':{}}
            segments=cache['segments']; fallbacks=cache.setdefault('fallbacks',{}); methods=cache.setdefault('fallback_methods',{})
            terms=authored.get('terminology',{}).get(locale,[])
            overrides=authored.get('entries',{}).get(locale,{})
            prepared={key:mask_text(entry['source'],terms) for key,entry in source['entries'].items()}
            needed=sorted(set(text for key,(text,replacements) in prepared.items() if key not in derived and key not in overrides and any(c.isalpha() for c in text)), key=lambda text:(len(text),digest(text)))
            pending=[text for text in needed if digest(text) not in segments]
            work = [] if getattr(args,'cached_only',False) else pending[:args.round_size] if args.round_size else pending
            for offset in range(0,len(work),args.batch):
                batch=work[offset:offset+args.batch]
                try:
                    translated=provider.translate(batch,locale)
                except Exception as error:
                    atomic(state/'failure.json',json.dumps({'locale':locale,'source_revision':source['source_revision'],'inputs':batch,'error':str(error)},ensure_ascii=False))
                    raise
                for text,value in zip(batch,translated): segments[digest(text)]=value
                atomic(cache_path,json.dumps(cache,ensure_ascii=False))
                progress={'locale':locale,'done':len(needed)-len(pending)+offset+len(batch),'total':len(needed),
                          'source_revision':source['source_revision'],'seconds':round(time.monotonic()-started), 'published':False}
                atomic(state/'progress.json',json.dumps(progress))
                print(json.dumps(progress),flush=True)
                if args.seconds and time.monotonic()-started>=args.seconds:
                    print('Checkpoint retained; resume with the same command. Prior published bundle unchanged.',flush=True); return 2
            partial = len(work) < len(pending)
            incomplete = incomplete or partial
            entries={}; repair_writes=0; locale_failures=[]
            for entry_index,(key,entry) in enumerate(source['entries'].items(),1):
                if key in derived: continue
                masked,replacements=prepared[key]
                applied_terms=[term for _,_,_,term in term_matches(entry['source'],terms)]
                fallback_used=False; method=None
                if key in overrides:
                    translated=restore_checked(overrides[key]['text'],[],entry)
                    method=dict(overrides[key]['provenance'],method='authored-override')
                elif any(c.isalpha() for c in masked):
                    if digest(masked) not in segments: continue
                    fallback_key=digest(entry['source']+json.dumps(entry['protected'])+(json.dumps([(term['sources'],term['target']) for term in applied_terms],ensure_ascii=False) if applied_terms else ''))
                    fallback_used=fallback_key in fallbacks
                    if fallback_used:
                        try:
                            translated=restore_checked(fallbacks[fallback_key],[],entry)
                            method=methods.get(fallback_key,{'method':'split-fallback-unreviewed'})
                        except ValueError:
                            del fallbacks[fallback_key]; methods.pop(fallback_key,None); fallback_used=False
                    if not fallback_used:
                        try: translated=restore_checked(segments[digest(masked)],replacements,entry)
                        except ValueError as initial:
                            if getattr(args,'cached_only',False): raise
                            progress={'phase':'repairing-translation','locale':locale,'entry':key,'done':entry_index,'total':len(source['entries']),'reason':str(initial),'published':False,'seconds':round(time.monotonic()-started)}
                            atomic(state/'progress.json',json.dumps(progress)); print(json.dumps(progress),flush=True)
                            try:
                                translated,method=repair_entry(provider,locale,entry,masked,replacements,segments,terms)
                            except Exception as error:
                                failure={'locale':locale,'entry':key,'source':entry['source'],'source_revision':source['source_revision'],'initial_error':str(initial),'error':str(error)}
                                locale_failures.append(failure); failures.append(failure)
                                atomic(state/'failure.json',json.dumps(failure,ensure_ascii=False))
                                atomic(state/'failures.json',json.dumps(failures,ensure_ascii=False))
                                atomic(cache_path,json.dumps(cache,ensure_ascii=False))
                                print(json.dumps({'rejected':key,'locale':locale,'error':str(error)}),flush=True)
                                continue
                            fallbacks[fallback_key]=translated; methods[fallback_key]=method; fallback_used=True
                            repair_writes+=1
                            if repair_writes%32==0: atomic(cache_path,json.dumps(cache,ensure_ascii=False))
                else: translated=restore_checked(masked,replacements,entry)
                provenance=dict(review='machine-unreviewed',context='preserved-literal' if not any(c.isalpha() for c in masked) else 'sentence-split-unreviewed' if hasattr(provider,'sp') and len(provider.sp.encode(masked))>1000 else 'whole-input',protection=method['method'] if method else 'numeric-markers-v1')
                if method: provenance['repair']=method
                if applied_terms: provenance['terminology']=applied_terms
                entries[key]={'source_sha256':entry['source_sha256'],'text':translated,'provenance':provenance}
                if entry_index%128==0:
                    progress={'phase':'validating-protection','locale':locale,'done':entry_index,'total':len(source['entries']),'published':False,'seconds':round(time.monotonic()-started)}
                    atomic(state/'progress.json',json.dumps(progress)); print(json.dumps(progress),flush=True)
            if repair_writes: atomic(cache_path,json.dumps(cache,ensure_ascii=False))
            catalog={'schema':1,'product':'xz-mods','locale':locale,'source_revision':source['source_revision'],'provider':provider.identity,'provider_identity':identity,'entries':entries}
            if locale_failures:
                atomic(state/(locale+'.rejected-quality.json'),json.dumps({'errors':locale_failures,'catalog':catalog},ensure_ascii=False))
                continue
            if not partial: catalog['entries'].update(derive_search_entries(source,catalog))
            errors=validate_quality(source,catalog,POLICY,complete=not partial)
            if errors:
                atomic(state/(locale+'.rejected-quality.json'),json.dumps({'errors':errors,'catalog':catalog},ensure_ascii=False))
                raise ValueError(locale+': '+'; '.join(errors[:10]))
            catalog['quality']={'introduced_unknown_markers':0,'protected_literal_errors':0,'policy_sha256':file_hash(Path(__file__).with_name('xz_translation_quality.py'))}
            atomic(state/(locale+'.staged.json'),json.dumps(catalog,ensure_ascii=False))
            catalogs[locale]=catalog
        if failures:
            raise ValueError(f'{len(failures)} translations failed quality validation: {failures[0]["error"]}; see {state / "failures.json"}. Prior published bundle retained.')
        if incomplete:
            receipt={'source_revision':source['source_revision'],'published':False,'inference_calls':getattr(provider,'inference_calls',None),'staged_entries':{locale:len(catalog['entries']) for locale,catalog in catalogs.items()}}
            atomic(state/'round.json',json.dumps(receipt))
            print(json.dumps(receipt),flush=True)
            print('All-language round checkpointed. Resume to continue; published bundle unchanged.',flush=True); return 2
        if snapshot_hash and file_hash(args.source)!=snapshot_hash: raise ValueError('Packaged source changed during run; resume with the new source.')
        if not getattr(args,'source',None) and extract()['source_revision']!=source['source_revision']: raise ValueError('Sources changed during run. Resume against new sources.')
        quality=require_quality({'source':source,'catalogs':catalogs})
        atomic(state/'quality.json',json.dumps({'source_revision':source['source_revision'],'locales':quality}))
        if getattr(args,'cached_only',False):
            atomic(state/'cached-validation.json',json.dumps({'source_revision':source['source_revision'],'inference_calls':getattr(provider,'inference_calls',None),'entries':{locale:len(catalog['entries']) for locale,catalog in catalogs.items()},'published':False}))
            print('Complete cached set validated without publication',flush=True); return 0
        data={'source':source,'catalogs':catalogs}
        # Canonical generation contains every locale in a single atomic file replacement.
        output=getattr(args,'output',None) or ROOT/'ui/xz-l10n-data.js'
        if getattr(args,'publication_reserve',None): publish_reserved(output,script(data),args.publication_reserve)
        else: atomic(output,script(data))
        receipt={'published':True,'source_revision':source['source_revision'],'inference_calls':getattr(provider,'inference_calls',None),'locales':{locale:{'entries':len(catalog['entries']),'changed_text':sum(item['text']!=source['entries'][key]['source'] for key,item in catalog['entries'].items()),'unchanged_text':sum(item['text']==source['entries'][key]['source'] for key,item in catalog['entries'].items())} for locale,catalog in catalogs.items()},'review':'machine-unreviewed','unchanged_text_is_not_translation_credit':True}
        atomic(state/'progress.json',json.dumps(receipt))
        print('Published complete all-language generation',flush=True)
        return 0
    finally:
        lockfile.close()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--model',type=Path,required=True)
    p.add_argument('--state',type=Path,required=True)
    p.add_argument('--source',type=Path,help='Packaged immutable source snapshot')
    p.add_argument('--output',type=Path,help='Independent generated bundle destination')
    p.add_argument('--publication-reserve',type=Path,help='Preallocated file on the output volume, consumed by atomic publication')
    p.add_argument('--overrides',type=Path,help='Authored, source-hash-bound terminology and translation repairs')
    p.add_argument('--batch',type=int,default=32)
    p.add_argument('--threads',type=int,default=4)
    p.add_argument('--cached-only',action='store_true',help='Validate and stage retained cache without inference or publication')
    p.add_argument('--round-size',type=int,default=0,help='Checkpoint after this many segments per locale; 0 completes each locale')
    p.add_argument('--seconds',type=int,default=0,help='Bounded work interval; 0 runs through publication')
    args=p.parse_args()
    if args.batch<1 or args.threads<1 or args.seconds<0 or args.round_size<0: p.error('Invalid limits')
    return run(args)
if __name__=='__main__': raise SystemExit(main())
