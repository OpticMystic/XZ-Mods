"""Run the frozen batch backend against a disposable, generated USB library."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import struct
import wave

APP = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--resources', type=Path, default=APP / 'resources')
parser.add_argument('--toolkit', type=Path, default=APP.parent / 'xdj-xz-toolkit')
parser.add_argument('--fixture-root', type=Path, required=True)
parser.add_argument('--evidence', type=Path, required=True)
parser.add_argument('--engine-data', type=Path, help='Existing fully cached engine for optional real UMX-HQ generation')
args = parser.parse_args()
if args.fixture_root.exists() or args.evidence.exists():
    raise ValueError('Choose new fixture and evidence paths')
args.fixture_root.mkdir(parents=True)
sys.path.insert(0, str(args.toolkit.resolve()))
from builder import managed_usb, overcue_writer as writer
from builder.tests.pdb_fixture import write_export
from builder.tests.test_overcue_writer import QuietJob, render_roles

volume = args.fixture_root.resolve() / 'USB test library'
paths = {number: f'/Contents/Test/Batch track {number}.wav' for number in (1, 2, 3)}
write_export(volume / 'PIONEER/rekordbox/export.pdb', paths)
for number, relative in paths.items():
    source = volume / relative.lstrip('/')
    source.parent.mkdir(parents=True, exist_ok=True)
    if number == 2:
        source.write_bytes(b'This deliberately invalid WAV tests failure continuation.')
        continue
    with wave.open(str(source), 'wb') as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(44100)
        output.writeframes(b'\0' * 44100 * 4)
    roles = args.fixture_root / f'roles-{number}'
    render_roles(roles, 96000, seed=number)
    writer.publish(volume, relative, number, hashlib.sha256(source.read_bytes()).hexdigest(),
                   'wav', roles, 'batch-acceptance-v1', 'local-test', QuietJob())

resources = args.resources.resolve()
backend = resources / 'backend' / ('xz-mods-service.exe' if os.name == 'nt' else 'xz-mods-service')
env = {**os.environ, 'XZ_BUILDER_RESOURCES': str(resources),
       'XZ_AUDIO_HELPER': str(resources / ('xz-audio-helper.exe' if os.name == 'nt' else 'xz-audio-helper')),
       'XZ_BUILDER_DATA': str(args.fixture_root.resolve() / 'data')}
env.pop('PYTHONPATH', None)
env.pop('PYTHONHOME', None)

def tree():
    return {str(p.relative_to(volume)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in volume.rglob('*') if p.is_file()}

def call(request, cancel=None):
    current = dict(env)
    if cancel:
        current['XZ_BUILDER_CANCEL_FILE'] = str(cancel.resolve())
    else:
        current.pop('XZ_BUILDER_CANCEL_FILE', None)
    process = subprocess.run([str(backend)],
                             input=json.dumps(request), text=True, encoding='utf-8',
                             capture_output=True, env=current, timeout=90)
    assert process.returncode == 0, process.stderr
    lines = process.stdout.splitlines()
    assert all(len(line.encode('utf-8')) < 65536 for line in lines)
    events = [json.loads(line) for line in lines]
    assert events[-1]['event'] == 'result', events
    return events

identity = managed_usb.identity(volume, media=True)
request = {'method': 'generate_library_stem_batch', 'volume': str(volume),
           'expected_identity': identity, 'track_ids': ['1', '2', '3'], 'preset': 'umxhq'}
before = tree()
events = call(request)
final = events[-1]
assert final['ok'], final
report = final['result']
assert (report['reused'], report['failed'], report['prepared'], report['completed']) == (2, 1, 0, 3), report
assert [row['state'] for row in report['items']] == ['reused', 'failed', 'reused'], report
assert before == tree(), 'Batch changed an original file or existing prepared bundle'
assert any(event.get('batch', {}).get('items', [{}])[0].get('state') == 'running' for event in events)
cancel = args.fixture_root / 'cancel'
cancel.write_text('cancel')
cancelled = call(request, cancel)[-1]
assert cancelled['cancelled'] and not cancelled['ok'], cancelled
assert [row['state'] for row in cancelled['result']['items']] == ['queued'] * 3
assert before == tree()
invalid = call({**request, 'track_ids': ['999999']})[-1]
assert not invalid['ok'] and 'no longer' in invalid['error'], invalid
changed = call({**request, 'expected_identity': {**identity, 'directory': 'changed'}})[-1]
assert not changed['ok'] and 'USB changed' in changed['error'], changed

evidence = {'frozen_backend': str(backend),
            'fixture_volume': str(volume), 'checks': {
                'verified_existing_stems_reused_without_model_or_engine': True,
                'invalid_audio_failure_continues_to_next_track': True,
                'all_original_audio_database_and_prepared_stems_unchanged': True,
                'per_track_progress_survives_native_json_limit': True,
                'cancel_returns_full_unstarted_queue': True,
                'unknown_track_rejected': True, 'changed_media_rejected': True},
            'batch_result': report, 'hardware_verified': False}
if args.engine_data:
    from builder import engines
    data = args.engine_data.resolve()
    catalog = engines.registry()
    marker = json.loads((data / 'setup-umxhq.json').read_text())
    expected = hashlib.sha256(json.dumps(catalog, sort_keys=True).encode()).hexdigest()
    assert marker['registry_sha256'] == expected, 'Use an existing engine with this exact registry'
    assert (data / 'engine-python' / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')).is_file()
    for name in catalog['presets']['umxhq']['models']:
        record = catalog['models'][name]
        path = data / 'models' / name
        assert path.stat().st_size == record['bytes'] and engines.file_sha(path) == record['sha256']
    source = volume / 'Contents/Test/Batch track 4.wav'
    with wave.open(str(source), 'wb') as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(44100)
        output.writeframes(b''.join(struct.pack('<hh', *([int(8000 * math.sin(i * 440 * 2 * math.pi / 44100))] * 2)) for i in range(44100)))
    write_export(volume / 'PIONEER/rekordbox/export.pdb', {**paths, 4: '/Contents/Test/Batch track 4.wav'})
    originals = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in volume.rglob('*') if p.is_file() and 'CDJMODS' not in p.parts}
    env['XZ_BUILDER_DATA'] = str(data)
    actual = call({**request, 'track_ids': ['1', '4', '3']})[-1]
    assert actual['ok'], actual
    assert [i['state'] for i in actual['result']['items']] == ['reused', 'prepared', 'reused'], actual
    verified = call({'method': 'inspect_overcue', 'source': str(source)})[-1]
    assert verified['ok'] and verified['result']['verified_mixes'] == 7, verified
    assert originals == {name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in originals}
    evidence['real_model_batch'] = actual['result']
    evidence['checks']['real_umxhq_inference_then_seven_mix_page_readback'] = True
    evidence['checks']['generation_preserves_original_audio_and_rekordbox_export'] = True
args.evidence.parent.mkdir(parents=True, exist_ok=True)
args.evidence.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
print(json.dumps(evidence, indent=2))
