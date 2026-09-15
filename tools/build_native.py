"""Build the independent Windows shell with an app-local static C runtime."""
import argparse
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--release',action='store_true')
a=p.parse_args();app=Path(__file__).resolve().parents[1]
env=os.environ.copy();env['CARGO_BUILD_JOBS']='1'
env['RUSTFLAGS']=(env.get('RUSTFLAGS','')+' -C target-feature=+crt-static').strip()
command=['cargo','build','--manifest-path',str(app/'src-tauri/Cargo.toml'),'--locked','--target','x86_64-pc-windows-msvc']
if a.release:command.append('--release')
subprocess.run(command,check=True,env=env)
