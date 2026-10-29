"""Fail on brand names in what an XZ Mods user sees: the desktop app, the manual and the XZ's own screens.

  python tools/check_trademarks.py        # one line per hit; exit 1 when there is any

XZ Mods names one other product: the XDJ-XZ, as the player it works with. It names no DJ software, no maker and
no other player, and shows no logo. Say what is meant instead: "your DJ software", "the music your DJ software
exported to the USB", "the button above LINK".

What is checked, and only the parts a user reads:
  - the desktop app (ui/): text and attributes in HTML, string literals in JS and CSS, SVG titles; ui/manual is the
    manual as the app shows it
  - the manual: manual/chapters, manual.json, and the menu map's screen titles and moves
  - the XZ: string literals in source/xdj-xz-toolkit/mods (not tests, not log lines), and the labels in
    settings-schema.json
  - the Python backend's messages, which the app shows (source/xdj-xz-toolkit/builder, string literals)

Internal names the user never reads may keep the old words: setting keys stored on the USB, folder names on the USB
(the export lives in PIONEER/rekordbox, stems in CDJMODS), element ids, field names of the app's API, enum values of
the report contract. ALLOW lists each one; anything new is a deliberate entry here with its reason. SKIP lists the
files that are history (release notes of builds already shipped) or machine contracts.

build_manual.py check and the release gate run this.
"""
import json
from html.parser import HTMLParser
from pathlib import Path
import re
import sys

APP = Path(__file__).resolve().parents[1]
MODS = 'source/xdj-xz-toolkit/mods'
TERMS = re.compile(r'rekordbox|pioneer|serato|alphatheta|traktor|\bdjay\b|virtual ?dj|\bcdjs?\b|\bcdj-?\d', re.I)

# Internal names, removed before TERMS is matched. Each says why the user never reads it.
ALLOW = [
    (r'rekordbox_(?:press|shift|menu|assign|running|verified|choice\w*|names|help|order)\b', 'setting keys on the USB, '
     'API fields and C symbols of the button above LINK and the library check'),
    (r'requires_rekordbox_export', 'an API field'),
    (r'repair-rekordbox-block', 'an element id'),
    (r'\bPIONEER(?=[\\/]|\\\\)|(?<=[\\/])rekordbox(?=[\\/.]|$)|^PIONEER$|USBANLZ', 'folder names on the USB, where the '
     'exported library lives'),
    (r'rekordbox\.exe|\(\?i\)rekordbox|-ix rekordbox', 'the process name the library repair checks is closed'),
    (r'pioneerdj_xdj_xz[\w./]*|www\.pioneerdj\.com/[\w/.-]*', "file names and the page of the player maker's GPL source "
     'archive, which the loader is built from'),
    (r'^PioneerDJ$', "the USB device name the player reports, compared in C"),
    (r'\bCDJMODS\b', 'the stems folder on the USB, shared with other tools'),
]
ALLOWED = re.compile('|'.join(f'(?:{a})' for a, _ in ALLOW), re.I | re.M)

SKIP = {
    'ui/mod-changes.js': 'what changed in each build already shipped, generated from mod-changes.json history',
    'ui/support-contract.js': 'the report contract shared with the support service; its enum values are keys, the '
                              'wizard shows its own words (ui/support.js)',
    'source/xdj-xz-toolkit/builder/support_contract.py': 'the same report contract, embedded for the backend',
}


def find(text):
    """The brand names in text, after the internal names are taken out."""
    return [m.group(0) for m in TERMS.finditer(ALLOWED.sub(' ', text))]


# ---------------------------------------------------------------- what a user reads in each kind of file

STRING = re.compile(r"""'(?:[^'\\\n]|\\.)*'|"(?:[^"\\\n]|\\.)*"|`(?:[^`\\]|\\.)*`""", re.S)


def code_strings(text, line_comment='//', at=False):
    """(line, string) for every string literal in JS, CSS or C, comments skipped; (line, string, offset) with at."""
    out, i, n, line = [], 0, len(text), 1
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1; i += 1
        elif line_comment and text.startswith(line_comment, i):
            i = text.find('\n', i)
            i = n if i < 0 else i
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            line += text.count('\n', i, j); i = j
        elif c in '\'"`':
            m = STRING.match(text, i)
            if not m:
                i += 1; continue
            out.append((line, m.group(0)[1:-1], i) if at else (line, m.group(0)[1:-1]))
            line += m.group(0).count('\n'); i = m.end()
        else:
            i += 1
    return out


class _Html(HTMLParser):
    CODE = {'id', 'class', 'for', 'href', 'src', 'name', 'type', 'rel', 'role', 'aria-controls', 'aria-labelledby',
            'aria-describedby', 'data-open', 'data-page', 'data-key', 'data-manual-open'}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.found, self.skip = [], 0

    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style'): self.skip += 1
        for k, v in attrs:
            if v and k not in self.CODE and not k.startswith('data-'): self.found.append((self.getpos()[0], v))

    def handle_endtag(self, tag):
        if tag in ('script', 'style'): self.skip = max(0, self.skip - 1)

    def handle_data(self, data):
        if not self.skip and data.strip(): self.found.append((self.getpos()[0], data))


def html_text(text):
    p = _Html()
    p.feed(text)
    return p.found


LOG = re.compile(r'\b(?:xz_log|fprintf|printf|perror|puts|dbg|log_line|_Static_assert|static_assert)\s*\(|#\s*include')


def c_strings(text):
    """String literals in C, except in log calls and compile-time asserts. A literal's statement starts after the last
    ; { or } before it."""
    out = []
    for n, s, i in code_strings(text, at=True):
        start = max(text.rfind(c, 0, i) for c in ';{}') + 1
        if not LOG.search(text[start:i]): out.append((n, s))
    return out


def py_strings(text):
    """Sentences in Python string literals: the messages the app shows. Docstrings, comments and single words (dict
    keys, file names) are not read by a user."""
    import io
    import tokenize
    out = []
    kinds = {tokenize.STRING, getattr(tokenize, 'FSTRING_MIDDLE', -1)}
    for tok in tokenize.generate_tokens(io.StringIO(text).readline):
        s = tok.string
        if tok.type not in kinds or s.lstrip('rbfuRBFU').startswith(('"""', "'''")): continue
        if ' ' in s: out.append((tok.start[0], s))
    return out


def json_values(text, keys):
    """(0, value) for every string value under one of `keys`, anywhere in the document."""
    out = []

    def walk(v, key=None):
        if isinstance(v, dict):
            for k, x in v.items(): walk(x, k)
        elif isinstance(v, list):
            for x in v: walk(x, key)
        elif isinstance(v, str) and (keys is None or key in keys):
            out.append((0, v))
    walk(json.loads(text))
    return out


def whole(text):
    return [(n, l) for n, l in enumerate(text.split('\n'), 1)]


def targets():
    """(repo path, function returning (line, text) pairs) for every file a user reads."""
    out = []
    for p in sorted((APP / 'ui').rglob('*')):
        rel = p.relative_to(APP).as_posix()
        if not p.is_file(): continue
        kind = {'.js': code_strings, '.css': lambda t: code_strings(t, None), '.html': html_text, '.svg': whole}.get(p.suffix)
        if kind: out.append((rel, kind))
    for p in sorted((APP / 'manual/chapters').glob('*.md')): out.append((p.relative_to(APP).as_posix(), whole))
    out.append(('manual/manual.json', whole))
    out.append(('manual/menu-tree/menu-tree.json', lambda t: json_values(t, {'title', 'label', 'control', 'target', 'path'})))
    out.append((f'{MODS}/settings-schema.json', lambda t: json_values(t, {'label'})))
    for p in sorted((APP / MODS).rglob('*')):
        rel = p.relative_to(APP).as_posix()
        if p.suffix not in ('.c', '.h') or not p.is_file(): continue
        if re.search(r'(^|/)(tests?|integration|vendor|third_party)/|(^|/)test_|_test\.|fixture', rel): continue
        out.append((rel, c_strings))
    for p in sorted((APP / 'source/xdj-xz-toolkit/builder').rglob('*.py')):
        rel = p.relative_to(APP).as_posix()
        if re.search(r'(^|/)test|/tests/', rel): continue
        out.append((rel, py_strings))
    return [(rel, read) for rel, read in out if rel not in SKIP]


def problems():
    found = []
    for rel, read in targets():
        path = APP / rel
        if not path.is_file(): continue
        text = path.read_text(encoding='utf-8-sig', errors='replace').replace('\r\n', '\n')
        for line, s in read(text):
            hits = find(s)
            if hits:
                where = f'{rel}:{line}' if line else rel
                found.append(f'trademarks: {where}: {", ".join(sorted(set(hits)))} in {s.strip()[:120]!r}')
    return found


def main():
    found = problems()
    for line in found: print(line)
    print('trademarks: none in what a user sees' if not found else
          f'trademarks: {len(found)} hit(s). Say what is meant without the brand, or add an internal name to ALLOW in '
          'tools/check_trademarks.py with its reason')
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
