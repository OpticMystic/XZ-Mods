#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Lab stand-in for the stock XDJ-XZ player on Pro DJ Link: devices 1, 2 and 33 from one address, keep-alives every
2 s (the XZ's layout: structure 3, peers 1, mixer model 1), beat packets for decks 1 and 2, and 0x124-byte status
packets every 200 ms to every other device that announces itself. Deck 1 answers a tempo master request when it
is master. Lab only: refuses to run outside a network namespace named xzv-*.

  stock_standin.py --ip 10.79.0.10 --broadcast 10.79.0.255 [--master 1] [--seconds N]
"""
import argparse
import random
import select
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
import xz_sync_verify as v  # noqa: E402

MAC = bytes.fromhex("00005e00533a")


def macs(device):
    m = bytearray(MAC)
    if device != 1:
        m[0] |= 0x02
        m[5] += 1 if device == 2 else 2
    return bytes(m)


def keepalive(device, ip):
    b = v.header_announce(0x06, "XDJ-XZ", 0x36, structure=0x03)
    b[0x24] = device
    b[0x25] = 0x01
    b[0x26:0x2c] = macs(device)
    b[0x2c:0x30] = ip
    b[0x30] = 0x01
    b[0x34] = 0x02 if device == 33 else 0x01
    b[0x35] = 0x01 if device == 33 else 0x00
    return bytes(b)


def beat(device, bpm, bib):
    b = v.header_status(0x28, "XDJ-XZ", device, 0x3c)
    ms = int(round(60000 / bpm))
    for off, mult in ((0x24, 1), (0x28, 2), (0x30, 4), (0x38, 8)):
        struct.pack_into(">I", b, off, ms * mult)
    struct.pack_into(">I", b, 0x2c, ms * (5 - bib))
    struct.pack_into(">I", b, 0x34, ms * (9 - bib))
    b[0x3c:0x54] = b"\xff" * 24
    struct.pack_into(">I", b, 0x54, v.UNITY)
    struct.pack_into(">H", b, 0x5a, int(round(bpm * 100)))
    b[0x5c] = bib
    b[0x5f] = device
    return bytes(b)


def status(device, bpm, beat_no, bib, master, counter, handoff):
    b = v.header_status(0x0a, "XDJ-XZ", device, 0x124 - 0x24, subtype=0x03)
    b[0x24] = device
    b[0x26] = 0x01
    b[0x27] = 0x01
    b[0x28] = 1
    b[0x29] = 0x02 if device == 1 else 0x03
    b[0x2a] = 0x01
    struct.pack_into(">I", b, 0x2c, 100 + device)
    b[0x68] = 0x01
    b[0x6a] = b[0x6b] = 0x04
    b[0x6f] = b[0x73] = 0x00
    b[0x75] = 0x01
    b[0x7b] = 0x03
    b[0x7c:0x80] = b"1.26"
    b[0x89] = 0x84 | 0x40 | (0x20 if master else 0) | 0x08
    b[0x8a] = 0xff
    b[0x8b] = 0x9a
    struct.pack_into(">I", b, 0x8c, v.UNITY)
    struct.pack_into(">H", b, 0x90, 0x8000)
    struct.pack_into(">H", b, 0x92, int(round(bpm * 100)))
    struct.pack_into(">H", b, 0x94, 0x7fff)
    struct.pack_into(">H", b, 0x96, 0xffff)
    struct.pack_into(">I", b, 0x98, v.UNITY)
    b[0x9d] = 0x0d
    b[0x9e] = 0x01 if master else 0x00
    b[0x9f] = handoff
    struct.pack_into(">I", b, 0xa0, beat_no)
    struct.pack_into(">H", b, 0xa4, 0x01ff)
    b[0xa6] = bib
    b[0xb6] = 0x01
    struct.pack_into(">I", b, 0xc0, v.UNITY)
    struct.pack_into(">I", b, 0xc4, v.UNITY)
    struct.pack_into(">I", b, 0xc8, counter)
    b[0xcc] = 0x1f
    return bytes(b)


def main():
    ns = subprocess.run(["ip", "netns", "identify"], capture_output=True, text=True).stdout.split()
    if not ns or not ns[0].startswith("xzv-"):
        raise SystemExit("lab only: run inside an xzv- network namespace")
    p = argparse.ArgumentParser()
    p.add_argument("--ip", required=True)
    p.add_argument("--broadcast", required=True)
    p.add_argument("--master", type=int, default=0, help="stock deck that starts as tempo master (0 none)")
    p.add_argument("--seconds", type=float, default=600)
    a = p.parse_args()
    ip = socket.inet_aton(a.ip)
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    tx.bind((a.ip, 0))
    rx = {}
    for port in (v.PORT_ANNOUNCE, v.PORT_BEAT):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("", port))
        rx[s] = port
    bpm = {1: 128.0, 2: 128.0}
    start = time.monotonic()
    t_beat = {1: start + 0.2, 2: start + 0.35}
    beats = {1: 0, 2: 0}
    master = a.master
    handoff = 0xff
    peers = {}
    counter = 0
    next_ka = next_status = start
    end = start + a.seconds
    while time.monotonic() < end:
        now = time.monotonic()
        if now >= next_ka:
            for d in (1, 2, 33):
                tx.sendto(keepalive(d, ip), (a.broadcast, v.PORT_ANNOUNCE))
            next_ka = now + 2.0
        for d in (1, 2):
            if now >= t_beat[d]:
                beats[d] += 1
                tx.sendto(beat(d, bpm[d], (beats[d] - 1) % 4 + 1), (a.broadcast, v.PORT_BEAT))
                t_beat[d] += 60.0 / bpm[d] + random.uniform(-0.002, 0.002)
        if now >= next_status:
            counter += 1
            for peer_ip in sorted(set(peers.values())):
                for d in (1, 2):
                    tx.sendto(status(d, bpm[d], max(beats[d], 1), (max(beats[d], 1) - 1) % 4 + 1, master == d, counter,
                                     handoff if master == d else 0xff), (peer_ip, v.PORT_STATUS))
            next_status = now + 0.2
        ready, _, _ = select.select(list(rx), [], [], 0.005)
        for s in ready:
            data, (src, _) = s.recvfrom(2048)
            d = v.decode(rx[s], data)
            if not d or src == a.ip:
                continue
            if d["type"] == "keepalive":
                peers[d["device"]] = d["ip"]
            elif d["type"] == "master_request" and master in (1, 2):
                tx.sendto(v.master_response("XDJ-XZ", master, True), (src, v.PORT_BEAT))
                handoff = d["requester"]
                master_was = master
                print(f"deck {master_was} yields master to {d['requester']}", flush=True)
                master = 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
