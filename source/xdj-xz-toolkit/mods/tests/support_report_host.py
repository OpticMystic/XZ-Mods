"""Linux half of verify_support_report.py: build the report helper with ASan/UBSan in test mode and run every
pending-report path against a local fake HTTPS Worker that speaks the bug desk's controller-queue contract
(services/support/CONTROLLER-QUEUE.md, controller-queue.ts pendingUpload). Never contacts the network or a device.

usage: support_report_host.py --mbedtls <mbedtls-3.6.7 dir> --out <empty dir>
Writes <out>/bodies/*.json (every body the fake Worker received), <out>/saved/*.json (every stick file left behind)
and <out>/receipt.json."""
import argparse
import ast
import hashlib
import json
import os
import shutil
import socket
import ssl
import stat
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / 'support'
HOST = 'xz-mods-support.will-026.workers.dev'
KEY = '00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff'
CANARIES = ['Artist - Secret Title', '/media/usb1/Contents', 'My Private Track', '192.168.7.42', 'de:ad:be:ef:00:01',
            'serial=ABC123', KEY]


def sources():
    tree = ast.parse((SUPPORT / 'build.py').read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and node.targets[0].id == 'SOURCES':
            return ast.literal_eval(node.value.func.value).split()
    raise SystemExit('SOURCES not found')


def build(mbedtls, out):
    objects = []
    flags = ['-O1', '-g', '-std=c11', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             f'-DMBEDTLS_CONFIG_FILE="{SUPPORT / "mbedtls_xz_config.h"}"', f'-I{mbedtls / "include"}']
    procs = []
    for name in sources():
        obj = out / 'obj' / (name + '.o')
        obj.parent.mkdir(exist_ok=True)
        procs.append(subprocess.Popen(['gcc', *flags, f'-I{mbedtls / "library"}', '-c', str(mbedtls / 'library' / (name + '.c')), '-o', str(obj)]))
        objects.append(str(obj))
        if len(procs) >= 8:
            assert procs.pop(0).wait() == 0
    for p in procs:
        assert p.wait() == 0
    binary = out / 'xz-support-test'
    subprocess.run(['gcc', *flags, '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-DXZ_SUPPORT_TEST',
                    str(SUPPORT / 'support_tool.c'), *objects, '-o', str(binary)], check=True)
    return binary


def certificate(out):
    key, cert = out / 'key.pem', out / 'cert.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:prime256v1', '-nodes',
                    '-keyout', str(key), '-out', str(cert), '-days', '30', '-subj', '/CN=' + HOST,
                    '-addext', 'subjectAltName=DNS:' + HOST], check=True, capture_output=True)
    return key, cert


class Worker:
    """A local stand-in for the Worker: one TLS connection at a time, answered by `mode`."""

    def __init__(self, key, cert):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(str(cert), str(key))
        self.sock = socket.socket()
        self.sock.bind(('127.0.0.1', 0))
        self.sock.listen(4)
        self.port = self.sock.getsockname()[1]
        self.mode = 'accept'
        self.requests = []
        self.reference = 'r-4f7k-2m9q'
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            raw, _ = self.sock.accept()
            try:
                conn = self.context.wrap_socket(raw, server_side=True)
            except (ssl.SSLError, OSError):
                raw.close()
                continue
            threading.Thread(target=self.handle_and_close, args=(conn,), daemon=True).start()

    def handle_and_close(self, conn):
        try:
            self.handle(conn)
        except (ssl.SSLError, OSError):
            pass
        finally:
            conn.close()

    def handle(self, conn):
        data = b''
        while b'\r\n\r\n' not in data:
            chunk = conn.recv(65536)
            if not chunk:
                return
            data += chunk
        head, body = data.split(b'\r\n\r\n', 1)
        lines = head.decode().split('\r\n')
        headers = {k.lower(): v for k, v in (line.split(': ', 1) for line in lines[1:])}
        length = int(headers.get('content-length', '0'))
        while len(body) < length:
            body += conn.recv(65536)
        self.requests.append({'line': lines[0], 'headers': headers, 'body': body})
        mode = self.mode
        digest = hashlib.sha256(body).hexdigest()
        if mode != 'v1' and mode != 'hang' and (headers.get('content-type') != 'application/json' or headers.get('x-report-sha256') != digest
                                                or headers.get('x-report-consent') != 'private-report/v2'
                                                or headers.get('x-report-id') != json.loads(body)['report_id']):
            mode = 'bad_headers'  # what the Worker answers: 400 invalid_headers
        if mode == 'hang':
            time.sleep(4)
            return
        if mode == 'v1':
            sent = json.loads(body)
            payload = json.dumps({'schema': 'xz-support-receipt/1', 'report_id': sent['report_id'],
                                  'payload_sha256': headers['x-report-sha256'], 'private_logs': 'stored',
                                  'public_issue': 'pending'}).encode()
            status = '201 Created'
        elif mode == 'bad_headers':
            payload, status = b'{"error":"invalid_headers"}', '400 Bad Request'
        elif mode == 'reject':
            payload, status = b'{"error":"not_found"}', '404 Not Found'
        elif mode == 'bad_json':
            payload, status = b'{"report_reference":', '200 OK'
        else:
            sent = json.loads(body)
            receipt = {'schema': 'xz-controller-accepted/1', 'report_id': sent['report_id'] if mode != 'wrong_id' else 'f' * 32,
                       'report_reference': self.reference, 'payload_sha256': digest if mode != 'wrong_sha' else '0' * 64,
                       'accepted_at': 1793059200000, 'comments_pending': True}
            payload, status = json.dumps(receipt).encode(), '201 Created'
        if mode == 'accept_chunked':
            chunks = payload[:7], payload[7:]
            wire = b''.join(b'%x\r\n%s\r\n' % (len(c), c) for c in chunks) + b'0\r\n\r\n'
            conn.sendall(f'HTTP/1.1 {status}\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n'.encode() + wire)
        else:
            conn.sendall(f'HTTP/1.1 {status}\r\nContent-Type: application/json\r\nContent-Length: {len(payload)}\r\nConnection: close\r\n\r\n'.encode() + payload)


def fixtures(out):
    xz = out / 'xz'
    bridge = xz / 'dev/shm/xz-four-deck'
    bridge.mkdir(parents=True)
    (bridge / 'status.json').write_text('{"state":"WAITING","reason":"WAIT_LOOPBACK","private":"Artist - Secret Title /media/usb1/Contents/My Private Track.mp3 192.168.7.42"}')
    (bridge / 'bridge.log').write_text('1700000000 WAIT_LOOPBACK\n1700000005 READY_CH3_CH4\nde:ad:be:ef:00:01 serial=ABC123\n')
    (xz / 'dev/shm/xz-usb-coldplug.log').write_text('USB_COLDPLUG result=READY slot=1\n')
    (xz / 'dev/shm/xz-mods.log').write_text('Loaded /media/usb1/Contents/My Private Track.mp3\nTERMINATED_SIGNAL\n')
    return xz


def diary(usb, now):
    folder = usb / 'VJ.Tools/XZ-Mods-diary'
    folder.mkdir(parents=True, exist_ok=True)
    header = {'type': 'session', 'schema': 'xz-mods-diary/2', 'session_id': 'a' * 32, 'epoch': now - 120, 'monotonic_ms': 0,
              'version': '0.5.0-preview.22.7', 'firmware': '1.26', 'settings_sha256': 'b' * 64, 'slots': [1],
              'interface': 'MODERN', 'deck_view': 4, 'segment_generation': 3}
    lines = [json.dumps(header, separators=(',', ':'))]
    lines.append('{"type":"event","sequence":1,"monotonic_ms":1000,"component":"deck","state":"error","reason":"observed","notice":"none","deck":1,"slot":0,"detail_code":8302}')
    lines.append('{"type":"metrics","sequence":2,"monotonic_ms":2000,"cpu_percent":41,"free_ram_kib":51234,"audio_underrun_frames":[0,0,null,null],"sync_phase_error_ms":[null,null,3,null],"key_shift_max_us":[null,null,null,null],"usb_read_latency_us":800,"dropped_records":0}')
    lines.append('{"type":"event","sequence":3,"monotonic_ms":3000,"component":"deck","state":"playing","reason":"observed","notice":"none","deck":1,"slot":0,"detail_code":0,"title":"Artist - Secret Title"}')
    lines.append('{"type":"event","sequence":4,"monotonic_ms":4000,"component":"ui","state":"shown","reason":"observed","notice":"track_read","deck":0,"slot":0,"detail_code":0}')
    lines.append('{"type":"event","sequence":5,"monotonic_ms":5000,"component":"deck","state":"Artist - Secret Title","reason":"observed","notice":"none","deck":1,"slot":0,"detail_code":0}')
    (folder / 'segment-03.jsonl').write_text('\n'.join(lines) + '\n{"type":"event","seq')
    old = dict(header, segment_generation=2, epoch=now - 99999)
    (folder / 'segment-02.jsonl').write_text(json.dumps(old, separators=(',', ':')) + '\n')


def request(report_id, *, loader=True, topic='NOT_WORKING', text='Deck 1 stopped at 192.168.7.42 after loading', usb=None, extra='',
            before='PLAY', mods_page=7, alert='8302,0,0', deck_view=1):
    lines = ['xz-pending-request/1', 'report_id=' + report_id, 'topic=' + topic, 'text=' + text, 'version=0.5.0-preview.22.7',
             'interface=MODERN', 'before=' + before, 'entry=ALERT', f'deck_view={deck_view}', 'alert=' + alert,
             f'mods_page={mods_page}', 'settings=1,0,1,0,1,0', 'settings_sha256=' + 'c' * 64]
    if loader:
        lines += ['loader_id=L-4F7K-2M9Q', 'loader_key=' + KEY, 'installation_id=I-4F7K-2M9Q']
    if usb is not None:
        lines.append('usb=' + str(usb))
    return ('\n'.join(lines) + '\n' + extra).encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mbedtls', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / 'bodies').mkdir()
    (out / 'saved').mkdir()
    helper = build(args.mbedtls.resolve(), out)
    key, cert = certificate(out)
    worker = Worker(key, cert)
    xz = fixtures(out)
    now = int(time.time())
    env = {**os.environ, 'XZ_SUPPORT_TEST_MODE': '1', 'XZ_SUPPORT_CA_FILE': str(cert), 'XZ_SUPPORT_TEST_CONNECT_HOST': '127.0.0.1',
           'XZ_SUPPORT_TEST_ORIGIN': f'https://{HOST}:{worker.port}', 'XZ_SUPPORT_SOURCE_ROOT': str(xz),
           'XZ_SUPPORT_TEST_DEADLINE_MS': '1500', 'ASAN_OPTIONS': 'detect_leaks=1'}
    closed = socket.socket()
    closed.bind(('127.0.0.1', 0))
    offline_origin = f'https://{HOST}:{closed.getsockname()[1]}'
    closed.close()
    results = []

    def run(name, stdin, *, mode='accept', origin=None, expect_exit=0, state=None):
        worker.mode = mode
        before = len(worker.requests)
        e = dict(env)
        if origin:
            e['XZ_SUPPORT_TEST_ORIGIN'] = origin
        r = subprocess.run([str(helper), 'pending', str(out / ('state-' + (state or name)))], input=stdin, env=e, capture_output=True, timeout=60)
        stdout, stderr = r.stdout.decode(), r.stderr.decode()
        assert r.returncode == expect_exit, (name, r.returncode, stdout, stderr)
        assert 'ERROR: AddressSanitizer' not in stderr and 'runtime error' not in stderr, (name, stderr)
        assert KEY not in stdout + stderr, name
        got = worker.requests[before:]
        for i, req in enumerate(got):
            (out / 'bodies' / f'{name}-{i}.json').write_bytes(req['body'])
        results.append({'scenario': name, 'exit': r.returncode, 'stdout': stdout.strip().splitlines(), 'requests': len(got)})
        return stdout.strip().splitlines(), stderr, got

    def stick(name):
        usb = out / ('usb-' + name)
        diary(usb, now)
        return usb

    def pending_files(usb):
        folder = usb / 'VJ.Tools/XZ-Mods-reports/pending'
        found = sorted(folder.glob('*.json')) if folder.is_dir() else []
        for f in found:
            assert len(f.name) == 37 and all(c in '0123456789abcdef' for c in f.name[:32]), f.name
            (out / 'saved' / f'{usb.name}-{f.name}').write_bytes(f.read_bytes())
        return found

    # 1. Accepted: saved first, sent, the stick copy removed, the reference canonical, no tracking link.
    usb = stick('accept')
    rid = '1' * 32
    lines, _, got = run('accept', request(rid, usb=usb))
    assert lines[0] == 'SAVED_FIRST ' + rid and lines[-1] == f'SENT {rid} R-4F7K-2M9Q', lines
    assert not pending_files(usb) and len(got) == 1 and not (out / 'state-accept' / f'report-{rid}.json').exists()
    req = got[0]
    h = req['headers']
    assert req['line'] == 'POST /v2/xz/pending HTTP/1.1' and h['authorization'] == 'Bearer ' + KEY and h['x-report-id'] == rid
    assert h['x-report-sha256'] == hashlib.sha256(req['body']).hexdigest() and h['x-report-consent'] == 'private-report/v2'
    assert h['content-type'] == 'application/json' and int(h['content-length']) == len(req['body']) and 'x-loader-id' not in h
    body = json.loads(req['body'])
    for canary in CANARIES:
        assert canary not in req['body'].decode(), canary
    assert body['schema'] == 'xz-support-report/2' and body['origin'] == 'controller' and body['report_id'] == rid
    assert body['loader_id'] == 'L-4F7K-2M9Q' and body['installation_id'] == 'I-4F7K-2M9Q' and body['comments_pending'] is True
    assert body['screen_state'] == {'surface': 'STOCK', 'stock_page': 'PERFORMANCE', 'mods_page': 'UNKNOWN', 'interface': 'MODERN',
                                    'deck_view': 'FOUR', 'alert_code': 'READ_ERROR', 'counter_code': 'E-8302'}, body['screen_state']
    assert body['description'] == 'Deck 1 stopped at <redacted-ip> after loading'
    assert body['kind_hint'] == 'BUG' and body['reporter_hints'] == {'topic': 'NOT_WORKING'} and body['category'] == 'app_usb'
    items = {i['source_id']: i for i in body['private']['items']}
    assert set(items) == {'runtime_events', 'bridge_status', 'bridge_history', 'coldplug', 'usb_settings', 'usb_diary'}
    assert any(f.get('code') == 'WAIT_LOOPBACK' for f in items['bridge_status']['facts'])
    assert any(f.get('code') == 'TERMINATED_SIGNAL' for f in items['runtime_events']['facts'])
    assert items['runtime_events']['facts'][0]['version'] == '0.5.0-preview.22.7'
    diary_facts = items['usb_diary']['facts']
    assert any(f.get('detail_code') == 8302 and f.get('deck') == 1 and f.get('clock_basis') == 'device_utc' for f in diary_facts), diary_facts
    assert any(f.get('cpu_percent') == 41 and f.get('sync_phase_error_ms') == [None, None, 3, None] for f in diary_facts)
    assert any(f.get('notice') == 'track_read' for f in diary_facts)
    assert items['usb_diary']['omission_count'] >= 3  # the titled line, the bad state, the line still being written
    assert any(f.get('setting') == 'deck_view' and f.get('value') == 1 for f in items['usb_settings']['facts'])

    # 2. A chunked acceptance. An acceptance for other bytes or another report is not one: kept on the stick.
    usb = stick('chunked')
    lines, _, _ = run('chunked', request('2' * 32, usb=usb), mode='accept_chunked')
    assert lines[-1] == f'SENT {"2" * 32} R-4F7K-2M9Q', lines
    for n, mode in enumerate(('wrong_sha', 'wrong_id', 'bad_json')):
        rid = f'{3 + n}' * 32
        usb = stick(mode)
        lines, _, _ = run(mode, request(rid, usb=usb), mode=mode)
        assert lines[-1] == f'SAVED {rid} UNKNOWN' and len(pending_files(usb)) == 1, (mode, lines)

    # 3. Refused (an unregistered loader, say): kept on the stick, byte for byte what was offered.
    usb = stick('reject')
    lines, stderr, got = run('reject', request('6' * 32, usb=usb), mode='reject')
    assert lines == ['SAVED_FIRST ' + '6' * 32, f'SAVED {"6" * 32} REJECTED'] and 'PENDING_HTTP_404' in stderr, (lines, stderr)
    files = pending_files(usb)
    assert len(files) == 1 and files[0].name == f'{"6" * 32}.json' and files[0].read_bytes() == got[0]['body']

    # 4. Timeout: the request went out and no answer came. Kept on the stick; the report ID makes a relay the same report.
    usb = stick('timeout')
    started = time.time()
    lines, _, _ = run('timeout', request('7' * 32, usb=usb), mode='hang')
    assert lines[-1] == f'SAVED {"7" * 32} UNKNOWN' and time.time() - started < 10 and len(pending_files(usb)) == 1, lines

    # 5. Offline: nothing listens. 6. An older loader without an identity never contacts the network.
    usb = stick('offline')
    lines, _, got = run('offline', request('8' * 32, usb=usb), origin=offline_origin)
    assert lines[-1] == f'SAVED {"8" * 32} OFFLINE' and not got and len(pending_files(usb)) == 1, lines
    usb = stick('no-identity')
    lines, _, got = run('no-identity', request('9' * 32, usb=usb, loader=False))
    assert lines[-1] == f'SAVED {"9" * 32} NO_IDENTITY' and not got, lines
    saved = json.loads(pending_files(usb)[0].read_bytes())
    assert saved['loader_id'] is None and saved['installation_id'] is None and saved['comments_pending'] is True

    # 7. One report ID, one set of bytes: a retry with other words resends the stick copy unchanged.
    first = pending_files(usb)[0].read_bytes()
    lines, _, _ = run('no-identity-retry', request('9' * 32, usb=usb, loader=False, text='Second try'))
    assert len(pending_files(usb)) == 1 and pending_files(usb)[0].read_bytes() == first and b'Second try' not in first
    usb = stick('offline')
    lines, _, got = run('offline-then-online', request('8' * 32, usb=usb, text='Other words'))
    assert lines[-1] == f'SENT {"8" * 32} R-4F7K-2M9Q' and got[0]['body'] == (out / 'saved' / f'usb-offline-{"8" * 32}.json').read_bytes()
    assert not pending_files(usb)

    # 8. No stick: sent anyway when online; UNSAVED when also offline, and TRY AGAIN sends the bytes kept in RAM.
    lines, _, _ = run('no-stick-online', request('a' * 32, usb=out / 'missing'))
    assert lines == [f'SENT {"a" * 32} R-4F7K-2M9Q'], lines
    lines, _, _ = run('no-stick-offline', request('b' * 32, usb=out / 'missing'), origin=offline_origin, expect_exit=1, state='ram')
    assert lines == ['UNSAVED NO_STICK OFFLINE'], lines
    kept = (out / 'state-ram' / f'report-{"b" * 32}.json').read_bytes()
    lines, _, got = run('no-stick-retry', request('b' * 32, usb=out / 'missing', text='Typed again'), state='ram')
    assert lines == [f'SENT {"b" * 32} R-4F7K-2M9Q'] and got[0]['body'] == kept and not (out / 'state-ram' / f'report-{"b" * 32}.json').exists()
    lines, _, _ = run('no-usb-key', request('c' * 32), origin=offline_origin, expect_exit=1)
    assert lines == ['UNSAVED NO_STICK OFFLINE'], lines

    # 9. Read-only stick (ignored as root, which has no read-only directories).
    if os.geteuid() != 0:
        usb = stick('read-only')
        (usb / 'VJ.Tools').chmod(stat.S_IRUSR | stat.S_IXUSR)
        lines, _, _ = run('read-only', request('d' * 32, usb=usb), origin=offline_origin, expect_exit=1)
        assert lines == ['UNSAVED READ_ONLY OFFLINE'], lines
        lines, _, _ = run('read-only-online', request('e' * 32, usb=usb))
        assert lines[-1] == f'SENT {"e" * 32} R-4F7K-2M9Q', lines
        (usb / 'VJ.Tools').chmod(stat.S_IRWXU)

    # 10. A stick already holding 32 unsent reports is not filled further; other names do not count.
    usb = stick('limit')
    folder = usb / 'VJ.Tools/XZ-Mods-reports/pending'
    folder.mkdir(parents=True)
    for n in range(32):
        (folder / f'{n:032x}.json').write_text('{}')
    (folder / 'notes.json').write_text('{}')
    (folder / '.tmp-0123.json').write_text('{}')
    lines, _, _ = run('limit', request('f' * 32, usb=usb), origin=offline_origin, expect_exit=1)
    assert lines == ['UNSAVED LIMIT OFFLINE'], lines
    (folder / f'{0:032x}.json').unlink()
    lines, _, _ = run('limit-room', request('f' * 32, usb=usb), origin=offline_origin)
    assert lines[-1] == f'SAVED {"f" * 32} OFFLINE', lines

    # 11. Skipped text, NOT SURE, and the screen when MODS was opened over no stock page: the MODS page, the hub warning.
    usb = stick('skipped')
    lines, _, got = run('skipped', request('0a' * 16, usb=usb, topic='', text='', before='TAKEOVER', mods_page=7,
                                           alert='0,0,8307', deck_view=2))
    body = json.loads(got[0]['body'])
    assert body['description'] == '' and body['comments_pending'] is True and body['kind_hint'] is None and body['reporter_hints'] == {'topic': None}
    assert body['screen_state'] == {'surface': 'MODS', 'stock_page': 'UNKNOWN', 'mods_page': 'EXTRAS', 'interface': 'MODERN',
                                    'deck_view': 'MIXER', 'alert_code': 'HUB_UNSUPPORTED', 'counter_code': 'E-8307'}, body['screen_state']
    lines, _, got = run('unknown-screen', request('0b' * 16, usb=stick('unknown-screen'), before='OTHER', mods_page=34, alert='8306,0,0', deck_view=0))
    body = json.loads(got[0]['body'])
    assert body['screen_state'] == {'surface': 'MODS', 'stock_page': 'UNKNOWN', 'mods_page': 'SUPPORT', 'interface': 'MODERN',
                                    'deck_view': 'TWO', 'alert_code': 'READ_ERROR', 'counter_code': 'UNKNOWN'}, body['screen_state']
    lines, _, got = run('long-text', request('0c' * 16, usb=stick('long-text'), text='192.168.1.1 ' * 23))
    body = json.loads(got[0]['body'])
    assert len(body['description']) == 280 and '192.168' not in body['description']

    # 12. Malformed requests are refused before anything is collected, written or sent.
    usb = stick('malformed')
    good = request('0' * 32, usb=usb)
    bad = [request('0' * 31, usb=usb), request('0' * 32, usb=usb, topic='OTHER'), request('0' * 32, usb=usb, text='x' * 281),
           request('0' * 32, usb=usb, extra='colour=red\n'), request('0' * 32, usb=usb, extra='topic=NEW_IDEA\n'),
           good.replace(b'loader_id=L-4F7K-2M9Q\n', b''), good.replace(b'installation_id=I-4F7K-2M9Q\n', b''),
           good.replace(b'loader_id=L-4F7K-2M9Q', b'loader_id=l4f7k2m9q'), good.replace(b'installation_id=I-4F7K-2M9Q', b'installation_id=I-4F7K-2M9U'),
           good.replace(b'alert=8302,0,0', b'alert=1234,0,0'), good.replace(b'mods_page=7', b'mods_page=44'),
           request('0' * 32, usb=usb, text='tab\there'), request('0' * 32, usb='relative/usb'), b'x' * 5000,
           good.replace(b'xz-pending-request/1', b'xz-pending-request/2')]
    for n, stdin in enumerate(bad):
        lines, stderr, got = run(f'malformed-{n}', stdin, expect_exit=2)
        assert 'INVALID_PENDING_REQUEST' in stderr and not got and not lines, (n, lines, stderr)
    assert not pending_files(usb)

    # 14. The v1 send still carries its exact headers after the header refactor.
    state = out / 'state-v1'
    r = subprocess.run([str(helper), 'prepare', str(state), 'bridge', 'Synthetic regression', 'Synthetic only'], env=env, capture_output=True, timeout=30)
    assert r.returncode == 0, r.stderr
    digest = hashlib.sha256((state / 'draft.json').read_bytes()).hexdigest()
    worker.mode = 'v1'
    before = len(worker.requests)
    r = subprocess.run([str(helper), 'send', str(state), digest, env['XZ_SUPPORT_TEST_ORIGIN']], env=env, capture_output=True, timeout=30)
    assert r.returncode == 0 and r.stdout.startswith(b'STORED '), (r.stdout, r.stderr)
    h = worker.requests[before]['headers']
    assert worker.requests[before]['line'] == 'POST /v1/reports HTTP/1.1' and h['x-report-sha256'] == digest
    assert h['x-report-consent'] == 'public-summary-private-logs/v1' and h['authorization'].startswith('Bearer ') and 'x-loader-id' not in h
    results.append({'scenario': 'v1-send', 'exit': 0})

    (out / 'receipt.json').write_text(json.dumps({'helper_sha256': hashlib.sha256(helper.read_bytes()).hexdigest(), 'results': results}, indent=2) + '\n')
    print(f'PASS pending reports: {len(results)} scenarios (sent, chunked, wrong digest/report/JSON, refused, timeout, offline, '
          'no identity, exact-byte retries from stick and RAM, no stick, read-only, limit, skipped text, screen_state, malformed, v1 headers)')


if __name__ == '__main__':
    main()
