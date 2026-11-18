"""Record a short clip of the XZ's standard screen for the setup wizard. Read-only.

Reads /dev/fb0 (800x480 RGB565) over the diagnostic telnet link and pipes the frames to
this computer with nc, the same method E:/Claude/xz-capture-20261012/record_xz.py used. It
writes nothing on the deck, sends no key or touch, and the loop on the deck ends by itself
when this recorder disconnects. Expect about 3 to 6 frames per second: each frame is a
full 768 KB copy.

Before recording, on the deck:
  1. MODS > APPEARANCE > INTERFACE: CLASSIC, THEME: ORIGINAL (the standard look).
  2. MODS > CONTROLS > STEM ROW LAYOUT: HIDDEN, so the screen is the deck's own layout.
  3. Load a track on each deck and press PLAY on both, so waveforms and counters move.
  4. Close the MODS panel. The small MODS button in the top right corner stays; it is part
     of the standard interface with XZ Mods installed.

Then:  python tools/wizard_media/record_stock_clip.py --host 169.254.177.67 --seconds 12 --output <dir>
and:   python tools/build_wizard_media.py --zig <zig.exe> --stock-clip <dir>/stock-screen.mkv
"""
import argparse
import datetime
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

W, H = 800, 480
FRAME = W * H * 2
# open_telnet speaks the deck's diagnostic shell; it lives with the toolkit's vendor tools.
sys.path.insert(0, r'C:/Users/short/Github/VJToolsLibrary/packages/xdj-xz-toolkit/vendor')


def local_address(host):
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect((host, 9))
        return probe.getsockname()[0]
    finally:
        probe.close()


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--host', default='169.254.177.67')
    p.add_argument('--pc-host', help='This computer on the deck link (found automatically)')
    p.add_argument('--port', type=int, default=4252)
    p.add_argument('--seconds', type=float, default=12)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    from tools.xz_nand_dump import open_telnet
    a.output.mkdir(parents=True, exist_ok=True)
    pc = a.pc_host or local_address(a.host)
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((pc, a.port))
    server.listen(1)
    server.settimeout(15)
    telnet = open_telnet(a.host)
    telnet.sendall(f"while dd if=/dev/fb0 bs={FRAME} count=1 2>/dev/null; do :; done | nc {pc} {a.port}\r\n".encode())
    conn, _ = server.accept()
    conn.settimeout(10)
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    clip = a.output / 'stock-screen.mkv'
    # Wall-clock timestamps keep the real timing of each captured frame.
    encoder = subprocess.Popen(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-use_wallclock_as_timestamps', '1',
                                '-f', 'rawvideo', '-pixel_format', 'rgb565le', '-video_size', f'{W}x{H}', '-i', 'pipe:',
                                '-fps_mode', 'vfr', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '12', '-pix_fmt', 'yuv444p',
                                str(clip)], stdin=subprocess.PIPE)
    frames, started = 0, time.time()
    try:
        while time.time() - started < a.seconds:
            buf = bytearray()
            while len(buf) < FRAME:
                chunk = conn.recv(min(1 << 20, FRAME - len(buf)))
                if not chunk:
                    raise ConnectionError('the XZ closed the stream')
                buf.extend(chunk)
            encoder.stdin.write(bytes(buf))
            frames += 1
    finally:
        for closing in (conn, telnet, server):
            try:
                closing.close()
            except OSError:
                pass
        encoder.stdin.close()
        encoder.wait()
    seconds = time.time() - started
    report = {'recorded_at': datetime.datetime.now().isoformat(timespec='seconds'), 'host': a.host, 'device': '/dev/fb0',
              'frames': frames, 'seconds': round(seconds, 2), 'fps': round(frames / seconds, 2), 'clip': clip.name,
              'read_only': True}
    (a.output / 'recording.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
