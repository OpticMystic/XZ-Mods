from pathlib import Path
import argparse,subprocess,tempfile
from ui_sources import UI_SOURCES
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-key-feedback-ui-') as folder:
    exe=Path(folder)/'feedback.exe'
    subprocess.run([str(a.zig),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror',*[str(root/name) for name in UI_SOURCES],str(root/'test_key_feedback_ui.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,cwd=folder)
