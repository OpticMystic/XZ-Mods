"""Selected USB tracks, serial preparation, and bounded per-track results."""
from __future__ import annotations

import json
from .host import executable
import re
from . import cache, engines, managed_usb, overcue_writer, rekordbox_pdb, stem_library
from .jobs import Cancelled

FORMAT = 'xz-stem-batch/1'
MAX_TRACKS = 100


def _short(value, budget):
    # The Windows host drops JSON lines over 64 KiB, including escaped Unicode.
    text = str(value)
    if len(json.dumps(text).encode('ascii')) <= budget:
        return text
    size = 0
    for index, char in enumerate(text):
        size += len(json.dumps(char)) - 2
        if size > budget - 5:
            return text[:index] + '...'
    return text


def _selected(request, root):
    ids = request.get('track_ids')
    if not isinstance(ids, list) or not 0 < len(ids) <= MAX_TRACKS:
        raise ValueError('Select between 1 and 100 tracks for a batch')
    normalized = []
    for value in ids:
        if not isinstance(value, (str, int)) or isinstance(value, bool) or not re.fullmatch(r'[1-9][0-9]{0,9}', str(value)):
            raise ValueError('Each selected track must have a Rekordbox track ID')
        number = int(value)
        if number > 0xffffffff or number in normalized:
            raise ValueError('Selected track IDs must be unique unsigned 32-bit IDs')
        normalized.append(number)
    tracks = rekordbox_pdb.library_tracks(cache._regular(root.joinpath(*rekordbox_pdb.EXPORT_PDB)))
    by_id = {track['id']: track for track in tracks}
    if any(number not in by_id for number in normalized):
        raise ValueError('A selected track is no longer in this USB library. Refresh and select it again.')
    return [by_id[number] for number in normalized]


def _report(items, *, cancelled=False, error=None, error_code=None, eta=None):
    counts = {state: sum(item['state'] == state for item in items) for state in ('prepared', 'reused', 'failed')}
    report = {'format': FORMAT, 'total': len(items), 'completed': sum(counts.values()),
              **counts, 'cancelled': cancelled, 'aborted': error is not None,
              'items': [dict(item) for item in items]}
    if error is not None:
        report['error'] = _short(error, 300)
    if error_code:
        report['error_code'] = error_code
    if eta is not None:
        report['eta_seconds'] = eta
    return report


def _error_code(exc):
    text = str(exc)
    return 'usb_full' if text.startswith('Not enough USB space') else None


class BatchClock:
    """Time left for the batch from this machine's learned rates and each running track's overall progress."""

    def __init__(self, preset, items, frames_by_id):
        self.preset, self.items, self.frames_by_id = preset, items, frames_by_id
        self.device = None

    def seconds(self):
        total, loaded = 0.0, any(item['state'] in ('prepared', 'reused') for item in self.items)
        for item in self.items:
            if item['state'] not in ('queued', 'running'):
                continue
            frames = self.frames_by_id.get(item['id'], 0)
            try:
                estimate = engines.estimate_track_seconds(self.preset, frames, self.device, include_load=not loaded)
            except ValueError:
                return None
            loaded = True
            if item['state'] == 'running':
                estimate *= 1 - min(100.0, float(item.get('overall') or 0)) / 100
            total += estimate
        return round(total)


class TargetChanged(Exception):
    pass


class TrackJob:
    def __init__(self, parent, root, identity, items, item, clock=None):
        self.parent, self.root, self.identity = parent, root, identity
        self.items, self.item, self.clock = items, item, clock

    def check(self):
        self.parent.check()
        try:
            managed_usb.check_target(self.root, self.identity, media=True)
        except (ValueError, OSError) as exc:
            raise TargetChanged(str(exc)) from exc

    def progress(self, stage, message, **details):
        self.check()
        self.item.update(stage=_short(stage, 32), message=_short(message, 120))
        if 'pct' in details:
            self.item['pct'] = details['pct']
        if 'overall' in details:
            self.item['overall'] = details['overall']
        eta = self.clock.seconds() if self.clock else None
        self.parent.progress(stage, message, **details, eta_seconds=eta, batch=_report(self.items, eta=eta))

    def run(self, argv, **kwargs):
        self.check()
        result = self.parent.run(argv, **kwargs)
        self.check()
        return result


def generate(request, job, resources):
    root = cache._safe_path(request['volume'])
    identity = request['expected_identity']
    managed_usb.check_target(root, identity, media=True)
    preset = request.get('preset')
    if preset not in engines.registry()['presets']:
        raise ValueError('Unknown separation preset')
    tracks = _selected(request, root)
    items = [{'id': str(track['id']), 'title': _short(track['title'], 80),
              'artist': _short(track['artist'], 40), 'state': 'queued', 'stage': 'waiting'} for track in tracks]
    current = None
    clock = BatchClock(preset, items, {str(track['id']): int(track['duration'] * 44100) for track in tracks})
    try:
        job.progress('batch', f'{len(items)} tracks queued', eta_seconds=clock.seconds(), batch=_report(items, eta=clock.seconds()))
        with engines.Engine(preset, job, resources) as engine:
            clock.device = (engine.state or {}).get('device')
            for track, item in zip(tracks, items):
                current = item
                child = TrackJob(job, root, identity, items, item, clock)
                child.check()
                item.update(state='running', stage='inspect')
                child.progress('inspect', f'Checking track {track["id"]}')
                try:
                    # Re-read the export and index for each track; earlier publications change the index.
                    current_tracks = rekordbox_pdb.library_tracks(cache._regular(root.joinpath(*rekordbox_pdb.EXPORT_PDB)))
                    if not any(t['id'] == track['id'] and t['file_path'] == track['file_path'] for t in current_tracks):
                        raise ValueError('Track identity changed in the Rekordbox export. Refresh the library.')
                    index, _ = overcue_writer.read_index(root / 'CDJMODS')
                    state = stem_library.stem_status(root, track, index, None)
                    source = stem_library.source_path(root, track['file_path'])
                    if state['stem_status'] == 'present':
                        child.progress('verify', 'Checking existing stems before reusing them')
                        verified = json.loads(child.run([executable(resources,'xz-overcue-check'), source]))
                        if (verified.get('format') != 'overcue-stems/4' or verified.get('verified_mixes') != 7
                                or verified.get('all_pages_verified') is not True or verified.get('source_identity_verified') is not True):
                            raise ValueError('Existing stems did not pass verification. They were left unchanged.')
                        item.update(state='reused', stage='complete')
                    elif state['can_generate']:
                        result = engines.separate({'source': str(source), 'preset': preset}, child, resources, engine=engine)
                        item.update(state='reused' if result.get('reused') else 'prepared', stage='complete')
                    else:
                        raise ValueError(state.get('detail') or state['stem_label'])
                    item.pop('message', None)
                    item.pop('pct', None)
                except (Cancelled, TargetChanged):
                    raise
                except Exception as exc:
                    item.update(state='failed', stage='failed', error=_short(exc, 160))
                    if _error_code(exc):
                        item['error_code'] = _error_code(exc)
                    item.pop('message', None)
                    item.pop('pct', None)
                    item.pop('overall', None)
                    item.pop('pct', None)
                job.progress('batch', f'{_report(items)["completed"]} of {len(items)} tracks finished', batch=_report(items))
    except Cancelled:
        if current is not None and current['state'] == 'running':
            current.update(state='cancelled', stage='cancelled')
            current.pop('message', None)
        return _report(items, cancelled=True)
    except TargetChanged as exc:
        if current is not None and current['state'] == 'running':
            current.update(state='failed', stage='failed', error=_short(exc, 160))
            current.pop('message', None)
        return _report(items, error=exc, error_code='usb_changed')
    return _report(items)
