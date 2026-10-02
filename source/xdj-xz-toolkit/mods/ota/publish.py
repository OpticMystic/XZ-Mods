"""Create and serve signed XZ runtime updates on an explicitly selected LAN address."""
from __future__ import annotations
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import struct
import threading
import time
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

ROOT=Path(__file__).resolve().parent
FILES=('libxz-mods.so','libxz-receiver.so','xz-doom','source.zip')
APP_SHA256=bytes.fromhex('3a7c6c507ea67b2484d0cbc58136a495374c8ede5eeef1ffdffcfe86c7a3dae3')
def build(bundle:Path,destination:Path,key:Path,sequence:int,label:str):
    private=Ed25519PrivateKey.from_private_bytes(key.read_bytes())
    public=private.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
    if public.hex()!= (ROOT/'trusted-key.hex').read_text().strip():raise ValueError('Signing key does not match the provisioned XZ trust key')
    if not 0<sequence<2**32 or not label or len(label.encode('ascii'))>31:raise ValueError('Invalid update sequence or label')
    payload=[];manifest=bytearray(512)
    struct.pack_into('<8sII32s32s32sI',manifest,0,b'XZOTA1\0\0',1,sequence,b'xdj-xz/1.26',APP_SHA256,label.encode('ascii'),4)
    for index,name in enumerate(FILES):
        data=(bundle/name).read_bytes()
        if not data or len(data)>(16*1024*1024 if name=='source.zip' else 2*1024*1024):raise ValueError('Update file exceeds its limit: '+name)
        if name!='source.zip' and (data[:6]!=b'\x7fELF\x01\x01' or int.from_bytes(data[18:20],'little')!=40 or int.from_bytes(data[36:40],'little')&0x400):raise ValueError('Update binary has the wrong ARM ABI: '+name)
        struct.pack_into('<32sI32s28s',manifest,116+index*96,name.encode(),len(data),hashlib.sha256(data).digest(),bytes(28))
        payload.append(data)
    signed=bytes(manifest)+private.sign(bytes(manifest))
    blob=signed+b''.join(payload)
    destination.mkdir(parents=True,exist_ok=True)
    for name,data in [('latest.xzu',blob),('manifest.bin',signed)]:
        temporary=destination/(name+'.next');temporary.write_bytes(data);temporary.replace(destination/name)
    (destination/'release.json').write_text(json.dumps({'sequence':sequence,'label':label,'sha256':hashlib.sha256(blob).hexdigest(),'bytes':len(blob),'files':list(FILES),'private_game_data_included':False,'public_published':False},indent=2)+'\n')
    return blob

def serve(directory:Path,host:str,port:int):
    if host in ('0.0.0.0','::'):raise ValueError('Choose the address of the XZ network adapter')
    socket.inet_aton(host)
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            name=self.path.split('?',1)[0]
            if name not in ('/manifest.bin','/latest.xzu'):self.send_error(404);return
            data=(directory/name[1:]).read_bytes()
            self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError):pass
        def log_message(self,*args):pass
    server=ThreadingHTTPServer((host,port),Handler)
    discovery=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);discovery.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);discovery.bind(('0.0.0.0',port))
    def discover():
        while True:
            data,peer=discovery.recvfrom(128)
            if data==b'XZOTA_DISCOVER_1':discovery.sendto(f'XZOTA1 http://{host}:{port}'.encode(),peer)
    threading.Thread(target=discover,daemon=True).start()
    print(f'XZ_OTA_READY http://{host}:{port}',flush=True)
    server.serve_forever()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--key',type=Path,default=Path(os.environ.get('LOCALAPPDATA','.'))/'XZ Mods Release Keys/runtime-ota-ed25519.key')
    p.add_argument('--sequence',type=int,default=int(time.time()))
    p.add_argument('--label',default='local-runtime')
    p.add_argument('--serve',action='store_true')
    p.add_argument('--host',default='169.254.168.58')
    p.add_argument('--port',type=int,default=50009)
    a=p.parse_args()
    if a.bundle:build(a.bundle,a.output,a.key,a.sequence,a.label)
    if a.serve:serve(a.output.resolve(),a.host,a.port)
