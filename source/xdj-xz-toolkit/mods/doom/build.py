"""Build the separate GPL Doom executable for the XDJ-XZ 1.26 ARM ABI."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent

def inspect(path: Path, threaded: bool=False) -> dict:
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM':
            raise ValueError('Expected ARM32 little-endian executable')
        if elf['e_flags'] & 0x400:
            raise ValueError('XZ needs the soft-float calling convention')
        versions = set()
        section = elf.get_section_by_name('.gnu.version_r')
        if section:
            for _, auxiliaries in section.iter_versions():
                versions.update(aux.name for aux in auxiliaries)
        if any(v.startswith('GLIBC_') and tuple(map(int, v[6:].split('.'))) > (2, 13) for v in versions):
            raise ValueError('Imports exceed XZ glibc 2.13')
        dynamic = elf.get_section_by_name('.dynamic')
        libraries = [t.needed for t in dynamic.iter_tags() if t.entry.d_tag == 'DT_NEEDED']
        allowed={'libc.so.6', 'libm.so.6', 'librt.so.1', 'ld-linux.so.3'}
        if threaded: allowed.add('libpthread.so.0')
        if set(libraries) - allowed:
            raise ValueError(f'Unexpected dependencies: {libraries}')
    return {'architecture': 'ARM32 soft-float ABI', 'libraries': libraries,
            'symbol_versions': sorted(versions), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

def build(zig: Path, output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    source = json.loads((ROOT / 'source.json').read_text())
    for name, digest in source['upstream_sha256'].items():
        if hashlib.sha256((ROOT/'vendor'/name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Pinned upstream source changed: {name}')
    deh=json.loads((ROOT/'chex_deh/source.json').read_text())
    for name,digest in deh['files'].items():
        if hashlib.sha256((ROOT/'chex_deh'/name).read_bytes()).hexdigest()!=digest:raise ValueError('Pinned DeHackEd source changed: '+name)
    engine=output/'engine';engine.mkdir(exist_ok=True)
    for path in (ROOT/'vendor').iterdir():
        if path.is_file():shutil.copyfile(path,engine/path.name)
    features=(engine/'doomfeatures.h').read_text()
    if features.count('#undef FEATURE_DEHACKED')!=1:raise ValueError('DeHackEd feature gate changed')
    (engine/'doomfeatures.h').write_text(features.replace('#undef FEATURE_DEHACKED','#define FEATURE_DEHACKED 1'))
    # Keep the vendored snapshot unchanged. Fix the generic port's missing
    # normal-quit termination in the generated translation unit.
    system = (ROOT/'vendor/i_system.c').read_text()
    original = '    exit(0);\n#endif\n}'
    if system.count(original) != 1:
        raise ValueError('Upstream quit patch no longer applies exactly once')
    patched = output/'i_system_xz.c'
    patched.write_text(system.replace(original, '    exit(0);\n#endif\n    exit(0); /* XZ: normal game quit ends the standalone process. */\n}'))
    # USB users choose files by contents. A Doom II IWAD named doom1.wad
    # must not get forced into Doom I by the upstream filename shortcut.
    iwad=(ROOT/'vendor/d_iwad.c').read_text();shortcut='*mission = IdentifyIWADByName(result, mask);'
    if iwad.count(shortcut)!=1:raise ValueError('IWAD content-identification patch no longer applies')
    patched_iwad=output/'d_iwad_xz.c';patched_iwad.write_text(iwad.replace(shortcut,'*mission = IdentifyIWADByName(result, mask) == pack_chex ? pack_chex : none; /* XZ: preserve Chex, identify other IWADs by their map lumps. */'))
    base = [str(zig.resolve()), 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9',
            '-O2', '-s', '-std=gnu11', '-D_DEFAULT_SOURCE', '-DNORMALUNIX', '-DLINUX',
            '-DDOOMGENERIC_RESX=320', '-DDOOMGENERIC_RESY=200', '-I'+str(engine),'-I'+str(ROOT/'chex_deh')]
    sources = [str(patched if n == 'i_system.c' else patched_iwad if n=='d_iwad.c' else engine/n) for n in source['sources']]
    sources += [str(ROOT/'chex_deh'/n) for n in deh['files'] if n.endswith('.c')]
    binary = output/'xz-doom'
    subprocess.run(base + sources + [str(ROOT/'doomgeneric_xz.c'), str(ROOT/'xz_controls.c'),
                                    '-lm', '-Wl,--gc-sections', '-o', str(binary)], check=True)
    test = output/'doom-controls-test'
    subprocess.run(base + ['-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                          str(ROOT/'xz_controls.c'), str(ROOT/'test_controls.c'), '-o', str(test)], check=True)
    native_test=output/'doom-native-input-test'
    subprocess.run(base + ['-UNDEBUG','-Wall','-Wextra','-Werror',str(ROOT/'native_bridge.c'),
                          str(ROOT/'wad_catalog.c'),
                          str(ROOT.parent/'audio/vendor/sha256/sha256.c'),
                          str(ROOT/'test_native_bridge.c'),'-pthread','-o',str(native_test)],check=True)
    result = {'engine': source['repository'], 'commit': source['commit'], 'game': inspect(binary),
              'controls_test': inspect(test), 'wad_included': False, 'hardware_verified': False,
              'native_input_test': inspect(native_test, threaded=True),
              'rendering': 'All game logic/rendering on XZ; loopback IPC to resident XZ Mods receiver',
              'audio': False, 'engine_resolution': [320, 200], 'display_resolution': [800, 480]}
    (output/'build.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.zig, args.output.resolve())
