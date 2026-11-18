"""Run the real NSIS resource backup hook on isolated Windows fixtures."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--makensis', type=Path, default=Path(os.environ.get('LOCALAPPDATA', '.'))/'tauri/NSIS/makensis.exe')
args = parser.parse_args()
hook = Path(__file__).resolve().parents[1]/'src-tauri/windows/installer-hooks.nsh'
def files(root):
    return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
with tempfile.TemporaryDirectory(prefix='xz-resource-backup-') as folder:
    work = Path(folder).resolve()
    install = work/'app with spaces'
    assert install.resolve().is_relative_to(work)
    program = work/'fixture.exe'
    script = work/'fixture.nsi'
    script.write_text(f'''Name "XZ resource backup fixture"
OutFile "{program}"
InstallDir "{install}"
RequestExecutionLevel user
SilentInstall silent
!include "{hook}"
Section
  !insertmacro NSIS_HOOK_PREINSTALL
SectionEnd
''', encoding='utf-8')
    subprocess.run([str(args.makensis),'/V2',str(script)],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    def run():
        return subprocess.run([str(program),'/S'],creationflags=subprocess.CREATE_NO_WINDOW).returncode
    assert run()==0 and not (install/'resource-backups').exists()
    for revision in (1,2):
        resources=install/'resources'
        for name in ('source/toolkit/stale.txt','runtime/manifest.json','runtime/old-helper','mod-version-seeds/catalog.json','inference/__pycache__/old.pyc'):
            path=resources/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(str(revision)+name)
        expected=files(resources)
        assert run()==0
        assert not resources.exists()
        assert files(install/f'resource-backups/resources-{revision}')==expected
    first=files(install/'resource-backups/resources-1')
    unknown=install/'resources/unknown.txt'
    unknown.parent.mkdir()
    unknown.write_text('preserve unidentified data')
    assert run()!=0 and unknown.read_text()=='preserve unidentified data'
    assert files(install/'resource-backups/resources-1')==first
print('PASS fresh install, complete resource backup, repeat preservation and unknown-resource refusal')
