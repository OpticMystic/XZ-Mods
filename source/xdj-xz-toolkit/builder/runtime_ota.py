# SPDX-License-Identifier: MIT
"""Serve only a provisioned, signed runtime release to XZs on the selected LAN."""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import hashlib
import json
from pathlib import Path
import select
import socket
import sys

def network_addresses():
    if sys.platform=='darwin':
        import re
        import subprocess
        text=subprocess.check_output(['/sbin/ifconfig','-a'],text=True,timeout=10)
        return sorted({ip for ip in re.findall(r'^\s+inet (\d+\.\d+\.\d+\.\d+) ',text,re.MULTILINE) if not ip.startswith('127.')})
    addresses={item[4][0] for item in socket.getaddrinfo(socket.gethostname(),None,socket.AF_INET)}
    return sorted((ip for ip in addresses if not ip.startswith('127.')),key=lambda ip:(ip!='169.254.168.58',ip))

def serve(resources:Path,address:str,job):
    if address not in network_addresses():raise ValueError('Choose an IPv4 address of this computer’s XZ network adapter')
    release=resources/'ota-release';record=json.loads((release/'release.json').read_text())
    bundle=release/'latest.xzu';manifest=release/'manifest.bin'
    if hashlib.sha256(bundle.read_bytes()).hexdigest()!=record['sha256'] or len(manifest.read_bytes())!=576:raise ValueError('The signed OTA release is missing or changed. Reinstall the current XZ Mods app.')
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            name=self.path.split('?',1)[0]
            if name not in ('/manifest.bin','/latest.xzu'):self.send_error(404);return
            data=(release/name[1:]).read_bytes()
            self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError):pass
        def log_message(self,*args):pass
    server=ThreadingHTTPServer((address,50009),Handler);server.timeout=.25
    discovery=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);discovery.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    try:
        discovery.bind(('0.0.0.0',50009));discovery.setblocking(False)
        job.progress('serving','XZ network updates are ready. On the XZ, open MODS > Extras > Network updates.',url=f'http://{address}:50009',version=record['label'],sequence=record['sequence'],public_published=False)
        while True:
            job.check();readable,_,_=select.select([server,discovery],[],[],.25)
            if server in readable:server.handle_request()
            if discovery in readable:
                data,peer=discovery.recvfrom(128)
                if data==b'XZOTA_DISCOVER_1':discovery.sendto(f'XZOTA1 http://{address}:50009'.encode(),peer)
    finally:discovery.close();server.server_close()
