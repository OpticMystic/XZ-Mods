"""Inspect a frozen native audio-chain capture without treating silence as a pass."""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
STAGES=('decode','stems','stretch','key')
def inspect(path):
    data=path.read_bytes()
    version,enabled,frames,records,record_bytes,ring_bytes=struct.unpack_from('<6I',data)
    if version!=2 or enabled or record_bytes!=188 or len(data)!=24+8*ring_bytes:
        raise ValueError('Expected complete frozen trace version 2 with 188-byte records')
    if ring_bytes!=16+records*record_bytes+frames*8:raise ValueError('Trace sizes disagree')
    result={'capture':str(path),'frozen':True,'clock':'CLOCK_MONOTONIC',
        'timing_note':'Instrumented region durations exclude final record copy and callback tail; cannot prove full callback deadlines.',
        'history_seconds_at_44100':frames/44100,'streams':[]}
    for deck in range(2):
        for stage in range(4):
            base=24+(deck*4+stage)*ring_bytes
            busy,dropped,count,cursor=struct.unpack_from('<4I',data,base)
            if busy:raise ValueError('Producer was still writing at freeze')
            pcm=np.frombuffer(data,dtype='<f4',count=frames*2,offset=base+16+records*record_bytes).reshape(-1,2)
            items=[];events=[];overwritten=0
            for i in range(max(0,count-records),count):
                at=base+16+(i%records)*record_bytes;words=struct.unpack_from('<47I',data,at)
                start,saved=words[3:5];age=(cursor-start)&0xffffffff
                if not saved:
                    events.append({'time_us':words[0]+(words[1]<<32),'tid':words[2],'flags':words[8],
                        'frames':words[7],'epoch':words[5],'rate':words[6],'instrumented_region_us':words[9],
                        'position':struct.unpack('<i',struct.pack('<I',words[12]))[0],
                        'return':struct.unpack('<i',struct.pack('<I',words[13]))[0],
                        'reader':hex(words[28]),'manager':hex(words[29]),'aux':words[10:12]})
                    continue
                if saved>frames or age>frames or age<saved:overwritten+=1;continue
                samples=pcm[(np.arange(saved)+start)%frames]
                finite=np.isfinite(samples);clean=np.where(finite,samples,0)
                levels=struct.unpack_from('<3f',data,at+15*4)
                deadline=words[7]*1000000/words[6] if words[6] else None
                prepared=words[18:28]
                signed=lambda w:struct.unpack('<i',struct.pack('<I',w))[0]
                items.append({'time_us':words[0]+(words[1]<<32),'tid':words[2],
                    'epoch':words[5],'rate':words[6],'frames':words[7],'saved':saved,'flags':words[8],
                    'instrumented_region_us':words[9],'instrumented_prefix_us':words[30],'block_audio_period_us':deadline,
                    'prefix_exceeds_audio_period':deadline is not None and words[30]>deadline,
                    'aux':words[10:12],'position':signed(words[12]),'return':signed(words[13]),'cents':signed(words[14]),
                    'levels':levels,'prepared_masks':prepared[0],'misses':prepared[1],
                    'prepared_state':prepared[2]&65535,'starved':prepared[2]>>16,
                    'window':signed(prepared[3]),'scale':struct.unpack('<d',struct.pack('<2I',*prepared[4:6]))[0],
                    'offset':struct.unpack('<d',struct.pack('<2I',*prepared[6:8]))[0],
                    'window_first':struct.unpack('<q',struct.pack('<2I',*prepared[8:10]))[0],
                    'reader':hex(words[28]),'manager':hex(words[29]),
                    'mix':{'requested_masks':words[31],'applied_masks':words[32],'previous_masks':words[33],
                        'flags':words[34],'chosen_slot':signed(words[35]),'window_masks':words[36],
                        'window_first':struct.unpack('<q',struct.pack('<2I',*words[37:39]))[0],
                        'source_gain':struct.unpack('<f',struct.pack('<I',words[39]))[0],
                        'component_gains':struct.unpack('<2f',struct.pack('<2I',*words[40:42])),
                        'previous_source_gain':struct.unpack('<f',struct.pack('<I',words[42]))[0],
                        'clipped_samples':words[43],'nonfinite_before_clip':words[44],
                        'peak_before_clip':struct.unpack('<f',struct.pack('<I',words[45]))[0],'previously_starved':words[46]},
                    'peak':float(np.max(np.abs(clean))), 'rms':float(np.sqrt(np.mean(clean.astype(np.float64)**2))),
                    'nonfinite':int(np.sum(~finite)), 'zero_samples':int(np.sum(clean==0)),
                    'at_or_above_unity':int(np.sum(np.abs(clean)>=.99999))})
            result['streams'].append({'deck':deck+1,'stage':STAGES[stage],'dropped':dropped,
                'blocks_total':count,'captured_blocks':len(items),'blocks':items,'events':events,'overwritten_blocks':overwritten,
                'source_sampling_stride':67 if any(x['flags']&0x10000 for x in items) else 1,
                'status':'RECORDED' if items else 'NO EVIDENCE'})
    result['audio_quality_verdict']='INCONCLUSIVE: locate the first divergent stage and correlate with the repeated audible event'
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=inspect(a.capture);a.output.write_text(json.dumps(result,indent=2))
    print(json.dumps([{k:s[k] for k in ('deck','stage','dropped','blocks_total','captured_blocks','status')} for s in result['streams']],indent=2))
