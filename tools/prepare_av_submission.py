"""Prepare the antivirus false-positive submission package for a built Windows release.

Collects every shipped Windows executable, DLL and installer, records SHA-256s, audits each PE file for
the cheap heuristic triggers (no version resource, no manifest, packer sections, shipped debug files,
download-and-run installer steps), and writes ready-to-paste submission text per antivirus vendor plus
an owner checklist. Nothing leaves this computer unless you pass --virustotal-lookup or
--virustotal-upload; see docs/antivirus-false-positives.md.

  python tools/prepare_av_submission.py --release-dir dist/<version>
  python tools/prepare_av_submission.py --installer XZ.Mods_0.2.1_x64-setup.exe --tag v0.2.1 --detection Trojan:Win32/Bearfoos
"""
import argparse
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

APP = Path(__file__).resolve().parents[1]
REPOSITORY = 'https://github.com/OpticMystic/XZ-Mods'
PRODUCT = 'XZ Mods'
PUBLISHER = 'OpticMystic'
INSTALLER = re.compile(r'^XZ[ .]Mods_(?P<version>.+)_x64-setup\.exe$')
PORTABLE = 'XZ-Mods-Builder-preview-win64.zip'
PE_SUFFIXES = {'.exe', '.dll', '.pyd', '.sys', '.scr', '.cpl', '.ocx'}
# Files this project compiles. Everything else in the payload comes from another publisher
# (Python, OpenSSL, NumPy, uv, NSIS plug-ins) and is listed for reference only.
FIRST_PARTY = {'xz-mods-builder.exe', 'XZ Mods.exe', 'xz-mods-service.exe', 'xz-audio-helper.exe', 'xz-overcue-check.exe'}
PACKER_SECTIONS = {'UPX0', 'UPX1', 'UPX2', '.aspack', '.adata', '.MPRESS1', '.MPRESS2', '.themida', '.vmp0', '.vmp1',
                   '.enigma1', '.enigma2', 'PEC2', '.petite', '.nsp0', '.nsp1'}
SUSPICIOUS_STRINGS = re.compile(rb'(?i)(inject|keylog|shellcode|CreateRemoteThread|WriteProcessMemory|URLDownloadToFile)')
MICROSOFT_LIMIT = 50 * 1024 * 1024

# Each vendor's own false-positive channel, checked 2026-11-03. Sources are in docs/antivirus-false-positives.md.
VENDORS = [
    {'key': 'microsoft', 'name': 'Microsoft (Defender Antivirus, SmartScreen)', 'limit_mb': 50,
     'where': 'https://www.microsoft.com/en-us/wdsi/filesubmission',
     'how': 'Choose "Software developer", sign in with a free Microsoft account, one file per submission (50 MB limit). '
            'Track it at https://www.microsoft.com/en-us/wdsi/submissionhistory. If the verdict is wrong, use the developer '
            'contact form on the result page.',
     'fields': {'Product': 'Microsoft Defender Antivirus (Windows 11). For a SmartScreen warning pick Microsoft Defender SmartScreen.',
                'Company name': PUBLISHER + ' (open-source project)',
                'Detection name': '{detection}',
                'Should be': 'Incorrectly detected as malware/malicious (false positive)'}},
    {'key': 'avast-avg', 'name': 'Avast and AVG (Gen Digital)', 'limit_mb': None,
     'where': 'https://www.avast.com/false-positive-file-form.php',
     'how': 'Upload the file on the false-positive form; it covers both Avast and AVG engines. Larger files: email '
            'DL-Virus@gendigital.com with a download link. AVG form: https://www.avg.com/submit-sample'},
    {'key': 'norton', 'name': 'Norton and Symantec (Gen Digital / Broadcom)', 'limit_mb': 90,
     'where': 'https://submit.norton.com/',
     'how': 'Answer "Yes" to "I am the vendor of the potentially mis-identified software". Uploads above about 90 MB '
            'fail; give the download URL and SHA-256 instead. Broadcom enterprise: https://symsubmit.symantec.com'},
    {'key': 'kaspersky', 'name': 'Kaspersky', 'limit_mb': None,
     'where': 'https://opentip.kaspersky.com/',
     'how': 'Scan the file or its SHA-256 on OpenTIP, then press "Submit to reanalyze" and enter your email. '
            'Fallback: email newvirus@kaspersky.com with the file in a ZIP protected by the password "infected".'},
    {'key': 'eset', 'name': 'ESET', 'limit_mb': None,
     'where': 'mailto:samples@eset.com',
     'how': 'Email samples@eset.com with the subject "False positive", the file in a ZIP or RAR protected by the '
            'password "infected", and the text below. ESET replies only if it needs more information. '
            'Reference: https://support.eset.com/en/kb141'},
    {'key': 'bitdefender', 'name': 'Bitdefender', 'limit_mb': 20,
     'where': 'https://www.bitdefender.com/submit/',
     'how': 'Choose the "False positive" category. The form rejects files above about 20 MB; for those, email '
            'virus_submission@bitdefender.com with a download link and SHA-256, or a ZIP protected by the password "infected".'},
    {'key': 'mcafee', 'name': 'McAfee (consumer) and Trellix (enterprise)', 'limit_mb': None,
     'where': 'https://www.mcafee.com/support/s/article/000001921',
     'how': 'Follow the article\'s false-positive steps or email virus_research@mcafee.com with "False positive" in '
            'the subject and the file in a ZIP protected by the password "infected". Trellix: datasubmission@trellix.com'},
    {'key': 'malwarebytes', 'name': 'Malwarebytes', 'limit_mb': None,
     'where': 'https://forums.malwarebytes.com/forum/122-false-positives/',
     'how': 'Post in the False Positives forum (free forum account) with the text below and the download link. '
            'Reference: https://help.malwarebytes.com/hc/en-us/articles/31589211404571'},
    {'key': 'sophos', 'name': 'Sophos', 'limit_mb': None,
     'where': 'https://support.sophos.com/',
     'how': 'Use "Submit a sample" on the support site, or email samples@sophos.com with the file in a ZIP protected '
            'by the password "infected". Sophos may refuse MSI files; the NSIS .exe is fine.'},
    {'key': 'trendmicro', 'name': 'Trend Micro', 'limit_mb': None,
     'where': 'https://www.trendmicro.com/en_us/about/legal/detection-reevaluation.html',
     'how': 'Use the "File Re-classify Request" form linked on that page (https://forms.office.com/r/cbngNKqZ6H). '
            'Email fallback: virus_doctor@trendmicro.com'},
]


def digest(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def entropy(data):
    if not data:
        return 0.0
    return -sum(c / len(data) * math.log2(c / len(data)) for c in collections.Counter(data).values())


def version_strings(blob):
    """Leaf strings of a VS_VERSIONINFO resource (CompanyName, FileVersion, ...)."""
    found = {}

    def walk(offset, end):
        while offset + 6 <= end:
            length, value_length, kind = struct.unpack_from('<HHH', blob, offset)
            if length < 6:
                return
            key_end = offset + 6
            while key_end + 1 < len(blob) and blob[key_end:key_end + 2] != b'\0\0':
                key_end += 2
            key = blob[offset + 6:key_end].decode('utf-16-le', 'replace')
            value_at = (key_end + 2 + 3) & ~3
            value_size = value_length * 2 if kind == 1 else value_length
            if kind == 1 and value_length:
                found[key] = blob[value_at:value_at + value_size].decode('utf-16-le', 'replace').rstrip('\0')
            walk((value_at + value_size + 3) & ~3, min(offset + length, end))
            offset = (offset + length + 3) & ~3

    walk(0, len(blob))
    return found


def audit_pe(path):
    """Header facts antivirus heuristics weigh. Returns None for files that are not PE images."""
    data = Path(path).read_bytes()
    if data[:2] != b'MZ' or len(data) < 0x40:
        return None
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        return None
    machine, section_count, _, _, _, optional_size, characteristics = struct.unpack_from('<HHIIIHH', data, pe + 4)
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    wide = magic == 0x20B
    subsystem, dll_flags = struct.unpack_from('<HH', data, optional + 68)
    directories = optional + (112 if wide else 96)
    count = struct.unpack_from('<I', data, directories - 4)[0]
    directory = [struct.unpack_from('<II', data, directories + 8 * i) for i in range(min(count, 16))]
    sections = []
    for i in range(section_count):
        at = optional + optional_size + 40 * i
        name = data[at:at + 8].rstrip(b'\0').decode('latin-1')
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from('<IIII', data, at + 8)
        sections.append({'name': name, 'rva': virtual_address, 'virtual_size': virtual_size, 'raw_size': raw_size,
                         'raw_pointer': raw_pointer,
                         'entropy': round(entropy(data[raw_pointer:raw_pointer + raw_size]), 2)})

    def offset(rva):
        for section in sections:
            if section['rva'] <= rva < section['rva'] + max(section['virtual_size'], section['raw_size']):
                return rva - section['rva'] + section['raw_pointer']
        return None

    resource_types, version = set(), {}
    if len(directory) > 2 and directory[2][0]:
        base = offset(directory[2][0])

        def entries(at):
            named, ids = struct.unpack_from('<HH', data, at + 12)
            return [struct.unpack_from('<II', data, at + 16 + 8 * i) for i in range(named + ids)]

        def first_leaf(at):
            while True:
                found = entries(base + at)
                if not found:
                    return None
                at = found[0][1]
                if not at & 0x80000000:
                    return at
                at &= 0x7FFFFFFF

        if base is not None:
            for kind, target in entries(base):
                if kind & 0x80000000:
                    continue
                resource_types.add(kind)
                if kind == 16 and target & 0x80000000:
                    leaf = first_leaf(target & 0x7FFFFFFF)
                    if leaf is not None:
                        rva, size = struct.unpack_from('<II', data, base + leaf)
                        start = offset(rva)
                        if start is not None:
                            version = version_strings(data[start:start + size])
    pdb = None
    if len(directory) > 6 and directory[6][0]:
        at = offset(directory[6][0])
        for i in range((directory[6][1] // 28) if at is not None else 0):
            kind, size, _, raw = struct.unpack_from('<IIII', data, at + 28 * i + 12)
            if kind == 2 and data[raw:raw + 4] == b'RSDS':
                pdb = data[raw + 24:raw + size].split(b'\0')[0].decode('utf-8', 'replace')
    end = max((s['raw_pointer'] + s['raw_size'] for s in sections), default=0)
    signed = len(directory) > 4 and directory[4][1] > 0
    return {
        'machine': {0x14C: 'x86', 0x8664: 'x64', 0xAA64: 'arm64'}.get(machine, hex(machine)),
        'dll': bool(characteristics & 0x2000),
        'subsystem': {2: 'gui', 3: 'console'}.get(subsystem, subsystem),
        'aslr': bool(dll_flags & 0x40), 'dep': bool(dll_flags & 0x100), 'cfg': bool(dll_flags & 0x4000),
        'authenticode': signed,
        'version': version,
        'manifest': 24 in resource_types,
        'icon': 14 in resource_types,
        'pdb_path': pdb,
        'sections': [{k: s[k] for k in ('name', 'raw_size', 'entropy')} for s in sections],
        'overlay_bytes': max(0, len(data) - end - (directory[4][1] if signed else 0)),
        'packer_sections': sorted({s['name'] for s in sections} & PACKER_SECTIONS),
        'suspicious_strings': sorted({m.group(0).decode('latin-1').lower() for m in SUSPICIOUS_STRINGS.finditer(data)}),
    }


def starts_with_mz(path):
    with open(path, 'rb') as stream:
        return stream.read(2) == b'MZ'


def find_7zip():
    for candidate in (shutil.which('7z'), shutil.which('7za'),
                      Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / '7-Zip/7z.exe',
                      Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / '7-Zip/7z.exe'):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


def collect_payload(installer, app_exe, resources, workspace, extract):
    """Return (root, how) for the files the installer installs."""
    sevenzip = find_7zip() if extract else None
    if sevenzip:
        root = workspace / 'payload'
        subprocess.run([str(sevenzip), 'x', '-y', '-bso0', '-bsp0', '-o' + str(root), str(installer)], check=True)
        return root, 'extracted from the installer with 7-Zip'
    if not app_exe or not resources:
        raise SystemExit('Install 7-Zip to read the installer, or pass --app-exe and --resources from the build tree.')
    root = workspace / 'payload'
    root.mkdir()
    shutil.copyfile(app_exe, root / 'xz-mods-builder.exe')
    shutil.copytree(resources, root / 'resources')
    return root, 'copied from the build tree (--app-exe, --resources); install 7-Zip to read the installer itself'


def priority(item):
    """Order first-party files the way they should be submitted: the app, its service, the installer, helpers."""
    order = ['xz-mods-builder.exe', 'XZ Mods.exe', 'xz-mods-service.exe']
    name = Path(item['name']).name
    return (order.index(name) if name in order else 3 if item['kind'] == 'installer' else 4, item['name'])


def classify(name):
    return 'first-party' if Path(name).name in FIRST_PARTY or INSTALLER.match(Path(name).name) else 'third-party'


def findings_for(artifacts, payload_names):
    found = []

    def add(level, path, text):
        found.append({'level': level, 'file': path, 'finding': text})

    for item in artifacts:
        pe, name = item.get('pe'), item['name']
        if item['origin'] == 'first-party' and pe:
            if not pe['version']:
                add('fix', name, 'No version resource (publisher, product, version). Unlabelled executables score worse.')
            else:
                missing = [key for key in ('CompanyName', 'ProductName', 'FileVersion', 'LegalCopyright')
                           if not pe['version'].get(key)]
                if missing:
                    add('fix', name, 'Version resource lacks ' + ', '.join(missing) + '.')
            if not pe['dll'] and not pe['manifest']:
                add('fix', name, 'No application manifest.')
            if not pe['authenticode']:
                add('info', name, 'Not Authenticode-signed. Expected until the project has a code-signing certificate.')
            if pe['size'] > MICROSOFT_LIMIT:
                add('info', name, 'Larger than the 50 MB Microsoft upload limit; submit the smaller files and quote this SHA-256 and URL.')
            if pe['suspicious_strings']:
                add('info', name, 'Contains strings scanners look for (' + ', '.join(pe['suspicious_strings']) +
                    '); in this project they come from linked libraries (rustls key logging, WebView2 hooks).')
        if pe and pe['packer_sections']:
            add('fix', name, 'Packed with ' + ', '.join(pe['packer_sections']) + '. Packers are a strong malware signal; ship unpacked.')
    for name in payload_names:
        lower = name.lower()
        if lower.endswith('.pdb'):
            add('fix', name, 'Debug database shipped in the installer. Remove it from resources/.')
        if lower.endswith('nsisdl.dll'):
            add('fix', name, 'The installer downloads an executable and runs it (WebView2 downloadBootstrapper). '
                'Use embedBootstrapper in src-tauri/tauri.conf.json.')
    for item in artifacts:
        if item['name'].replace('\\', '/').startswith('resources/runtime/') and item.get('pe'):
            add('fix', item['name'], 'Windows executable inside the XDJ-XZ runtime folder. Player-only tools must not ship as Windows PE files.')
    return found


def submission_text(vendor, context):
    files = '\n'.join('  {}  {}  ({:,} bytes)'.format(a['sha256'], a['name'], a['size'])
                      for a in sorted((a for a in context['artifacts'] if a['origin'] == 'first-party'), key=priority))
    fields = ''.join('{}: {}\n'.format(k, v.format(**context)) for k, v in vendor.get('fields', {}).items())
    return f"""Where: {vendor['where']}
How: {vendor['how']}
{('Form fields:' + chr(10) + fields) if fields else ''}
---- paste below this line ----
False positive report: {PRODUCT} {context['version']} for Windows x64

Software: {PRODUCT} {context['version']} (Windows x64)
Publisher: {PUBLISHER}, open-source project under the MIT licence
Source code: {REPOSITORY}
Release page: {context['release_url']}
Installer download: {context['installer_url']}
Installer SHA-256: {context['installer_sha256']}
Detection name: {context['detection']}

What it is: {PRODUCT} is an open-source desktop app for owners of the Pioneer DJ XDJ-XZ controller. It prepares a
USB drive that adds an optional mod loader to the controller and prepares song stems for it. It is built with Tauri
(Rust and Microsoft WebView2) and runs a bundled Python backend frozen with PyInstaller. The installer comes from
Tauri's NSIS bundler and installs for the current user without administrator rights.

Why we believe this is a false positive: every executable listed below is built from the public source code at the
address above, and the release publishes its SHA-256 checksums. The files are not Authenticode-signed yet because the
project cannot afford a code-signing certificate; detections on this release look like heuristic or machine-learning
verdicts on a new unsigned build rather than any malicious behaviour.

Network behaviour you may see: when the user prepares a USB drive, the app downloads the official XDJ-XZ firmware
from the manufacturer's (AlphaTheta) download servers and checks it against pinned hashes. If the user turns on stem
generation, the bundled uv tool (by Astral) downloads Python and PyTorch into the app's own data folder. The updater
downloads signed {PRODUCT} updates from the GitHub release page above. The app does not change Windows system files
or other programs.

First-party files in this release (SHA-256):
{files}

Contact: {REPOSITORY}/issues
"""


def checklist(context, upload_dir):
    first = sorted((a for a in context['artifacts'] if a['origin'] == 'first-party'), key=priority)
    rows = []
    for vendor in VENDORS:
        limit = vendor['limit_mb']
        fits = [a['name'] for a in first if limit is None or a['size'] <= limit * 1024 * 1024]
        too_big = [a['name'] for a in first if a['name'] not in fits]
        rows.append('| {} | {} | `submissions/{}.txt` | {} |'.format(
            vendor['name'], vendor['where'], vendor['key'],
            ', '.join('`' + Path(n).name + '`' for n in fits) +
            ('; quote hash + URL for ' + ', '.join('`' + Path(n).name + '`' for n in too_big) if too_big else '')))
    fixes = [f for f in context['findings'] if f['level'] == 'fix']
    fix_lines = '\n'.join('- `{}`: {}'.format(f['file'], f['finding']) for f in fixes) or '- None found.'
    return f"""# Antivirus false-positive checklist: {PRODUCT} {context['version']}

Generated {context['generated']} from `{context['installer_name']}` (payload {context['payload_how']}).
Nothing in this folder has been uploaded anywhere. Uploading is your decision.

Release: {context['release_url']}
Installer: {context['installer_url']}
Installer SHA-256: `{context['installer_sha256']}`

## Before publishing

1. Fix every "fix" finding below, or accept it on purpose.
2. Optional: rerun with `--defender-scan` to scan the files with this PC's Microsoft Defender
   (Defender's own sample-submission setting decides whether anything is sent to Microsoft).
3. Optional: rerun with `--virustotal-lookup` (sends hashes only) after you have published, to see which engines flag it.

## After publishing: submit to Microsoft first

Microsoft Defender is what most Windows users run. At https://www.microsoft.com/en-us/wdsi/filesubmission choose
"Software developer" and sign in, then submit one file per submission, in the order of the table below. When a user
reported a detection, start with the file their Windows Security "Protection history" names. Microsoft asks for the
specific files rather than whole installers, and files above 50 MB cannot be uploaded; quote their SHA-256 and the
download URL in the text instead. Paste `submissions/microsoft.txt` into "Additional information". Check results at
https://www.microsoft.com/en-us/wdsi/submissionhistory and press "Rescan" there after Microsoft updates its verdict.

Submit to the other vendors only when one of them flags the release (check VirusTotal or the user's report). Each text
file in `submissions/` starts with where to send it and how.

| Vendor | Where | Text | Upload these |
| --- | --- | --- | --- |
{chr(10).join(rows)}

Files for upload are in `{upload_dir.name}/`; the installer is `{context['installer_path']}`. When a vendor asks for an archive, zip the file with the password
`infected` (7-Zip: `7z a -tzip -pinfected sample.zip <file>`). If a vendor flags `uninstall.exe`, it is created by
the installer at `%LOCALAPPDATA%\\{PRODUCT}\\uninstall.exe`.

## Findings

{fix_lines}

All files, SHA-256s and PE details: `manifest.json`, `SHA256SUMS.txt`.
"""


def defender_scan(paths):
    tool = Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'Windows Defender/MpCmdRun.exe'
    if not tool.is_file():
        return {'available': False}
    results = []
    for path in paths:
        run = subprocess.run([str(tool), '-Scan', '-ScanType', '3', '-File', str(path), '-DisableRemediation'],
                             capture_output=True, text=True, errors='replace')
        results.append({'file': str(path), 'exit_code': run.returncode, 'threat_found': run.returncode == 2,
                        'output': run.stdout.strip()[-2000:]})
    return {'available': True, 'results': results}


class VirusTotal:
    """VirusTotal public API v3. Needs a free account key in VT_API_KEY; the free tier allows 4 requests a minute."""
    API = 'https://www.virustotal.com/api/v3'

    def __init__(self, key, opener=urllib.request.urlopen, pause=15.0):
        self.key, self.opener, self.pause, self.last = key, opener, pause, 0.0

    def request(self, url, body=None, content_type=None):
        wait = self.last + self.pause - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        headers = {'x-apikey': self.key, 'accept': 'application/json'}
        if content_type:
            headers['content-type'] = content_type
        try:
            with self.opener(urllib.request.Request(url, data=body, headers=headers), timeout=600) as response:
                return response.status, json.loads(response.read() or b'{}')
        except urllib.error.HTTPError as error:
            with error:
                return error.code, json.loads(error.read() or b'{}')
        finally:
            self.last = time.monotonic()

    def lookup(self, sha256):
        status, body = self.request(f'{self.API}/files/{sha256}')
        if status == 404:
            return {'known': False}
        if status != 200:
            return {'error': status, 'detail': body.get('error')}
        attributes = body['data']['attributes']
        flagged = {engine: result.get('result') for engine, result in attributes.get('last_analysis_results', {}).items()
                   if result.get('category') in ('malicious', 'suspicious')}
        return {'known': True, 'stats': attributes.get('last_analysis_stats'), 'flagged_by': flagged,
                'report': f'https://www.virustotal.com/gui/file/{sha256}'}

    def upload(self, path):
        url = f'{self.API}/files'
        if path.stat().st_size > 32 * 1024 * 1024:
            status, body = self.request(f'{self.API}/files/upload_url')
            if status != 200:
                return {'error': status, 'detail': body.get('error')}
            url = body['data']
        boundary = 'xz-mods-' + hashlib.sha256(path.name.encode()).hexdigest()[:24]
        payload = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{path.name}"\r\n'
                   'Content-Type: application/octet-stream\r\n\r\n').encode() + path.read_bytes() + f'\r\n--{boundary}--\r\n'.encode()
        status, body = self.request(url, payload, 'multipart/form-data; boundary=' + boundary)
        return {'uploaded': status == 200, 'status': status, 'analysis': (body.get('data') or {}).get('id')}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--release-dir', type=Path, help='Output folder of tools/build_release.py')
    source.add_argument('--installer', type=Path, help='A Windows installer, for example one downloaded from an older release')
    parser.add_argument('--output', type=Path, help='Default: <release-dir>-av-submission beside the release folder')
    parser.add_argument('--tag', help='GitHub release tag for download links. Default: from latest.json, else v<version>')
    parser.add_argument('--detection', default='none reported yet (new release submitted before users report it)',
                        help='Detection name from the report, for example Trojan:Win32/Bearfoos')
    parser.add_argument('--app-exe', type=Path, help='Built app exe; used with --resources when 7-Zip is not installed')
    parser.add_argument('--resources', type=Path, help='Prepared resources folder matching the installer')
    parser.add_argument('--no-extract', action='store_true', help='Use --app-exe and --resources even if 7-Zip is installed')
    parser.add_argument('--defender-scan', action='store_true', help='Scan the files with this PC\'s Microsoft Defender')
    parser.add_argument('--virustotal-lookup', action='store_true', help='Send SHA-256s (not files) to VirusTotal. Needs VT_API_KEY')
    parser.add_argument('--virustotal-upload', action='store_true',
                        help='Upload first-party files VirusTotal does not know yet. They become available to VirusTotal '
                             'partners. Needs VT_API_KEY. Never runs without this flag')
    args = parser.parse_args(argv)

    release_dir = args.release_dir.resolve() if args.release_dir else None
    if release_dir:
        found = [p for p in release_dir.iterdir() if INSTALLER.match(p.name)]
        if len(found) != 1:
            raise SystemExit('Expected exactly one XZ Mods installer in ' + str(release_dir))
        installer = found[0]
    else:
        installer = args.installer.resolve()
    match = INSTALLER.match(installer.name)
    if not match:
        raise SystemExit('Not an XZ Mods installer name: ' + installer.name)
    version = match['version']
    tag = args.tag
    feed = release_dir / 'latest.json' if release_dir else None
    if not tag and feed and feed.is_file():
        url = json.loads(feed.read_text(encoding='utf-8'))['platforms']['windows-x86_64']['url']
        tag = re.search(r'/releases/download/([^/]+)/', url)[1]
    tag = tag or 'v' + version
    asset = installer.name.replace(' ', '.')
    output = (args.output or (release_dir.with_name(release_dir.name + '-av-submission') if release_dir
                              else installer.parent / ('XZ-Mods-' + version + '-av-submission'))).resolve()
    if output.exists():
        raise SystemExit('Output already exists: ' + str(output))
    if args.virustotal_lookup or args.virustotal_upload:
        if not os.environ.get('VT_API_KEY'):
            raise SystemExit('Set VT_API_KEY to your VirusTotal API key to use the VirusTotal options.')

    with tempfile.TemporaryDirectory(prefix='xz-av-') as temporary:
        payload, payload_how = collect_payload(installer, args.app_exe, args.resources, Path(temporary), not args.no_extract)
        payload_names = sorted(p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file())
        artifacts = []

        def record(path, name, kind):
            item = {'name': name, 'kind': kind, 'origin': classify(name), 'size': path.stat().st_size, 'sha256': digest(path)}
            pe = audit_pe(path) if path.suffix.lower() in PE_SUFFIXES or kind != 'payload' else None
            if pe:
                pe['size'] = item['size']
                item['pe'] = pe
            artifacts.append(item)
            return item

        installer_item = record(installer, asset, 'installer')
        if release_dir and (release_dir / PORTABLE).is_file():
            record(release_dir / PORTABLE, PORTABLE, 'portable-zip')
        payload_paths = {}
        for name in payload_names:
            path = payload / name
            if path.suffix.lower() in PE_SUFFIXES or starts_with_mz(path):
                record(path, name, 'payload')
                payload_paths[name] = path
        # The NSIS plug-ins are unpacked to a temporary folder at install time; they are part of the installer.
        context = {
            'product': PRODUCT, 'publisher': PUBLISHER, 'version': version, 'tag': tag,
            'release_url': f'{REPOSITORY}/releases/tag/{tag}',
            'installer_url': f'{REPOSITORY}/releases/download/{tag}/{asset}',
            'installer_name': asset, 'installer_sha256': installer_item['sha256'], 'installer_path': str(installer),
            'detection': args.detection, 'payload_how': payload_how,
            'generated': time.strftime('%Y-%m-%d %H:%M:%S %z'),
            'artifacts': artifacts,
        }
        context['findings'] = findings_for(artifacts, payload_names)

        output.mkdir(parents=True)
        upload = output / 'upload'
        upload.mkdir()
        for item in artifacts:
            if item['kind'] == 'payload' and item['origin'] == 'first-party':
                shutil.copyfile(payload_paths[item['name']], upload / Path(item['name']).name)
        submissions = output / 'submissions'
        submissions.mkdir()
        for vendor in VENDORS:
            (submissions / (vendor['key'] + '.txt')).write_text(submission_text(vendor, context), encoding='utf-8')
        if args.defender_scan:
            context['defender_scan'] = defender_scan([installer, *sorted(upload.iterdir())])
        if args.virustotal_lookup or args.virustotal_upload:
            client = VirusTotal(os.environ['VT_API_KEY'])
            report = {}
            for item in artifacts:
                if item['origin'] != 'first-party':
                    continue
                result = client.lookup(item['sha256'])
                if args.virustotal_upload and result.get('known') is False:
                    path = installer if item['kind'] == 'installer' else upload / Path(item['name']).name
                    if item['kind'] == 'portable-zip':
                        path = release_dir / PORTABLE
                    result['upload'] = client.upload(path)
                report[item['name']] = result
            context['virustotal'] = report
        (output / 'SHA256SUMS.txt').write_text(''.join(f"{a['sha256']}  {a['name']}\n" for a in artifacts), encoding='utf-8')
        (output / 'manifest.json').write_text(json.dumps(context, indent=2) + '\n', encoding='utf-8')
        (output / 'CHECKLIST.md').write_text(checklist(context, upload), encoding='utf-8')

    fixes = [f for f in context['findings'] if f['level'] == 'fix']
    print(f'Antivirus submission package: {output}')
    print(f"{len(artifacts)} Windows files recorded, {sum(a['origin'] == 'first-party' for a in artifacts)} first-party. Nothing was uploaded"
          + (' except the VirusTotal requests you asked for.' if 'virustotal' in context else '.'))
    for finding in fixes:
        print(f"FIX  {finding['file']}: {finding['finding']}")
    if 'defender_scan' in context and context['defender_scan'].get('available'):
        flagged = [r['file'] for r in context['defender_scan']['results'] if r['threat_found']]
        print('Defender: ' + ('flagged ' + ', '.join(flagged) if flagged else 'no threats found'))
    print('Next: open CHECKLIST.md in that folder.')
    return context


if __name__ == '__main__':
    main()
