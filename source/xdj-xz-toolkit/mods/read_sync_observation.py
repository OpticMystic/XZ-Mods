"""Exact-own-runtime SYNC observations. Read-only; no native-state inference.
Records are historical material transitions. Counters are independent observations.
The caller must supply a freshly verified PID/start/MAC/full mapping identity.
"""
import argparse,base64,hashlib,importlib.util,io,json,re,struct
from datetime import datetime,timezone
from pathlib import Path
SCHEMA = {'symbol': 'xz_mods_sync_observation_v1', 'magic': 1398361905, 'version': 1, 'header': ['magic', 'version', 'size', 'counter_count', 'record_count', 'record_words', 'reserved0', 'reserved1'], 'counters': ['record_busy', 'record_exhausted', 'resets', 'stops', 'install_attempt', 'install_already', 'install_ok', 'install_failed', 'test_install', 'owner_tick', 'owner_disabled', 'owner_enabled', 'owner_busy', 'owner_locked', 'owner_no_demand', 'owner_demand', 'owner_no_clock', 'owner_throttle', 'owner_poll', 'native_publish', 'native_clear', 'native_unqualified', 'native_load_fence', 'native_qualified', 'cache_copy', 'cache_null', 'cache_unavailable', 'cache_no_revision', 'cache_stale', 'cache_ok', 'snapshot', 'snapshot_empty', 'request', 'request_accepted', 'request_refused', 'step', 'step_disabled', 'no_master', 'clock_bad', 'anchor_bad', 'lease_release', 'lease_lost', 'claim_ok', 'claim_fail', 'va_claim_fail', 'not_ready', 'occupied', 'previous_master_invalid', 'no_ticket', 'build_fail', 'offer_ok', 'offer_fail', 'master_pending', 'consumer', 'consumer_disabled', 'consumer_no_frame', 'consumer_original', 'status_read', 'status_scope', 'status_fallback', 'status_ok', 'status_refused', 'beat_read', 'beat_scope', 'beat_fallback', 'beat_ok', 'beat_refused', 'fresh_wrong_scope', 'fresh_dead', 'fresh_generation', 'fresh_clock_fail', 'fresh_future', 'fresh_expired', 'fresh_ok', 'expired_withdraw', 'foreign_status', 'foreign_beat', 'operator_external', 'operator_internal', 'operator_dummy', 'master_call', 'master_call_ok', 'master_call_fail', 'master_before', 'master_after', 'status_no_scope', 'status_wrong_manager', 'status_wrong_source', 'beat_no_scope', 'beat_wrong_manager', 'beat_wrong_source', 'plan_reason_0', 'plan_reason_1', 'plan_reason_2', 'plan_reason_3', 'plan_reason_4', 'plan_reason_5', 'plan_reason_6', 'plan_reason_7', 'plan_reason_8', 'plan_reason_9', 'plan_reason_10', 'plan_reason_11', 'native_reason_0', 'native_reason_1', 'native_reason_2', 'native_reason_3', 'native_reason_4', 'native_reason_5', 'native_reason_6', 'native_reason_7', 'native_reason_8', 'native_reason_9', 'cache_reason_0', 'cache_reason_1', 'cache_reason_2', 'cache_reason_3', 'cache_reason_4', 'cache_reason_5', 'cache_reason_6', 'cache_reason_7', 'cache_reason_8', 'cache_reason_9', 'soft3_reason_0', 'soft3_reason_1', 'soft3_reason_2', 'soft3_reason_3', 'soft3_reason_4', 'soft3_reason_5', 'soft3_reason_6', 'soft3_reason_7', 'soft3_reason_8', 'soft3_reason_9', 'soft3_reason_10', 'soft3_reason_11', 'soft3_reason_12', 'soft3_reason_13', 'soft4_reason_0', 'soft4_reason_1', 'soft4_reason_2', 'soft4_reason_3', 'soft4_reason_4', 'soft4_reason_5', 'soft4_reason_6', 'soft4_reason_7', 'soft4_reason_8', 'soft4_reason_9', 'soft4_reason_10', 'soft4_reason_11', 'soft4_reason_12', 'soft4_reason_13'], 'records': {'install': {'fields': ['session', 'stage', 'result', 'installed', 'mask'], 'keys': 5}, 'owner': {'fields': ['session', 'enabled', 'outcome', 'time_ms'], 'keys': 3}, 'native': {'fields': ['session', 'reason', 'available', 'phase_available', 'native_deck', 'master_id', 'source_revision', 'playing', 'manual', 'admitted', 'pending', 'qualification', 'captured_ms', 'captured_us_lo', 'captured_us_hi', 'uncertainty_us', 'observed_us_lo', 'observed_us_hi'], 'keys': 12}, 'cache': {'fields': ['session', 'result', 'reason', 'available', 'phase_available', 'native_deck', 'master_id', 'source_revision', 'playing', 'manual', 'captured_ms', 'age_ms', 'uncertainty_us', 'observed_us_lo', 'observed_us_hi'], 'keys': 10}, 'soft3': {'fields': ['session', 'requested', 'locked', 'reason', 'master_deck', 'master_scope', 'master', 'playing', 'loaded', 'timeline_flags', 'route_epoch', 'load_revision', 'transport_generation', 'media_lo', 'media_hi', 'observed_request_seq', 'phase_error_us', 'correction_ppm', 'tempo_x100', 'bpm_x100', 'timeline_at_ms', 'snapshot_revision', 'control_revision'], 'keys': 16}, 'soft4': {'fields': ['session', 'requested', 'locked', 'reason', 'master_deck', 'master_scope', 'master', 'playing', 'loaded', 'timeline_flags', 'route_epoch', 'load_revision', 'transport_generation', 'media_lo', 'media_hi', 'observed_request_seq', 'phase_error_us', 'correction_ppm', 'tempo_x100', 'bpm_x100', 'timeline_at_ms', 'snapshot_revision', 'control_revision'], 'keys': 16}, 'request3': {'fields': ['session', 'kind', 'deck', 'pressed', 'shift', 'held', 'accepted'], 'keys': 7}, 'request4': {'fields': ['session', 'kind', 'deck', 'pressed', 'shift', 'held', 'accepted'], 'keys': 7}, 'link': {'fields': ['session', 'outcome', 'selected', 'clock_result', 'anchor_ok', 'active', 'committed', 'source_id', 'observed_ready', 'observed_master', 'route_epoch', 'source_revision', 'playback_epoch', 'captured_us_lo', 'captured_us_hi', 'observed_us_lo', 'observed_us_hi', 'gap_lo', 'gap_hi'], 'keys': 13}, 'lease': {'fields': ['session', 'source_id', 'active', 'committed', 'observed_master', 'claim_master_epoch', 'observed_master_epoch', 'claim_foreign_epoch', 'observed_foreign_epoch', 'foreign_present', 'payload_still_ours'], 'keys': 11}, 'fresh': {'fields': ['session', 'outcome', 'mode', 'depth', 'source_id', 'generation', 'now_us_lo', 'now_us_hi', 'captured_us_lo', 'captured_us_hi'], 'keys': 6}, 'master_call': {'fields': ['session', 'requested_id', 'result', 'observed_request3_seq', 'observed_request4_seq'], 'keys': 5}, 'master_observed': {'fields': ['session', 'before_id', 'after_id', 'original_result'], 'keys': 4}, 'operator': {'fields': ['session', 'input_id', 'classification', 'result'], 'keys': 4}}, 'record_words': 28, 'size': 2260}
NATIVE_RAW_FORMAT = 0x52565731  # RVW1, three little-endian IEEE binary32 values
SIZE = SCHEMA["size"]

def native_raw_sample(row,words):
 # Stored values describe the retained event, never the latest native fader.
 out={'raw_values_present':False,'format':None,'scope':'retained_material_event','latest':False}
 if not row['stable']:out['missing_reason']='unstable_record';return out
 if words[19]!=NATIVE_RAW_FORMAT:out['missing_reason']='absent_or_unknown_format';return out
 out['format']='RVW1'
 if words[20]!=1:out['missing_reason']='raw_values_absent';return out
 import math
 out.update(raw_values_present=True,current_session=row['current_session'])
 names=('bpm','speed','bar_phase')
 for name,bits in zip(names,words[21:24]):
  value=struct.unpack('<f',struct.pack('<I',bits))[0];finite=math.isfinite(value)
  out[name+'_bits']=bits;out[name]=value if finite else None
  out[name+'_in_range']=finite and (0<=value<4 if name=='bar_phase' else value>0)
 fields=row['fields']
 out.update(source_available=bool(fields['available']),phase_available=bool(fields['phase_available']),
  captured_ms=fields['captured_ms'],
  captured_us=(fields['captured_us_hi']<<32)|fields['captured_us_lo'],
  observed_us=(fields['observed_us_hi']<<32)|fields['observed_us_lo'])
 return out
SYMBOL = SCHEMA["symbol"]

def decode(first,second):
 if len(first)!=SIZE or len(second)!=SIZE:raise ValueError('Incomplete observation object')
 a=struct.unpack('<%dI'%(SIZE//4),first);b=struct.unpack('<%dI'%(SIZE//4),second)
 expected=(SCHEMA['magic'],1,SIZE,len(SCHEMA['counters']),len(SCHEMA['records']),28,0,0)
 if a[:8]!=expected or b[:8]!=expected:raise ValueError('Observation ABI/version/size mismatch')
 n=len(SCHEMA['counters']);result={'atomic_snapshot':False,'counters':dict(zip(SCHEMA['counters'],b[8:8+n])),'records':{}}
 for index,(name,spec) in enumerate(SCHEMA['records'].items()):
  start=8+n+index*29;x,y=a[start:start+29],b[start:start+29]
  stable=x==y and y[0]!=0 and y[0]%2==0 and y[0]<0xfffffffd
  row={'stable':stable,'sequence':y[0],'historical_material_transition':True}
  if stable:
   row['fields']=dict(zip(spec['fields'],y[1:]))
   reset_index=8+SCHEMA['counters'].index('resets')
   row['current_session']=a[reset_index]==b[reset_index]==row['fields']['session']
   signed={'native_deck','master_id','master_deck','phase_error_us','correction_ppm','tempo_x100','bpm_x100','result','selected','observed_master','before_id','after_id','original_result','input_id','requested_id'}
   for field in signed & row['fields'].keys():
    value=row['fields'][field];row['fields'][field]=value-0x100000000 if value>=0x80000000 else value
  if name=='native':row['raw_native_sample']=native_raw_sample(row,y)
  result['records'][name]=row
 for side in (3,4):
  software=result['records']['soft'+str(side)];request=result['records']['request'+str(side)]
  if software['stable']:
   observed=software['fields']['observed_request_seq']
   software['matching_request_observed']=bool(observed and request['stable'] and observed==request['sequence'] and software['fields']['session']==request['fields']['session'])
   software['request_application_proven']=False
 return result

def process_start(stat,pid):
 end=stat.rfind(')')
 if not stat.startswith(str(pid)+' (') or end<0:raise ValueError('Malformed process identity')
 fields=stat[end+1:].split()
 if len(fields)<20 or not fields[19].isdecimal():raise ValueError('Missing process start time')
 return int(fields[19])

def mapping_identity(rows,address,size):
 owned=tuple(tuple(r) for r in rows if len(r)==6 and r[-1]=='/dev/shm/libxz-mods.so')
 if not owned:raise ValueError('Own runtime is not mapped')
 coverage=[];cursor=address
 for row in rows:
  if len(row)<5:raise ValueError('Malformed process mapping')
  lo,hi=(int(x,16) for x in row[0].split('-'))
  if lo<address+size and hi>address:
   if lo>cursor or hi<=lo or 'r' not in row[1]:raise ValueError('Unreadable or gapped object mapping')
   coverage.append(tuple(row));cursor=max(cursor,hi)
 if cursor<address+size:raise ValueError('Observation object is not completely mapped')
 return {'own':[list(r) for r in owned],'coverage':[list(r) for r in coverage]}

def symbol_offset(data):
 from elftools.elf.elffile import ELFFile
 elf=ELFFile(io.BytesIO(data))
 if elf.elfclass!=32 or not elf.little_endian or elf['e_machine']!='EM_ARM' or elf['e_type']!='ET_DYN':
  raise ValueError('Expected exact own ARM32 shared runtime')
 table=elf.get_section_by_name('.dynsym');symbols=table.get_symbol_by_name(SYMBOL) if table else None
 if not symbols or len(symbols)!=1:raise ValueError('No unique own exported observation symbol')
 sym=symbols[0]
 if sym['st_size']!=SIZE or sym['st_info']['type']!='STT_OBJECT' or sym['st_info']['bind']!='STB_GLOBAL' or sym['st_other']['visibility']!='STV_DEFAULT':
  raise ValueError('Unexpected observation symbol size/type/binding/visibility')
 offset=int(sym['st_value']);segments=[p for p in elf.iter_segments() if p['p_type']=='PT_LOAD']
 if not segments or min(p['p_vaddr'] for p in segments)!=0:raise ValueError('Unsupported runtime load origin')
 if not any(p['p_vaddr']<=offset and offset+SIZE<=p['p_vaddr']+p['p_memsz'] for p in segments):
  raise ValueError('Own observation is outside load segments')
 return offset

def collect(diagnostic,data,offset,expected):
 if hashlib.sha256(data).hexdigest()!=expected['sha256']:raise ValueError('Supplied own runtime SHA differs')
 if expected['pid']<1 or expected['start_ticks']<1 or expected['mac'].lower()!='c8:3d:fc:0c:a8:3a':
  raise ValueError('Explicit controller/PID/start binding required')
 def command(value):return diagnostic.command(value+'; echo').strip()
 def identity():
  if command('cat /sys/class/net/eth0/address').lower()!=expected['mac'].lower():raise ValueError('Controller MAC changed')
  if command('pidof rbp')!=str(expected['pid']):raise ValueError('Native PID changed')
  pid=expected['pid'];start=process_start(command(f'cat /proc/{pid}/stat'),pid)
  if start!=expected['start_ticks']:raise ValueError('Process start changed')
  if command('sha256sum /dev/shm/libxz-mods.so').split()[0]!=expected['sha256']:raise ValueError('Own RAM runtime SHA changed')
  rows=[r.split() for r in command(f'cat /proc/{pid}/maps').splitlines()]
  base=[r for r in rows if len(r)==6 and r[-1]=='/dev/shm/libxz-mods.so' and r[2]=='00000000']
  if len(base)!=1:raise ValueError('Expected one own zero-offset mapping')
  address=int(base[0][0].split('-')[0],16)+offset
  maps=mapping_identity(rows,address,SIZE)
  if maps!=expected['mapping']:raise ValueError('Own runtime or proof mapping identity changed')
  if process_start(command(f'cat /proc/{pid}/stat'),pid)!=start:raise ValueError('Process changed during identity read')
  return address,start,maps
 binding=identity();address=binding[0]
 if not 0<address<=0xffffffff-SIZE:raise ValueError('Own symbol address out of ARM32 bounds')
 def read():
  # Exactly the bounded own symbol, including no adjacent page contents.
  raw=command(f'dd if=/proc/{expected["pid"]}/mem bs=1 skip={address} count={SIZE} 2>/dev/null | base64')
  return base64.b64decode(''.join(raw.splitlines()),validate=True)
 first=read();second=read();result=decode(first,second)
 if identity()!=binding:raise ValueError('Identity changed during observation')
 result.update(runtime_sha256=expected['sha256'],pid=expected['pid'],process_start_ticks=binding[1],
  controller_mac=expected['mac'],mapping_identity=binding[2],symbol=SYMBOL,symbol_bytes=SIZE,
  captured_at=datetime.now(timezone.utc).isoformat(),controller_modified=False)
 return result

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--sha256',required=True)
 ap.add_argument('--pid',type=int,required=True);ap.add_argument('--start-ticks',type=int,required=True)
 ap.add_argument('--mac',required=True);ap.add_argument('--mapping',type=Path,required=True)
 ap.add_argument('--host',required=True);ap.add_argument('--source',required=True)
 ap.add_argument('--monitor',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
 a=ap.parse_args()
 if a.output.exists() or not re.fullmatch('[0-9a-f]{64}',a.sha256):raise ValueError('New output and exact lower-case SHA256 required')
 if a.pid<1 or a.start_ticks<1 or a.mac.lower()!='c8:3d:fc:0c:a8:3a':raise ValueError('Explicit controller/PID/start binding required')
 data=a.binary.read_bytes()
 if hashlib.sha256(data).hexdigest()!=a.sha256:raise ValueError('Local exact runtime SHA differs')
 offset=symbol_offset(data)
 expected=dict(sha256=a.sha256,pid=a.pid,start_ticks=a.start_ticks,mac=a.mac,mapping=json.loads(a.mapping.read_text()))
 spec=importlib.util.spec_from_file_location('existing_xz_monitor',a.monitor);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 d=module.Diagnostic(a.host,a.source)
 try:result=collect(d,data,offset,expected)
 finally:d.close()
 with a.output.open('x',encoding='utf-8',newline='\n') as f:json.dump(result,f,indent=2);f.write('\n')
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
