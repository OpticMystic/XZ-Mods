"""Bounded reader for the optional own-player scratch service proof.
Offline decode needs only Python stdlib. Live use belongs to the hardware owner.
Every exported block is checked independently; the object is never an atomic snapshot.
"""
import argparse,base64,hashlib,importlib.util,json,re,struct,time
from pathlib import Path
from datetime import datetime,timezone
SCHEMA = {'current': ['instance', 'session', 'attempt', 'at_ms', 'state', 'contact', 'vinyl', 'resume', 'route', 'generation', 'load_revision', 'source_rate', 'releasing', 'overflow', 'need_flush', 'controls', 'last_arrival', 'arrival_age', 'arrival_future', 'queue_depth', 'max_queue', 'trajectory', 'max_trajectory', 'pending', 'max_pending', 'target_lo', 'target_hi', 'heard_lo', 'heard_hi', 'submitted_lo', 'submitted_hi', 'completed_lo', 'completed_hi', 'worker_start', 'worker_gap', 'max_worker_gap', 'worker_cost', 'max_worker_cost', 'clock_future', 'position_seen', 'position_at', 'position_age', 'position_future', 'position_applied', 'requested_generation', 'applied_generation', 'source_valid', 'wire_submitted_lo', 'wire_submitted_hi', 'wire_completed_lo', 'wire_completed_hi', 'staged_lo', 'staged_hi', 'observed_completed_lo', 'observed_completed_hi', 'first_reason', 'first_at', 'input_calls', 'input_accepted', 'input_refused', 'input_latest_missed'], 'history': ['at_ms', 'event', 'session', 'attempt', 'state', 'contact', 'route', 'generation', 'trajectory', 'queue_depth', 'pending', 'worker_gap', 'worker_cost', 'position_age', 'position_applied', 'submit_result', 'transition_result', 'flush_result', 'position_result', 'resolver_result', 'reader_result', 'submitted_lo', 'submitted_hi', 'completed_lo', 'completed_hi'], 'input': ['at_ms', 'kind', 'pressed', 'delta', 'route', 'published_state', 'accepted', 'reason', 'queue_depth', 'release_owed', 'reserved_release'], 'counter': ['calls', 'accepted', 'refused', 'no_route', 'no_resolver', 'state_refused', 'route_changed', 'fifo_full', 'latest_missed', 'max_depth', 'accepted_touch', 'accepted_release', 'start_requests', 'stop_requests']}
SYMBOL='xz_mods_xp_service_proof_v1'
VERSION=1
HISTORY=16
OPS=['submit','transition','flush','position','resolver','reader']
SERVICE_FIELDS=['result_bits','calls','failures','failure_streak_calls','failure_since_ms','max_failure_ms','duration_ms','max_duration_ms']
CW=len(SCHEMA['current']);IW=len(SCHEMA['input']);HW=len(SCHEMA['history']);NW=len(SCHEMA['counter'])
SW=len(OPS)*8
LW=CW+SW+2+HISTORY*HW+NW+2*(IW+1)
HEADER=48
DECK_BYTES=NW*4+2*(IW+1)*4+(CW+SW+1)*4+HISTORY*(HW+1)*4+2*(LW+1)*4
SIZE=HEADER+2*DECK_BYTES
EXPECTED=(VERSION,SIZE,HEADER,DECK_BYTES,CW,IW,HW,LW,HISTORY,SW,NW,2)
UNKNOWN=0xffffffff


def block(a,b,offset,words,names=None):
    left=a[offset:offset+4*(words+1)];right=b[offset:offset+4*(words+1)]
    x=struct.unpack('<'+str(words+1)+'I',left);y=struct.unpack('<'+str(words+1)+'I',right)
    if not x[0] and not y[0]:return {'status':'unpublished'}
    if x[0]!=y[0] or y[0]&1 or left!=right:return {'status':'unstable'}
    values=list(y[1:])
    return {'status':'stable','sequence':y[0],'data':dict(zip(names,values)) if names else values}


def services(values):
    return {name:dict(zip(SERVICE_FIELDS,values[i*8:i*8+8])) for i,name in enumerate(OPS)}


def current(values):
    out=dict(zip(SCHEMA['current'],values[:CW]))
    out['services']=services(values[CW:CW+SW])
    for key in list(out):
        if key.endswith('_lo'):
            prefix=key[:-3];out[prefix]=out[key]+(out[prefix+'_hi']<<32)
    return out


def latch(values):
    out=current(values)
    count,nxt=values[CW+SW:CW+SW+2]
    if count>HISTORY or nxt>=HISTORY:return {'status':'unsupported','reason':'invalid history indices'}
    out['history_count']=count;out['history_next']=nxt
    out['target_source_frames']=out['target']/(1<<32)
    data=values[CW+SW+2:]
    out['pretrigger']=[dict(zip(SCHEMA['history'],data[i*HW:(i+1)*HW])) for i in [(nxt-count+j)%HISTORY for j in range(count)]]
    offset=CW+SW+2+HISTORY*HW
    out['input_counters']=dict(zip(SCHEMA['counter'],values[offset:offset+NW]));offset+=NW
    out['input_counters_atomic_snapshot']=False
    for name in ('input_latest','input_last_refusal'):
        status=values[offset];offset+=1
        out[name]={'status':{0:'unpublished',1:'stable',2:'unstable'}.get(status,'unsupported')}
        if status==1:out[name]['data']=dict(zip(SCHEMA['input'],values[offset:offset+IW]))
        offset+=IW
    # These are observations, never a claim about the controller's physical cause.
    out['evidence']=[name for name in ('submit','transition','position') if out['services'][name]['failures']]
    out['incident_retained']=bool(out['first_reason'])
    return out


def decode(a,b):
    if len(a)!=SIZE or len(b)!=SIZE:return {'status':'incomplete','atomic_snapshot':False,'expected_bytes':SIZE}
    x=struct.unpack_from('<12I',a);y=struct.unpack_from('<12I',b)
    if x!=y:return {'status':'unstable','atomic_snapshot':False,'reason':'changing ABI header'}
    if y!=EXPECTED:return {'status':'unsupported','atomic_snapshot':False,'header':y}
    decks=[]
    for side in range(2):
        offset=HEADER+side*DECK_BYTES
        ca=struct.unpack_from('<'+str(NW)+'I',a,offset);cb=struct.unpack_from('<'+str(NW)+'I',b,offset)
        row={'deck':side+3,'counters':dict(zip(SCHEMA['counter'],cb)),'counters_changed':ca!=cb,'counters_atomic_snapshot':False}
        offset+=NW*4
        for name in ('latest','last_refusal'):
            row[name]=block(a,b,offset,IW,SCHEMA['input']);offset+=(IW+1)*4
        row['current']=block(a,b,offset,CW+SW);offset+=(CW+SW+1)*4
        if row['current']['status']=='stable':row['current']['data']=current(row['current']['data'])
        row['history']=[]
        for i in range(HISTORY):
            row['history'].append(block(a,b,offset,HW,SCHEMA['history']));offset+=(HW+1)*4
        for name in ('first','previous'):
            row[name]=block(a,b,offset,LW);offset+=(LW+1)*4
            if row[name]['status']=='stable':row[name]['data']=latch(row[name]['data'])
        decks.append(row)
    return {'status':'supported','atomic_snapshot':False,'version':VERSION,'bytes':SIZE,'decks':decks}


def process_start(stat,pid):
    end=stat.rfind(')');fields=stat[end+1:].split()
    if not stat.startswith(str(pid)+' (') or end<0 or len(fields)<20 or not fields[19].isdecimal():raise ValueError('Malformed process identity')
    return int(fields[19])


def mapping_identity(rows,address,size):
    if address<0 or size<1 or address+size>1<<32:raise ValueError('Mapping range overflow')
    parsed=[];owned=[]
    for row in rows:
        if len(row)<5 or len(row)>6:raise ValueError('Malformed mapping')
        start,end=(int(x,16) for x in row[0].split('-'))
        if start>=end:raise ValueError('Malformed mapping range')
        if len(row)==6 and row[5]=='/dev/shm/libxz-mods.so':owned.append(tuple(row))
        if start<address+size and end>address:
            if 'r' not in row[1] or (len(row)==6 and row[5]!='/dev/shm/libxz-mods.so'):raise ValueError('Proof mapping unreadable or foreign')
            parsed.append((start,end,tuple(row)))
    cursor=address
    for start,end,row in sorted(parsed):
        if start>cursor:raise ValueError('Proof mapping has a gap')
        cursor=max(cursor,end)
    if not owned or cursor<address+size:raise ValueError('Proof object is not fully mapped')
    return {'runtime':sorted(owned),'proof':[row for start,end,row in sorted(parsed)]}


def mapping_sha(identity):
    return hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def own_symbol(binary):
    # Parse only the supplied own ARM ELF and this exported object. No private offsets.
    if not 52<=len(binary)<=64*1024*1024 or binary[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('Expected own ARM32 little-endian ELF')
    h=struct.unpack_from('<HHIIIIIHHHHHH',binary,16)
    et,machine,version,entry,phoff,shoff,flags,ehsize,phsize,phnum,shsize,shnum,shstr=h
    if et!=3 or machine!=40 or ehsize!=52 or phsize!=32 or shsize!=40 or not 1<=phnum<=64 or not 1<=shnum<=512:raise ValueError('Unsupported own runtime ELF')
    def read(fmt,offset):
        n=struct.calcsize(fmt)
        if offset<0 or offset+n>len(binary):raise ValueError('Truncated own ELF')
        return struct.unpack_from(fmt,binary,offset)
    loads=[read('<8I',phoff+i*phsize) for i in range(phnum)]
    loads=[p for p in loads if p[0]==1]
    if not any(p[1]==0 and p[2]==0 for p in loads):raise ValueError('Unsupported runtime load bias')
    sections=[read('<10I',shoff+i*shsize) for i in range(shnum)]
    found=[]
    for sec in sections:
        if sec[1]!=11:continue
        if sec[9]!=16 or sec[5]%16 or sec[5]>8*1024*1024 or sec[6]>=shnum:raise ValueError('Invalid own dynsym')
        names=sections[sec[6]]
        if names[4]+names[5]>len(binary):raise ValueError('Truncated symbol strings')
        strings=binary[names[4]:names[4]+names[5]]
        for i in range(sec[5]//16):
            name,value,size,info,other,shndx=read('<IIIBBH',sec[4]+i*16)
            if name>=len(strings):raise ValueError('Invalid symbol name')
            end=strings.find(b'\0',name)
            if end<0:raise ValueError('Unterminated symbol name')
            if strings[name:end]==SYMBOL.encode():
                if size!=SIZE or info&15!=1 or info>>4!=1 or other&3 or not shndx:raise ValueError('Own proof ABI is not exported')
                if not any(p[2]<=value and value+SIZE<=p[2]+p[5] and p[6]&4 for p in loads):raise ValueError('Own proof outside readable load segments')
                found.append(value)
    if len(found)!=1:raise ValueError('Missing or duplicate own service proof symbol')
    return found[0]


def validated_identity(command,binary,offset,pid,start,mac,maps_hash):
    if command('cat /sys/class/net/eth0/address').lower()!=mac:raise ValueError('Controller MAC changed')
    if command('pidof rbp')!=str(pid):raise ValueError('Native PID changed')
    if process_start(command(f'cat /proc/{pid}/stat'),pid)!=start:raise ValueError('Process start changed')
    fields=command('sha256sum /dev/shm/libxz-mods.so').split()
    if not fields or fields[0]!=hashlib.sha256(binary).hexdigest():raise ValueError('Runtime SHA256 differs')
    rows=[x.split() for x in command(f'cat /proc/{pid}/maps').splitlines()]
    roots=[x for x in rows if len(x)==6 and x[5]=='/dev/shm/libxz-mods.so' and x[2]=='00000000']
    if len(roots)!=1:raise ValueError('Expected one own runtime base mapping')
    address=int(roots[0][0].split('-')[0],16)+offset
    identity=mapping_identity(rows,address,SIZE)
    if mapping_sha(identity)!=maps_hash:raise ValueError('Supplied mapping identity differs')
    if process_start(command(f'cat /proc/{pid}/stat'),pid)!=start:raise ValueError('Process changed during identity read')
    return address,identity


def capture(command,binary,offset,pid,start,mac,maps_hash):
    began=time.monotonic()
    raw_command=command
    def command(value):
        if time.monotonic()-began>30:raise TimeoutError('Capture budget exhausted before command')
        result=raw_command(value)
        if time.monotonic()-began>30:raise TimeoutError('Capture budget exhausted during command')
        return result
    binding=validated_identity(command,binary,offset,pid,start,mac,maps_hash)
    address=binding[0];page=address//4096;count=(address%4096+SIZE+4095)//4096
    def read():
        raw=command(f'dd if=/proc/{pid}/mem bs=4096 skip={page} count={count} 2>/dev/null | base64')
        if len(raw)>count*4096*2:raise ValueError('Oversized memory response')
        data=base64.b64decode(''.join(raw.splitlines()),validate=True)
        if len(data)!=count*4096:raise ValueError('Incomplete memory read')
        return data[address%4096:address%4096+SIZE]
    report=decode(read(),read())
    if validated_identity(command,binary,offset,pid,start,mac,maps_hash)!=binding:raise ValueError('Binding changed during proof read')
    if time.monotonic()-began>30:raise ValueError('Observation exceeded bounded capture window')
    report.update(pid=pid,process_start_ticks=start,mac=mac,runtime_sha256=hashlib.sha256(binary).hexdigest(),mapping_identity=binding[1],controller_modified=False)
    return report


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('host','source','runtime-sha256','mac','mapping-sha256'):ap.add_argument('--'+name,required=True)
    for name in ('pid','start-ticks'):ap.add_argument('--'+name,type=int,required=True)
    for name in ('binary','monitor','output'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args()
    if a.pid<1 or a.start_ticks<1 or a.output.exists():raise ValueError('Fresh output and explicit positive PID/start required')
    if not re.fullmatch('[0-9a-f]{64}',a.runtime_sha256) or not re.fullmatch('[0-9a-f]{64}',a.mapping_sha256) or not re.fullmatch('(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',a.mac):raise ValueError('Explicit SHA256/MAC identities required')
    if a.binary.stat().st_size>64*1024*1024:raise ValueError('Runtime too large')
    binary=a.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest()!=a.runtime_sha256:raise ValueError('Supplied runtime hash differs from local bytes')
    offset=own_symbol(binary)
    spec=importlib.util.spec_from_file_location('xz_service_monitor',a.monitor);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    diagnostic=module.Diagnostic(a.host,a.source)
    try:
        def command(value):return diagnostic.command(value+'; echo').strip()
        report=capture(command,binary,offset,a.pid,a.start_ticks,a.mac,a.mapping_sha256)
    finally:diagnostic.close()
    report['captured_at']=datetime.now(timezone.utc).isoformat()
    with a.output.open('x',encoding='utf-8',newline='\n') as f:json.dump(report,f,indent=2);f.write('\n')
    print(report['status'])

if __name__=='__main__':main()
