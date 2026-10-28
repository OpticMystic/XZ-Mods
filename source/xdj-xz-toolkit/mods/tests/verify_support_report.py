"""Reports from the XZ: loader identity, the pending-report helper and its contract. No network, no device.

1. Loader identity tests on the host and on ARM under qemu.
2. The report helper built for the XZ (support/build.py) under the GLIBC 2.4 import gate.
3. support_report_host.py on Linux (WSL on Windows): the helper with ASan/UBSan against a local fake HTTPS Worker.
4. Every body that fake Worker received and every file left on a stick validated against the bug desk's canonical
   spec/generated/controller-pending.schema.json (with its fixture), and the helper's copies of the contract's enums
   (fact codes and states, topics, screen_state) checked against the contract.

usage: verify_support_report.py --zig <zig> --mbedtls <mbedtls-3.6.7 dir> --out <new dir>"""
import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

from elftools.elf.elffile import ELFFile
import jsonschema

MODS = Path(__file__).resolve().parents[1]
REPO = MODS.parents[2]


def linux(path, wsl):
    value = Path(path).resolve().as_posix()
    return '/mnt/' + value[0].lower() + value[2:] if wsl else value


def glibc_gate(binary):
    with binary.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf['e_machine'] == 'EM_ARM' and not elf['e_flags'] & 0x400
        versions = set()
        section = elf.get_section_by_name('.gnu.version_r')
        if section:
            for _, aux in section.iter_versions():
                versions.update(a.name for a in aux)
        imports = sorted(s.name for s in elf.get_section_by_name('.dynsym').iter_symbols() if s.name and s['st_shndx'] == 'SHN_UNDEF')
    too_new = [v for v in versions if v.startswith('GLIBC_') and tuple(map(int, v[6:].split('.'))) > (2, 4)]
    assert not too_new, f'{binary.name}: {too_new} exceed GLIBC_2.4'
    assert not [s for s in imports if 'isoc99' in s], imports
    return sorted(versions), imports


def c_list(source, name):
    match = re.search(r'static const char \*const ' + name + r'\[\] = \{(.*?)\};', source, re.S)
    return re.findall(r'"([^"]*)"', match.group(1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True)
    parser.add_argument('--mbedtls', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--wsl-distro', default='Ubuntu')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    wsl = os.name == 'nt'
    prefix = ['wsl', '-d', args.wsl_distro, '--'] if wsl else []
    zig = str(Path(args.zig).resolve())
    receipt = {}

    # 1. Identity, host and ARM.
    flags = ['-O1', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
    sources = [str(MODS / 'support/loader_identity.c'), str(MODS / 'tests/test_loader_identity.c')]
    host = out / ('identity.exe' if wsl else 'identity')
    subprocess.run([zig, 'cc', *flags, *sources, '-o', str(host)], check=True)
    (out / 'identity-host').mkdir()
    fixture = REPO / 'spec/fixtures/loader-identity.json'
    subprocess.run([str(host), str(out / 'identity-host'), str(fixture)], check=True)
    arm = out / 'identity-arm'
    subprocess.run([zig, 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9', *flags, *sources, '-o', str(arm)], check=True)
    (out / 'identity-qemu').mkdir()
    subprocess.run(prefix + ['qemu-arm', '-L', '/usr/arm-linux-gnueabi', linux(arm, wsl), linux(out / 'identity-qemu', wsl), linux(fixture, wsl)], check=True)
    receipt['identity'] = 'host and ARM passed'

    # 2. The helper as packaged for the XZ, under the import gate.
    subprocess.run(['python', str(MODS / 'support/build.py'), '--zig', zig, '--mbedtls', str(args.mbedtls.resolve()),
                    '--output', str(out / 'helper-arm')], check=True)
    versions, imports = glibc_gate(out / 'helper-arm/xz-support')
    receipt['helper_arm'] = {'symbol_versions': versions, 'imports': imports,
                             'build': json.loads((out / 'helper-arm/build.json').read_text())}

    # 3. Linux: ASan/UBSan helper against the fake Worker. Its scratch lives on the Linux side for real permissions.
    scratch = '/tmp/xz-support-report-' + os.urandom(4).hex()
    run = subprocess.run(prefix + ['python3', linux(MODS / 'tests/support_report_host.py', wsl), '--mbedtls', linux(args.mbedtls, wsl),
                                   '--out', scratch], capture_output=True, text=True, timeout=1200)
    (out / 'host.log').write_text(run.stdout + run.stderr)
    print(run.stdout.strip())
    assert run.returncode == 0, run.stderr[-4000:]
    subprocess.run(prefix + ['cp', '-r', scratch + '/bodies', scratch + '/saved', scratch + '/receipt.json', linux(out, wsl)], check=True)
    subprocess.run(prefix + ['rm', '-rf', scratch], check=True)

    # 4. The contract, as the bug desk generated it.
    contract = json.loads((REPO / 'spec/support-contract.json').read_text())
    schema = json.loads((REPO / 'spec/generated/controller-pending.schema.json').read_text())
    identity_schema = json.loads((REPO / 'spec/generated/loader-identity.schema.json').read_text())
    jsonschema.Draft202012Validator(identity_schema).validate(json.loads(fixture.read_text()))
    facts = schema['properties']['private']['properties']['items']['items']['properties']['facts']['items']['properties']
    screen = schema['properties']['screen_state']['properties']
    source = (MODS / 'support/support_tool.c').read_text()
    assert c_list(source, 'v2_codes') == facts['code']['enum'], 'helper code list differs from the contract'
    assert c_list(source, 'v2_states') == facts['state']['enum'], 'helper state list differs from the contract'
    assert c_list(source, 'topics') == contract['reporter_hints']['topic']
    assert c_list(source, 'mods_pages') == screen['mods_page']['enum'], 'MODS pages differ from the contract'
    assert ['UNKNOWN', *c_list(source, 'deck_views')] == screen['deck_view']['enum']
    assert set(c_list(source, 'place_pages')) <= set(screen['stock_page']['enum'])
    pages = re.search(r'enum xz_ui_page \{(.*?)\};', (MODS / 'ui/ui.h').read_text(), re.S).group(1)
    names = [n[6:] for n in re.findall(r'XZ_UI_[A-Z0-9_]+', pages) if n != 'XZ_UI_PAGE_COUNT']
    assert ['UNKNOWN', *names] == screen['mods_page']['enum'], 'enum xz_ui_page no longer matches the contract'
    validator = jsonschema.Draft202012Validator(schema)
    fixture_report = json.loads((REPO / 'spec/fixtures/controller-pending-report.json').read_text())
    assert not list(validator.iter_errors(fixture_report))
    files = sorted((out / 'bodies').glob('*.json')) + sorted((out / 'saved').glob('*.json'))
    assert len(files) >= 20, files
    for path in files:
        raw = path.read_bytes()
        report = json.loads(raw)
        assert set(report) <= set(schema['properties']) and set(fixture_report) <= set(report) | {'kind_hint', 'reporter_hints'}
        errors = sorted(validator.iter_errors(report), key=lambda e: list(e.path))
        assert not errors, (path.name, [f'{list(e.path)}: {e.message}' for e in errors[:5]])
        # Checks the Worker adds to the schema: 280 characters, both IDs or neither, the page of the surface only.
        state = report['screen_state']
        assert len(report['description']) <= 280 and len(report['description'].encode()) <= 1120
        assert (report['loader_id'] is None) == (report['installation_id'] is None)
        assert (state['surface'] == 'MODS' or state['mods_page'] == 'UNKNOWN') and (state['surface'] == 'STOCK' or state['stock_page'] == 'UNKNOWN')
        assert len(raw) <= contract['limits']['report_bytes']
        if path.parent.name == 'saved':
            assert path.name.endswith(report['report_id'] + '.json')
    receipt['contract'] = {'valid': [p.parent.name + '/' + p.name for p in files]}
    (out / 'verify-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS reports from the XZ: identity host+ARM with the contract fixture, helper ARM GLIBC_2.4 gate, '
          f'{len(files)} uploaded bodies and stick files valid against controller-pending.schema.json')


if __name__ == '__main__':
    main()
