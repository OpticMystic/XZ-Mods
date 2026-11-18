"""Create a synthetic Python v2 packet, optionally importing the real native exporter."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'source/xdj-xz-toolkit'))
from builder import support_sources, support_reports
from builder.jobs import Job

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--native-helper',type=Path)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    app=out/'app';app.mkdir(exist_ok=True);usb=out/'usb';usb.mkdir(exist_ok=True)
    os.environ['XZ_BUILDER_DATA']=str(app)
    if args.native_helper:
        fixture=out/'xz';bridge=fixture/'dev/shm/xz-four-deck';bridge.mkdir(parents=True,exist_ok=True)
        (bridge/'status.json').write_text('{"state":"WAITING","reason":"WAIT_LOOPBACK","private":"MUSIC_TITLE_CANARY /music/private.wav 192.168.1.2"}')
        (bridge/'bridge.log').write_text('1700000000 WAIT_LOOPBACK\nPRIVATE_TOKEN_CANARY\n')
        state=out/'state'
        env={**os.environ,'XZ_SUPPORT_TEST_MODE':'1','XZ_SUPPORT_SOURCE_ROOT':str(fixture)}
        prepared=subprocess.run([str(args.native_helper),'prepare',str(state),'bridge','Synthetic regression','Synthetic only'],env=env,capture_output=True,timeout=20)
        (out/'native-prepare.log').write_bytes(prepared.stdout+prepared.stderr)
        if prepared.returncode:raise SystemExit('Native prepare failed: '+str(prepared.returncode))
        digest=hashlib.sha256((state/'draft.json').read_bytes()).hexdigest()
        exported=subprocess.run([str(args.native_helper),'export',str(state),digest,str(usb)],env=env,capture_output=True,timeout=20)
        (out/'native-export.log').write_bytes(exported.stdout+exported.stderr)
        if exported.returncode:raise SystemExit('Native export failed: '+str(exported.returncode))
    else:
        folder=usb/'XZ_RUNTIME';folder.mkdir(exist_ok=True);(folder/'session.txt').write_text('WAIT_LOOPBACK\nPRIVATE_TOKEN_CANARY\n')
    job=Job();found=support_sources.discover(job,[{'volume':str(usb)}])
    draft=support_reports.dispatch({'method':'support_prepare_v2','scan_generation':found['scan_generation'],'category':'bridge','description':'The synthetic four-deck check failed.','reporter_hints':{'topic':'NOT_WORKING','problem_area':'FOUR_DECKS_LOOPBACK'}},job)
    frozen=support_reports.load(draft['report_id'])
    assert 'CANARY' not in frozen['body']
    assert 'WAIT_LOOPBACK' in frozen['body']
    (out/'report.json').write_bytes(frozen['body'].encode())
    (out/'receipt.json').write_text(json.dumps({'report_id':draft['report_id'],'sha256':draft['sha256'],'bytes':draft['bytes'],'native_export':bool(args.native_helper)},indent=2)+'\n')
    print(str(out/'report.json'))

if __name__=='__main__':main()
