"""Play Wave Rider's actual C renderer using exported 2EX waveforms. No hardware or audio control."""
import argparse
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import math
from pathlib import Path
import struct
import subprocess
import threading
import time
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--analysis', nargs='+', required=True, type=Path, help='One 3-band 2EX, or drums harmonics vocal 2EX files in that order')
parser.add_argument('--port', default=8776, type=int)
args = parser.parse_args()
root = Path(__file__).resolve().parent
if len(args.analysis) not in (1, 3): parser.error('Provide one 3-band file or three stem files')

def pwv7(path):
    data = path.read_bytes()
    if data[:4] != b'PMAI': raise ValueError('Expected Rekordbox analysis')
    at, end = struct.unpack_from('>II', data, 4)
    if end > len(data): raise ValueError('Truncated analysis')
    while at + 12 <= end:
        head, size = struct.unpack_from('>II', data, at + 4)
        if head < 12 or size < head or at + size > end: raise ValueError('Invalid analysis section')
        if data[at:at+4] == b'PWV7':
            width, count = struct.unpack_from('>II', data, at + 12)
            if width != 3 or not 0 < count <= 1000000 or head + count*3 > size: raise ValueError('Invalid PWV7')
            return data[at+head:at+head+count*3]
        at += size
    raise ValueError('No real three-band waveform in analysis')

parts = [pwv7(path) for path in args.analysis]
kind = len(parts) == 3
if kind:
    count = max(len(part)//3 for part in parts)
    bands = bytearray(count*3)
    for channel, part in enumerate(parts):
        for i in range(len(part)//3): bands[i*3+channel] = min(255, sum(part[i*3:i*3+3]))
else:
    bands = parts[0]
    count = len(bands)//3
levels = sorted(sum(bands[i*3:i*3+3]) for i in range(count))
normalization = max(1, levels[math.ceil(count*.995)-1])
args.output.mkdir(parents=True, exist_ok=True)
dll = args.output.resolve()/'wave-rider-preview.dll'
subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-DXZ_WAVE_RIDER_SOURCE_PORTABLE', '-shared', *[str(root/name) for name in
    ('wave_rider.c', 'wave_rider_render.c', 'wave_rider_source.c', 'wave_rider_preview.c')],
    str(root.parent/'ui/ui_draw.c'), '-o', str(dll)], check=True)
game = ctypes.CDLL(str(dll))
game.rider_preview_load.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_char_p, ctypes.c_uint]
game.rider_preview_step.argtypes = [ctypes.c_uint]
game.rider_preview_key.argtypes = [ctypes.c_int, ctypes.c_uint]
game.rider_preview_move.argtypes = [ctypes.c_float, ctypes.c_uint]
game.rider_preview_touch.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
game.rider_preview_seek.argtypes = [ctypes.c_double]
game.rider_preview_frame.restype = ctypes.POINTER(ctypes.c_uint16)
game.rider_preview_status.restype = ctypes.c_char_p
def now(): return int(time.monotonic()*1000)&0xffffffff
buffer = ctypes.create_string_buffer(bytes(bands))
if not game.rider_preview_load(buffer, count, normalization, kind, b'REAL USB STEM ANALYSIS' if kind else b'REAL USB 3-BAND ANALYSIS', now()): raise RuntimeError('Native analysis load failed')
lock = threading.Lock()
def tick():
    while True:
        with lock: game.rider_preview_step(now())
        time.sleep(.016)
threading.Thread(target=tick, daemon=True).start()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def respond(self, kind, data):
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        if self.path == '/': self.respond('text/html; charset=utf-8', (root/'wave_rider_preview.html').read_bytes())
        elif self.path == '/state':
            with lock: data = game.rider_preview_status()
            self.respond('application/json', data)
        elif self.path == '/frame':
            with lock: raw = ctypes.string_at(game.rider_preview_frame(), 800*480*2)
            output = io.BytesIO()
            Image.frombytes('RGB', (800, 480), raw, 'raw', 'BGR;16').save(output, format='PNG', compress_level=1)
            self.respond('image/png', output.getvalue())
        else: self.send_error(404)
    def do_POST(self):
        if self.path != '/input': self.send_error(404); return
        length = int(self.headers.get('Content-Length', 0))
        if not 0 < length < 2048: self.send_error(400); return
        try:
            action = json.loads(self.rfile.read(length))
            with lock:
                kind = action['kind']
                if kind == 'touch': game.rider_preview_touch(int(action['x']), int(action['y']), int(action['down']), now())
                elif kind == 'move': game.rider_preview_move(float(action['delta']), now())
                elif kind == 'key': game.rider_preview_key(int(action['key']), now())
                elif kind == 'pause': game.rider_preview_pause()
                elif kind == 'seek': game.rider_preview_seek(float(action['seconds'])*150)
                else: raise ValueError('Unknown action')
            self.respond('application/json', b'{"ok":true}')
        except (KeyError, TypeError, ValueError): self.send_error(400)

print(f'Native Wave Rider preview with actual USB analysis: http://127.0.0.1:{args.port}', flush=True)
ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
