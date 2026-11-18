#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Runs xz_sync_verify.py against lab stand-ins in private network namespaces (WSL Ubuntu, as root). Nothing
reaches the LAN: the namespaces hang off one bridge in a namespace of their own and have no default route.

Pro DJ Link (--djlink): stock_standin.py plays the stock player (devices 1, 2, 33); the REAL xz-djlink helper
(host build) announces decks 3 and 4 from the bus that xz_djlink_sim writes. Runs:
  1. passive + announce + handoff (deck 3 is master; the verifier takes master, then deck 3 asks for it back)
  2. guided deck 3: the lab answers each prompt by scripting the deck (load, play, tempo, master, pause)
  3. sync control 'on' with only deck 3 loaded
Ableton Link (--link): the player half (link_runtime.c, link_policy.c) in the Link stand-in with the REAL
xz-link-peer (host build); the verifier observes the session. The current policy must show no discontinuity in
steady play; with --old-policy FILE the track-end case must show the r29 jumps (about one per second).

  python3 run_verify_lab.py --out DIR [--djlink] [--link] [--old-policy link_policy.c]
Exit 0 when every expectation holds.
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.join(os.path.dirname(HERE), "xz_sync_verify.py")
HELPERS = os.path.dirname(os.path.dirname(HERE))
DJ = os.path.join(HELPERS, "djlink", "out")
PEER = os.path.join(HELPERS, "linkpeer")
MODS = os.path.join(os.path.dirname(HELPERS), "mods")
SUBNET = "10.79.0"
NODES = {"xzv-dut": 10, "xzv-pc": 20}
BUS = "/dev/shm/xzv-djlink/bus"


def sh(*args, check=True):
    return subprocess.run(args, check=check, capture_output=True, text=True)


def net_up():
    net_down()
    sh("ip", "netns", "add", "xzv-core")
    sh("ip", "-n", "xzv-core", "link", "add", "br0", "type", "bridge")
    sh("ip", "-n", "xzv-core", "link", "set", "br0", "type", "bridge", "mcast_snooping", "0")
    sh("ip", "-n", "xzv-core", "link", "set", "br0", "up")
    for ns, host in NODES.items():
        sh("ip", "netns", "add", ns)
        port = "p" + ns[4:]
        sh("ip", "-n", "xzv-core", "link", "add", port, "type", "veth", "peer", "name", "eth0", "netns", ns)
        sh("ip", "-n", "xzv-core", "link", "set", port, "master", "br0")
        sh("ip", "-n", "xzv-core", "link", "set", port, "up")
        sh("ip", "-n", ns, "link", "set", "lo", "up")
        sh("ip", "-n", ns, "link", "set", "eth0", "address", f"02:79:00:00:00:{host:02x}")
        sh("ip", "-n", ns, "addr", "add", f"{SUBNET}.{host}/24", "broadcast", f"{SUBNET}.255", "dev", "eth0")
        sh("ip", "-n", ns, "link", "set", "eth0", "up")
        sh("ip", "-n", ns, "route", "add", "224.0.0.0/4", "dev", "eth0")
        if "default" in sh("ip", "-n", ns, "route").stdout:
            raise SystemExit("a lab namespace has a default route")


def net_down():
    for ns in ("xzv-dut", "xzv-pc", "xzv-core"):
        pids = sh("ip", "netns", "pids", ns, check=False).stdout.split()
        for pid in pids:
            subprocess.run(["kill", "-9", pid], capture_output=True)
        sh("ip", "netns", "del", ns, check=False)


class Proc:
    def __init__(self, name, ns, argv, out_dir, env=None):
        self.lines = []
        self.log = open(os.path.join(out_dir, name + ".log"), "w")
        self.p = subprocess.Popen(["ip", "netns", "exec", ns, *argv], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, bufsize=1, env=env)
        self.t = threading.Thread(target=self.read, daemon=True)
        self.t.start()

    def read(self):
        for line in self.p.stdout:
            self.lines.append(line)
            self.log.write(line)
            self.log.flush()

    def send(self, obj):
        self.p.stdin.write(json.dumps(obj) + "\n")
        self.p.stdin.flush()

    def wait_for(self, text, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if any(text in l for l in self.lines):
                return True
            time.sleep(0.05)
        return False

    def stop(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                self.p.kill()


def deck(n, **kw):
    return {"cmd": "deck", "deck": n, **kw}


def verify(out, name, *args, on_line=None, timeout=240):
    argv = [sys.executable, "-I", VERIFY, "--unit", f"{SUBNET}.10", "--iface-ip", f"{SUBNET}.20",
            "--broadcast", f"{SUBNET}.255", "--out", os.path.join(out, name + ".json"), *args]
    p = Proc(name, "xzv-pc", argv, out)
    seen = set()
    end = time.time() + timeout
    while p.p.poll() is None and time.time() < end:
        for i, line in enumerate(list(p.lines)):
            if i not in seen:
                seen.add(i)
                if on_line:
                    on_line(line)
        time.sleep(0.05)
    p.stop()
    with open(os.path.join(out, name + ".json")) as f:
        return json.load(f)


def rows(result):
    return {r["check"]: r for r in result["rows"]}


def expect(results, name, cond, detail):
    results.append((name, bool(cond), detail))
    print(f"{'PASS' if cond else 'FAIL'} {name}: {detail}", flush=True)


def djlink(out, results):
    os.makedirs(os.path.dirname(BUS), exist_ok=True)
    sim = Proc("sim", "xzv-dut", [os.path.join(DJ, "xz-djlink-sim"), "--bus", BUS], out)
    if not sim.wait_for('"ready"', 5):
        raise SystemExit("sim did not start")
    base3 = dict(announce=1, load=True, track_player=1, track_slot=2, track_type=1, track_id=1234, bpm=128.0,
                 first_beat_ms=37, length_ms=3600000, pitch=0.0, play=True, master=True, sync=False, on_air=True)
    base4 = dict(announce=1, load=True, track_player=1, track_slot=3, track_type=1, track_id=5678, bpm=174.0,
                 first_beat_ms=120, length_ms=3600000, pitch=-2.5, play=True, master=False, sync=True, on_air=True)
    sim.send(deck(3, **base3))
    sim.send(deck(4, **base4))
    stock = Proc("stock", "xzv-dut", [sys.executable, "-I", os.path.join(HERE, "stock_standin.py"), "--ip", f"{SUBNET}.10",
                                      "--broadcast", f"{SUBNET}.255"], out)
    helper = Proc("helper", "xzv-dut", [os.path.join(DJ, "xz-djlink"), "--iface", "eth0", "--bus", BUS, "--no-realtime",
                                        "--log", "/dev/stdout"], out)
    time.sleep(6)

    # 1. passive, announce, handoff to the verifier and back to deck 3.
    def on_line(line):
        if "The virtual player is tempo master" in line:
            time.sleep(1.0)
            sim.send(deck(3, wants_master=True))
    r = rows(verify(out, "run1-handoff", "--seconds", "15", "--announce", "--handoff", "--handback-deck", "3",
                    "--expect-decks", on_line=on_line))
    for check in ("stock devices announced (1, 2, 33)", "decks 3 and 4 announced", "keep-alive cadence of 3/4 vs stock",
                  "deck 3 beat timing as good as stock", "deck 4 beat timing as good as stock", "deck 3 beat numbering",
                  "deck 4 beat numbering", "deck 3 status fields as stock fills them", "deck 3 status rate vs stock",
                  "deck 3 status agrees with its beat packets", "deck 4 status agrees with its beat packets",
                  "one tempo master at a time", "handoff to the virtual player", "handoff back to a deck"):
        got = r.get(check, {})
        expect(results, f"run 1: {check}", got.get("verdict") == "PASS", got.get("detail", "missing"))
    fields = r.get("deck 4 status fields as stock fills them", {})
    expect(results, "run 1: deck 4 status fields compared", fields.get("verdict") in ("PASS", "FAIL"), fields.get("detail", "missing"))

    # 2. guided deck 3: answer each prompt.
    sim.send(deck(3, master=False))
    sim.send(deck(4, master=True))
    actions = {"Load a track on DECK 3": deck(3, load=True, track_id=4321, play=False),
               "Press PLAY on DECK 3 (": deck(3, play=True),
               "Move the TEMPO fader of DECK 3": deck(3, pitch=1.5),
               "Press MASTER on DECK 3": deck(3, wants_master=True),
               "Press PLAY on DECK 3 again": deck(3, play=False)}

    def guided(line):
        for k, cmd in actions.items():
            if k in line:
                time.sleep(1.0)
                sim.send(cmd)
    r = rows(verify(out, "run2-guided", "--seconds", "5", "--announce", "--guided", "3", "--guided-step-seconds", "6",
                    on_line=guided))
    for act in ("load", "play", "tempo", "master", "pause"):
        got = r.get(f"guided deck 3: {act}", {})
        expect(results, f"run 2: guided {act}", got.get("verdict") == "PASS", got.get("detail", "missing"))

    # 3. sync control with only deck 3 loaded: it acts on deck 3.
    sim.send(deck(4, unload=True))
    sim.send(deck(3, load=True, play=True, sync=False))
    time.sleep(2)
    r = rows(verify(out, "run3-sync", "--seconds", "4", "--announce", "--sync-command", "on"))
    got = r.get("sync control 'on'", {})
    expect(results, "run 3: sync control reaches deck 3", "deck 3: sync 0->1" in got.get("detail", ""), got.get("detail", "missing"))
    for p in (helper, stock, sim):
        p.stop()
    bad = [l for l in helper.lines if "AddressSanitizer" in l or "runtime error" in l]
    expect(results, "helper clean", not bad, f"{len(bad)} sanitizer lines")


def link(out, results, policy, name, freeze):
    """The Link stand-in (real player half + real helper) in xzv-dut, the verifier in xzv-pc."""
    bin_dir = "/tmp/xzv-bin"
    os.makedirs(bin_dir, exist_ok=True)
    shm = "/dev/shm/xzv-link"
    subprocess.run(["rm", "-rf", shm])
    os.makedirs(shm + "/a")
    exe = os.path.join(bin_dir, "standin-" + name)
    subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread", f"-I{PEER}/src", f"-I{MODS}",
                    f'-DXZ_LINK_DIR="{shm}/a"', os.path.join(PEER, "lab", "standin.c"), os.path.join(MODS, "link_runtime.c"),
                    policy, os.path.join(MODS, "link_clock.c"), os.path.join(MODS, "djlink_facts.c"), "-lm", "-o", exe],
                   check=True)
    subprocess.run(["cp", os.path.join(PEER, "out", "host", "xz-link-peer"), shm + "/a/xz-link-peer"], check=True)
    os.chmod(shm + "/a/xz-link-peer", 0o700)
    env = dict(os.environ)
    if freeze:
        env.update(XZ_LAB_FREEZE_S="25", XZ_LAB_FREEZE_LEN_S="30")
    stand = Proc("link-standin-" + name, "xzv-dut", [exe, "70", "40", "1000", "1000", "0", os.path.join(out, f"truth-{name}.bin"),
                                                    os.path.join(out, f"standin-{name}.log"), os.path.join(out, f"standin-{name}.txt"), "0"],
                 out, env=env)
    time.sleep(5)
    r = rows(verify(out, "link-" + name, "--seconds", "55", "--link", "--expect-link"))
    stand.p.wait(60)
    subprocess.run(["cp", shm + "/a/log", os.path.join(out, f"helper-{name}.log")])
    return r


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--djlink", action="store_true")
    p.add_argument("--link", action="store_true")
    p.add_argument("--old-policy", help="an older link_policy.c, run on the track-end case too")
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    results = []
    net_up()
    try:
        if a.djlink:
            djlink(a.out, results)
        if a.link:
            r = link(a.out, results, os.path.join(MODS, "link_policy.c"), "current-track-end", True)
            jumps = r.get("Link phase discontinuities", {})
            expect(results, "Link: unit seen, clock measured", r.get("Link clock measured", {}).get("verdict") == "PASS",
                   r.get("Link clock measured", {}).get("detail", "missing"))
            tempo = r.get("Link session tempo", {})
            expect(results, "Link: tempo of the stand-in master (128 BPM on a +40 ppm clock)",
                   abs(tempo.get("tempo", 0) - 128.0) < 0.02, tempo.get("detail", "missing"))
            # The stand-in's track ends at 25 s and the next one starts at 55 s (its clock; the verifier starts 5 s
            # later): one jump, for the next track, and none while the master stands still.
            starts = [t for t, _b, _ms in jumps.get("jumps", [])]
            expect(results, "Link: current policy, track end: one jump, at the next track only",
                   len(starts) == 1 and 47.0 <= starts[0] <= 53.0, jumps.get("detail", "missing"))
            if a.old_policy:
                r = link(a.out, results, a.old_policy, "old-track-end", True)
                jumps = r.get("Link phase discontinuities", {})
                n = len(jumps.get("jumps", []))
                expect(results, "Link: old policy, track end: the r29 jumps are seen (about one a second)",
                       jumps.get("verdict") == "FAIL" and n >= 15, jumps.get("detail", "missing"))
    finally:
        net_down()
    failed = [n for n, ok, _ in results if not ok]
    print(f"\n{len(results) - len(failed)} of {len(results)} expectations hold" + (f"; failed: {failed}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
