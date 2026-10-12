"""Offline checks for the deferred C DJ Profile link/QR codec, excluded from the XZ runtime.

These checks do not establish a working online profile page or a QR feature in XZ Mods.
Compare dj_link.c against Python, the TypeScript fragment codec and a QR decoder.

Run in WSL Ubuntu (gcc 13). Optional, used when importable: qrcodegen (Nayuki reference), zxingcpp + PIL
(QR scan). Node is found as `node` or `node.exe` (Windows node through WSL interop); NODE=<path> overrides.
XZ_SCRATCH=<dir> puts the files Node reads somewhere Windows can see (default: a temp dir).
"""
from pathlib import Path
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
CG = REPO / 'packages' / 'creative-graph'
FIXTURES = CG / 'fixtures' / 'dj-profile'
PREFIX = 'https://vj.tools/dj#p1.'
QR_VERSION = 12
V12_M_BYTES = 287  # byte-mode capacity of version 12, ECC M

try:
    import qrcodegen as nayuki
except ImportError:
    nayuki = None
try:
    import zxingcpp
    from PIL import Image
except ImportError:
    zxingcpp = None

failures = []


def check(ok, message):
    if not ok:
        failures.append(message)
        print('FAIL', message)


# --- Python mirror of encodeProfileFragment plus the C reader's leniency ---
CONTROL = re.compile('[\u0000-\u001f\u007f-\u009f]')
JS_SPACE = '\t\n\u000b\u000c\r                  　﻿'


def text_ok(value, limit, nonblank):
    return isinstance(value, str) and len(value) <= limit and not CONTROL.search(value) and not (nonblank and value.strip(JS_SPACE) == '')


def https_ok(value):
    if not isinstance(value, str) or not value.startswith('https://') or CONTROL.search(value) or any(c in JS_SPACE for c in value):
        return False
    if len(value.encode('utf-16-le')) // 2 > 2048:
        return False
    authority = re.split(r'[/?#\\]', value[8:], maxsplit=1)[0].rsplit('@', 1)[-1]
    return authority.split(':', 1)[0] != ''


def compact(doc, nlinks=None, tagline=True):
    out = {'i': doc['dj_id'], 'n': doc['dj_name']}
    if tagline and text_ok(doc.get('tagline'), 80, False):
        out['t'] = doc['tagline']
    if isinstance(doc.get('accent'), str) and re.fullmatch(r'#[0-9a-fA-F]{6}', doc['accent']):
        out['a'] = doc['accent']
    links = [[l['kind'], l['url']] for l in doc.get('links') or [] if isinstance(l, dict)
             and text_ok(l.get('kind'), 40, True) and https_ok(l.get('url'))][:12]
    links = links[:nlinks] if nlinks is not None else links
    if links:
        out['l'] = links
    return out


def payload(obj):
    return json.dumps(obj, separators=(',', ':'), ensure_ascii=False)


def stored(data):
    out, at = bytearray(), 0
    while True:
        chunk = data[at:at + 65535]
        at += len(chunk)
        final = at >= len(data)
        out += bytes([1 if final else 0]) + len(chunk).to_bytes(2, 'little') + (len(chunk) ^ 0xffff).to_bytes(2, 'little') + chunk
        if final:
            return bytes(out)


def link_for(obj):
    return PREFIX + base64.urlsafe_b64encode(stored(payload(obj).encode())).decode().rstrip('=')


def fits(text):
    if nayuki is None:
        return len(text.encode()) <= V12_M_BYTES
    try:
        nayuki.QrCode.encode_segments([nayuki.QrSegment.make_bytes(text.encode())], nayuki.QrCode.Ecc.MEDIUM, 1, QR_VERSION, 0, False)
        return True
    except nayuki.DataTooLongError:
        return False


def decode_link(link):
    data = link[len(PREFIX):]
    raw = base64.urlsafe_b64decode(data + '=' * (-len(data) % 4))
    return zlib.decompress(raw, -15).decode()


def expected_link(doc):
    """(link, dropped, compact or None) the C encoder must produce."""
    if https_ok(doc.get('page_url')) and len(doc['page_url']) <= 1200 and fits(doc['page_url']):
        return doc['page_url'], 0, None
    full = compact(doc)
    nlinks, tagline, dropped = len(full.get('l', [])), 't' in full, 0
    while True:
        obj = compact(doc, nlinks, tagline)
        link = link_for(obj)
        if (nlinks == 0 and not tagline) or fits(link):
            return link, dropped, obj
        if nlinks:
            nlinks -= 1
        else:
            tagline = False
        dropped += 1


# --- Documents ---
def profile(dj_id, name, **extra):
    doc = {'schema': 'vj.tools.dj-profile', 'version': 1, 'dj_id': dj_id, 'revision': 'r1', 'dj_name': name, 'note': '', 'secondary_logos': []}
    doc.update(extra)
    return doc


def crafted():
    twelve = [{'kind': f'link-{i}', 'url': f'https://example.com/dj/very-long-path/{i:02d}'} for i in range(12)]
    return [
        ('unicode name', profile('djp_uni', 'Zoë Ñandú 東京 🎧', tagline='ünïcödé, 🎶 live', accent='#00AAff')),
        ('quotes and backslashes', profile('djp_q', 'DJ "Q" \\ Back/slash', tagline='He said "hi" \\ /path/ <b>&amp;</b>',
                                           links=[{'kind': 'web"site', 'url': 'https://example.com/a?b="c"&d=\\e'}])),
        ('newline tagline (page would reject: omitted)', profile('djp_nl', 'NL', tagline='line one\nline two', accent='#123456')),
        ('tab and DEL in tagline (omitted)', profile('djp_tab', 'Tab', tagline='a\tb\u007f')),
        ('empty tagline', profile('djp_empty', 'Empty', tagline='')),
        ('twelve links', profile('djp_twelve', 'Twelve', tagline='All the links', accent='#ff00ff', links=twelve)),
        ('thirteen links (first 12 used)', profile('djp_13', 'Thirteen', links=twelve + [{'kind': 'x', 'url': 'https://x.example'}])),
        ('long tagline dropped after links', profile('djp_long', 'Long', tagline='🎧' * 80, links=twelve[:2])),
        ('https page_url wins', profile('djp_page', 'Page', page_url='https://example.com/dj/page', links=twelve[:1])),
        ('http page_url ignored', profile('djp_http', 'HTTP', page_url='http://example.com/dj/page')),
        ('page_url too long for the QR', profile('djp_bigpage', 'Big', page_url='https://example.com/' + 'p' * 400)),
        ('bad accent and link kinds skipped', profile('djp_bad', 'Bad', accent='red',
                                                      links=[{'kind': ' ', 'url': 'https://a.example'}, {'kind': 'ok', 'url': 'http://a.example'},
                                                             {'kind': 'ok', 'url': 'https://ok.example/x'}])),
        ('max-length id', profile('d' * 128, 'Id')),
    ]


def documents():
    cases = json.loads((FIXTURES / 'cases.json').read_text(encoding='utf-8'))
    out = [(f'cases.json: {c["name"]}', c['document']) for c in cases['profile'] if c['valid']]
    for path in sorted((FIXTURES / 'stick-two-profiles').rglob('profile.json')):
        doc = json.loads(path.read_text(encoding='utf-8'))
        out.append((f'stick: {path.parent.name}', doc))
        # The same profile without page_url, so the fragment path runs on the fixture data too.
        if 'page_url' in doc:
            out.append((f'stick: {path.parent.name} without page_url', {k: v for k, v in doc.items() if k != 'page_url'}))
    return out + crafted()


NODE_SCRIPT = r'''
import { pathToFileURL } from 'node:url'
import { readFileSync, writeFileSync } from 'node:fs'
import { deepStrictEqual } from 'node:assert'
const [modulePath, pairsPath, resultPath] = process.argv.slice(2)
const m = await import(pathToFileURL(modulePath).href)
const pairs = JSON.parse(readFileSync(pairsPath, 'utf8'))
const results = []
const fragmentOf = (link) => link.slice(link.indexOf('#') + 1)
for (const pair of pairs) {
  const r = { name: pair.name, errors: [] }
  try {
    const decoded = await m.decodeProfileFragment(fragmentOf(pair.link))
    if (!decoded.ok) r.errors.push('TS decode failed: ' + decoded.error)
    else {
      try { deepStrictEqual(decoded.value, pair.expected) } catch (e) { r.errors.push('decoded C link != expected: ' + JSON.stringify(decoded.value)) }
    }
    const parsed = m.parseProfileDocument(pair.document)
    r.tsValid = parsed.ok
    if (parsed.ok) {
      const doc = parsed.value
      // Apply the same drops the C encoder made, then compare decoded payloads.
      if (doc.links) doc.links = doc.links.slice(0, pair.keptLinks)
      if (!pair.keptTagline) delete doc.tagline
      const ts = await m.encodeProfileFragment(doc)
      const tsDecoded = await m.decodeProfileFragment(ts)
      r.tsFragmentChars = ts.length
      if (!tsDecoded.ok) r.errors.push('TS could not decode its own fragment')
      else if (decoded.ok) {
        try { deepStrictEqual(tsDecoded.value, decoded.value) } catch (e) { r.errors.push('TS encoder payload != C payload: ' + JSON.stringify(tsDecoded.value)) }
      }
    }
  } catch (e) { r.errors.push('exception: ' + e) }
  results.push(r)
}
writeFileSync(resultPath, JSON.stringify(results))
const bad = results.filter((r) => r.errors.length)
for (const r of bad) for (const e of r.errors) console.log(`TS FAIL ${r.name}: ${e}`)
console.log(`TS ${modulePath}: ${results.length - bad.length}/${results.length} C links decode to the expected fields; ${results.filter((r) => r.tsValid).length} TS-encoded payloads compared`)
process.exitCode = bad.length ? 1 : 0
'''


def node_command():
    node = os.environ.get('NODE') or shutil.which('node') or shutil.which('node.exe')
    if not node:
        return None, None
    windows = node.lower().endswith('.exe') and shutil.which('wslpath')
    convert = (lambda p: subprocess.run(['wslpath', '-w', str(p)], check=True, capture_output=True, text=True).stdout.strip()) if windows else str
    return node, convert


def fragment_object(obj):
    """What decodeProfileFragment returns for a compact payload."""
    out = {'dj_id': obj['i'], 'dj_name': obj['n'], 'links': [{'kind': k, 'url': u} for k, u in obj.get('l', [])]}
    if 't' in obj:
        out['tagline'] = obj['t']
    if 'a' in obj:
        out['accent'] = obj['a']
    return out


def scan(matrix):
    """Renders the module matrix with a 4-module quiet zone and reads it back with zxing-cpp."""
    side, scale, quiet = len(matrix), 6, 4
    size = (side + 2 * quiet) * scale
    image = Image.new('L', (size, size), 255)
    pixels = image.load()
    for y, row in enumerate(matrix):
        for x, dark in enumerate(row):
            if dark:
                for dy in range(scale):
                    for dx in range(scale):
                        pixels[(x + quiet) * scale + dx, (y + quiet) * scale + dy] = 0
    found = zxingcpp.read_barcodes(image)
    return found[0].text if found else None


def reference(text, max_version):
    qr = nayuki.QrCode.encode_segments([nayuki.QrSegment.make_bytes(text.encode())], nayuki.QrCode.Ecc.MEDIUM, 1, max_version, -1, True)
    return [[1 if qr.get_module(x, y) else 0 for x in range(qr.get_size())] for y in range(qr.get_size())]


def main():
    with tempfile.TemporaryDirectory(prefix='xz-dj-link-') as temporary:
        temporary = Path(temporary)
        binary = temporary / 'test_dj_link'
        subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(ROOT),
                        str(ROOT / 'tests/test_dj_link.c'), str(ROOT / 'vendor/qrcodegen/qrcodegen.c'), '-o', str(binary)], check=True)

        # 1. Self-test.
        check(subprocess.run([str(binary), 'self']).returncode == 0, 'self-test')

        # 2. Links against the Python mirror and zlib.
        pairs, sizes = [], []
        for index, (name, doc) in enumerate(documents()):
            path = temporary / 'profile.json'
            path.write_text(json.dumps(doc, ensure_ascii=index % 2 == 0, indent=2 if index % 3 == 0 else None), encoding='utf-8')
            run = subprocess.run([str(binary), 'link', str(path)], check=True, capture_output=True, text=True)
            link = run.stdout.strip()
            dropped = int(run.stderr.split()[-1])
            want, want_dropped, obj = expected_link(doc)
            check(link == want, f'{name}: link\n  C:  {link}\n  py: {want}')
            check(dropped == want_dropped, f'{name}: dropped {dropped}, expected {want_dropped}')
            if obj is not None and link.startswith(PREFIX):
                check(decode_link(link) == payload(obj), f'{name}: zlib-decoded payload {decode_link(link)!r} != {payload(obj)!r}')
                full = compact(doc)
                pairs.append({'name': name, 'link': link, 'expected': fragment_object(obj), 'document': doc,
                              'keptLinks': len(obj.get('l', [])), 'keptTagline': 't' in obj or 't' not in full})
            sizes.append((name, len(link), dropped))
        print(f'links: {len(sizes)} documents checked against the Python mirror')

        # 3. The TypeScript codec, from dist (when it exports the codec) and from source (Node type stripping).
        node, convert = node_command()
        ts_ran, ts_chars = [], {}
        modules = [CG / 'src' / 'dj-profile.ts']
        dist = CG / 'dist' / 'index.js'
        if dist.exists() and 'decodeProfileFragment' in (CG / 'dist' / 'dj-profile.js').read_text(encoding='utf-8'):
            modules.insert(0, dist)
        scratch = Path(os.environ['XZ_SCRATCH']) / 'ts' if os.environ.get('XZ_SCRATCH') else Path(tempfile.mkdtemp(prefix='xz-dj-link-node-'))
        scratch.mkdir(parents=True, exist_ok=True)
        (scratch / 'check.mjs').write_text(NODE_SCRIPT, encoding='utf-8')
        (scratch / 'pairs.json').write_text(json.dumps(pairs, ensure_ascii=False), encoding='utf-8')
        ts_pending = []
        for module in modules:
            result = scratch / f'result-{module.stem}.json'
            if node is not None:
                try:
                    run = subprocess.run([node, convert(scratch / 'check.mjs'), convert(module), convert(scratch / 'pairs.json'), convert(result)],
                                         capture_output=True, text=True)
                except OSError as error:  # WSL without binfmt interop: node.exe is visible but not runnable
                    node = None
                    print(f'node not runnable here ({error.strerror}); run the TS step on the host')
                else:
                    print(run.stdout, end='')
                    check(run.returncode == 0, f'node {module.name}: exit {run.returncode}\n{run.stderr[-2000:]}')
            if node is None:
                ts_pending.append(f'node {scratch / "check.mjs"} {module} {scratch / "pairs.json"} {result}')
                continue
            results = json.loads(result.read_text(encoding='utf-8'))
            ts_ran.append(f'{module.relative_to(CG)}: {len(results)} C links decoded, {sum(1 for r in results if r.get("tsValid"))} TS-encoded payloads compared')
            ts_chars.update({r['name']: len(PREFIX) - 3 + r['tsFragmentChars'] for r in results if 'tsFragmentChars' in r})
        for line in ts_ran:
            print('TS cross-check:', line)
        if ts_pending:
            print('TS cross-check PENDING; run on a host with node (paths are this machine\'s; translate for Windows):')
            for line in ts_pending:
                print('  ' + line)

        # 4. QR: scan with zxing-cpp and compare with Nayuki's Python reference.
        texts = [(p['link'], QR_VERSION) for p in pairs[:6]]
        texts += [('https://vj.tools/dj#p1.fixture', QR_VERSION), ('https://example.com/dj/page', QR_VERSION),
                  ('x' * 1200, 40), ('Zoë 東京 🎧', 2)]
        scanned = compared = 0
        for text, max_version in texts:
            out = subprocess.run([str(binary), 'qr', text, str(max_version)], check=True, capture_output=True, text=True).stdout.split()
            side = int(out[0])
            matrix = [[int(c) for c in row] for row in out[1:]]
            check(side > 0 and len(matrix) == side and all(len(r) == side for r in matrix), f'qr {text[:40]!r}: side {side}')
            if side == 0:
                continue
            if zxingcpp is not None:
                got = scan(matrix)
                check(got == text, f'qr {text[:40]!r}: zxing read {got!r}')
                scanned += 1
            if nayuki is not None:
                check(matrix == reference(text, max_version), f'qr {text[:40]!r}: matrix differs from Nayuki Python reference')
                compared += 1
        too_long = subprocess.run([str(binary), 'qr', 'y' * (V12_M_BYTES + 1), str(QR_VERSION)], check=True, capture_output=True, text=True).stdout.split()
        check(too_long[0] == '0', 'qr: 288 bytes must not fit version 12 M')
        just = subprocess.run([str(binary), 'qr', 'y' * V12_M_BYTES, str(QR_VERSION)], check=True, capture_output=True, text=True).stdout.split()
        check(just[0] == str(4 * QR_VERSION + 17), 'qr: 287 bytes must fit version 12 M')
        print(f'qr: {scanned} scanned by zxing-cpp, {compared} identical to Nayuki Python reference'
              + ('' if zxingcpp else ' (zxing-cpp/Pillow not installed)') + ('' if nayuki else ' (qrcodegen not installed)'))

        print('\nlink sizes (C link, QR version at ECC M, fields dropped; TS deflate link for comparison):')
        for name, length, dropped in sizes:
            version = next((v for v in range(1, 41) if length <= capacity(v)), None)
            ts = f'  TS {ts_chars[name]}' if name in ts_chars else ''
            print(f'  {length:4d} chars  v{version or "-":<2}  dropped {dropped}{ts}  {name}')

    print('verify_dj_link:', 'FAIL' if failures else 'ok', f'({len(failures)} failures)')
    return 1 if failures else 0


def capacity(version):
    if nayuki is None:
        return V12_M_BYTES if version == QR_VERSION else 0
    total = nayuki.QrCode._get_num_data_codewords(version, nayuki.QrCode.Ecc.MEDIUM) * 8
    return (total - 4 - (8 if version < 10 else 16)) // 8


if __name__ == '__main__':
    sys.exit(main())
