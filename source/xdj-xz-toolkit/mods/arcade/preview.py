"""Play the actual native C game on Windows in a local browser. No XZ connection."""
import argparse
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import subprocess
import threading
import time
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--port', type=int, default=8769)
args = parser.parse_args()
root = Path(__file__).resolve().parent
args.output.mkdir(parents=True, exist_ok=True)
dll = args.output.resolve() / 'arcade-preview.dll'
subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared',
                *[str(root / n) for n in ('arcade.c', 'render.c', 'preview.c')], str(root.parent / 'ui/ui_draw.c'), '-o', str(dll)], check=True)
game = ctypes.CDLL(str(dll))
game.preview_init.argtypes = [ctypes.c_uint]
game.preview_step.argtypes = [ctypes.c_uint]
game.preview_key.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint]
game.preview_move.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_uint]
game.preview_touch.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
game.preview_music.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_uint]
game.preview_frame.restype = ctypes.POINTER(ctypes.c_uint16)
game.preview_status.restype = ctypes.c_char_p
lock = threading.Lock()
def now(): return int(time.monotonic() * 1000) & 0xffffffff
game.preview_init(now())
def tick():
    while True:
        with lock: game.preview_step(now())
        time.sleep(.008)
threading.Thread(target=tick, daemon=True).start()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def respond(self, kind, data):
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        if self.path == '/': self.respond('text/html; charset=utf-8', (root / 'preview.html').read_bytes())
        elif self.path == '/state':
            with lock: data = game.preview_status()
            self.respond('application/json', data)
        elif self.path == '/frame':
            with lock: raw = ctypes.string_at(game.preview_frame(), 800 * 480 * 2)
            output = io.BytesIO()
            Image.frombytes('RGB', (800, 480), raw, 'raw', 'BGR;16').save(output, format='PNG', compress_level=1)
            self.respond('image/png', output.getvalue())
        else: self.send_error(404)
    def do_POST(self):
        if self.path != '/input': self.send_error(404); return
        size = int(self.headers.get('Content-Length', '0'))
        if not 0 < size < 2048: self.send_error(400); return
        try:
            action = json.loads(self.rfile.read(size))
            deck = int(action.get('deck', 0))
            with lock:
                if action['kind'] == 'key': game.preview_key(deck, int(action['key']), now())
                elif action['kind'] == 'move': game.preview_move(deck, float(action['delta']), now())
                elif action['kind'] == 'touch': game.preview_touch(int(action['x']), int(action['y']), int(action['down']), now())
                elif action['kind'] == 'reset': game.preview_init(now())
                elif action['kind'] == 'music': game.preview_music(int(action['command']), float(action.get('value', 0)), now())
                else: raise ValueError('Unknown action')
            self.respond('application/json', b'{"ok":true}')
        except (ValueError, KeyError, TypeError): self.send_error(400)

print(f'Actual C engine and native 800x480 RGB565 renderer: http://127.0.0.1:{args.port}', flush=True)
ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
