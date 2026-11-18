# SPDX-License-Identifier: MIT
"""Map stem file names from Suno, Demucs, UVR, Moises or a DAW to the three OverCue groups.

Pure Python so the frozen service can review a stem set before any audio is decoded.
"""
from __future__ import annotations

from pathlib import PurePath
import re

GROUPS = ('vocals', 'drums', 'harmonics')
INSTRUMENTAL = 'instrumental'
MIX = 'mix'
ASSIGNABLE = (*GROUPS, INSTRUMENTAL, MIX)
AUTO_CONFIDENCE = 0.9
AMBIGUOUS_CONFIDENCE = 0.6

# (group, confidence, label, phrases). Phrases are lowercase token sequences.
TABLE = (
    ('vocals', 1.0, 'Vocals', ('vocals', 'vocal', 'vox', 'lead vocals', 'lead vocal', 'lead vox', 'voice', 'voices',
                               'acapella', 'a cappella', 'acappella', 'a capella', 'singing')),
    ('vocals', 1.0, 'Backing Vocals', ('backing vocals', 'backing vocal', 'backing vox', 'backingvocals', 'bgv', 'bgvs',
                                       'harmony vocals', 'background vocals')),
    ('vocals', 0.7, 'Choir', ('choir', 'backing', 'harmonies')),
    ('drums', 1.0, 'Drums', ('drums', 'drum', 'drum kit', 'drumkit')),
    ('drums', 1.0, 'Percussion', ('percussion', 'perc', 'percs')),
    ('drums', 0.9, 'Drum parts', ('kick', 'kicks', 'snare', 'snares', 'hi hat', 'hi hats', 'hihat', 'hihats', 'hats',
                                  'cymbals', 'toms', 'claps', 'overheads')),
    ('drums', 0.7, 'Beat', ('beat', 'beats', 'kit')),
    ('harmonics', 1.0, 'Bass', ('bass', 'bass guitar', 'sub bass', 'bassline')),
    ('harmonics', 1.0, 'Guitar', ('guitar', 'guitars', 'gtr', 'electric guitar', 'acoustic guitar')),
    ('harmonics', 1.0, 'Keys', ('keys', 'keyboard', 'keyboards', 'piano', 'pianos', 'organ', 'rhodes')),
    ('harmonics', 1.0, 'Strings', ('strings', 'string', 'violin', 'violins', 'cello', 'orchestra')),
    ('harmonics', 1.0, 'Brass', ('brass', 'horns', 'horn', 'trumpet', 'trombone')),
    ('harmonics', 1.0, 'Woodwinds', ('woodwinds', 'woodwind', 'winds', 'flute', 'sax', 'saxophone', 'clarinet')),
    ('harmonics', 1.0, 'Synth', ('synth', 'synths', 'synthesizer', 'pad', 'pads', 'arp', 'lead synth')),
    ('harmonics', 1.0, 'FX', ('fx', 'sfx', 'efx', 'effects')),
    ('harmonics', 1.0, 'Other', ('other', 'others')),
    ('harmonics', 1.0, 'Harmonics', ('harmonics', 'harmonic')),
    ('harmonics', 0.9, 'Melody', ('melody', 'melodies', 'chords')),
    (INSTRUMENTAL, 1.0, 'Instrumental', ('instrumental', 'inst', 'instrum', 'no vocals', 'no vocal', 'novocals',
                                         'no vox', 'without vocals', 'accompaniment', 'karaoke', 'backing track',
                                         'minus one')),
    (MIX, 1.0, 'Full mix', ('mix', 'full mix', 'fullmix', 'original', 'original mix', 'master', 'mastered', 'mixdown',
                            'full song', 'full track', 'stereo mix')),
    (MIX, 0.7, 'Song', ('song',)),
)

# Tokens that start a separator model's name; anything after them describes the model, not the part.
MODEL_MARKERS = frozenset(('uvr', 'mdx', 'mdxnet', 'mdx23c', 'demucs', 'htdemucs', 'roformer', 'melband', 'kuielab',
                           'spleeter', 'kim'))
# Words that name the download rather than the song, as in "Midnight Drive Suno Stems.zip".
TITLE_NOISE = frozenset(('stem', 'stems', 'suno', 'split', 'splitter', 'separated', 'multitrack', 'multitracks'))
SUNO_LABELS = frozenset(('Vocals', 'Backing Vocals', 'Drums', 'Bass', 'Guitar', 'Keys', 'Strings', 'Brass', 'Woodwinds',
                         'Percussion', 'Synth', 'FX', 'Instrumental'))
SUNO_ONLY = frozenset(('Backing Vocals', 'Brass', 'Woodwinds', 'Synth', 'FX', 'Percussion'))

PHRASES = {tuple(phrase.split()): (group, confidence, label) for group, confidence, label, phrases in TABLE
           for phrase in phrases}
LONGEST = max(len(phrase) for phrase in PHRASES)
BRACKETS = re.compile(r'[(\[{]([^)\]}]*)[)\]}]')


def tokens(text):
    text = re.sub(r'([a-z])([A-Z])', r'\1 \2', text)
    return [token for token in re.split(r'[^a-z0-9]+', text.casefold()) if token]


def matches(words):
    """Longest phrase at each position, scanning left to right without overlap."""
    found, at = [], 0
    while at < len(words):
        for length in range(min(LONGEST, len(words) - at), 0, -1):
            entry = PHRASES.get(tuple(words[at:at + length]))
            if entry:
                found.append(entry)
                at += length
                break
        else:
            at += 1
    return found


def _without(words, title):
    if not title:
        return words
    for start in range(len(words) - len(title) + 1):
        if words[start:start + len(title)] == title:
            return words[:start] + words[start + len(title):]
    return words


def _before_model(words):
    for at, word in enumerate(words[1:], 1):
        if word in MODEL_MARKERS:
            return words[:at]
    return words


def _title(text):
    return [word for word in tokens(text) if word not in TITLE_NOISE]


def classify(name, titles=()):
    """One file name -> {'group', 'label', 'confidence'}; group is None when nothing matched."""
    stem = PurePath(name).stem
    words = tokens(stem)
    titles = [title for title in map(_title, titles) if title]
    for title in titles:
        if words == title:
            return {'group': MIX, 'label': 'Full mix', 'confidence': 1.0}
    for segment in reversed(BRACKETS.findall(stem)):
        found = matches(_before_model(tokens(segment)))
        if found:
            group, confidence, label = found[-1]
            return {'group': group, 'label': label, 'confidence': confidence}
    remaining = words
    for title in titles:
        remaining = _without(remaining, title)
    remaining = _before_model(remaining)
    found = matches(remaining)
    if not found:
        if remaining != words and not any(not word.isdigit() for word in remaining):
            return {'group': MIX, 'label': 'Full mix', 'confidence': 1.0}
        return {'group': None, 'label': None, 'confidence': 0.0}
    group, confidence, label = found[-1]
    if len({entry[0] for entry in found}) > 1:
        confidence = min(confidence, AMBIGUOUS_CONFIDENCE)
    return {'group': group, 'label': label, 'confidence': confidence}


def map_files(names, titles=(), overrides=None, hints=()):
    """Map a stem set. overrides[i] (a group or None) wins over the name of names[i]."""
    overrides = list(overrides or [None] * len(names))
    if len(overrides) != len(names):
        raise ValueError('Group overrides must match the stem files')
    files = []
    for name, override in zip(names, overrides):
        if override is not None and override not in ASSIGNABLE:
            raise ValueError('Assign every stem to Vocals, Drums, Harmonics, Instrumental or Full mix')
        detected = classify(name, titles)
        entry = {'name': PurePath(name).name, **detected, 'origin': 'name'}
        if override is not None:
            entry.update(group=override, confidence=1.0, origin='override')
        files.append(entry)
    return {'files': files, **review(files, hints)}


def review(files, hints=()):
    warnings = []
    stems = [entry for entry in files if entry['group'] != MIX]
    for entry in files:
        if entry['group'] is None:
            warnings.append(f"Choose a group for {entry['name']}.")
        elif entry['confidence'] < AUTO_CONFIDENCE:
            warnings.append(f"Check the group for {entry['name']}.")
    if not stems:
        warnings.append('No stem files found. Add the separated parts, not only the full song.')
    instrumental = [entry for entry in stems if entry['group'] == INSTRUMENTAL]
    parts = [entry for entry in stems if entry['group'] in ('drums', 'harmonics')]
    if instrumental and parts:
        warnings.append(f"{instrumental[0]['name']} already contains the drums and other instruments. "
                        'Remove it or remove the separate drum and instrument files.')
    if len(instrumental) > 1:
        warnings.append('More than one file looks like the instrumental. Keep one.')
    labels = [entry['label'] for entry in stems if entry['origin'] == 'name' and entry['label']]
    for label in sorted({label for label in labels if labels.count(label) > 1}):
        warnings.append(f'More than one file looks like {label}. Check that no part is counted twice.')
    label_set = {entry['label'] for entry in stems if entry['label']}
    suno = ('suno' in {word for hint in hints for word in tokens(hint)}
            or any('suno' in tokens(entry['name']) for entry in files)
            or bool(label_set) and label_set <= SUNO_LABELS and bool(label_set & SUNO_ONLY))
    return {'verdict': 'needs_review' if warnings else 'auto', 'source': 'suno' if suno else 'generic',
            'layout': 'vocals_instrumental' if instrumental else 'groups', 'warnings': warnings}
