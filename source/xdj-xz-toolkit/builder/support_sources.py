"""Read-only fixed-source discovery. Local identity handles never enter reports."""
from contextlib import contextmanager, ExitStack
from datetime import datetime, timezone
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import stat
import time

from .support_contract import CONTRACT, SOURCES
from .jobs import Cancelled

LIMITS = CONTRACT['limits']
REGISTRY = {s['source_id']: s for s in SOURCES['sources']}
REASONS = set(CONTRACT['reasons'])
STATES = set(CONTRACT['states'])
VERSION = re.compile(r'(?:unknown|[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.]+)?(?:\+[a-z0-9.]+)?)')
HEX = re.compile(r'[a-f0-9]{32}')
SESSION = secrets.token_hex(16)

class SourceError(ValueError):
    pass

def encoded(value):
    return json.dumps(value, ensure_ascii=True, separators=(',', ':')).encode('utf-8')

def checked_path(path):
    path = Path(path)
    if not path.is_absolute() or str(path).startswith(('\\\\', '//')) or any(':' in p for p in path.parts[1:]):
        raise SourceError('unsafe_path')
    for parent in (*reversed(path.parents), path):
        info = parent.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise SourceError('unsafe_path')
    return path

def identity(path):
    info = checked_path(path).stat()
    if not stat.S_ISDIR(info.st_mode):
        raise SourceError('unsafe_path')
    return [info.st_dev, info.st_ino]

def stamp(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)

@contextmanager
def locked_file(path):
    """Pin all parent directories against replacement while a file is open."""
    path = checked_path(path)
    if not stat.S_ISREG(path.stat().st_mode):raise SourceError('unsafe_path')
    with ExitStack() as stack:
        parents = [(p, identity(p)) for p in reversed(path.parents)]
        if os.name == 'nt':
            import msvcrt
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            create = kernel.CreateFileW
            create.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                               ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            create.restype = ctypes.c_void_p
            close = kernel.CloseHandle
            close.argtypes = [ctypes.c_void_p]
            # Parent handles omit FILE_SHARE_DELETE, fencing directory/root swaps.
            for parent, _ in parents:
                handle = create(str(parent), 0x80, 3, None, 3, 0x02200000, None)
                if handle == ctypes.c_void_p(-1).value:
                    raise SourceError('changed')
                stack.callback(close, handle)
                checked_path(parent)
            handle = create(str(path), 0x80000000, 1, None, 3, 0x00200000, None)
            if handle == ctypes.c_void_p(-1).value:
                raise SourceError('changed')
            try:
                fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
            except BaseException:
                close(handle)
                raise
        else:
            # Pin each directory relative to its already-open parent, O_NOFOLLOW.
            directory = os.open(path.anchor, os.O_RDONLY | os.O_DIRECTORY)
            stack.callback(os.close, directory)
            for part in path.parts[1:-1]:
                directory = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
                stack.callback(os.close, directory)
            fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        file = stack.enter_context(os.fdopen(fd, 'rb'))
        info = os.fstat(file.fileno())
        if not stat.S_ISREG(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise SourceError('unsafe_path')
        if stamp(path.stat()) != stamp(info):
            raise SourceError('changed')
        yield file, info
        if stamp(os.fstat(file.fileno())) != stamp(info) or stamp(checked_path(path).stat()) != stamp(info):
            raise SourceError('changed')
        if any(identity(parent) != expected for parent, expected in parents):
            raise SourceError('changed')

class Budget:
    def __init__(self, job, seconds=10):
        self.job = job
        self.deadline = time.monotonic() + seconds
        self.remaining = LIMITS['raw_scan_bytes']

    def check(self):
        try:
            self.job.check()
        except Cancelled:
            raise SourceError('cancelled') from None
        if time.monotonic() >= self.deadline or self.remaining <= 0:
            raise SourceError('budget')

    def read(self, path, cap, tail=False, root=None, expected=None, head=False):
        self.check()
        if root is not None:
            try:
                if identity(root) != expected:raise SourceError('removed')
            except OSError:raise SourceError('removed') from None
        with locked_file(path) as (file, before):
            if not tail and not head and before.st_size > cap:
                raise SourceError('oversized')
            amount = min(before.st_size, cap)
            if amount > self.remaining:
                raise SourceError('budget')
            self.remaining -= amount
            truncated = before.st_size > cap
            if truncated and tail:
                file.seek(before.st_size - cap)
            raw = file.read(amount)
            if len(raw) != amount:
                raise SourceError('changed')
            self.check()
        if root is not None and identity(root) != expected:
            raise SourceError('removed')
        if truncated and tail:
            # A tail may start halfway through a line. Never parse that fragment.
            raw = raw.partition(b'\n')[2]
        return raw, truncated

def store_root():
    from .support_reports import root
    path = root().parent / 'support-scans'
    path.mkdir(exist_ok=True)
    checked_path(path)
    return path

def _record(generation):
    if not isinstance(generation, str) or not HEX.fullmatch(generation):
        raise ValueError('Invalid scan generation')
    path = store_root() / (generation + '.json')
    with locked_file(path) as (file, info):
        if info.st_size > LIMITS['report_bytes'] * 2:
            raise ValueError('Invalid scan record')
        record = json.loads(file.read())
    if record['generation'] != generation or record['expires_at'] < time.time():
        raise ValueError('Scan expired; collect again')
    return record

def _save(record):
    path = store_root() / (record['generation'] + '.json')
    temporary = path.with_suffix('.tmp')
    with temporary.open('xb') as file:
        file.write(encoded(record))
    os.replace(temporary, path)

def discover(job, roots=None, supersedes=None):
    from . import managed_usb
    job.check()
    generation = secrets.token_hex(16)
    root = store_root()
    # A live USB step rescans when sticks change. The superseded scan record is local and unsent.
    if isinstance(supersedes, str) and HEX.fullmatch(supersedes):
        for suffix in ('.json', '.lock'):
            try:
                checked_path(root / (supersedes + suffix)).unlink()
            except (OSError, ValueError):
                pass
    entries = list(root.iterdir())
    # Only local scan records are removed, never USB files or draft/receipt data.
    for entry in entries:
        if HEX.fullmatch(entry.stem) and entry.suffix == '.json' and time.time()-entry.stat().st_mtime > 600:
            try:
                entry.with_suffix('.lock').unlink(missing_ok=True)
                checked_path(entry).unlink()
            except OSError:pass
    if len(list(root.glob('*.json'))) >= 16:
        raise ValueError('Close old report dialogs before collecting again')
    candidates = managed_usb.support_candidates() if roots is None else roots
    sticks = []
    others = []
    excess = 0
    for candidate in candidates:
        job.check()
        path = Path(candidate['volume'])
        try:
            observed = identity(path)
            availability = {}
            for key in ('usb_session', 'usb_settings', 'native_export', 'usb_diary'):
                relative = REGISTRY[key]['path']
                target = path / (relative.rsplit('/', 1)[0] if key in ('native_export','usb_diary') else relative)
                try:
                    checked_path(target)
                    availability[key] = target.is_dir() if key in ('native_export','usb_diary') else target.is_file()
                except (OSError, ValueError):
                    availability[key] = False
            if not any(availability.values()):
                # A removable stick with nothing to read is still listed, so the wizard can say
                # "no diary on this stick". Fixed disks without markers stay invisible.
                if candidate.get('removable') and len(others) < 8:
                    others.append({'label':'USB', 'local_volume':str(path)})
                continue
            if len(sticks) == LIMITS['sticks']:
                excess += 1
                continue
            sticks.append({'handle':secrets.token_hex(16), 'slot':'usb_' + chr(97+len(sticks)),
                           'root':str(path), 'identity':observed, 'availability':availability})
        except (OSError, ValueError):
            continue
    record = {'generation':generation, 'expires_at':time.time()+600, 'sticks':sticks, 'excess':excess}
    _save(record)
    return {'scan_generation':generation, 'sticks':[{'handle':s['handle'], 'source_slot':s['slot'],
            'label':'USB ' + s['slot'][-1].upper(), 'local_volume':s['root'], 'sources':s['availability']} for s in sticks],
            'other_sticks':others, 'excess_sticks':excess, 'xz':'not_available'}

def window(request):
    incident = request.get('incident')
    if incident is None:
        return {'epoch':time.time(), 'before':900, 'after':0, 'known':True}
    if not isinstance(incident, dict) or set(incident)-{'time','uncertain','before_seconds','after_seconds'}:
        raise ValueError('Invalid incident window')
    before = incident.get('before_seconds',600)
    after = incident.get('after_seconds',300)
    if type(before) is not int or type(after) is not int or min(before,after)<0 or before+after>3600:
        raise ValueError('Incident window exceeds one hour')
    try:
        parsed = datetime.fromisoformat(incident.get('time',''))
        known = parsed.tzinfo is not None and incident.get('uncertain') is not True
        epoch = parsed.timestamp() if known else None
    except (TypeError, ValueError, OverflowError):
        epoch = None
        known = False
    return {'epoch':epoch, 'before':before, 'after':after, 'known':known}

def timed(fact, epoch, bounds, current=False, basis='device_utc'):
    if current:
        return {**fact,'relative_seconds':None,'clock_basis':'current','window_match':'unknown'}
    # Implausible device dates and missing timezone are never incident evidence.
    if epoch is None or not bounds['known'] or not 1767225600 <= epoch < 4102444800:
        return {**fact,'relative_seconds':None,'clock_basis':'unknown','window_match':'unknown'}
    offset = round(epoch-bounds['epoch'])
    if offset < -bounds['before'] or offset > bounds['after']:
        return None
    return {**fact,'relative_seconds':offset,'clock_basis':basis,'window_match':'inside'}

def events(raw, parser, bounds):
    facts = []
    omitted = 0
    for line in raw.decode('utf-8','replace').splitlines():
        clean = None
        epoch = None
        match = re.fullmatch(r'([0-9]{1,12}) ([A-Z][A-Z0-9_]{1,79})', line)
        if match:
            epoch = int(match[1]); code = match[2]
        else:
            boot = re.fullmatch(r'\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:]{8}\] ([A-Z][A-Z0-9_]{1,79}):[^\r\n]*', line)
            code = boot[1] if boot else line.removeprefix('reason=')
        if code in REASONS:
            clean = {'code':code}
        elif code in STATES:
            clean = {'state':code}
        elif re.fullmatch(r'state=[a-zA-Z_]+', line) and line[6:] in STATES:
            clean = {'state':line[6:]}
        elif parser == 'coldplug' or line.startswith('USB_COLDPLUG: '):
            patterns = [
                r'USB_COLDPLUG: pid=[0-9]+ attempt=([0-9]{1,6}) result=(process-exited)',
                r'USB_COLDPLUG: dev=[^\s]{1,160} action=mount slot=([12]) result=(notified|write-failed) mounted_after_ms=([0-9]{1,7}) attempt=([0-9]{1,6})',
                r'USB_COLDPLUG: pid=[0-9]+ result=(watch-complete) attempts=([0-9]{1,6})',
                r'USB_COLDPLUG: pid=[0-9]+ attempt=([0-9]{1,6}) ready=\[[^\]\r\n]{0,128}\] elapsed_ms=([0-9]{1,7})']
            matches = [re.fullmatch(p,line) for p in patterns]
            if matches[0]:clean={'attempt':int(matches[0][1]),'result':matches[0][2]}
            elif matches[1]:clean={'slot':int(matches[1][1]),'result':matches[1][2],'elapsed_ms':min(int(matches[1][3]),3600000),'attempt':int(matches[1][4])}
            elif matches[2]:clean={'result':matches[2][1],'attempt':int(matches[2][2])}
            elif matches[3]:clean={'attempt':int(matches[3][1]),'elapsed_ms':min(int(matches[3][2]),3600000)}
        elif parser == 'exit_codes':
            m = re.fullmatch(r'XZ_EXIT (errno|win32|xz_display)=([0-9]{1,5})',line)
            if m and int(m[2])<=65535:clean={'error_namespace':m[1],'error_code':int(m[2])}
        elif parser == 'app_events':
            try:
                event=json.loads(line)
                if event.get('schema')=='xz-app-event/1' and event.get('component') in ('desktop','loader') and event.get('result') in ('started','succeeded','failed','cancelled'):
                    clean={'component':event['component'],'result':event['result']}
                    epoch=event.get('epoch') if type(event.get('epoch')) in (int,float) else None
            except (ValueError,TypeError,AttributeError):pass
        if clean is None:
            omitted += 1
            continue
        fact=timed(clean,epoch,bounds,basis='host_utc' if parser=='app_events' else 'device_utc')
        if fact is None:omitted += 1
        else:facts.append(fact)
    unknown=[i for i,f in enumerate(facts) if f['window_match']=='unknown']
    remove=set(unknown[:-LIMITS['unknown_tail_events']])
    return [f for i,f in enumerate(facts) if i not in remove],omitted+len(remove)

def bridge(raw, bounds, current=False):
    try:
        data=json.loads(raw)
    except ValueError:
        # Native v1 bridge.json contains collect_facts() code lines. Accept only
        # whole known codes, never an arbitrary-text JSON fallback.
        lines=raw.decode('utf-8','replace').splitlines()
        if lines and all(line in REASONS or line in STATES for line in lines):
            return events(raw,'events',bounds)
        raise SourceError('invalid') from None
    if not isinstance(data,dict):raise SourceError('invalid')
    fact={};omitted=0
    for key,value in data.items():
        if key=='schema' and value=='xz-bridge-status/1':continue
        if key in ('reason','terminal_reason') and value in REASONS:
            if key=='terminal_reason' or 'code' not in fact:fact['code']=value
        elif key=='state' and isinstance(value,str) and value in STATES:fact[key]=value
        elif key=='attempt' and type(value) is int and 0<=value<=1000000:fact[key]=value
        elif key in ('retryable','route_ready') and type(value) is bool:fact[key]=value
        else:omitted+=1
    return ([timed(fact,None,bounds,current=current)] if fact else []),omitted

def item(source, slot, facts=None, reason=None, truncated=False, omitted=0):
    facts=facts or []
    value={'item_id':secrets.token_hex(16),'source_id':source,'source_slot':slot,'parser_version':1,'capture':'at_collection',
           'clock_confidence':'current' if facts and all(f['clock_basis']=='current' for f in facts) else 'known' if facts and all(f['window_match']=='inside' for f in facts) else 'unknown',
           'selected':True,'facts':facts,'missing_reason':reason,'truncated':truncated,'omission_count':min(omitted,1000000)}
    while len(value['facts'])>512 or len(encoded(value))>LIMITS['item_bytes']:
        value['facts'].pop(0);value['omission_count']=min(value['omission_count']+1,1000000);value['truncated']=True
    return value

def failure(exc):
    if isinstance(exc,SourceError):return str(exc)
    return 'missing' if isinstance(exc,FileNotFoundError) else 'invalid'

def native_exports(stick, budget, bounds, category):
    base=Path(stick['root']);directory=base/'VJTOOLS/support-reports';candidates=[]
    try:
        checked_path(directory)
        with os.scandir(directory) as entries:
            for number,entry in enumerate(entries):
                budget.check()
                if number>=64:raise SourceError('budget')
                m=re.fullmatch(r'xz-support-report-([a-f0-9]{32})\.json',entry.name)
                if m and entry.is_file(follow_symlinks=False):
                    info=entry.stat(follow_symlinks=False)
                    candidates.append((abs(info.st_mtime-(bounds['epoch'] or time.time())),entry.name,m[1]))
        result=[]
        for _,name,ident in sorted(candidates)[:3]:
            raw,truncated=budget.read(directory/name,524288,root=base,expected=stick['identity'])
            data=json.loads(raw)
            if data.get('schema')!='xz-support-report/1' or data.get('origin')!='controller' or data.get('report_id')!=ident:
                raise SourceError('invalid')
            items=data.get('private',{}).get('items')
            if not isinstance(items,list) or len(items)>8:raise SourceError('invalid')
            labels=set()
            mapping={'bridge.json':'bridge_status','bridge.log':'bridge_history','bootstrap.log':'coldplug','runtime.log':'runtime_events'}
            permitted={'build.json','loader.json','network.json','app.log',*mapping}
            for exported in items:
                if not isinstance(exported,dict) or set(exported)!={'label','content','missing','truncated'} or exported['label'] not in permitted or exported['label'] in labels:
                    raise SourceError('invalid')
                labels.add(exported['label'])
                if type(exported['content']) is not str or len(exported['content'].encode())>131072 or type(exported['missing']) is not bool or type(exported['truncated']) is not bool:
                    raise SourceError('invalid')
                source=mapping.get(exported['label'])
                if not source or category not in REGISTRY[source]['categories']:continue
                if exported['missing']:result.append(item(source,stick['slot'],reason='missing'));continue
                content=exported['content'].encode()
                facts,omitted=bridge(content,bounds) if source=='bridge_status' else events(content,REGISTRY[source]['parser'],bounds)
                result.append(item(source,stick['slot'],facts,truncated=exported['truncated'],omitted=omitted))
        return result or [item('native_export',stick['slot'],reason='missing')]
    except (OSError,ValueError,KeyError,TypeError) as exc:
        # Partial validation of an export never grants authority to its other items.
        return [item('native_export',stick['slot'],reason=failure(exc))]

def diary(stick, budget, bounds):
    from .support_validation import validate
    spec=SOURCES['diary'];base=Path(stick['root']);directory=base/spec['directory']
    collected=[];omitted=0;truncated=False;seen=set();ranges=[]
    try:
        checked_path(directory)
    except FileNotFoundError:
        return item('usb_diary',stick['slot'],reason='missing'), {'state':'absent'}
    except (OSError,ValueError) as exc:
        return item('usb_diary',stick['slot'],reason=failure(exc)), {'state':'unavailable'}
    found=False;errors=False
    for name in spec['segments']:
        path=directory/name
        try:
            if checked_path(path).stat().st_size>spec['segment_max_bytes']:raise SourceError('oversized')
            header_raw,_=budget.read(path,spec['header_max_bytes'],root=base,expected=stick['identity'],head=True)
            if b'\n' not in header_raw:raise SourceError('invalid')
            header=json.loads(header_raw.split(b'\n',1)[0]);validate(header,spec['session_header'])
            if header['deck_view'] not in (2,4):raise SourceError('invalid')
            raw,clipped=budget.read(path,spec['collector_segment_tail_bytes'],root=base,expected=stick['identity'],tail=True)
            truncated|=clipped;found=True
            collected.append(timed({'component':'runtime','version':header['version'],'provenance':'native_export',
                                    'firmware':header['firmware'],'interface':header['interface'],'deck_view':header['deck_view']},None,bounds))
            first=last=None
            lines=raw.splitlines(keepends=True)
            for line in lines:
                if not line.endswith(b'\n') or len(line)>spec['line_max_bytes']:
                    omitted+=1;continue
                try:
                    entry=json.loads(line)
                    if entry==header:continue
                    kind=entry.get('type')
                    if kind not in ('event','metrics'):raise ValueError('Unknown diary line')
                    validate(entry,spec[kind+'_line'])
                    if kind=='metrics' and len(entry['sync_phase_error_ms'])!=4:raise ValueError('Four deck metrics required')
                    mono=entry['monotonic_ms']
                    if mono<header['monotonic_ms']:raise ValueError('Invalid session time')
                    key=(header['session_id'],entry['sequence'])
                    if key in seen:omitted+=1;continue
                    seen.add(key)
                    first=mono if first is None else min(first,mono);last=mono if last is None else max(last,mono)
                    epoch=header['epoch']+(mono-header['monotonic_ms'])/1000 if header['epoch'] is not None else None
                    clean={k:v for k,v in entry.items() if k not in ('type','monotonic_ms','sequence')}
                    output=timed(clean,epoch,bounds)
                    if output is None:omitted+=1
                    else:collected.append(output)
                except (ValueError,TypeError,AttributeError,KeyError):omitted+=1
            if first is not None:
                ranges.append((header['epoch'],header['monotonic_ms'],first,last))
        except FileNotFoundError:continue
        except (OSError,ValueError,TypeError,KeyError) as exc:
            omitted+=1;errors=True
            if isinstance(exc,SourceError) and str(exc) in ('budget','cancelled'):
                truncated=True;break
    if not found:return item('usb_diary',stick['slot'],reason='invalid' if errors else 'missing',omitted=omitted),{'state':'unavailable' if errors else 'absent'}
    unknown=[i for i,f in enumerate(collected) if f['clock_basis']=='unknown']
    drop=set(unknown[:-LIMITS['unknown_tail_events']]);omitted+=len(drop)
    result=item('usb_diary',stick['slot'],[f for i,f in enumerate(collected) if i not in drop],truncated=truncated,omitted=omitted)
    known=[(epoch+(first-start)/1000,epoch+(last-start)/1000) for epoch,start,first,last in ranges if epoch is not None and 1767225600<=epoch<4102444800]
    duration=min(604800,round(sum((last-first)/1000 for _,_,first,last in ranges)))
    start=min((v[0] for v in known),default=None);end=max((v[1] for v in known),default=None)
    relative=bool(start is not None and bounds['known'] and -604800<=start-bounds['epoch']<=604800 and -604800<=end-bounds['epoch']<=604800)
    result['coverage']={'duration_seconds':duration,'start_relative_seconds':round(start-bounds['epoch']) if relative else None,
                        'end_relative_seconds':round(end-bounds['epoch']) if relative else None,'clock_confidence':'known' if relative else 'unknown'}
    local={'state':'present','duration_seconds':duration,'start_utc':datetime.fromtimestamp(start,timezone.utc).isoformat() if start else None,
           'end_utc':datetime.fromtimestamp(end,timezone.utc).isoformat() if end else None,'truncated':truncated}
    return result,local

def collect(request, job):
    generation=request.get('scan_generation') or discover(job)['scan_generation']
    if not isinstance(generation,str) or not HEX.fullmatch(generation):raise ValueError('Invalid scan generation')
    from .jobs import exclusive_lock
    with exclusive_lock(store_root()/(generation+'.lock'),'This scan is already collecting'):
        return _collect({**request,'scan_generation':generation},job)

def _collect(request, job):
    generation=request['scan_generation']
    record=_record(generation)
    if 'snapshot' in record:
        return record['snapshot']  # Explicit new discovery is required to collect again.
    from .support_validation import validate
    hints=request.get('reporter_hints',{})
    validate(hints,CONTRACT['report_schema']['properties']['reporter_hints'])
    category=request.get('category',CONTRACT['problem_area_categories'].get(hints.get('problem_area'),'other'))
    if category not in CONTRACT['categories']:raise ValueError('Invalid category')
    hint=request.get('kind_hint')
    if hints.get('topic')=='NEW_IDEA':hint='FEATURE'
    elif hints.get('topic')=='HOW_DO_I' and hint is None:hint='QUESTION'
    if hint is not None and hint not in CONTRACT['kind_hints']:raise ValueError('Invalid kind hint')
    bounds=window(request);budget=Budget(job,LIMITS['scan_seconds']);items=[];unfinished=0;diary_coverage={}
    selected=request.get('source_ids',list(REGISTRY))
    if not isinstance(selected,list) or any(s not in REGISTRY for s in selected):raise ValueError('Unknown diagnostic source')
    handles=request.get('stick_handles',[s['handle'] for s in record['sticks']])
    if not isinstance(handles,list) or any(h not in [s['handle'] for s in record['sticks']] for h in handles):raise ValueError('Unknown USB handle')
    if hint=='FEATURE':selected=[]
    elif hint=='QUESTION' and request.get('include_diagnostics') is not True:
        selected=[s for s in selected if s in ('usb_settings','packaged_manifest','packaged_profile')]
    for stick in record['sticks']:
        if stick['handle'] not in handles:continue
        for source in ('usb_diary','usb_session','usb_settings','native_export'):
            if source not in selected or category not in REGISTRY[source]['categories']:continue
            if source=='usb_diary':
                collected,covered=diary(stick,budget,bounds);items.append(collected);diary_coverage[stick['slot']]=covered;continue
            if source=='native_export':
                items.extend(native_exports(stick,budget,bounds,category));continue
            try:
                spec=REGISTRY[source];base=Path(stick['root'])
                raw,truncated=budget.read(base/spec['path'],spec['max_read_bytes'],tail=source=='usb_session',root=base,expected=stick['identity'])
                if source=='usb_settings':
                    from . import usb_settings
                    resources=os.environ.get('XZ_BUILDER_RESOURCES')
                    if not resources:raise SourceError('no_matching_schema')
                    schema_raw,_=budget.read(Path(resources)/'runtime/settings-schema.json',131072)
                    manifest_raw,_=budget.read(Path(resources)/'runtime/manifest.json',131072)
                    if hashlib.sha256(schema_raw).hexdigest()!=json.loads(manifest_raw).get('settings_schema_sha256'):raise SourceError('no_matching_schema')
                    values=usb_settings.parse(raw,json.loads(schema_raw))
                    facts=[timed({'setting':k,'value':v,'valid':True},None,bounds,current=True) for k,v in values.items() if k in SOURCES['setting_bounds']]
                    omitted=len(values)-len(facts)
                else:facts,omitted=events(raw,spec['parser'],bounds)
                items.append(item(source,stick['slot'],facts,truncated=truncated,omitted=omitted))
            except (OSError,ValueError,KeyError,TypeError) as exc:
                reason=failure(exc);items.append(item(source,stick['slot'],reason=reason));unfinished+=reason in ('budget','cancelled')
    for source in ('app_events','loader_status','packaged_manifest','packaged_profile'):
        if source not in selected or category not in REGISTRY[source]['categories']:continue
        spec=REGISTRY[source];slot='app' if spec['location']=='app' else 'package'
        base=os.environ.get('XZ_BUILDER_DATA' if slot=='app' else 'XZ_BUILDER_RESOURCES')
        try:
            if not base:raise SourceError('missing')
            raw,truncated=budget.read(Path(base)/spec['path'],spec['max_read_bytes'],tail=source=='app_events')
            if source=='app_events':facts,omitted=events(raw,spec['parser'],bounds)
            elif source=='loader_status':facts,omitted=bridge(raw,bounds,current=True)
            else:
                data=json.loads(raw);facts=[];omitted=0
                if source=='packaged_profile':
                    manifest_raw,_=budget.read(Path(base)/'runtime/manifest.json',131072)
                    if hashlib.sha256(raw).hexdigest()!=json.loads(manifest_raw).get('quad_package_sha256'):raise SourceError('invalid')
                    data={k+'_sha256':v.get('sha256') for k,v in data.get('components',{}).items() if isinstance(v,dict)}
                for component in ('runtime','bridge','module'):
                    digest=data.get(component+'_sha256')
                    if type(digest) is str and re.fullmatch(r'[a-f0-9]{64}',digest):
                        facts.append(timed({'component':component,'sha256':digest,'provenance':'packaged_manifest'},None,bounds,current=True))
                version=data.get('app_version')
                if type(version) is str and len(version)<=64 and VERSION.fullmatch(version):
                    facts.append(timed({'component':'desktop','version':version,'provenance':'packaged_manifest'},None,bounds,current=True))
            items.append(item(source,slot,facts,truncated=truncated,omitted=omitted))
        except (OSError,ValueError,KeyError,TypeError) as exc:
            reason=failure(exc);items.append(item(source,slot,reason=reason));unfinished+=reason in ('budget','cancelled')
    omitted_items=max(0,len(items)-LIMITS['items'])+record['excess']
    items=items[:LIMITS['items']]
    while len(encoded(items))>LIMITS['diagnostic_bytes']:
        items.pop();omitted_items+=1
    snapshot={'scan_generation':generation,'items':items,'omitted_items':omitted_items,'unfinished_sources':unfinished,
              'diary_coverage':diary_coverage,'incident':{'before_seconds':bounds['before'],'after_seconds':bounds['after'],'clock_confidence':'known' if bounds['known'] else 'unknown'}}
    record['snapshot']=snapshot;_save(record)
    return snapshot

def diagnostic_event(component, result):
    """Fixed backend boundary facts, no exception text or request data."""
    if component not in ('desktop','loader') or result not in ('started','succeeded','failed','cancelled'):
        raise ValueError('Unknown diagnostic event')
    base=os.environ.get('XZ_BUILDER_DATA')
    if not base:return
    try:
        from .jobs import exclusive_lock
        path=checked_path(base)/'app.log'
        with exclusive_lock(Path(base)/'support-events.lock'):
            previous=b''
            if path.exists():
                with locked_file(path) as (file,info):
                    file.seek(max(0,info.st_size-32768));previous=file.read(32768)
                    if info.st_size>32768:previous=previous.partition(b'\n')[2]
            entry=encoded({'schema':'xz-app-event/1','component':component,'result':result,'epoch':time.time(),
                           'monotonic_ms':int(time.monotonic()*1000),'session':SESSION})+b'\n'
            temporary=Path(base)/('support-event-'+secrets.token_hex(8)+'.tmp')
            try:
                with temporary.open('xb') as file:file.write(previous+entry)
                os.replace(temporary,path)
            finally:temporary.unlink(missing_ok=True)
    except (OSError,ValueError,RuntimeError):
        pass  # Diagnostic storage cannot turn a completed USB operation into failure.
