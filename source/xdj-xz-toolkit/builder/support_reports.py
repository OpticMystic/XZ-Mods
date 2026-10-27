"""Bounded, offline support drafts. Never performs network IO."""
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import stat
from datetime import datetime, timezone

from .support_contract import CONTRACT

CAP=CONTRACT['limits']['report_bytes']
PAGE=12000  # JSON escaping still fits the 64 KiB event envelope.
LABELS={'build.json','loader.json','bridge.json','bridge.log','bootstrap.log','runtime.log','network.json','app.log'}
HEX=re.compile(r'[0-9a-f]{32}')
# No unrestricted text survives collection. Retain only known state/reason/numeric facts.
STATES={'NEW','ACTIVE','RETIRED','WAITING','READY','FAILED','STOPPED','retryable','route_ready','waiting','ready','stopping','stopped'}
FIELDS={'schema','state','reason','terminal_reason','retryable','attempt','route_ready','app_version','runtime_sha256','bridge_sha256','module_sha256'}
REASONS={'WAIT_MUSIC_MOUNT','WAIT_AUDIO_DEVICE','WAIT_LOOPBACK','WAIT_MUSIC_DESCRIPTORS','WAIT_MUSIC_SERIAL_READ','WAIT_NATIVE_PAUSED','WAIT_SAFE_LOOPBACK_DISCONNECT','MUSIC_READY','READY_CH3_CH4','STOPPED_WITH_ERROR','STOP_PENDING','RACE_MUSIC_CAPTURE','RACE_MUSIC_DESCRIPTORS','FATAL_BINDING_NOT_NEW','FATAL_PROCESS_SET_CHANGE','UNSUPPORTED_MUSIC_FILESYSTEM','UNSUPPORTED_MUSIC_MOUNT','UNSUPPORTED_MUSIC_SERIAL_LENGTH','UNSUPPORTED_UNIT_OR_USB2_TOPOLOGY','UNSUPPORTED_USB2_BLOCK_ANCESTRY','CLEANUP_MODULE_UNLOAD_FAILURE','CLEANUP_NODE_REMOVE_FAILURE','TERMINATED_ADMISSION_DEBOUNCE_FAILURE','TERMINATED_APPLY_LOAD_FAILURE','TERMINATED_BIND_DEBOUNCE_FAILURE','TERMINATED_EVENT_SOCKET_FAILURE','TERMINATED_FOREIGN_MODULE','TERMINATED_FOREIGN_MODULE_OR_NODE','TERMINATED_FOREIGN_NODE','TERMINATED_MODULE_ADMISSION_FAILURE','TERMINATED_MUSIC_IDENTITY','TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE','TERMINATED_NODE_REMOVE_FAILURE','TERMINATED_NODE_WRITE_FAILURE','TERMINATED_PID_WRITE_FAILURE','TERMINATED_PROBE_LOAD_FAILURE','TERMINATED_PROBE_UNLOAD_FAILURE','TERMINATED_PROCESS_SET_CHANGE','TERMINATED_SCAN_FAILURE','TERMINATED_SIGNAL','TERMINATED_STOPPED_MEDIA_ADMISSION','TERMINATED_STOP_MARKER','TERMINATED_UNSPECIFIED','WAIT_MOUNT','WAIT_MEDIA','ACTIVE_MISMATCH','MOUNT_AMBIGUOUS','UNSUPPORTED_KERNEL','UNSUPPORTED_MODEL','OWNER_MISMATCH','NOT_READY','READY','NONE','CANCELLED','FAILED'}

def need(value,message):
    if not value: raise ValueError(message)

def regular(path):
    # Reject symlink/junction parents, including the selected root itself.
    for parent in (path,*path.parents):
        if parent.exists():
            s=parent.lstat()
            need(not stat.S_ISLNK(s.st_mode) and not getattr(s,'st_file_attributes',0)&0x400,'Linked report path refused')
    need(path.is_file(),'Report file missing')
    return path

def root():
    configured=os.environ.get('XZ_BUILDER_DATA')
    need(configured and Path(configured).is_absolute(),'Host app_local_data_dir is required')
    path=Path(configured)/'support-reports'
    path.mkdir(parents=True,exist_ok=True)
    for parent in (path,*path.parents):
        s=parent.lstat();need(not stat.S_ISLNK(s.st_mode) and not getattr(s,'st_file_attributes',0)&0x400,'Linked draft store refused')
    return path

def bounded(path,limit):
    with regular(path).open('rb') as f:
        size=os.fstat(f.fileno()).st_size
        f.seek(max(0,size-limit));data=f.read(limit+1)
    need(len(data)<=limit,'Report changed while reading')
    return data,size>limit

def safe(value):
    if type(value) is bool or type(value) is int and 0<=value<=1000000:return value
    if isinstance(value,str) and (value in STATES or value in REASONS or value in ('xz-bridge-status/1','xz-support-report/1') or len(value)<=64 and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.]+)?(?:\+[a-z0-9.]+)?',value)):
        return value
    return None

def collect(base,label,relative):
    try: raw,truncated=bounded(Path(base)/relative,131072)
    except (OSError,ValueError):return {'label':label,'content':'','truncated':False,'missing':True},0,1
    return sanitize(label,raw.decode('utf-8','replace'),truncated)

def sanitize(label,source,truncated=False):
    kept=[];omitted=redactions=0
    if label.endswith('.json'):
        try:
            obj=json.loads(source);need(type(obj) is dict,'Expected facts')
            for k,v in obj.items():
                if k in FIELDS and k.endswith('_sha256'):
                    clean=v if type(v) is str and re.fullmatch(r'[0-9a-f]{64}',v) else None
                else:clean=safe(v) if k in FIELDS else None
                if clean is not None:kept.append((k,clean))
                else:omitted+=1
            content=json.dumps(dict(kept),ensure_ascii=True)
        except (ValueError,TypeError):
            lines=source.splitlines()
            if label=='bridge.json' and lines and all(line in REASONS or line in STATES for line in lines):
                # Actual native v1 exports contain known code lines here.
                facts={}
                for line in lines:
                    if line in REASONS:facts['reason']=line
                    elif line in STATES:facts['state']=line
                content=json.dumps(facts,ensure_ascii=True)
            else:content='';omitted=1
    else:
        for line in source.splitlines():
            m=re.fullmatch(r'(state|reason|terminal_reason|attempt|route_ready|retryable)=(.*)',line)
            value=safe(m[2]) if m else None
            if value is not None:
                kept.append(m[1]+'='+str(value));continue
            # Actual bridge lines are '<epoch> CODE'. Boot session lines have a
            # fixed date prefix. Retain only known event codes, never raw suffixes.
            event=re.fullmatch(r'(?:[0-9]{1,12} )?([A-Z][A-Z0-9_]{1,79})',line)
            boot=re.fullmatch(r'\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:]{8}\] ([A-Z][A-Z0-9_]{1,79}):[^\r\n]*',line)
            code=event[1] if event else boot[1] if boot else None
            if code in REASONS or code in {'FAILED','VJ_NETWORK_READY','OTA_TRIAL_READY','OTA_BASE_OR_RECOVERY','OTA_ACCEPTED','OTA_ROLLBACK','USB_GADGET_PROBE_COMPLETE'}:
                kept.append('reason='+code)
                if line!='reason='+code:redactions+=1
            else:omitted+=1
        content='\n'.join(kept)
        if len(content.encode('utf-8'))>131072:
            clipped=content[:131072].rsplit('\n',1)[0]
            omitted+=len(kept)-len(clipped.splitlines());content=clipped;truncated=True
    return {'label':label,'content':content,'truncated':truncated,'missing':False},min(redactions+omitted,1000000),omitted

def whole_json(path,limit):
    raw,truncated=bounded(path,limit)
    need(not truncated,'Metadata exceeds limit')
    obj=json.loads(raw);need(type(obj) is dict,'Expected metadata object')
    return obj,raw

def build_facts():
    # These are host-selected packaged resources, never metadata from a USB or request.
    from .service import VERSION
    result={'app_version':VERSION,'runtime_sha256':None,'bridge_sha256':None,'module_sha256':None}
    configured=os.environ.get('XZ_BUILDER_RESOURCES')
    if not configured or not Path(configured).is_absolute():return result
    resource=Path(configured)
    try:manifest,_=whole_json(resource/'runtime/manifest.json',131072)
    except (OSError,ValueError):return result
    version=manifest.get('app_version')
    if type(version) is str and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.]+)?',version) and len(version.encode())<=64:result['app_version']=version
    for key in ('runtime_sha256','bridge_sha256','module_sha256'):
        value=manifest.get(key)
        if type(value) is str and re.fullmatch(r'[0-9a-f]{64}',value):result[key]=value
    # The packaged manifest pins the exact profile bytes. No executable is opened.
    try:
        package,raw=whole_json(resource/'runtime/quad-package.json',2*1024*1024)
        need(hashlib.sha256(raw).hexdigest()==manifest.get('quad_package_sha256'),'Profile metadata hash mismatch')
        for component in ('bridge','module'):
            value=package.get('components',{}).get(component,{}).get('sha256')
            if type(value) is str and re.fullmatch(r'[0-9a-f]{64}',value):result[component+'_sha256']=value
    except (OSError,ValueError,TypeError,AttributeError):pass
    return result

def exported_items(usb,job):
    # Native export uses this fixed directory and exact filename grammar. Never
    # recurse, follow linked parents, or open receipt/meta/key sidecars.
    directory=usb/'VJTOOLS/support-reports';candidates=[]
    try:
        for parent in (directory,*directory.parents):
            info=parent.lstat()
            need(not stat.S_ISLNK(info.st_mode) and not getattr(info,'st_file_attributes',0)&0x400,'Linked export directory refused')
        with os.scandir(directory) as entries:
            for index,entry in enumerate(entries):
                if index>=64:return [],0,1
                match=re.fullmatch(r'xz-support-report-([0-9a-f]{32})\.json',entry.name)
                if match:candidates.append((entry.stat(follow_symlinks=False).st_mtime_ns,entry.name,match[1]))
    except FileNotFoundError:return [],0,0
    except (OSError,ValueError):return [],0,1
    if not candidates:return [],0,0
    _,name,ident=max(candidates)
    try:
        job.check();report,_=whole_json(directory/name,CAP)
        need(report.get('schema')=='xz-support-report/1' and report.get('origin')=='controller' and report.get('report_id')==ident,'Invalid native export identity')
        items=report['private']['items'];need(type(items) is list and len(items)<=8,'Invalid exported items')
        seen=set();result=[];redactions=omitted=0
        for item in items:
            job.check();need(type(item) is dict and set(item)=={'label','content','truncated','missing'},'Invalid exported item')
            label=item['label'];need(type(label) is str and label in LABELS and label not in seen,'Invalid exported label');seen.add(label)
            need(type(item['content']) is str and len(item['content'].encode())<=131072 and type(item['missing']) is bool and type(item['truncated']) is bool,'Invalid exported item bounds')
            # Boot session below owns bootstrap.log. Never import contact, title,
            # description, IDs, keys or export-provided build identity.
            if label in ('build.json','loader.json','app.log','bootstrap.log'):continue
            clean,count,dropped=sanitize(label,item['content'],item['truncated']);clean['missing']=item['missing']
            result.append(clean);redactions+=count;omitted+=dropped
        return result,redactions,omitted
    except (OSError,ValueError,TypeError,KeyError):return [],0,1

def text(value,limit,required=False):
    need(type(value) is str and len(value.encode('utf-8'))<=limit and (value.strip() or not required),'Text exceeds report limit or is empty')
    return value

def load(ident):
    need(type(ident) is str and HEX.fullmatch(ident),'Invalid report ID')
    data,_=bounded(root()/(ident+'.draft'),CAP*2)
    d=json.loads(data);body=d['body'].encode('utf-8')
    need(len(body)<=CAP and hashlib.sha256(body).hexdigest()==d['sha256'],'Draft integrity failed')
    return d

def save(body):
    raw=json.dumps(body,ensure_ascii=True,separators=(',',':')).encode('utf-8')
    need(len(raw)<=CAP,'Report exceeds 512 KiB; remove private items')
    entries=list(root().iterdir())
    need(len(entries)<160 and sum(p.name.endswith('.draft') for p in entries)<32,'Local draft limit reached. Open Saved reports to resume or discard a local copy.')
    ident=body['report_id'];d={'body':raw.decode(),'sha256':hashlib.sha256(raw).hexdigest(),'key':secrets.token_hex(32)}
    path=root()/(ident+'.draft')
    with path.open('x',encoding='utf-8',newline='') as f:json.dump(d,f,separators=(',',':'))
    return {'report_id':ident,'sha256':d['sha256'],'bytes':len(raw),'pages':(len(d['body'])+PAGE-1)//PAGE,'privacy':'Private sanitized diagnostics; public summary is reviewed separately'}

def saved_reports(job):
    reports=[]
    for path in sorted(root().glob('*.draft')):
        job.check()
        if not HEX.fullmatch(path.stem):continue
        if len(reports)>=32:break
        try:
            raw,_=bounded(path,CAP*2+4096)
            entry={'report_id':path.stem,'stored_revision':hashlib.sha256(raw).hexdigest(),
                   'title':'Unreadable local report','readable':False}
            try:
                draft=load(path.stem);body=json.loads(draft['body'])
                entry.update(title=text(body['description'][:40] if body['schema']=='xz-support-report/2' else body['public_issue']['title'],160,True),sha256=draft['sha256'],readable=True)
            except (OSError,ValueError,KeyError,TypeError):pass
            reports.append(entry)
        except (OSError,ValueError):continue
    return {'reports':reports,'limit':32}

def dispatch(r,job):
    job.check();method=r['method']
    if method in ('support_sources','support_collect','support_source_preview','support_prepare_v2','support_edit_v2'):
        return dispatch_v2(r,job)
    if method=='support_list':return saved_reports(job)
    if method=='support_remove':
        ident=r.get('report_id');need(type(ident) is str and HEX.fullmatch(ident),'Invalid report ID')
        raw,_=bounded(root()/(ident+'.draft'),CAP*2+4096)
        need(hashlib.sha256(raw).hexdigest()==r.get('stored_revision'),'Local report changed; refresh saved reports')
        job.check()
        for suffix in ('.draft','.review','.receipt','.attempt'):(root()/(ident+suffix)).unlink(missing_ok=True)
        return {'discarded':True}
    if method=='support_resume':
        draft=load(r.get('report_id'));need(draft['sha256']==r.get('sha256'),'Local report changed; refresh saved reports')
        body=json.loads(draft['body']);ident=r['report_id']
        (root()/(ident+'.review')).unlink(missing_ok=True)
        if body['schema']=='xz-support-report/2':
            return {'report_id':ident,'sha256':draft['sha256'],'bytes':len(draft['body'].encode()),'pages':(len(draft['body'])+PAGE-1)//PAGE,
                    'schema':body['schema'],'fields':{'description':body['description'],'kind_hint':body.get('kind_hint'),
                    'reporter_hints':body.get('reporter_hints',{}),'category':body['category'],'contact':body['private']['contact'],
                    'contact_opt_in':body['private']['contact_consent'],'item_ids':[i['item_id'] for i in body['private']['items']]}}
        return {'report_id':ident,'sha256':draft['sha256'],'bytes':len(draft['body'].encode()),
                'pages':(len(draft['body'])+PAGE-1)//PAGE,
                'fields':{'title':body['public_issue']['title'],'description':body['public_issue']['description'],
                          'category':body['category'],'include_private':bool(body['private']['items'] or body['private']['contact']),
                          'contact':body['private']['contact'] or ''}}
    if method=='support_prepare' or method=='support_edit':
        old=json.loads(load(r['report_id'])['body']) if method=='support_edit' else None
        private=r.get('include_private') is True
        items=[];redactions=omitted=0
        if private and old:items=old['private']['items'];redactions=old['redactions']['count'];omitted=old['redactions']['omitted_lines']
        elif private:
            app=os.environ['XZ_BUILDER_DATA']
            sources=[(app,'app.log','app.log'),(app,'loader.json','loader-status.json')]
            if r.get('volume'):
                usb=Path(r['volume']);need(usb.is_absolute() and usb.is_dir(),'Choose a USB root')
                sources += [(usb,'bootstrap.log','XZ_RUNTIME/session.txt')]
                imported,count,dropped=exported_items(usb,job);items.extend(imported);redactions+=count;omitted+=dropped
            for base,label,relative in sources:
                job.check();item,count,dropped=collect(base,label,relative);items.append(item);redactions+=count;omitted+=dropped
        category=r.get('category','other');need(category in ('bridge','audio','display','other'),'Invalid category')
        contact=text(r['contact'],256) if private and r.get('contact_opt_in') is True else None
        body={'schema':'xz-support-report/1','report_id':secrets.token_hex(16),'created_at':datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),'origin':'loader','category':category,
          'build':old['build'] if old else build_facts(),
          'public_issue':{'title':text(r.get('title',''),160,True),'description':text(r.get('description',''),4096)},
          'private':{'contact':contact,'items':items},'redactions':{'count':min(redactions,1000000),'omitted_lines':min(omitted,1000000)}}
        removed=r.get('remove_items',[]);need(type(removed) is list and all(x in LABELS for x in removed),'Invalid private item')
        body['private']['items']=[i for i in items if i['label'] not in removed]
        job.check();return save(body)
    d=load(r.get('report_id'));need(r.get('sha256')==d['sha256'],'Stale preview; prepare and review again')
    if method=='support_discard':
        for suffix in ('.draft','.review','.receipt','.attempt'):
            (root()/(r['report_id']+suffix)).unlink(missing_ok=True)
        return {'discarded':True}
    if method=='support_preview':
        page=r.get('page',0);pages=(len(d['body'])+PAGE-1)//PAGE
        need(type(page) is int and 0<=page<pages,'Invalid preview page')
        # TODO phase 3: remove mandatory all-page review in Python, Rust and JS
        # together, replacing it with explicit consent to this exact frozen hash.
        # Complete preview is tracked in a bounded separate marker, never in uploaded bytes.
        marker=root()/(r['report_id']+'.review')
        seen=set(json.loads(marker.read_text())['pages']) if marker.exists() else set()
        seen.add(page);marker.write_text(json.dumps({'sha256':d['sha256'],'pages':sorted(seen),'complete':len(seen)==pages}))
        return {'page':page,'pages':pages,'sha256':d['sha256'],'text':d['body'][page*PAGE:(page+1)*PAGE]}
    raise ValueError('Unknown support operation')


def dispatch_v2(request,job):
    from . import support_sources as sources
    from .support_validation import validate_report
    method=request['method']
    if method=='support_sources':return sources.discover(job)
    if method=='support_source_preview':
        record=sources._record(request.get('scan_generation'))
        snapshot=record.get('snapshot');need(snapshot is not None,'Collection has not completed')
        selected=next((i for i in snapshot['items'] if i['item_id']==request.get('item_id')),None)
        need(selected is not None,'Unknown source item')
        content=sources.encoded(selected).decode();page=request.get('page',0);pages=max(1,(len(content)+PAGE-1)//PAGE)
        need(type(page) is int and 0<=page<pages,'Invalid source preview page')
        return {'item_id':selected['item_id'],'page':page,'pages':pages,'text':content[page*PAGE:(page+1)*PAGE]}
    if method=='support_collect':
        snapshot=sources.collect(request,job)
        return {'scan_generation':snapshot['scan_generation'],'incident':snapshot['incident'],
                'items':[{k:i[k] for k in ('item_id','source_id','source_slot','missing_reason','truncated','omission_count','clock_confidence')}|
                         {'bytes':len(sources.encoded(i))} for i in snapshot['items']],
                'diary_coverage':snapshot.get('diary_coverage',{}),'bytes':len(sources.encoded(snapshot['items'])),'omitted_items':snapshot['omitted_items'],'unfinished_sources':snapshot['unfinished_sources']}
    old=None
    if method=='support_edit_v2':
        draft=load(request.get('report_id'));need(draft['sha256']==request.get('sha256'),'Stale preview; prepare and review again')
        old=json.loads(draft['body']);need(old.get('schema')=='xz-support-report/2','Expected a v2 draft')
    hints=request.get('reporter_hints',old.get('reporter_hints',{}) if old else {})
    from .support_validation import validate
    validate(hints,CONTRACT['report_schema']['properties']['reporter_hints'])
    hint=request.get('kind_hint',old.get('kind_hint') if old else None)
    if hint is None:hint={'NEW_IDEA':'FEATURE','HOW_DO_I':'QUESTION','GOT_WORSE':'REGRESSION','WORK_DIFFERENTLY':'EXPECTATION','NOT_WORKING':'BUG'}.get(hints.get('topic'))
    need(hint is None or hint in CONTRACT['kind_hints'],'Invalid kind hint')
    if hints.get('topic')=='NEW_IDEA':hint='FEATURE'
    category=request.get('category',old['category'] if old else CONTRACT['problem_area_categories'].get(hints.get('problem_area'),'other'));need(category in CONTRACT['categories'],'Invalid category')
    if old:
        items=old['private']['items'];incident=old['incident'];coverage=old['coverage']
    elif hint=='FEATURE':
        items=[];incident={'before_seconds':900,'after_seconds':0,'clock_confidence':'unknown'}
        coverage={'scan_generation':secrets.token_hex(16),'omitted_items':0,'unfinished_sources':0}
    else:
        snapshot=sources.collect({**request,'category':category,'kind_hint':hint},job)
        items=snapshot['items'];incident=snapshot['incident'];coverage={k:snapshot[k] for k in ('scan_generation','omitted_items','unfinished_sources')}
    removed=request.get('omit_item_ids',[])
    need(type(removed) is list and all(type(x) is str and x in {i['item_id'] for i in items} for x in removed),'Unknown omitted item')
    included=[i for i in items if i['item_id'] not in removed and category in sources.REGISTRY[i['source_id']]['categories']]
    if hint=='FEATURE':included=[]
    elif hint=='QUESTION' and request.get('include_diagnostics') is not True:
        included=[i for i in included if i['source_id'] in ('usb_settings','packaged_manifest','packaged_profile')]
    coverage={**coverage,'omitted_items':min(1000000,coverage['omitted_items']+len(items)-len(included))}
    from .service import VERSION
    desktop=VERSION if len(VERSION)<=64 and sources.VERSION.fullmatch(VERSION) else 'unknown'
    contact_consent=request.get('contact_opt_in') is True
    body={'schema':'xz-support-report/2','report_id':secrets.token_hex(16),'origin':'loader','kind_hint':hint,
          'description':text(request.get('description',old['description'] if old else ''),4096,True),'category':category,'reporter_hints':hints,
          'build':old['build'] if old else {'desktop_version':desktop,'desktop_provenance':'desktop' if desktop!='unknown' else 'unknown','runtime_version':'unknown','runtime_provenance':'unknown'},
          'incident':incident,'private':{'contact':text(request.get('contact',''),256) if contact_consent else None,'contact_consent':contact_consent,'items':included},
          'coverage':coverage,'redactions':{'count':min(1000000,sum(i['omission_count'] for i in included)),
                                          'omitted_lines':min(1000000,sum(i['omission_count'] for i in included))}}
    # Measure serialized bytes, including escaping. Remove lowest-priority tails
    # deterministically before freezing; transmission must never truncate a draft.
    while len(sources.encoded(body))>CAP and body['private']['items']:
        body['private']['items'].pop();body['coverage']['omitted_items']+=1
    validate_report(body)
    job.check()
    result=save(body);result['privacy']='Private report. Diagnostics expire after 14 days; safe tracking lasts longer.'
    return result
