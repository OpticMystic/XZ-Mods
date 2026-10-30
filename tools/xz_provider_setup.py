"""Create an independent XZ venv, install pinned dependencies, then test it."""
import subprocess
import os
import sys
from pathlib import Path
import venv

runtime, model = map(Path, sys.argv[1:])
venv.EnvBuilder(with_pip=False, clear=False).create(runtime)
python = runtime / 'Scripts/python.exe'
flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
subprocess.run([str(python), '-m', 'ensurepip', '--upgrade'], check=True, timeout=300, creationflags=flags, stdout=sys.stdout, stderr=sys.stderr)
pip_env={key:value for key,value in os.environ.items() if not key.upper().startswith('PIP_')}
subprocess.run([str(python), '-m', 'pip', '--isolated', 'install', '--require-virtualenv', '--index-url', 'https://pypi.org/simple', '--disable-pip-version-check', '--no-input', '-r', str(Path(__file__).resolve().parents[1] / 'localization/requirements.txt')], check=True, timeout=1200, creationflags=flags, stdout=sys.stdout, stderr=sys.stderr, env=pip_env)
verified=subprocess.run([str(python), str(Path(__file__).with_name('xz_provider_verify.py')), str(model)], check=True, timeout=300, creationflags=flags, capture_output=True, text=True, encoding='utf-8')
print(verified.stdout, end='', flush=True)
