#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Checks, from a PC on the same LAN, how the XDJ-XZ's four decks look on Pro DJ Link and in an Ableton Link
session, with the unit's own stock decks 1 and 2 as the reference ("as good as stock"). Python 3.9+, standard
library only. Prints a PASS / FAIL / SKIP / INFO table and saves the same as JSON.

What it sends, and when:
  default            nothing. It listens to the broadcasts every device sends anyway: keep-alives (UDP 50000)
                     and beat packets (UDP 50001), and, with --link, Ableton Link announcements (multicast
                     224.76.78.75:20808).
  --link             also sends a few small Link "ping" messages (unicast, to the unit's Link helper) to measure
                     the session's clock. It does not join the session as a peer, so it never changes it.
  --announce         joins Pro DJ Link as a virtual player with a free number (7-15 unless --device): claim
                     packets and a keep-alive every 1.5 s (broadcast), and a "nothing loaded" status every
                     200 ms (unicast to each device's address). Players send their status packets only to
                     announced devices, so every status check needs this.
  --handoff          (needs --announce) asks the current tempo master to hand master to the virtual player,
                     holds it, then waits for a deck to ask for it back (press MASTER on a deck, or
                     --handback-deck N names the deck that must ask) and hands it over.
  --sync-command C   (needs --announce) sends one sync control packet to the unit: on, off or master.
  --metadata         asks the unit's database server (TCP 12523, then its dbserver port) for the title of the
                     track each deck reports. Titles are never saved unless --show-titles.

Never run it against a unit on stage. The active checks change the unit's master and sync state.

Usage examples:
  python xz_sync_verify.py --unit 192.168.12.147 --seconds 60
  python xz_sync_verify.py --unit 192.168.12.147 --seconds 90 --announce --link
  python xz_sync_verify.py --unit 192.168.12.147 --announce --guided 3 --seconds 120
  python xz_sync_verify.py --unit 192.168.12.147 --announce --handoff --handback-deck 3
  python xz_sync_verify.py --self-test
"""
import argparse
import json
import math
import os
import random
import select
import socket
import statistics
import struct
import sys
import time
import uuid

MAGIC = b"Qspt1WmJOL"
PORT_ANNOUNCE, PORT_BEAT, PORT_STATUS = 50000, 50001, 50002
LINK_GROUP, LINK_PORT = "224.76.78.75", 20808
UNITY = 0x100000
STOCK_DECKS, SOFT_DECKS, MIXER = (1, 2), (3, 4), 33


def now_us() -> int:
    return time.perf_counter_ns() // 1000


def u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def name_of(b, o):
    return b[o:o + 20].split(b"\0", 1)[0].decode("ascii", "replace")


# ---- Pro DJ Link packets -------------------------------------------------------------------------------------

def decode(port: int, b: bytes):
    """One datagram as a dict, or None. Field offsets follow the DJ Link analysis (deepsymmetry.org) and
    beat-link; the 50000 family uses the 'startup' header, 50001/50002 the 'status' header."""
    if len(b) < 0x24 or b[:10] != MAGIC:
        return None
    kind = b[10]
    if port == PORT_ANNOUNCE:
        d = {"port": port, "kind": kind, "name": name_of(b, 0x0c), "len": len(b)}
        if kind == 0x06 and len(b) >= 0x36:
            d.update(type="keepalive", device=b[0x24], mac=":".join(f"{x:02x}" for x in b[0x26:0x2c]),
                     ip=".".join(str(x) for x in b[0x2c:0x30]), peers=b[0x30], device_class=b[0x34], model=b[0x35],
                     structure=b[0x21])
        elif kind == 0x08 and len(b) >= 0x29:
            d.update(type="defend", device=b[0x24], ip=".".join(str(x) for x in b[0x25:0x29]))
        elif kind in (0x00, 0x02, 0x04, 0x0a):
            d.update(type="claim", device=b[0x2e] if kind == 0x02 and len(b) >= 0x2f else b[0x24] if kind == 0x04 else 0)
        else:
            d.update(type="announce-other")
        return d
    d = {"port": port, "kind": kind, "name": name_of(b, 0x0b), "device": b[0x21], "len": len(b)}
    if port == PORT_BEAT and kind == 0x28 and len(b) >= 0x60:
        d.update(type="beat", next_beat=u32(b, 0x24), second_beat=u32(b, 0x28), next_bar=u32(b, 0x2c),
                 fourth_beat=u32(b, 0x30), second_bar=u32(b, 0x34), eighth_beat=u32(b, 0x38),
                 pitch=u32(b, 0x54) / UNITY, bpm=u16(b, 0x5a) / 100.0, beat_in_bar=b[0x5c])
    elif port == PORT_BEAT and kind == 0x0b and len(b) >= 0x3c:
        pitch = u32(b, 0x2c)
        d.update(type="position", track_seconds=u32(b, 0x24), playhead_ms=u32(b, 0x28),
                 pitch_pct=(pitch - (1 << 32) if pitch >= 0x80000000 else pitch) / 100.0,
                 bpm=(u32(b, 0x38) / 10.0 if u32(b, 0x38) != 0xffffffff else None))
    elif port == PORT_BEAT and kind == 0x03 and len(b) >= 0x28:
        d.update(type="on_air", channels=[b[0x24 + i] for i in range(4)])
    elif port == PORT_BEAT and kind == 0x26 and len(b) >= 0x28:
        d.update(type="master_request", requester=b[0x27])
    elif port in (PORT_BEAT, PORT_STATUS) and kind == 0x27 and len(b) >= 0x2c:
        d.update(type="master_response", yielded=b[0x2b] == 1)
    elif port == PORT_BEAT and kind == 0x2a and len(b) >= 0x2c:
        d.update(type="sync_control", command=b[0x2b])
    elif port == PORT_STATUS and kind == 0x0a and len(b) >= 0xcc:
        flags = b[0x89]
        d.update(type="status", subtype=b[0x20], activity=b[0x27], track_device=b[0x28], track_slot=b[0x29],
                 track_type=b[0x2a], rekordbox_id=u32(b, 0x2c), play_state=b[0x7b],
                 firmware=b[0x7c:0x80].decode("ascii", "replace").strip("\0"), sync_counter=u32(b, 0x84),
                 flags=flags, playing=bool(flags & 0x40), master=bool(flags & 0x20), synced=bool(flags & 0x10),
                 on_air=bool(flags & 0x08), bpm_sync=bool(flags & 0x02), play2=b[0x8b], pitch=u32(b, 0x8c) / UNITY,
                 tempo_valid=u16(b, 0x90), bpm=(None if u16(b, 0x92) == 0xffff else u16(b, 0x92) / 100.0),
                 fader_pitch=u32(b, 0x98) / UNITY, play3=b[0x9d], master_mode=b[0x9e], master_handoff=b[0x9f],
                 beat=(None if u32(b, 0xa0) == 0xffffffff else u32(b, 0xa0)), cue_countdown=u16(b, 0xa4),
                 beat_in_bar=b[0xa6], packet_counter=u32(b, 0xc8), nx=b[0xcc])
    elif port == PORT_STATUS and kind == 0x29 and len(b) >= 0x38:
        d.update(type="mixer_status", flags=b[0x27], master=bool(b[0x27] & 0x20), bpm=u16(b, 0x2e) / 100.0,
                 master_handoff=b[0x36], beat_in_bar=b[0x37])
    elif port == PORT_STATUS and kind == 0x05:
        d.update(type="media_query")
    else:
        d.update(type="other")
    return d


def header_status(kind: int, name: str, device: int, payload_len: int, subtype=0x00, b1f=0x01) -> bytearray:
    """50001/50002 header: magic, kind, 20-byte name, 0x1f, subtype, D, len_r."""
    b = bytearray(0x24 + payload_len)
    b[:10] = MAGIC
    b[10] = kind
    nb = name.encode("ascii")[:20]
    b[0x0b:0x0b + len(nb)] = nb
    b[0x1f] = b1f
    b[0x20] = subtype
    b[0x21] = device
    struct.pack_into(">H", b, 0x22, payload_len)
    return b


def header_announce(kind: int, name: str, length: int, structure=0x02) -> bytearray:
    b = bytearray(length)
    b[:10] = MAGIC
    b[10] = kind
    nb = name.encode("ascii")[:20]
    b[0x0c:0x0c + len(nb)] = nb
    b[0x20] = 0x01
    b[0x21] = structure
    struct.pack_into(">H", b, 0x22, length)
    return b


def keepalive(name, device, mac: bytes, ip: bytes) -> bytes:
    b = header_announce(0x06, name, 0x36)
    b[0x24] = device
    b[0x25] = 0x01
    b[0x26:0x2c] = mac
    b[0x2c:0x30] = ip
    b[0x30] = 0x01
    b[0x34] = 0x01
    return bytes(b)


def claim_packets(name, device, mac: bytes, ip: bytes):
    """Hello 0x0a, first stage 0x00, second stage 0x02 (specific number), final 0x04: three of each (0x0a three
    times, then n = 1..3 for the others), 300 ms apart."""
    out = []
    for _ in range(3):
        b = header_announce(0x0a, name, 0x25)
        b[0x24] = 0x01
        out.append(bytes(b))
    for n in (1, 2, 3):
        b = header_announce(0x00, name, 0x2c)
        b[0x24] = n
        b[0x25] = 0x01
        b[0x26:0x2c] = mac
        out.append(bytes(b))
    for n in (1, 2, 3):
        b = header_announce(0x02, name, 0x32)
        b[0x24:0x28] = ip
        b[0x28:0x2e] = mac
        b[0x2e] = device
        b[0x2f] = n
        b[0x30] = 0x01
        b[0x31] = 0x02
        out.append(bytes(b))
    for n in (1, 2, 3):
        b = header_announce(0x04, name, 0x26)
        b[0x24] = device
        b[0x25] = n
        out.append(bytes(b))
    return out


def cdj_status(name, device, *, master=False, bpm=120.0, handoff_to=0xff, counter=0, sync_counter=0) -> bytes:
    """A 0x11c-byte CDJ status in beat-link's layout: nothing loaded, stopped; as tempo master it reports a
    rekordbox-valid tempo so the others can follow it."""
    b = header_status(0x0a, name, device, 0x11c - 0x24, subtype=0x03)
    b[0x24] = device
    b[0x26] = 0x01
    b[0x68] = 0x01
    b[0x6a] = b[0x6b] = 0x04
    b[0x6f] = b[0x73] = 0x04
    b[0x75] = 0x01
    b[0x76:0x7b] = b"\x00\x00\x01\x00\x00"
    b[0x7b] = 0x00
    b[0x7c:0x80] = b"1.43"
    struct.pack_into(">I", b, 0x84, sync_counter)
    b[0x89] = 0x84 | (0x20 if master else 0)
    b[0x8a] = 0xff
    b[0x8b] = 0x7e
    struct.pack_into(">I", b, 0x8c, UNITY)
    struct.pack_into(">H", b, 0x90, 0x8000 if master else 0x7fff)
    struct.pack_into(">H", b, 0x92, int(round(bpm * 100)) if master else 0xffff)
    struct.pack_into(">H", b, 0x94, 0x7fff)
    struct.pack_into(">H", b, 0x96, 0xffff)
    struct.pack_into(">I", b, 0x98, UNITY)
    b[0x9d] = 0x00
    b[0x9e] = 0x01 if master else 0x00
    b[0x9f] = handoff_to
    struct.pack_into(">I", b, 0xa0, 0xffffffff)
    struct.pack_into(">H", b, 0xa4, 0x01ff)
    b[0xb6] = 0x01
    struct.pack_into(">I", b, 0xc0, UNITY)
    struct.pack_into(">I", b, 0xc4, UNITY)
    struct.pack_into(">I", b, 0xc8, counter)
    b[0xcc] = 0x0f
    return bytes(b)


def master_request(name, device) -> bytes:
    b = header_status(0x26, name, device, 0x04)
    b[0x27] = device
    return bytes(b)


def master_response(name, device, yielded=True) -> bytes:
    b = header_status(0x27, name, device, 0x08)
    b[0x27] = device
    b[0x2b] = 1 if yielded else 0
    return bytes(b)


def sync_control(name, device, command) -> bytes:
    b = header_status(0x2a, name, device, 0x08)
    b[0x27] = device
    b[0x2b] = command
    return bytes(b)


SYNC_COMMANDS = {"on": 0x10, "off": 0x20, "master": 0x01}

# ---- Ableton Link (discovery v1 and measurement v1, as in Ableton Link 4.1.1) --------------------------------

ASDP = b"_asdp_v\x01"
LINKV = b"_link_v\x01"


def link_entries(b: bytes, o: int):
    out = {}
    while o + 8 <= len(b):
        key, size = struct.unpack_from(">II", b, o)
        o += 8
        if o + size > len(b):
            break
        out[key.to_bytes(4, "big").decode("ascii", "replace")] = b[o:o + size]
        o += size
    return out


def link_alive(b: bytes):
    """A discovery message: type (1 alive, 2 response, 3 bye-bye), ttl, node id and the node's state."""
    if len(b) < 20 or b[:8] != ASDP:
        return None
    mtype, ttl, _group = b[8], b[9], u16(b, 10)
    node = b[12:20].hex()
    e = link_entries(b, 20)
    d = {"type": mtype, "ttl": ttl, "node": node}
    if "tmln" in e and len(e["tmln"]) == 24:
        mpb, beat_origin, time_origin = struct.unpack(">qqq", e["tmln"])
        d["timeline"] = (mpb, beat_origin, time_origin)
    if "sess" in e and len(e["sess"]) == 8:
        d["session"] = e["sess"].hex()
    if "stst" in e and len(e["stst"]) == 17:
        d["playing"] = bool(e["stst"][0])
    if "mep4" in e and len(e["mep4"]) == 6:
        d["mep4"] = (".".join(str(x) for x in e["mep4"][:4]), u16(e["mep4"], 4))
    return d


def link_ping(host_us: int) -> bytes:
    return LINKV + b"\x01" + struct.pack(">II", 0x5f5f6874, 8) + struct.pack(">q", host_us)


def link_pong(b: bytes):
    """(session, ghost time, echoed host time) from a pong, or None."""
    if len(b) < 9 or b[:8] != LINKV or b[8] != 2:
        return None
    e = link_entries(b, 9)
    if "__gt" not in e or "__ht" not in e:
        return None
    return (e.get("sess", b"").hex(), struct.unpack(">q", e["__gt"])[0], struct.unpack(">q", e["__ht"])[0])


def timeline_beat(tl, ghost_us: float) -> float:
    mpb, beat_origin, time_origin = tl
    return beat_origin / 1e6 + (ghost_us - time_origin) / mpb


def wrap(x: float, period: float) -> float:
    """x folded into [-period/2, period/2)."""
    return (x + period / 2) % period - period / 2


# ---- the session model ---------------------------------------------------------------------------------------

class Run:
    def __init__(self, args):
        self.args = args
        self.t0 = now_us()
        self.keepalives = {}   # device -> list of (t, dict)
        self.beats = {}        # device -> list of (t, dict)
        self.status = {}       # device -> list of (t, dict)
        self.mixer = []        # (t, dict)
        self.positions = {}    # device -> count
        self.other = []        # (t, dict) for commands and responses we saw
        self.unknown = 0
        self.link_alive = {}   # node -> list of (t, dict)
        self.link_pongs = []   # (t_send, t_recv, ghost, session)
        self.events = []       # operator prompts and actions, (t, text)
        self.notes = []
        self.rows = []

    def t(self, t_us=None):
        return ((now_us() if t_us is None else t_us) - self.t0) / 1e6

    def row(self, check, verdict, detail, **data):
        self.rows.append({"check": check, "verdict": verdict, "detail": detail, **data})


class Net:
    """Sockets: 50000, 50001 and (when announcing) 50002 bound on all addresses; one sender; Link multicast."""

    def __init__(self, args, run: Run):
        self.args, self.run = args, run
        self.socks = {}
        for port in (PORT_ANNOUNCE, PORT_BEAT) + ((PORT_STATUS,) if args.announce else ()):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            try:
                s.bind((args.bind, port))
            except OSError as e:
                raise SystemExit(f"cannot listen on UDP {port}: {e}. Close rekordbox, PRO DJ LINK Bridge and other "
                                 f"DJ Link software on this PC and try again.")
            s.setblocking(False)
            self.socks[s] = ("djl", port)
        self.link_mc = self.link_uc = None
        if args.link:
            mc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            mc.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                mc.bind((args.bind, LINK_PORT))
            except OSError as e:
                raise SystemExit(f"cannot listen on UDP {LINK_PORT} for Ableton Link: {e}")
            mreq = socket.inet_aton(LINK_GROUP) + socket.inet_aton(args.iface_ip)
            mc.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
            mc.setblocking(False)
            uc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            uc.bind((args.iface_ip, 0))
            uc.setblocking(False)
            self.link_mc, self.link_uc = mc, uc
            self.socks[mc] = ("link", LINK_PORT)
            self.socks[uc] = ("linkuc", 0)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.tx.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        if args.iface_ip:
            self.tx.bind((args.iface_ip, 0))
        self.handlers = []

    def send(self, data: bytes, ip: str, port: int):
        try:
            self.tx.sendto(data, (ip, port))
        except OSError as e:
            self.run.notes.append(f"send to {ip}:{port} failed: {e}")

    def poll(self, timeout: float):
        ready, _, _ = select.select(list(self.socks), [], [], max(0.0, timeout))
        for s in ready:
            for _ in range(64):
                try:
                    data, (ip, _sport) = s.recvfrom(2048)
                except (BlockingIOError, InterruptedError):
                    break
                except OSError:
                    break
                t = now_us()
                what, port = self.socks[s]
                if what == "djl":
                    self.on_djl(t, port, ip, data)
                elif what == "link":
                    a = link_alive(data)
                    if a:
                        a["src"] = ip
                        self.run.link_alive.setdefault(a["node"], []).append((t, a))
                elif what == "linkuc":
                    p = link_pong(data)
                    if p:
                        session, ghost, host = p
                        self.run.link_pongs.append((host, t, ghost, session, ip))

    def on_djl(self, t, port, ip, data):
        ingest(self.run, t, port, ip, data, self.handlers)


def ingest(r, t, port, ip, data, handlers=()):
    """One Pro DJ Link datagram into the run."""
    d = decode(port, data)
    if d is None:
        r.unknown += 1
        return
    d["src"] = ip
    kind = d.get("type")
    if kind == "keepalive":
        r.keepalives.setdefault(d["device"], []).append((t, d))
    elif kind == "beat":
        r.beats.setdefault(d["device"], []).append((t, d))
    elif kind == "status":
        r.status.setdefault(d["device"], []).append((t, d))
    elif kind == "mixer_status":
        r.mixer.append((t, d))
    elif kind == "position":
        r.positions[d["device"]] = r.positions.get(d["device"], 0) + 1
    elif kind in ("master_request", "master_response", "sync_control", "defend", "on_air"):
        r.other.append((t, d))
    for h in handlers:
        h(t, d)


# ---- the virtual player (active) -----------------------------------------------------------------------------

class Virtual:
    NAME = "XZ-VERIFY"

    def __init__(self, args, run: Run, net: Net):
        self.args, self.run, self.net = args, run, net
        self.device = 0
        self.mac = bytes.fromhex(args.mac.replace(":", "")) if args.mac else uuid.getnode().to_bytes(6, "big")
        self.ip = socket.inet_aton(args.iface_ip)
        self.master = False
        self.bpm = 120.0
        self.handoff_to = 0xff
        self.counter = 0
        self.sync_counter = 0
        self.next_keepalive = 0
        self.next_status = 0
        self.defended = False
        self.requests = []
        net.handlers.append(self.on_packet)

    def on_packet(self, t, d):
        if d.get("type") == "defend" and d.get("device") == self.device and self.device:
            self.defended = True
        if d.get("type") == "master_request" and self.master and d.get("src") != self.args.iface_ip:
            self.requests.append((t, d))

    def pick_number(self):
        if self.args.device:
            return self.args.device
        taken = set(self.run.keepalives)
        for n in range(7, 16):
            if n not in taken:
                return n
        raise SystemExit("no free device number in 7-15")

    def join(self, until_us):
        """Listen 3 s for numbers in use, claim one, then keep-alives. Returns False when defended."""
        listen_until = now_us() + 3_000_000
        while now_us() < listen_until:
            self.net.poll(0.05)
        self.device = self.pick_number()
        self.run.events.append((self.run.t(), f"virtual player claims number {self.device}"))
        for pkt in claim_packets(self.NAME, self.device, self.mac, self.ip):
            self.net.send(pkt, self.args.broadcast, PORT_ANNOUNCE)
            end = now_us() + 300_000
            while now_us() < end:
                self.net.poll(0.02)
            if self.defended:
                return False
        self.tick()
        return not self.defended

    def peer_ips(self):
        ips = {d["ip"] for v in self.run.keepalives.values() for _, d in v[-1:]}
        ips.discard(self.args.iface_ip)
        return sorted(ips)

    def tick(self):
        t = now_us()
        if t >= self.next_keepalive:
            self.net.send(keepalive(self.NAME, self.device, self.mac, self.ip), self.args.broadcast, PORT_ANNOUNCE)
            self.next_keepalive = t + 1_500_000
        if t >= self.next_status:
            self.counter += 1
            pkt = cdj_status(self.NAME, self.device, master=self.master, bpm=self.bpm, handoff_to=self.handoff_to,
                             counter=self.counter, sync_counter=self.sync_counter)
            for ip in self.peer_ips():
                self.net.send(pkt, ip, PORT_STATUS)
            self.next_status = t + 200_000


def wait(run, net, virtual, seconds, until=None):
    """Poll for `seconds`, keeping the virtual player alive; stops early when until() is true."""
    end = now_us() + int(seconds * 1e6)
    while now_us() < end:
        net.poll(0.02)
        if virtual:
            virtual.tick()
        if until and until():
            return True
    return False


# ---- analysis --------------------------------------------------------------------------------------------------

def pct(values, q):
    if not values:
        return None
    v = sorted(values)
    return v[min(len(v) - 1, int(round(q * (len(v) - 1))))]


def fmt(x, unit="", digits=1):
    return "-" if x is None else f"{x:.{digits}f}{unit}"


def beat_stats(beats):
    """Interval statistics of one device's beat packets: jitter against the tempo the packets state, missing
    beats, beat-in-bar errors and the next-beat field against the stated BPM."""
    if len(beats) < 3:
        return None
    dev_ms, missing, numbering, field_err = [], 0, 0, []
    for (t0, a), (t1, b) in zip(beats, beats[1:]):
        eff = b["bpm"] * b["pitch"]
        if eff <= 0:
            continue
        expect = 60e6 / eff
        n = round((t1 - t0) / expect)
        if n < 1:
            n = 1
        if n == 1:
            dev_ms.append(((t1 - t0) - expect) / 1000.0)
        else:
            missing += n - 1
        if (a["beat_in_bar"] - 1 + n) % 4 + 1 != b["beat_in_bar"]:
            numbering += 1
    for _, b in beats:
        if b["bpm"] > 0 and b["next_beat"] != 0xffffffff:
            field_err.append(abs(b["next_beat"] - 60000.0 / b["bpm"]))
    absdev = [abs(x) for x in dev_ms]
    last = beats[-1][1]
    return {"count": len(beats), "tempo": last["bpm"] * last["pitch"], "bpm": last["bpm"], "pitch": last["pitch"],
            "jitter_p50_ms": pct(absdev, 0.5), "jitter_p99_ms": pct(absdev, 0.99), "jitter_max_ms": max(absdev) if absdev else None,
            "mean_dev_ms": statistics.mean(dev_ms) if dev_ms else None, "missing": missing, "numbering_errors": numbering,
            "next_beat_err_ms_max": max(field_err) if field_err else None}


STATUS_FIELDS = ("activity", "track_device", "track_slot", "track_type", "rekordbox_id", "play_state", "firmware",
                 "playing", "master", "synced", "on_air", "play2", "pitch", "tempo_valid", "bpm", "fader_pitch",
                 "play3", "master_mode", "beat", "beat_in_bar", "cue_countdown", "packet_counter", "nx", "len")


def populated(rows):
    """Fields a device filled with something other than an empty value at least once."""
    empty = {"rekordbox_id": 0, "track_device": 0, "track_slot": 0, "track_type": 0, "bpm": None, "beat": None,
             "beat_in_bar": 0, "firmware": "", "tempo_valid": 0x7fff, "packet_counter": 0, "play_state": 0,
             "activity": 0, "master_mode": 0, "play3": 0}
    seen = set()
    for _, d in rows:
        for f in STATUS_FIELDS:
            if f in ("playing", "master", "synced", "on_air"):
                continue
            if d.get(f) != empty.get(f, object()) and d.get(f) is not None:
                seen.add(f)
    return seen


def events_of(rows):
    """Changes a device reported in its status: load, play/pause, tempo, master, sync, on-air."""
    out, prev = [], None
    for t, d in rows:
        if prev is not None:
            if (d["track_device"], d["track_slot"], d["rekordbox_id"]) != (prev["track_device"], prev["track_slot"], prev["rekordbox_id"]):
                out.append((t, "load", f"track {d['track_device']}/{d['track_slot']}/{d['rekordbox_id']}"))
            if d["playing"] != prev["playing"]:
                out.append((t, "play" if d["playing"] else "pause", f"state {d['play_state']:#04x}"))
            if abs(d["pitch"] - prev["pitch"]) > 0.0002:
                out.append((t, "tempo", f"pitch {100 * (d['pitch'] - 1):+.2f} %"))
            if d["master"] != prev["master"]:
                out.append((t, "master" if d["master"] else "not master", ""))
            if d["synced"] != prev["synced"]:
                out.append((t, "sync on" if d["synced"] else "sync off", ""))
            if d["on_air"] != prev["on_air"]:
                out.append((t, "on air" if d["on_air"] else "off air", ""))
        prev = d
    return out


def analyse_devices(run: Run):
    a = run.args
    seen = sorted(run.keepalives)
    for n in seen:
        ka = run.keepalives[n]
        last = ka[-1][1]
        gaps = [(t1 - t0) / 1e6 for (t0, _), (t1, _) in zip(ka, ka[1:])]
        run.row(f"device {n}", "INFO",
                f"{last['name']!r} at {last['ip']} mac {last['mac']} class {last['device_class']} model {last['model']}; "
                f"{len(ka)} keep-alives, every {fmt(statistics.median(gaps) if gaps else None, ' s', 2)}",
                device=n, name=last["name"], ip=last["ip"], mac=last["mac"],
                keepalive_interval_s=statistics.median(gaps) if gaps else None)
    unit = [n for n in seen if run.keepalives[n][-1][1]["ip"] == a.unit]
    stock = [n for n in (1, 2, MIXER) if n in unit]
    run.row("stock devices announced (1, 2, 33)", "PASS" if len(stock) == 3 else "FAIL", f"seen from the unit: {unit}")
    soft = [n for n in SOFT_DECKS if n in unit]
    if not soft:
        run.row("decks 3 and 4 announced", "FAIL" if a.expect_decks else "SKIP",
                "no keep-alive for 3 or 4 from the unit (PRO DJ LINK DECKS 3/4 off, no DECK 3/4 route, or helper missing)")
    else:
        ok = soft == [3, 4]
        same = all(run.keepalives[n][-1][1]["name"] == "XDJ-XZ" for n in soft)
        run.row("decks 3 and 4 announced", "PASS" if ok and same else "FAIL",
                f"{soft} at the unit's address; names {[run.keepalives[n][-1][1]['name'] for n in soft]}")
        ref = [statistics.median([(t1 - t0) / 1e6 for (t0, _), (t1, _) in zip(run.keepalives[n], run.keepalives[n][1:])])
               for n in STOCK_DECKS if n in unit and len(run.keepalives[n]) > 2]
        mine = [statistics.median([(t1 - t0) / 1e6 for (t0, _), (t1, _) in zip(run.keepalives[n], run.keepalives[n][1:])])
                for n in soft if len(run.keepalives[n]) > 2]
        if ref and mine:
            worst = max(abs(m - statistics.mean(ref)) for m in mine)
            run.row("keep-alive cadence of 3/4 vs stock", "PASS" if worst <= 0.3 else "FAIL",
                    f"stock {fmt(statistics.mean(ref), ' s', 2)}, decks 3/4 {[round(m, 2) for m in mine]} s")
    others = [n for n in seen if n not in unit]
    if others:
        run.row("other devices on the LAN", "INFO", f"{others}")
    defends = [d for _, d in run.other if d["type"] == "defend"]
    run.row("no number conflicts", "PASS" if not defends else "FAIL",
            f"{len(defends)} defence packets" + (f": {[(d['device'], d['src']) for d in defends[:4]]}" if defends else ""))


def analyse_beats(run: Run):
    stats = {}
    for n in (1, 2, 3, 4):
        b = [(t, d) for t, d in run.beats.get(n, []) if d["src"] == run.args.unit]
        s = beat_stats(b)
        stats[n] = s
        if s:
            run.row(f"beats deck {n}", "INFO",
                    f"{s['count']} packets, {s['tempo']:.2f} BPM ({s['bpm']:.2f} x {s['pitch']:.5f}); |jitter| p50 "
                    f"{fmt(s['jitter_p50_ms'], ' ms', 2)} p99 {fmt(s['jitter_p99_ms'], ' ms', 2)} max {fmt(s['jitter_max_ms'], ' ms', 2)}; "
                    f"missing {s['missing']}; beat-in-bar errors {s['numbering_errors']}; next-beat field off by at most "
                    f"{fmt(s['next_beat_err_ms_max'], ' ms', 1)}", device=n, **s)
    ref = [stats[n] for n in STOCK_DECKS if stats.get(n)]
    ref_p99 = max((s["jitter_p99_ms"] for s in ref if s["jitter_p99_ms"] is not None), default=None)
    for n in SOFT_DECKS:
        s = stats.get(n)
        if not s:
            run.row(f"deck {n} beat packets", "SKIP", "no beat packets (not playing, no beat grid, or not announced)")
            continue
        bound = max(ref_p99 or 0.0, 5.0)
        ok = s["jitter_p99_ms"] is not None and s["jitter_p99_ms"] <= bound
        run.row(f"deck {n} beat timing as good as stock", "PASS" if ok else "FAIL",
                f"p99 |jitter| {fmt(s['jitter_p99_ms'], ' ms', 2)} vs stock {fmt(ref_p99, ' ms', 2)} (bound {bound:.1f} ms)")
        run.row(f"deck {n} beat numbering", "PASS" if s["numbering_errors"] == 0 else "FAIL",
                f"{s['numbering_errors']} beat-in-bar errors over {s['count']} packets, {s['missing']} missing beats")
        run.row(f"deck {n} next-beat field at 0 % pitch", "PASS" if (s["next_beat_err_ms_max"] or 0) <= 2.0 else "FAIL",
                f"largest difference from 60000/BPM: {fmt(s['next_beat_err_ms_max'], ' ms', 1)}")
    return stats


def analyse_status(run: Run, beats):
    if not run.args.announce:
        run.row("status packets", "SKIP", "players send status only to announced devices: run with --announce")
        return
    rates, fields = {}, {}
    for n in (1, 2, 3, 4):
        rows = [(t, d) for t, d in run.status.get(n, []) if d["src"] == run.args.unit]
        if len(rows) < 2:
            run.row(f"status deck {n}", "SKIP" if n in SOFT_DECKS else "INFO", f"{len(rows)} status packets")
            continue
        span = (rows[-1][0] - rows[0][0]) / 1e6
        rates[n] = (len(rows) - 1) / span if span > 0 else None
        fields[n] = populated(rows)
        last = rows[-1][1]
        track = f"{last['track_device']}/{last['track_slot']}/{last['track_type']}/{last['rekordbox_id']}"
        run.row(f"status deck {n}", "INFO",
                f"{len(rows)} packets, {fmt(rates[n], ' Hz', 2)}, len {last['len']:#x}; track (player/slot/type/id) {track}; "
                f"state {last['play_state']:#04x} flags {last['flags']:#04x} (play {int(last['playing'])} master {int(last['master'])} "
                f"sync {int(last['synced'])} on-air {int(last['on_air'])}); BPM {last['bpm']} pitch {100 * (last['pitch'] - 1):+.2f} %; "
                f"beat {last['beat']} in bar {last['beat_in_bar']}; firmware {last['firmware']!r}",
                device=n, rate_hz=rates[n], last={k: last[k] for k in STATUS_FIELDS if k in last})
    ref_fields = set.union(*[fields[n] for n in STOCK_DECKS if n in fields]) if any(n in fields for n in STOCK_DECKS) else None
    ref_rate = statistics.mean([rates[n] for n in STOCK_DECKS if rates.get(n)]) if any(rates.get(n) for n in STOCK_DECKS) else None
    for n in SOFT_DECKS:
        if n not in fields:
            run.row(f"deck {n} status", "FAIL" if n in run.keepalives else "SKIP", "no status packets from the unit for this deck")
            continue
        if ref_fields is not None:
            missing = sorted(ref_fields - fields[n])
            run.row(f"deck {n} status fields as stock fills them", "PASS" if not missing else "FAIL",
                    "same fields filled" if not missing else f"stock fills, deck {n} does not: {missing}")
        else:
            run.row(f"deck {n} status fields as stock fills them", "SKIP", "no stock status packets to compare with")
        if ref_rate:
            ok = 0.5 * ref_rate <= (rates[n] or 0) <= 2.0 * ref_rate
            run.row(f"deck {n} status rate vs stock", "PASS" if ok else "FAIL", f"{fmt(rates[n], ' Hz', 2)} vs stock {fmt(ref_rate, ' Hz', 2)}")
    # Plausibility against the beat packets, for every deck that plays.
    for n in (1, 2, 3, 4):
        rows = [(t, d) for t, d in run.status.get(n, []) if d["src"] == run.args.unit]
        b = beats.get(n)
        if not rows or not b:
            continue
        last = rows[-1][1]
        problems = []
        if last["bpm"] is not None and abs(last["bpm"] * last["pitch"] - b["tempo"]) > 0.05:
            problems.append(f"status tempo {last['bpm'] * last['pitch']:.2f} vs beats {b['tempo']:.2f}")
        playing_rows = [d for _, d in rows if d["playing"]]
        if not playing_rows:
            problems.append("beat packets flow but status never says playing")
        beat_rows = [(t, d["beat"]) for t, d in rows if d["beat"] is not None and d["playing"]]
        if len(beat_rows) > 2:
            span = (beat_rows[-1][0] - beat_rows[0][0]) / 1e6
            want = span * b["tempo"] / 60.0
            got = beat_rows[-1][1] - beat_rows[0][1]
            if span > 5 and abs(got - want) > max(2.0, 0.02 * want):
                problems.append(f"status beat counter moved {got} in {span:.1f} s, tempo says {want:.1f}")
        bars = [d["beat_in_bar"] for _, d in rows if d["playing"]]
        if bars and not all(1 <= x <= 4 for x in bars):
            problems.append("beat-in-bar outside 1-4 while playing")
        if last["track_type"] == 1 and last["tempo_valid"] != 0x8000:
            problems.append(f"rekordbox track but tempo field {last['tempo_valid']:#06x}")
        run.row(f"deck {n} status agrees with its beat packets", "PASS" if not problems else "FAIL",
                "; ".join(problems) or "tempo, play state, beat counter and bar position agree")
    # One master at a time.
    clash = 0
    latest = {}
    for n, rows in run.status.items():
        for t, d in rows:
            latest[n] = (t, d["master"])
            masters = [k for k, (tt, m) in latest.items() if m and t - tt < 600_000]
            clash += len(masters) > 1
    run.row("one tempo master at a time", "PASS" if not clash else "FAIL", f"{clash} status packets with two masters reported")
    for n in (1, 2, 3, 4):
        ev = events_of([(t, d) for t, d in run.status.get(n, []) if d["src"] == run.args.unit])
        if ev:
            run.row(f"deck {n} changes seen", "INFO", "; ".join(f"{run.t(t):.2f} s {k} {x}".strip() for t, k, x in ev[:20]),
                    events=[(run.t(t), k, x) for t, k, x in ev])


def analyse_guided(run: Run):
    """For each prompted action: when the status showed it, and how the beat packets followed."""
    for step in run.guided_steps:
        deck, action, t_prompt, t_end = step["deck"], step["action"], step["t_us"], step["end_us"]
        rows = [(t, d) for t, d in run.status.get(deck, []) if d["src"] == run.args.unit and t_prompt <= t <= t_end]
        ev = [e for e in events_of(rows) if e[1] == action]
        beats = [t for t, d in run.beats.get(deck, []) if d["src"] == run.args.unit and t_prompt <= t <= t_end + 3_000_000]
        if not ev:
            run.row(f"guided deck {deck}: {action}", "FAIL" if run.args.announce else "SKIP",
                    "not seen in the status packets" if run.args.announce else "needs --announce")
            continue
        t_ev = ev[0][0]
        detail = f"status showed it {run.t(t_ev):.2f} s"
        ok = True
        if action == "play":
            after = [t for t in beats if t >= t_ev - 500_000]
            first = (after[0] - t_ev) / 1000 if after else None
            detail += f"; first beat packet {fmt(first, ' ms', 0)} after"
            ok = first is not None and first < 2000
        elif action == "pause":
            later = [t for t in beats if t > t_ev + 700_000]
            detail += f"; {len(later)} beat packets later than 0.7 s after"
            ok = not later
        elif action == "tempo":
            pre = [(t, d) for t, d in run.beats.get(deck, []) if t_prompt <= t <= t_end + 3_000_000 and t >= t_ev]
            if pre:
                detail += f"; next beat packet {(pre[0][0] - t_ev) / 1000:.0f} ms later says pitch {100 * (pre[0][1]['pitch'] - 1):+.2f} %"
        run.row(f"guided deck {deck}: {action}", "PASS" if ok else "FAIL", detail)


def link_analysis(run: Run, beats):
    a = run.args
    if not a.link:
        run.row("Ableton Link session", "SKIP", "run with --link")
        return
    nodes = {n: v for n, v in run.link_alive.items() if any(d.get("src") == a.unit for _, d in v)}
    allnodes = {n: v for n, v in run.link_alive.items()}
    run.row("Ableton Link peers seen", "INFO", f"{len(allnodes)} node(s); from the unit: {len(nodes)}")
    if not nodes:
        run.row("unit in an Ableton Link session", "FAIL" if a.expect_link else "SKIP",
                "no Link announcement from the unit (ABLETON LINK disabled, helper missing, or no tempo master)")
        return
    node, alive = next(iter(nodes.items()))
    timelines = [(t, d["timeline"]) for t, d in alive if "timeline" in d]
    session = alive[-1][1].get("session")
    pongs = [p for p in run.link_pongs if p[4] == a.unit and p[3] == session]
    if len(pongs) < 3:
        run.row("Link clock measured", "FAIL", f"{len(pongs)} pongs from the unit's helper")
        return
    offsets = sorted(g - (h + r) / 2 for h, r, g, _s, _ip in pongs)
    rtt = [r - h for h, r, _g, _s, _ip in pongs]
    offset = statistics.median(offsets)
    spread = (pct(offsets, 0.75) - pct(offsets, 0.25)) / 1000
    run.row("Link clock measured", "PASS" if spread < 2 else "FAIL",
            f"{len(pongs)} pings, round trip median {statistics.median(rtt) / 1000:.2f} ms, offset spread (IQR) {spread:.3f} ms")
    tempos = [60e6 / tl[0] for _, tl in timelines]
    last_tl = timelines[-1][1]
    shown = [round(x, 2) for x in tempos]
    shown_changes = sum(1 for x, y in zip(shown, shown[1:]) if x != y)
    run.row("Link session tempo", "INFO", f"{tempos[-1]:.4f} BPM (shown {tempos[-1]:.2f}); {len(timelines)} announcements; "
            f"shown value changed {shown_changes} times", tempo=tempos[-1], shown_changes=shown_changes)
    # Discontinuities: a new timeline whose beat at the moment it arrived differs from the old one's.
    jumps = []
    for (t0, tl0), (t1, tl1) in zip(timelines, timelines[1:]):
        if tl0 == tl1:
            continue
        g = t1 + offset
        d_beats = wrap(timeline_beat(tl1, g) - timeline_beat(tl0, g), 4.0)
        d_ms = d_beats * tl1[0] / 1000
        if abs(d_ms) > 2.0:
            jumps.append((run.t(t1), d_beats, d_ms))
    span_s = (timelines[-1][0] - timelines[0][0]) / 1e6 if len(timelines) > 1 else 0
    per10 = len(jumps) / span_s * 10 if span_s > 0 else 0
    run.row("Link phase discontinuities", "PASS" if not jumps else "FAIL" if per10 > 0.5 else "INFO",
            f"{len(jumps)} jumps over {span_s:.0f} s ({per10:.2f} per 10 s)" +
            (f"; first: {[(round(t, 2), round(b, 3)) for t, b, _ in jumps[:6]]} (s, beats)" if jumps else "")
            + ("; check that nothing was seeked, looped or re-mastered" if jumps and per10 <= 0.5 else ""),
            jumps=[(round(t, 3), round(b, 4), round(m, 2)) for t, b, m in jumps])
    # Tempo and phase against the Pro DJ Link master.
    master = a.master_device or current_master(run)
    if not master or not beats.get(master):
        run.row("Link vs Pro DJ Link master", "SKIP",
                "no master deck known (needs --announce for status, or --master-device N) or it sends no beats")
        return
    mb = [(t, d) for t, d in run.beats[master] if d["src"] == a.unit]
    eff = beats[master]["tempo"]
    run.row("Link tempo vs master deck", "PASS" if abs(tempos[-1] - eff) <= 0.01 else "FAIL",
            f"Link {tempos[-1]:.4f} BPM, deck {master} {eff:.4f} BPM (difference {tempos[-1] - eff:+.4f})")
    offs = []
    tl_at = timelines
    k = 0
    for t, d in mb:
        while k + 1 < len(tl_at) and tl_at[k + 1][0] <= t:
            k += 1
        tl = tl_at[k][1]
        phase = timeline_beat(tl, t + offset)
        offs.append(wrap(phase - (d["beat_in_bar"] - 1), 4.0) * tl[0] / 1000)
    if len(offs) < 4:
        run.row("Link phase vs master beat packets", "SKIP", f"{len(offs)} master beats")
        return
    med = statistics.median(offs)
    sp = pct([abs(x - med) for x in offs], 0.95)
    run.row("Link phase vs master beat packets", "PASS" if abs(med) <= 10 else "FAIL",
            f"Link bar phase minus the master's beat packets: median {med:+.1f} ms, 95 % within +-{sp:.1f} ms of it "
            f"(includes the beat packets' own send jitter) over {len(offs)} beats", median_ms=med, spread_p95_ms=sp)


def current_master(run):
    best = None
    for n, rows in run.status.items():
        if rows and rows[-1][1]["master"]:
            best = n
    return best


# ---- active steps ------------------------------------------------------------------------------------------------

def step_handoff(run, net, v):
    a = run.args
    wait(run, net, v, 2.0)
    master = current_master(run)
    if not master:
        run.row("handoff to the virtual player", "SKIP", "no deck reports tempo master; press MASTER on a playing deck first")
        return
    mrow = run.status[master][-1][1]
    v.bpm = (mrow["bpm"] or 120.0) * mrow["pitch"]
    start = now_us()
    run.events.append((run.t(), f"asking deck {master} for tempo master"))
    net.send(master_request(v.NAME, v.device), run.keepalives[master][-1][1]["ip"], PORT_BEAT)

    def answered():
        return any(d["type"] == "master_response" and d["device"] == master and t >= start for t, d in run.other)
    got = wait(run, net, v, 2.0, answered)
    resp = [d for t, d in run.other if d["type"] == "master_response" and d["device"] == master and t >= start]
    if not got or not resp[0]["yielded"]:
        run.row("handoff to the virtual player", "FAIL", f"deck {master} {'refused' if got else 'did not answer within 2 s'}")
        return
    t_resp = [t for t, d in run.other if d["type"] == "master_response" and t >= start][0]
    v.master = True
    v.sync_counter += 1
    v.next_status = 0

    def dropped():
        rows = run.status.get(master, [])
        return rows and rows[-1][0] > t_resp and not rows[-1][1]["master"]
    ok = wait(run, net, v, 3.0, dropped)
    run.row("handoff to the virtual player", "PASS" if ok else "FAIL",
            f"deck {master} answered in {(t_resp - start) / 1000:.0f} ms and "
            + ("dropped master in its status" if ok else "still reports master 3 s later"))
    run.events.append((run.t(), "virtual player is tempo master; waiting for a deck to ask for it back"))
    deck = a.handback_deck
    print(f"\n>>> The virtual player is tempo master. Press MASTER on {'DECK ' + str(deck) if deck else 'a deck'} "
          f"within 30 s.\n", flush=True)
    t_ask = now_us()
    got = wait(run, net, v, 30.0, lambda: bool(v.requests))
    if not got:
        run.row("handoff back to a deck", "FAIL", "no deck asked for master within 30 s")
        v.master = False
        return
    t_req, req = v.requests[0]
    asker = req["requester"]
    if deck and asker != deck:
        run.row("handoff back to a deck", "FAIL", f"deck {asker} asked, expected {deck}")
    net.send(master_response(v.NAME, v.device, True), req["src"], PORT_BEAT)
    v.handoff_to = asker
    v.next_status = 0

    def taken():
        rows = run.status.get(asker, [])
        return rows and rows[-1][0] > t_req and rows[-1][1]["master"]
    ok = wait(run, net, v, 3.0, taken)
    v.master = False
    v.handoff_to = 0xff
    v.next_status = 0
    run.row("handoff back to a deck", "PASS" if ok else "FAIL",
            f"deck {asker} asked {(t_req - t_ask) / 1e6:.1f} s after the prompt; "
            + ("its status shows master" if ok else "its status did not show master within 3 s"))


def step_sync(run, net, v):
    command = SYNC_COMMANDS[run.args.sync_command]
    before = {n: run.status[n][-1][1] for n in (1, 2, 3, 4) if run.status.get(n)}
    start = now_us()
    net.send(sync_control(v.NAME, v.device, command), run.args.unit, PORT_BEAT)
    run.events.append((run.t(), f"sent sync control {run.args.sync_command} to {run.args.unit}"))
    wait(run, net, v, 2.0)
    changed = []
    for n, b in before.items():
        rows = [d for t, d in run.status.get(n, []) if t > start]
        if not rows:
            continue
        a = rows[-1]
        if (a["synced"], a["master"]) != (b["synced"], b["master"]):
            changed.append(f"deck {n}: sync {int(b['synced'])}->{int(a['synced'])} master {int(b['master'])}->{int(a['master'])}")
    run.row(f"sync control '{run.args.sync_command}'", "INFO",
            ("; ".join(changed) if changed else "no deck changed its sync or master flag within 2 s")
            + ". The packet names no deck and the unit's decks share one address: the helper acts on it only when "
            "exactly one of decks 3/4 has a track; the stock decks ignore remote sync (static analysis).")


# ---- dbserver (metadata) -----------------------------------------------------------------------------------------

def db_field_num(v, size):
    return {1: b"\x0f", 2: b"\x10", 4: b"\x11"}[size] + v.to_bytes(size, "big")


def db_message(txid, mtype, args):
    """dbserver message: magic, transaction, type, argument count, argument tag blob, arguments (all 4-byte
    numbers here)."""
    tags = bytes([0x06] * len(args)) + bytes(12 - len(args))
    out = db_field_num(0x872349ae, 4) + db_field_num(txid, 4) + db_field_num(mtype, 2) + db_field_num(len(args), 1)
    out += b"\x14" + struct.pack(">I", 12) + tags
    for x in args:
        out += db_field_num(x, 4)
    return out


def db_read_field(sock):
    tag = db_recv(sock, 1)[0]
    if tag == 0x0f:
        return db_recv(sock, 1)[0]
    if tag == 0x10:
        return u16(db_recv(sock, 2), 0)
    if tag == 0x11:
        return u32(db_recv(sock, 4), 0)
    if tag == 0x14:
        n = u32(db_recv(sock, 4), 0)
        return db_recv(sock, n)
    if tag == 0x26:
        n = u32(db_recv(sock, 4), 0)
        return db_recv(sock, 2 * n).decode("utf-16-be", "replace").rstrip("\0")
    raise ValueError(f"unknown field tag {tag:#x}")


def db_recv(sock, n):
    b = b""
    while len(b) < n:
        part = sock.recv(n - len(b))
        if not part:
            raise ConnectionError("closed")
        b += part
    return b


def db_read_message(sock):
    magic = db_read_field(sock)
    if magic != 0x872349ae:
        raise ValueError("bad message magic")
    txid, mtype, argc = db_read_field(sock), db_read_field(sock), db_read_field(sock)
    _tags = db_read_field(sock)
    return mtype, [db_read_field(sock) for _ in range(argc)]


def step_metadata(run, device):
    """Title of each deck's track from the unit's database server, by rekordbox id. Titles are kept out of the saved
    output unless --show-titles; only whether one came back and its length are recorded."""
    unit = run.args.unit
    try:
        with socket.create_connection((unit, 12523), timeout=3) as s:
            s.sendall(struct.pack(">I", 15) + b"RemoteDBServer\0")
            port = u16(db_recv(s, 2), 0)
    except OSError as e:
        run.row("database server", "FAIL", f"port lookup on TCP 12523 failed: {e}")
        return
    run.row("database server", "INFO", f"dbserver on TCP {port}")
    for n in (1, 2, 3, 4):
        rows = run.status.get(n, [])
        if not rows or not rows[-1][1]["rekordbox_id"]:
            continue
        st = rows[-1][1]
        try:
            with socket.create_connection((unit, port), timeout=3) as s:
                s.sendall(db_field_num(1, 4))
                db_recv(s, 5)
                s.sendall(db_message(0xfffffffe, 0x0000, [device]))
                db_read_message(s)
                dmst = (device << 24) | (0x01 << 16) | (st["track_slot"] << 8) | st["track_type"]
                s.sendall(db_message(1, 0x2002, [dmst, st["rekordbox_id"]]))
                mtype, args = db_read_message(s)
                count = args[1] if len(args) > 1 else 0
                s.sendall(db_message(2, 0x3000, [dmst, 0, count, 0, count, 0]))
                title = None
                for _ in range(count + 2):
                    mtype, args = db_read_message(s)
                    if mtype == 0x4101 and len(args) > 6 and args[6] == 0x04:
                        title = args[3]
                    if mtype == 0x4201:
                        break
            shown = title if run.args.show_titles else (f"<{len(title)} characters, hidden>" if title else None)
            run.row(f"deck {n} track identity", "PASS" if title else "FAIL",
                    f"track {st['track_device']}/{st['track_slot']}/{st['rekordbox_id']}: title {shown or 'not returned'}")
        except (OSError, ValueError, ConnectionError, IndexError) as e:
            run.row(f"deck {n} track identity", "FAIL", f"query failed: {e}")


# ---- link measurement --------------------------------------------------------------------------------------------

def link_measure(run, net):
    """A ping to every Link node the unit announced, about 5 per second."""
    for node, rows in run.link_alive.items():
        d = rows[-1][1]
        if d.get("src") != run.args.unit or "mep4" not in d:
            continue
        ip, port = d["mep4"]
        try:
            net.link_uc.sendto(link_ping(now_us()), (ip if ip != "0.0.0.0" else d["src"], port))
        except OSError as e:
            run.notes.append(f"Link ping failed: {e}")


# ---- driver --------------------------------------------------------------------------------------------------------

GUIDED = (("load", "Load a track on DECK {d} (it may already be loaded: load another one)"),
          ("play", "Press PLAY on DECK {d}"),
          ("tempo", "Move the TEMPO fader of DECK {d} a little"),
          ("master", "Press MASTER on DECK {d}"),
          ("pause", "Press PLAY on DECK {d} again to pause it"))


def local_ip(unit):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((unit, PORT_STATUS))  # no packet is sent by connect() on UDP
        return s.getsockname()[0]
    finally:
        s.close()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--unit", help="the XDJ-XZ's IPv4 address")
    p.add_argument("--seconds", type=float, default=60.0, help="passive listening time (default 60)")
    p.add_argument("--iface-ip", help="this PC's address on the unit's LAN (default: the one that routes to --unit)")
    p.add_argument("--broadcast", help="broadcast address (default: the /24 of --iface-ip)")
    p.add_argument("--bind", default="", help="address to bind the listening sockets to (default all)")
    p.add_argument("--announce", action="store_true", help="join as a virtual player (sends packets)")
    p.add_argument("--device", type=int, help="virtual player number (default: lowest free in 7-15)")
    p.add_argument("--mac", help="virtual player MAC (default: this PC's)")
    p.add_argument("--handoff", action="store_true", help="tempo master handoff to and from the virtual player")
    p.add_argument("--handback-deck", type=int, help="the deck that must ask for master back")
    p.add_argument("--sync-command", choices=sorted(SYNC_COMMANDS), help="send one sync control packet to the unit")
    p.add_argument("--metadata", action="store_true", help="ask the unit's database server for each deck's title")
    p.add_argument("--show-titles", action="store_true", help="put track titles in the output (off by default)")
    p.add_argument("--link", action="store_true", help="observe the Ableton Link session (sends Link pings)")
    p.add_argument("--master-device", type=int, help="the Pro DJ Link deck the Link session should follow")
    p.add_argument("--guided", help="prompt actions on these decks, e.g. 3 or 1,3; needs --announce to check them")
    p.add_argument("--guided-step-seconds", type=float, default=12.0)
    p.add_argument("--expect-decks", action="store_true", help="fail (not skip) when decks 3/4 are absent")
    p.add_argument("--expect-link", action="store_true", help="fail (not skip) when the unit is in no Link session")
    p.add_argument("--out", help="JSON result file (default xz-sync-verify-<time>.json here)")
    p.add_argument("--self-test", action="store_true", help="check the encoders and decoders, send nothing")
    p.add_argument("--replay", help="analyse a passive_capture.py JSON file instead of listening (sends nothing)")
    a = p.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.unit:
        p.error("--unit is required")
    if a.replay:
        return replay(a)
    if (a.handoff or a.sync_command or a.metadata) and not a.announce:
        p.error("--handoff, --sync-command and --metadata need --announce")
    a.iface_ip = a.iface_ip or local_ip(a.unit)
    a.broadcast = a.broadcast or ".".join(a.iface_ip.split(".")[:3] + ["255"])
    run = Run(a)
    run.guided_steps = []
    net = Net(a, run)
    print(f"listening on {a.iface_ip} for {a.seconds:.0f} s; unit {a.unit}; broadcast {a.broadcast}"
          + ("; announcing" if a.announce else "; listen only") + ("; Ableton Link" if a.link else ""), flush=True)
    v = None
    if a.announce:
        v = Virtual(a, run, net)
        if not v.join(now_us()):
            run.row("virtual player joined", "FAIL", f"number {v.device} was defended by another device")
            v = None
        else:
            run.row("virtual player joined", "INFO", f"as number {v.device} from {a.iface_ip}")
    end = now_us() + int(a.seconds * 1e6)
    next_ping = 0
    steps = []
    if a.guided:
        for deck in [int(x) for x in a.guided.split(",")]:
            for action, text in GUIDED:
                steps.append((deck, action, text.format(d=deck)))
        end = max(end, now_us() + int((len(steps) * a.guided_step_seconds + 5) * 1e6))
    next_step = now_us() + 3_000_000
    while now_us() < end:
        net.poll(0.02)
        if v:
            v.tick()
        t = now_us()
        if a.link and t >= next_ping:
            link_measure(run, net)
            next_ping = t + 200_000
        if steps and t >= next_step:
            deck, action, text = steps.pop(0)
            print(f">>> {text} (now, within {a.guided_step_seconds:.0f} s)", flush=True)
            run.guided_steps.append({"deck": deck, "action": action, "t_us": t, "end_us": t + int(a.guided_step_seconds * 1e6)})
            run.events.append((run.t(), text))
            next_step = t + int(a.guided_step_seconds * 1e6)
    if v and a.handoff:
        step_handoff(run, net, v)
    if v and a.sync_command:
        step_sync(run, net, v)
    if v and a.metadata:
        step_metadata(run, v.device)
    analyse_devices(run)
    beats = analyse_beats(run)
    analyse_status(run, beats)
    analyse_guided(run)
    link_analysis(run, beats)
    report(run)
    return 1 if any(r["verdict"] == "FAIL" for r in run.rows) else 0


def replay(a):
    """The passive analysis on a saved capture: {"packets": [{"t": seconds, "port", "src", "hex"}, ...]}."""
    a.announce = False
    a.iface_ip = a.iface_ip or "0.0.0.0"
    run = Run(a)
    run.guided_steps = []
    with open(a.replay, encoding="utf-8") as f:
        packets = json.load(f)["packets"]
    if packets:
        run.t0 = int(packets[0]["t"] * 1e6)
    for pk in packets:
        if pk["port"] in (PORT_ANNOUNCE, PORT_BEAT, PORT_STATUS):
            ingest(run, int(pk["t"] * 1e6), pk["port"], pk["src"], bytes.fromhex(pk["hex"]))
    analyse_devices(run)
    beats = analyse_beats(run)
    analyse_status(run, beats)
    report(run)
    return 1 if any(r["verdict"] == "FAIL" for r in run.rows) else 0


def report(run):
    width = max(len(r["check"]) for r in run.rows) if run.rows else 10
    print()
    for r in run.rows:
        print(f"{r['verdict']:<5} {r['check']:<{width}}  {r['detail']}")
    counts = {k: sum(1 for r in run.rows if r["verdict"] == k) for k in ("PASS", "FAIL", "SKIP", "INFO")}
    print(f"\n{counts['PASS']} pass, {counts['FAIL']} fail, {counts['SKIP']} skipped, {counts['INFO']} for information")
    for n in run.notes[:10]:
        print("note:", n)
    out = run.args.out or time.strftime("xz-sync-verify-%Y%m%d-%H%M%S.json")
    result = {"tool": "xz_sync_verify.py", "unit": run.args.unit, "seconds": run.args.seconds,
              "options": {k: v for k, v in vars(run.args).items() if k not in ("out",)}, "rows": run.rows,
              "events": run.events, "notes": run.notes, "counts": counts}
    with open(out, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1, default=str)
    print(f"saved {out}")


# ---- self test -----------------------------------------------------------------------------------------------------

def self_test():
    fails = []

    def check(c, what):
        if not c:
            fails.append(what)
    mac, ip = bytes.fromhex("00005e00533a"), bytes([192, 0, 2, 7])
    k = decode(PORT_ANNOUNCE, keepalive("XZ-VERIFY", 9, mac, ip))
    check(k and k["type"] == "keepalive" and k["device"] == 9 and k["ip"] == "192.0.2.7" and len(keepalive("x", 9, mac, ip)) == 0x36,
          "keep-alive round trip")
    claims = claim_packets("XZ-VERIFY", 9, mac, ip)
    check(len(claims) == 12 and [len(c) for c in claims[::3]] == [0x25, 0x2c, 0x32, 0x26], "claim packet lengths")
    check(decode(PORT_ANNOUNCE, claims[6])["device"] == 9 and decode(PORT_ANNOUNCE, claims[9])["device"] == 9, "claim numbers")
    s = decode(PORT_STATUS, cdj_status("XZ-VERIFY", 9, master=True, bpm=128.0, handoff_to=3, counter=5))
    check(s["type"] == "status" and s["master"] and s["bpm"] == 128.0 and s["master_handoff"] == 3 and s["len"] == 0x11c
          and s["packet_counter"] == 5 and s["tempo_valid"] == 0x8000 and s["master_mode"] == 1, "status round trip")
    r = decode(PORT_BEAT, master_request("XZ-VERIFY", 9))
    check(r["type"] == "master_request" and r["requester"] == 9 and r["len"] == 0x28, "master request")
    r = decode(PORT_BEAT, master_response("XZ-VERIFY", 9, True))
    check(r["type"] == "master_response" and r["yielded"] and r["len"] == 0x2c, "master response")
    r = decode(PORT_BEAT, sync_control("XZ-VERIFY", 9, 0x10))
    check(r["type"] == "sync_control" and r["command"] == 0x10 and r["len"] == 0x2c, "sync control")
    # A stock XZ beat packet (capture, unit identity scrubbed): device 1, 140.00 BPM at +7.50 %, beat 3.
    stock_beat = bytes.fromhex("5173707431576d4a4f4c2858444a2d585a0000000000000000000000000000010001003c000001ad00000359"
                               "00000359000006b300000a0c00000d65ffffffffffffffffffffffffffffffffffffffffffffffff00113333"
                               "000036b003000001")
    b = decode(PORT_BEAT, stock_beat)
    check(b["type"] == "beat" and b["device"] == 1 and b["bpm"] == 140.0 and abs(b["pitch"] - 1.075) < 1e-4
          and b["beat_in_bar"] == 3 and b["next_beat"] == 429, "stock beat packet")
    # Beat statistics on a synthetic stream with one missing beat and 1 ms of jitter.
    beats, t = [], 0
    for i in range(40):
        if i == 20:
            t += 500_000
            continue
        beats.append((t + (1000 if i % 2 else 0), dict(b, beat_in_bar=i % 4 + 1, pitch=1.0, bpm=120.0)))
        t += 500_000
    st = beat_stats(beats)
    check(st["missing"] == 1 and st["numbering_errors"] == 0 and abs(st["jitter_max_ms"] - 1.0) < 0.01, "beat statistics")
    # Link messages.
    tl = struct.pack(">qqq", 468750, 10_000_000, 5_000_000)
    alive = ASDP + bytes([1, 5, 0, 0]) + bytes(range(8)) + struct.pack(">II", 0x746d6c6e, 24) + tl \
        + struct.pack(">II", 0x73657373, 8) + bytes(8) + struct.pack(">II", 0x6d657034, 6) + bytes([192, 0, 2, 7, 0x51, 0x50])
    la = link_alive(alive)
    check(la and la["timeline"] == (468750, 10_000_000, 5_000_000) and la["mep4"] == ("192.0.2.7", 0x5150)
          and abs(60e6 / la["timeline"][0] - 128.0) < 1e-9, "Link alive")
    check(abs(timeline_beat(la["timeline"], 5_000_000 + 468750 * 2) - 12.0) < 1e-9, "Link timeline beat")
    pong = LINKV + b"\x02" + struct.pack(">II", 0x73657373, 8) + bytes(8) + struct.pack(">II", 0x5f5f6774, 8) \
        + struct.pack(">q", 123) + link_ping(77)[9:]
    check(link_pong(pong) == ("0000000000000000", 123, 77), "Link pong")
    check(abs(wrap(3.9, 4.0) + 0.1) < 1e-9 and abs(wrap(-2.1, 4.0) - 1.9) < 1e-9, "wrap")
    msg = db_message(1, 0x2002, [0x01020301, 77])
    check(msg[:5] == b"\x11\x87\x23\x49\xae" and len(msg) == 5 + 5 + 3 + 2 + 5 + 12 + 10, "dbserver message")
    for f in fails:
        print("FAIL", f)
    print(f"{'FAIL' if fails else 'PASS'} self-test: {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
