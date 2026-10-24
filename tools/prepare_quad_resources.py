"""Copy an independently pinned, owner-supplied quad closure without compiling helpers."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'source/xdj-xz-toolkit'))
from builder.quad_payload import install

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--quad-profile-sha256',required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Output must be new')
    install(a.input,a.output,a.quad_profile_sha256)
if __name__=='__main__':main()
