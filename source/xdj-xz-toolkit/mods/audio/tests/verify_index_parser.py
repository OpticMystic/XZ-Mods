#!/usr/bin/env python3
"""Catch quadratic work in the real index reader without wall-clock thresholds.

Only a temporary copy of the pinned jsmn header is instrumented. Count every
token inspected while finding the enclosing container, under the production
reader's actual preprocessor configuration. Parsing and last-entry lookup must
still succeed. No production/vendor file is changed by this verifier.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile

MODS = Path(__file__).resolve().parents[2]


def instrument_jsmn(vendor):
    text = vendor.read_text()
    probes = {
        'token = &tokens[parser->toknext - 1];':
            'token_inspections++; token = &tokens[parser->toknext - 1];',
        'token = &tokens[token->parent];':
            'token_inspections++; token = &tokens[token->parent];',
        'token = &tokens[i];': 'token_inspections++; token = &tokens[i];',
        'if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT)':
            'if ((token_inspections++, tokens[i].type == JSMN_ARRAY) || tokens[i].type == JSMN_OBJECT)',
        'parser->toksuper = tokens[parser->toksuper].parent;':
            'token_inspections++; parser->toksuper = tokens[parser->toksuper].parent;',
    }
    expected_counts = (1, 1, 2, 1, 1)
    for (old, new), expected in zip(probes.items(), expected_counts):
        if text.count(old) != expected:
            raise RuntimeError('Pinned parser changed; review token-inspection instrumentation')
        text = text.replace(old, new)
    vendor.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    compilers = parser.add_mutually_exclusive_group()
    compilers.add_argument('--cc', default='cc')
    compilers.add_argument('--zig')
    args = parser.parse_args()
    compiler = [str(Path(args.zig).resolve()), 'cc'] if args.zig else shlex.split(args.cc)
    with tempfile.TemporaryDirectory(prefix='xz-index-parser-') as directory:
        root = Path(directory)
        shutil.copytree(MODS / 'audio', root / 'audio')
        shutil.copytree(MODS / 'ram', root / 'ram')
        # The shared headers beside them (xz_cloexec.h since 92cd666, clock_compat.h, xz_mutex.h, ...).
        for header in MODS.glob('*.h'):
            shutil.copy2(header, root / header.name)
        vendor = root / 'audio/vendor/jsmn/jsmn.h'
        instrument_jsmn(vendor)
        binary = root / 'test-index-parser.exe'
        subprocess.run(compiler + ['-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-DXZ_INDEX_COST_INSTRUMENTED',
                        '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS',
                        str(root / 'audio/tests/test_index_parser.c'),
                        str(root / 'audio/stem_decode.c'),
                        str(root / 'audio/vendor/miniz/miniz_tinfl.c'),
                        str(root / 'audio/vendor/sha256/sha256.c'),
                        '-lm', '-o', str(binary)], check=True)
        # Sibling entries expose repeated searches back to the enclosing tracks
        # object. Enough metadata keeps this shaped like the actual catalog.
        tracks = {str(i): {'bundle': f'{i:016x}', 'file_path': f'/Contents/Track {i:04d}.mp3',
                          'source_sha256': 'a' * 64, 'frames': 12345678,
                          'drums_sha256': 'b' * 64, 'vocal_sha256': 'c' * 64,
                          'harmonics_sha256': 'd' * 64, 'full_mix_sha256': 'e' * 64}
                  for i in range(1, 257)}
        index = root / 'index.json'
        index.write_text(json.dumps({'schema': 'overcue-index/1', 'tracks': tracks}))
        run = subprocess.run([str(binary), str(index), tracks['256']['file_path'], '0000000000000100'],
                             capture_output=True, text=True, timeout=10)
        result = json.loads(run.stdout)
        result['reader_sha256'] = hashlib.sha256((MODS / 'audio/overcue_file.c').read_bytes()).hexdigest()
        print(json.dumps(result, indent=2))
        if run.returncode:
            raise SystemExit('FAIL: index parsing exceeded its linear token-work budget or returned the wrong entry')


if __name__ == '__main__':
    main()
