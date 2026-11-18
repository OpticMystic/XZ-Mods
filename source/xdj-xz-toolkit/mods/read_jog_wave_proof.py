"""Read paired own Jog wave counters; independent observations, never MCU proof."""
import argparse,json,struct
from pathlib import Path
SIZE=96
NAMES=('service','invalid','busy','changed','absent','ok','encoded','published','span_reject','adopted','committed')
def decode(first,second):
 if len(first)!=SIZE or len(second)!=SIZE:raise ValueError('Incomplete waveform counter object')
 a=struct.unpack('<24I',first);b=struct.unpack('<24I',second)
 if a[:2]!=(1,SIZE) or b[:2]!=a[:2]:raise ValueError('Waveform counter ABI mismatch')
 return {'version':1,'size':SIZE,'atomic_snapshot':False,'evidence':'INDEPENDENT_COUNTERS_ONLY','mcu_acceptance_observed':False,
  'sides':[{'physical_side':i+1,'logical_deck':i+3,'first':dict(zip(NAMES,a[2+11*i:13+11*i])),
   'second':dict(zip(NAMES,b[2+11*i:13+11*i])),
   'delta_modulo_32':{n:(b[2+11*i+j]-a[2+11*i+j])&0xffffffff for j,n in enumerate(NAMES)}} for i in range(2)]}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('first',type=Path);p.add_argument('second',type=Path);a=p.parse_args();print(json.dumps(decode(a.first.read_bytes(),a.second.read_bytes()),indent=2))
