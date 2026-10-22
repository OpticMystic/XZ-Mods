"""No device/network access: fabricated own ELF metadata, bytes and command replies."""
from pathlib import Path
import base64,hashlib,importlib.util,json,struct,sys,types,unittest
from unittest.mock import patch,Mock
ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('reader',ROOT.parent/'read_sync_observation.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
def blob():
 n=len(r.SCHEMA['counters']);v=[r.SCHEMA['magic'],1,r.SIZE,n,len(r.SCHEMA['records']),28,0,0]+list(range(n))
 for i in range(len(r.SCHEMA['records'])):v += [2,7]+[i+1]*27
 return struct.pack('<%dI'%len(v),*v)
def mutate(b,word,value):
 v=list(struct.unpack('<%dI'%(len(b)//4),b));v[word]=value;return struct.pack('<%dI'%len(v),*v)
class Fake:
 def __init__(self,raw=None):
  self.data=b'exact-own-runtime';self.raw=raw or blob();self.commands=[];self.replies={};self.start=222
  self.maps='10000000-10002000 r-xp 00000000 00:10 17 /dev/shm/libxz-mods.so\n10002000-10004000 rw-p 00002000 00:10 17 /dev/shm/libxz-mods.so'
  self.expected=dict(sha256=hashlib.sha256(self.data).hexdigest(),pid=31,start_ticks=self.start,mac='c8:3d:fc:0c:a8:3a',mapping=r.mapping_identity([x.split() for x in self.maps.splitlines()],0x10001000,r.SIZE))
 def command(self,c):
  c=c.removesuffix('; echo');self.commands.append(c)
  if c in self.replies:
   v=self.replies[c];return v() if callable(v) else v
  if c=='cat /sys/class/net/eth0/address':return self.expected['mac']
  if c=='pidof rbp':return '31'
  if c.endswith('/stat'):return '31 (rbp tricky) R '+'0 '*18+str(self.start)+' 0'
  if c.startswith('sha256sum'):return self.expected['sha256']+'  /dev/shm/libxz-mods.so'
  if c.endswith('/maps'):return self.maps
  if c.startswith('dd '):return base64.b64encode(self.raw).decode()
  raise AssertionError(c)
 def collect(self):return r.collect(self,self.data,0x1000,self.expected)
class ReaderTests(unittest.TestCase):
 def test_stable(self):
  f=Fake();x=f.collect();self.assertFalse(x['atomic_snapshot']);self.assertTrue(all(v['stable'] for v in x['records'].values()))
  reads=[c for c in f.commands if c.startswith('dd ')];self.assertEqual(len(reads),2);self.assertTrue(all(f'bs=1 skip=268439552 count={r.SIZE}' in c for c in reads))
 def test_counter_change_does_not_disqualify_record(self):
  x=r.decode(blob(),mutate(blob(),8,0xffffffff));self.assertTrue(x['records']['install']['stable']);self.assertEqual(x['counters']['record_busy'],0xffffffff)
 def test_session_and_request_marker(self):
  b=mutate(blob(),8+r.SCHEMA['counters'].index('resets'),7)
  names=list(r.SCHEMA['records']);start=8+len(r.SCHEMA['counters'])+names.index('soft3')*29
  field=r.SCHEMA['records']['soft3']['fields'].index('observed_request_seq')
  b=mutate(b,start+1+field,2);x=r.decode(b,b)
  self.assertTrue(x['records']['soft3']['current_session']);self.assertTrue(x['records']['soft3']['matching_request_observed']);self.assertFalse(x['records']['soft3']['request_application_proven'])
  q=8+len(r.SCHEMA['counters'])+names.index('request3')*29
  c=mutate(b,q,4);self.assertFalse(r.decode(c,c)['records']['soft3']['matching_request_observed'])
  c=mutate(b,8+r.SCHEMA['counters'].index('resets'),8);self.assertFalse(r.decode(b,c)['records']['soft3']['current_session'])
 def test_unstable_odd_empty_exhausted(self):
  start=8+len(r.SCHEMA['counters'])
  for value in [0,1,3,0xfffffffe]:
   with self.subTest(value=value):self.assertNotIn('fields',r.decode(mutate(blob(),start,value),mutate(blob(),start,value))['records']['install'])
  self.assertFalse(r.decode(blob(),mutate(blob(),start+4,91))['records']['install']['stable'])
  self.assertFalse(r.decode(blob(),mutate(blob(),start,4))['records']['install']['stable'])
 def test_header_and_short(self):
  for word in range(8):
   with self.subTest(word=word),self.assertRaises(ValueError):r.decode(blob(),mutate(blob(),word,0xdeadbeef))
  with self.assertRaises(ValueError):r.decode(blob()[:-1],blob())
 def test_identity_refusals(self):
  changes={'cat /sys/class/net/eth0/address':'00:00:00:00:00:00','pidof rbp':'31 32','cat /proc/31/stat':'31 (rbp) R '+'0 '*18+'223','sha256sum /dev/shm/libxz-mods.so':'bad  /dev/shm/libxz-mods.so','cat /proc/31/maps':'10000000-10002000 r-xp 00000000 00:10 18 /dev/shm/libxz-mods.so\n10002000-10004000 rw-p 00002000 00:10 18 /dev/shm/libxz-mods.so'}
  for c,v in changes.items():
   with self.subTest(command=c):
    f=Fake();f.replies[c]=v
    with self.assertRaises(ValueError):f.collect()
    self.assertFalse(any(x.startswith('dd ') for x in f.commands))
 def test_midread_identity_drift(self):
  for target in ['cat /proc/31/maps','cat /proc/31/stat','sha256sum /dev/shm/libxz-mods.so']:
   with self.subTest(target=target):
    f=Fake();original=f.command
    def changed(c):
     value=original(c)
     if c.startswith(target) and any(x.startswith('dd ') for x in f.commands):
      if target.endswith('maps'):return value.replace('00:10 17','00:10 18')
      if target.endswith('stat'):return value.replace('222','223')
      return 'bad '
     return value
    f.command=changed
    with self.assertRaises(ValueError):f.collect()
 def test_local_hash_start_mac(self):
  for key,value in [('sha256','0'*64),('pid',0),('start_ticks',0),('mac','c8:3d:fc:0c:a8:3b')]:
   f=Fake();f.expected[key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):f.collect()
   self.assertEqual(f.commands,[])
 def test_mapping_coverage(self):
  for rows in [[],[['10000000-10001004','---p','00000000','00:10','17','/dev/shm/libxz-mods.so']],[['10000000-10001004','r--p','00000000','00:10','17','/dev/shm/libxz-mods.so'],['10001008-10003000','rw-p','00001008','00:10','17','/dev/shm/libxz-mods.so']]]:
   with self.subTest(rows=rows),self.assertRaises(ValueError):r.mapping_identity(rows,0x10001000,r.SIZE)
 def test_same_address_coverage_identity(self):
  f=Fake();f.expected['mapping']['coverage'][0][3]='00:11'
  with self.assertRaises(ValueError):f.collect()
 def test_malformed_stat(self):
  for s in ['32 (rbp) R '+'0 '*30,'31 no-parens','31 (rbp) R 0']:
   with self.assertRaises(ValueError):r.process_start(s,31)
 def test_elf_gates(self):
  class Elf(dict):
   elfclass=32;little_endian=True
   def __init__(self,*a):super().__init__(e_machine='EM_ARM',e_type='ET_DYN')
   def get_section_by_name(self,n):return self
   def get_symbol_by_name(self,n):return [symbol]
   def iter_segments(self):return iter(segments)
  symbol={'st_value':4096,'st_size':r.SIZE,'st_info':{'type':'STT_OBJECT','bind':'STB_GLOBAL'},'st_other':{'visibility':'STV_DEFAULT'}}
  segments=[{'p_type':'PT_LOAD','p_vaddr':0,'p_memsz':16384}]
  with patch.dict(sys.modules,{'elftools':types.ModuleType('elftools'),'elftools.elf':types.ModuleType('elftools.elf'),'elftools.elf.elffile':types.SimpleNamespace(ELFFile=Elf)}):
   self.assertEqual(r.symbol_offset(b'own'),4096)
   for key,value in [('st_size',r.SIZE+4),('st_value',20000)]:
    old=symbol[key];symbol[key]=value
    with self.assertRaises(ValueError):r.symbol_offset(b'own')
    symbol[key]=old
   for obj,key,value in [(symbol['st_info'],'type','STT_FUNC'),(symbol['st_info'],'bind','STB_LOCAL'),(symbol['st_other'],'visibility','STV_HIDDEN')]:
    old=obj[key];obj[key]=value
    with self.assertRaises(ValueError):r.symbol_offset(b'own')
    obj[key]=old
 def test_incomplete_base64(self):
  f=Fake(blob()[:-4])
  with self.assertRaises(ValueError):f.collect()
 def test_cli_preflight_before_transport(self):
  for key,value in [('pid',0),('start_ticks',0),('mac','c8:3d:fc:0c:a8:3b')]:
   args=types.SimpleNamespace(output=Mock(),sha256='1'*64,pid=31,start_ticks=222,mac='c8:3d:fc:0c:a8:3a');args.output.exists.return_value=False;setattr(args,key,value)
   with patch.object(r.argparse.ArgumentParser,'parse_args',return_value=args),patch.object(r.importlib.util,'spec_from_file_location') as loader:
    with self.assertRaises(ValueError):r.main()
    loader.assert_not_called()
if __name__=='__main__':unittest.main(verbosity=2)
