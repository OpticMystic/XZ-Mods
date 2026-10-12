"""Run the real bounded receiver over loopback with complete/partial/wrong-peer input."""
from pathlib import Path
import argparse,hashlib,json,random,socket,subprocess,time

ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
binary=out/'receive-test'
subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'receive.c'),'-o',str(binary)],check=True)
data=random.Random(7398).randbytes(98304)
def test(name,payload,peer='127.0.0.1'):
    target=out/(name+'.bin')
    with target.open('wb') as output:
        proc=subprocess.Popen([str(binary),str(len(data)),'127.0.0.1','127.0.0.1'],stdout=output)
        deadline=time.monotonic()+3
        while True:
            try:sock=socket.create_connection(('127.0.0.1',50009),timeout=.2,source_address=(peer,0));break
            except OSError:
                if time.monotonic()>deadline:proc.kill();raise
                time.sleep(.05)
        with sock:
            try:sock.sendall(payload);sock.shutdown(socket.SHUT_WR)
            except OSError:pass
        status=proc.wait(timeout=3)
    return status,target.read_bytes()
status,got=test('complete',data);assert status==0 and got==data
status,got=test('partial',data[:-8092]);assert status!=0 and len(got)<len(data)
status,got=test('wrong-peer',data,peer='127.0.0.2');assert status!=0 and not got
assert subprocess.run([str(binary),'2700097','127.0.0.1','127.0.0.1']).returncode==2
record={'complete_exact_bytes':True,'missing_tail_rejected':True,'wrong_peer_rejected':True,
        'oversize_rejected':True,'synthetic_bytes':len(data),'controller_touched':False}
(out/'verification.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record))
