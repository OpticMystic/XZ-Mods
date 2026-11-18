"""Fixed controller report files. Keys travel only through the native host's pipe."""
from contextlib import contextmanager, ExitStack
import ctypes
from datetime import datetime, timezone
import hashlib
from itertools import islice
import json
import os
from pathlib import Path
import re
import secrets
import stat

from .support_contract import CONTRACT
from .support_sources import Budget, checked_path, identity, locked_file, SourceError
from .support_validation import validate, validate_report
from .jobs import exclusive_lock

QUEUE = CONTRACT['controller_queue']
IDENTITY = QUEUE['identity_path']
PENDING = QUEUE['pending_path']
SENT = QUEUE['sent_path']
NAME = re.compile(QUEUE['file_pattern'])

def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:raise ValueError('Duplicate controller field')
        result[key] = value
    return result

def decode(raw):
    return json.loads(raw.decode('utf-8'), object_pairs_hook=_object,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Invalid controller number')))

def validate_pending(report):
    validate(report, CONTRACT['pending_report_schema'])
    validate_report(report)
    if len(report['description']) > QUEUE['typed_characters']:raise ValueError('Controller text exceeds limit')
    if (report['loader_id'] is None) != (report['installation_id'] is None):raise ValueError('Incomplete controller identity')
    screen = report['screen_state']
    if screen['surface'] != 'MODS' and screen['mods_page'] != 'UNKNOWN':raise ValueError('Inconsistent screen page')
    if screen['surface'] != 'STOCK' and screen['stock_page'] != 'UNKNOWN':raise ValueError('Inconsistent screen page')
    return report

def read_identity(volume):
    path = Path(volume) / IDENTITY
    try:
        with locked_file(path) as (file, info):
            if info.st_size > QUEUE['identity_bytes'] or info.st_nlink != 1:raise ValueError('Invalid loader identity')
            value = decode(file.read(QUEUE['identity_bytes'] + 1))
        validate(value, CONTRACT['loader_identity_schema'])
        return value
    except FileNotFoundError:
        return None

@contextmanager
def pinned_directories(*paths):
    """Deny Windows directory rename while committing fixed USB metadata."""
    with ExitStack() as stack:
        observed = {}
        for path in paths:
            for parent in reversed((Path(path), *Path(path).parents)):
                if parent in observed:continue
                observed[parent] = identity(parent)
                if os.name == 'nt':
                    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
                    create = kernel.CreateFileW
                    create.argtypes = [ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
                    create.restype = ctypes.c_void_p
                    close = kernel.CloseHandle
                    close.argtypes = [ctypes.c_void_p]
                    handle = create(str(parent),0x80,3,None,3,0x02200000,None)
                    if handle == ctypes.c_void_p(-1).value:raise SourceError('changed')
                    stack.callback(close,handle)
                    if identity(parent) != observed[parent]:raise SourceError('changed')
        yield
        if any(identity(path) != expected for path,expected in observed.items()):raise SourceError('changed')

def _mkdir(path):
    path = Path(path)
    if path.exists():checked_path(path);return
    if not path.parent.exists():_mkdir(path.parent)
    with pinned_directories(path.parent):
        path.mkdir(exist_ok=True)
        checked_path(path)

def write_identity(volume, expected_volume, value, previous):
    """Called only by an authorized loader build/update, after loader readback."""
    validate(value, CONTRACT['loader_identity_schema'])
    volume = checked_path(volume)
    if identity(volume) != expected_volume:raise SourceError('changed')
    path = volume / IDENTITY
    _mkdir(path.parent)
    lock = path.parent / '.loader-identity.lock'
    if lock.exists():
        checked_path(lock)
        if not lock.is_file() or lock.stat().st_nlink != 1 or lock.stat().st_size > 1:raise SourceError('unsafe_path')
    with pinned_directories(path.parent), exclusive_lock(lock):
        if identity(volume) != expected_volume or read_identity(volume) != previous:raise SourceError('changed')
        raw = (json.dumps(value,separators=(',',':'))+'\n').encode()
        temporary = path.parent / ('.loader-identity-'+secrets.token_hex(16)+'.tmp')
        try:
            with temporary.open('xb') as file:
                file.write(raw);file.flush();os.fsync(file.fileno())
            if identity(volume) != expected_volume or read_identity(volume) != previous:raise SourceError('changed')
            os.replace(temporary,path)
            if read_identity(volume) != value:raise SourceError('changed')
        finally:
            temporary.unlink(missing_ok=True)
    return {'loader_id':value['loader_id'],'installation_id':value['installation_id']}

def prepare_loader_request(request):
    """Validate native metadata before a potentially long loader build starts."""
    value = request.get('_loader_identity')
    if value is None:return None
    validate(value,CONTRACT['loader_identity_schema'])
    volume = checked_path(request['volume'])
    if request.get('_loader_volume_identity') != identity(volume):raise SourceError('changed')
    previous = read_identity(volume)
    expected = request.get('_previous_loader_identity')
    if previous != expected:raise SourceError('changed')
    return (volume,identity(volume),value,previous)

def finish_loader_request(prepared):
    return write_identity(*prepared) if prepared is not None else None

def scan(job, roots=None):
    """At most four roots, 64 directory entries, 32 files and 2 MiB per tick."""
    from . import managed_usb
    candidates = managed_usb.support_candidates() if roots is None else roots
    budget = Budget(job)
    budget.remaining = QUEUE['scan_bytes']
    reports, loaders, omitted = [], [], 0
    inspected = 0
    for candidate in islice(iter(candidates),26):
        volume = Path(candidate['volume'])
        try:
            budget.check()
            observed = identity(volume)
            if not (volume/IDENTITY).exists() and not (volume/PENDING).exists():continue
            if inspected >= 4:omitted += 1;continue
            inspected += 1
            try:
                loader = read_identity(volume)
                if loader is not None:loaders.append({'volume':str(volume),'identity':observed,'loader_identity':loader})
            except (OSError,ValueError):omitted += 1
            folder = checked_path(volume / PENDING)
            with os.scandir(folder) as entries:
                names = [entry.name for entry in islice(entries, QUEUE['scan_entries'])]
            for name in sorted(names):
                budget.check()
                if not NAME.fullmatch(name):continue
                if len(reports) >= QUEUE['scan_files']:return {'reports':reports,'loaders':loaders,'omitted':omitted+1}
                try:
                    raw,_ = budget.read(folder/name,CONTRACT['limits']['report_bytes'],root=volume,expected=observed)
                    report = validate_pending(decode(raw))
                    if report['report_id'] != name[:-5]:raise ValueError('Controller filename mismatch')
                    reports.append({'volume':str(volume),'identity':observed,'report_id':report['report_id'],
                                    'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw)})
                except (OSError,ValueError):omitted += 1
        except (OSError,ValueError):continue
    return {'reports':reports,'loaders':loaders,'omitted':omitted}

def read_pending(volume, expected, report_id, digest, job):
    if not isinstance(report_id,str) or not NAME.fullmatch(report_id+'.json'):raise ValueError('Invalid controller report ID')
    budget = Budget(job)
    raw,_ = budget.read(Path(volume)/PENDING/(report_id+'.json'),CONTRACT['limits']['report_bytes'],root=volume,expected=expected)
    report = validate_pending(decode(raw))
    if report['report_id'] != report_id or hashlib.sha256(raw).hexdigest() != digest:raise SourceError('changed')
    return raw

def mark_sent(volume, expected, report_id, digest, acceptance, job):
    """Native host supplies verified Worker acceptance; recheck exact source bytes."""
    from .support_contract import parse_report_reference, format_report_reference
    ref = acceptance.get('report_reference')
    parsed = parse_report_reference(ref)
    if acceptance.get('schema') != 'xz-controller-accepted/1' or acceptance.get('report_id') != report_id or acceptance.get('payload_sha256') != digest or parsed is None or format_report_reference(parsed) != ref:
        raise ValueError('Worker acceptance does not match pending report')
    raw = read_pending(volume,expected,report_id,digest,job)
    source = Path(volume)/PENDING/(report_id+'.json')
    destination = Path(volume)/SENT/(report_id+'.json')
    _mkdir(destination.parent)
    with pinned_directories(source.parent,destination.parent):
        if identity(volume) != expected:raise SourceError('changed')
        if os.name == 'nt':
            _move_accepted_windows(source,destination,raw)
            return {'report_id':report_id,'sent':True}
        # Windows rename fails if a destination already exists. Never replace sent evidence.
        if destination.exists():
            with locked_file(destination) as (file,info):
                if info.st_size != len(raw) or info.st_nlink != 1 or file.read(len(raw)+1) != raw:raise SourceError('changed')
            if read_pending(volume,expected,report_id,digest,job) != raw:raise SourceError('changed')
            source.unlink()
        else:
            if read_pending(volume,expected,report_id,digest,job) != raw:raise SourceError('changed')
            source.rename(destination)
    return {'report_id':report_id,'sent':True}

def _move_accepted_windows(source,destination,raw):
    """Rename the verified file handle; deny file writes and replacement until close."""
    import msvcrt
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    create=kernel.CreateFileW
    create.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
    create.restype=ctypes.c_void_p
    close=kernel.CloseHandle;close.argtypes=[ctypes.c_void_p]
    handle=create(str(checked_path(source)),0x80010000,0,None,3,0x00200000,None)
    if handle==ctypes.c_void_p(-1).value:raise SourceError('changed')
    try:fd=msvcrt.open_osfhandle(handle,os.O_RDONLY|os.O_BINARY)
    except BaseException:close(handle);raise
    with os.fdopen(fd,'rb') as file:
        info=os.fstat(file.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or info.st_size!=len(raw) or file.read(len(raw)+1)!=raw:raise SourceError('changed')
        set_info=kernel.SetFileInformationByHandle
        set_info.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32]
        set_info.restype=ctypes.c_int
        if destination.exists():
            with locked_file(destination) as (sent,sent_info):
                if sent_info.st_size!=len(raw) or sent_info.st_nlink!=1 or sent.read(len(raw)+1)!=raw:raise SourceError('changed')
            delete=ctypes.c_ubyte(1)
            if not set_info(handle,4,ctypes.byref(delete),ctypes.sizeof(delete)):raise SourceError('changed')
        else:
            encoded=str(destination).encode('utf-16-le')
            class Rename(ctypes.Structure):
                _fields_=[('replace',ctypes.c_ubyte),('root',ctypes.c_void_p),('length',ctypes.c_uint32),('name',ctypes.c_ushort*(len(encoded)//2+1))]
            rename=Rename();rename.length=len(encoded)
            ctypes.memmove(ctypes.addressof(rename)+Rename.name.offset,encoded,len(encoded))
            if not set_info(handle,3,ctypes.byref(rename),ctypes.sizeof(rename)):raise SourceError('changed')

def dispatch(request,job):
    method = request['method']
    if method == 'controller_scan':return scan(job)
    if method == 'controller_identity':
        volume = checked_path(request['volume'])
        return {'identity':identity(volume),'loader_identity':read_identity(volume)}
    if method == 'controller_read':
        raw = read_pending(request['volume'],request['identity'],request['report_id'],request['sha256'],job)
        return {'body':raw.decode('utf-8')}
    if method == 'controller_sent':
        return mark_sent(request['volume'],request['identity'],request['report_id'],request['sha256'],request['acceptance'],job)
    raise ValueError('Unknown controller queue operation')
