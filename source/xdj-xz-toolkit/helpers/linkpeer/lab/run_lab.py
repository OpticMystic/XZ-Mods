"""Ableton Link lab: the real helper and the real player half in private namespaces, judged from a reference peer.

Run from Windows (Python 3) after `python ../build.py`:
    python run_lab.py [--seconds 300] [--attempts 3] [--obs-gaps]
    python run_lab.py --self-test

Each attempt runs run_lab.sh as root in WSL Ubuntu (`wsl -d Ubuntu -u root --cd <lab> --exec ...`, as
harness/wsl_root.sh does) and writes results/<stamp>-<n>/. With --obs-gaps the player stand-in stops the master's
observations for 0.3-1.0 s every 5-40 s (standin.c), so the gap case is exercised on purpose.

Checks, as in tests/test_link_policy.c where the lab's timing allows:
  - every helper check passes (refusals, SIGTERM, record removal, no child process, no fork/exec, affinity);
  - steady phase error, session minus master, measured on the reference peer: max under 2 ms;
  - forces: exactly one at the first lock (before 5 s), exactly one within 2 s of the seek, and no other force
    unless the session itself stepped: a force is also expected within 2 s after the helper of the stand-in saw
    the number of peers change (helper-a.log "peers"), because joining a session that was already running moves
    its timeline (Link adopts it) and the policy forces it back. One such force per change, named in the summary;
  - the tempo change reaches the reference peer within 300 ms: 90 % of the step. The time until the reference is
    within 0.005 BPM of the new tempo is reported too;
  - from 100 s on (the clock ratio is learnt): the reference tempo within 0.005 BPM of the master's tempo on the
    monotonic clock, and at most two changes of the shown tempo (0.01 BPM) in steady windows;
  - turning the setting off stops the helper and removes the record.

Verdicts. A check is PASS, FAIL or VOID; an attempt is FAIL when any of its checks failed, else VOID when it was
disturbed or a check is VOID, else PASS; the run is FAIL (exit 1) when any attempt failed, naming every failed
attempt and check, else PASS (exit 0) when the last attempt passed, else VOID (exit 2). A failed check is never
hidden by a stall: VOID means only that no check failed but the measurement was disturbed, so the run must be
repeated. Attempts are repeated only while they are VOID.
  - An attempt is disturbed when more than 5 % of the reference samples are void (measure.c: the measurer woke more
    than 30 ms late or the reference record was older than 40 ms) or a service pass of the stand-in came more than
    250 ms late.
  - Only one check can be VOID by itself: the tempo change reaching the reference within 300 ms, whose samples are
    the few between the change and 300 ms after it. It is VOID, not FAIL, when it took longer than 300 ms and the lab
    recorded a stall inside [change - 100 ms, change + 300 ms]: a void or late reference sample (the measurer was
    blind from the sample before to that one) or a late stand-in service pass (service_gaps_s in standin.txt, the
    interval from the pass before to that one, moved onto the measurer's time line with the two programs' start
    times). Every other check is judged on non-void samples only, which by construction were not taken in a stall,
    so it is never voided: a failure there is a failure of the system under test, stall or not.
"""
import argparse
import csv
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

LAB = Path(__file__).resolve().parent
VOID_FRACTION = 0.05
SERVICE_GAP_US = 250000
REACH_MS = 300.0
SAMPLE_PERIOD_S = 0.01
LATE_SAMPLE_S = 0.03


def linux(path: Path) -> str:
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]


def kv(path: Path) -> dict:
    out = {}
    if path.is_file():
        for line in path.read_text().splitlines():
            parts = line.split(None, 1)
            if parts:
                out[parts[0]] = parts[1] if len(parts) > 1 else ""
    return out


def pairs(text: str) -> list:
    """'t:len t:len' -> [(t, len)] in seconds."""
    out = []
    for item in (text or "").split():
        t, _, n = item.partition(":")
        out.append((float(t), float(n)))
    return out


def blind_intervals(rows: list, standin_gaps: list, offset_s: float) -> list:
    """Intervals (measurer time, seconds) in which the lab saw a stall: void or late reference samples, and late
    stand-in service passes (each given as the time of the late pass and the gap before it, on the stand-in's time
    line, moved by offset_s)."""
    out = []
    prev = None
    for row in rows:
        t = float(row["t_s"])
        if prev is not None and (row["void"] == "1" or t - prev > LATE_SAMPLE_S):
            out.append((prev, t, "reference sample %s" % ("void" if row["void"] == "1" else "late")))
        prev = t
    for t, n in standin_gaps:
        out.append((t - n + offset_s, t + offset_s, "stand-in service pass %.0f ms late" % (n * 1000.0)))
    return out


def reach_check(rows: list, blind: list) -> dict:
    """The tempo change: the first sample whose truth shows the new tempo, then the first one where the reference
    has 90 % of the step, and the first one within 0.005 BPM."""
    reach_ms = settle_ms = change_t = new_bpm = old_bpm = None
    prev = None
    for row in rows:
        if row["void"] == "1" or not row["true_bpm"]:
            continue
        bpm = float(row["true_bpm"])
        if prev is not None and change_t is None and abs(bpm - prev) > 0.5:
            change_t, new_bpm, old_bpm = float(row["t_s"]), bpm, prev
        prev = bpm
        if change_t is not None:
            ref = float(row["ref_tempo"])
            if reach_ms is None and abs(ref - new_bpm) <= 0.1 * abs(new_bpm - old_bpm):
                reach_ms = (float(row["t_s"]) - change_t) * 1000.0
            if settle_ms is None and abs(ref - new_bpm) <= 0.005:
                settle_ms = (float(row["t_s"]) - change_t) * 1000.0
    r = {"change_t": change_t, "reach_ms": reach_ms, "settle_ms": settle_ms}
    if reach_ms is not None and reach_ms <= REACH_MS:
        r["status"], r["detail"] = "PASS", "%.0f ms" % reach_ms
        return r
    if change_t is None:
        r["status"], r["detail"] = "FAIL", "no tempo change seen"
        return r
    lo, hi = change_t - 0.1, change_t + REACH_MS / 1000.0
    cover = [b for b in blind if b[0] < hi and b[1] > lo]
    took = "%.0f ms" % reach_ms if reach_ms is not None else "never"
    if cover:
        r["status"] = "VOID"
        r["detail"] = "took %s, and the lab was stalled inside [%.3f, %.3f] s: %s" % (
            took, lo, hi, "; ".join("%s %.3f-%.3f s" % (b[2], b[0], b[1]) for b in cover))
    else:
        r["status"], r["detail"] = "FAIL", "took %s, no stall recorded inside [%.3f, %.3f] s" % (took, lo, hi)
    return r


def session_changes(path: Path) -> list:
    """Times (helper log seconds) at which the helper's peer count changed, from the second "peers" event on."""
    out, last = [], None
    if path.is_file():
        for line in path.read_text(errors="replace").splitlines():
            try:
                ev = json.loads(line)
            except ValueError:
                continue
            if ev.get("ev") == "peers":
                if last is not None and ev.get("n") != last:
                    out.append(float(ev["t"]))
                last = ev.get("n")
    return out


def force_check(forces: list, seek: float, changes: list) -> tuple:
    first = [t for t in forces if t < 5.0][:1]
    at_seek = [t for t in forces if seek <= t < seek + 2.0]
    explained, unexplained, used = [], [], set()
    for t in forces:
        if t in first or t in at_seek:
            continue
        j = next((c for c in changes if c not in used and c <= t + 0.1 and t - c <= 2.0), None)
        if j is None:
            unexplained.append(t)
        else:
            used.add(j)
            explained.append("%.3f s after the session's peers changed at %.3f s (helper log)" % (t, j))
    good = len(first) == 1 and len(at_seek) == 1 and not unexplained
    detail = "forces at %s s; first lock %s, seek %s" % (forces, first or "none", at_seek or "none")
    if explained:
        detail += "; " + "; ".join(explained)
    if unexplained:
        detail += "; unexplained %s" % unexplained
    return ("PASS" if good else "FAIL"), detail


def judge(out: Path, a) -> dict:
    run_log = (out / "run.log").read_text() if (out / "run.log").is_file() else ""
    checks = [line for line in run_log.splitlines() if line.startswith(("PASS ", "FAIL "))]
    m, s = kv(out / "measure.txt"), kv(out / "standin.txt")
    rows = list(csv.DictReader((out / "measure.csv").open())) if (out / "measure.csv").is_file() else []
    r = {"checks": checks, "measure": m, "standin": s}
    total, voids = int(m.get("samples", 0) or 0), int(m.get("void", 0) or 0)
    gap = int(s.get("max_service_gap_us", 0) or 0)
    r["void_fraction"] = voids / total if total else 1.0
    reasons = []
    if rows and r["void_fraction"] > VOID_FRACTION:
        reasons.append("%.1f %% of the reference samples void" % (100 * r["void_fraction"]))
    if rows and gap > SERVICE_GAP_US:
        reasons.append("a stand-in service pass %.0f ms late" % (gap / 1000.0))
    r["disturbed"] = "; ".join(reasons)
    forces = [float(x) for x in s.get("force_times_s", "").split()]
    r["forces"] = forces
    r["session_changes"] = session_changes(out / "helper-a.log")
    forces_status = force_check(forces, a.seek, r["session_changes"])
    offset = 0.0
    if s.get("start_us") and m.get("start_us"):
        offset = (int(s["start_us"]) - int(m["start_us"])) / 1e6
    blind = blind_intervals(rows, pairs(s.get("service_gaps_s", "")), offset)
    reach = reach_check(rows, blind)
    r["tempo_reach_ms"], r["tempo_within_0005_ms"] = reach["reach_ms"], reach["settle_ms"]
    r["obs_gaps"] = pairs(s.get("obs_gaps_s", ""))
    good = [row for row in rows if row["void"] != "1"]
    late = [row for row in good if row["steady"] == "1" and float(row["t_s"]) >= 100.0]
    r["late_steady_samples"] = len(late)
    r["late_tempo_dev_max"] = max((abs(float(x["ref_tempo"]) - float(x["true_bpm"])) for x in late), default=None)
    shown = [round(float(x["ref_tempo"]) * 100) for x in late]
    r["late_shown_values"] = sorted(set(v / 100 for v in shown))
    changes = [float(q["t_s"]) for p, q in zip(late, late[1:]) if round(float(p["ref_tempo"]) * 100) != round(float(q["ref_tempo"]) * 100)]
    r["late_shown_changes"] = len(changes)
    r["late_shown_change_times"] = changes
    errors = sorted(abs(float(x["error_us"])) for x in good if x["steady"] == "1")
    r["steady_samples"] = len(errors)
    if errors:
        r["steady_mean_abs_us"] = sum(errors) / len(errors)
        r["steady_p99_us"] = errors[min(len(errors) - 1, int(len(errors) * 0.99))]
        r["steady_max_us"] = errors[-1]
        signed = [float(x["error_us"]) for x in good if x["steady"] == "1"]
        r["steady_mean_us"] = sum(signed) / len(signed)

    def ok(cond, detail):
        return ("PASS" if cond else "FAIL", detail)

    failed_helper = [c for c in checks if c.startswith("FAIL")]
    bounds = [
        ("helper and run checks", ok(bool(checks) and not failed_helper, "; ".join(failed_helper) or "%d checks" % len(checks))),
        ("steady phase error max < 2 ms", ok(bool(errors) and errors[-1] < 2000.0, "max %.0f us" % errors[-1] if errors else "no samples")),
        ("forces only at first lock, seek and session changes", forces_status),
        ("tempo change reaches the reference within 300 ms (90 % of the step)", (reach["status"], reach["detail"])),
        ("reference tempo within 0.005 BPM of the master from 100 s",
         ok(r["late_tempo_dev_max"] is not None and r["late_tempo_dev_max"] <= 0.005, "max %s BPM" % r["late_tempo_dev_max"])),
        ("shown tempo changes at most twice from 100 s",
         ok(r["late_shown_changes"] <= 2, "%d changes at %s s, values %s" % (len(changes), ["%.2f" % t for t in changes[:40]], r["late_shown_values"]))),
    ]
    r["bounds"] = [{"check": c, "status": st, "detail": d} for c, (st, d) in bounds]
    r["verdict"] = attempt_verdict(r["bounds"], r["disturbed"])
    return r


def attempt_verdict(bounds: list, disturbed: str) -> str:
    if any(b["status"] == "FAIL" for b in bounds):
        return "FAIL"
    if disturbed or any(b["status"] == "VOID" for b in bounds):
        return "VOID"
    return "PASS"


def run_verdict(attempts: list):
    """attempts: [{"n", "dir", "verdict", "bounds", "disturbed"}] -> (verdict, exit code, summary lines)."""
    failed = [(t, b) for t in attempts for b in t["bounds"] if b["status"] == "FAIL"]
    if failed:
        return "FAIL", 1, ["FAIL attempt %d (%s): %s: %s" % (t["n"], t["dir"], b["check"], b["detail"]) for t, b in failed]
    if attempts and attempts[-1]["verdict"] == "PASS":
        return "PASS", 0, ["PASS attempt %d (%s)" % (attempts[-1]["n"], attempts[-1]["dir"])]
    lines = []
    for t in attempts:
        why = [t["disturbed"]] if t["disturbed"] else []
        why += ["%s: %s" % (b["check"], b["detail"]) for b in t["bounds"] if b["status"] == "VOID"]
        lines.append("VOID attempt %d (%s): no check failed, but %s; rerun" % (t["n"], t["dir"], "; ".join(why) or "no attempt ran"))
    return "VOID", 2, lines or ["VOID: no attempt ran"]


def print_attempt(r: dict) -> None:
    keys = ("verdict", "disturbed", "void_fraction", "forces", "tempo_reach_ms", "tempo_within_0005_ms", "steady_mean_us",
            "steady_mean_abs_us", "steady_p99_us", "steady_max_us", "late_tempo_dev_max", "late_shown_values", "late_shown_changes",
            "obs_gaps")
    print(json.dumps({k: r[k] for k in keys if k in r}, indent=1), flush=True)
    for b in r["bounds"]:
        print("%s %s (%s)" % (b["status"], b["check"], b["detail"]))


# ---- self-test of the verdict rules ------------------------------------------------------------------------------
def self_test() -> int:
    bad = 0

    def expect(name, got, want):
        nonlocal bad
        good = got == want
        bad += not good
        print("%s %s: %r%s" % ("PASS" if good else "FAIL", name, got, "" if good else " (want %r)" % (want,)))

    P = {"check": "a", "status": "PASS", "detail": ""}
    F = {"check": "shown tempo changes at most twice from 100 s", "status": "FAIL", "detail": "28 changes"}
    V = {"check": "tempo change reaches the reference within 300 ms (90 % of the step)", "status": "VOID", "detail": "stalled"}
    expect("failed check in a disturbed attempt is FAIL", attempt_verdict([P, F], "a stand-in service pass 437 ms late"), "FAIL")
    expect("all checks pass in a disturbed attempt is VOID", attempt_verdict([P, P], "6.0 % of the reference samples void"), "VOID")
    expect("a VOID check alone makes the attempt VOID", attempt_verdict([P, V], ""), "VOID")
    expect("a VOID check does not hide a FAIL", attempt_verdict([V, F], ""), "FAIL")
    expect("clean attempt is PASS", attempt_verdict([P, P], ""), "PASS")

    def att(n, verdict, bounds, disturbed=""):
        return {"n": n, "dir": "d%d" % n, "verdict": verdict, "bounds": bounds, "disturbed": disturbed}

    v = run_verdict([att(1, "VOID", [P], "x"), att(2, "FAIL", [P, F], "y"), att(3, "PASS", [P])])
    expect("a failed attempt before a pass makes the run FAIL", v[:2], ("FAIL", 1))
    expect("the summary names the attempt and the check", v[2], ["FAIL attempt 2 (d2): shown tempo changes at most twice from 100 s: 28 changes"])
    expect("VOID then PASS is PASS", run_verdict([att(1, "VOID", [P], "x"), att(2, "PASS", [P])])[:2], ("PASS", 0))
    v = run_verdict([att(1, "VOID", [P], "x"), att(2, "VOID", [V])])
    expect("only VOID attempts make the run VOID, exit 2", v[:2], ("VOID", 2))
    expect("a VOID summary says why", "stalled" in v[2][1] and "x" in v[2][0], True)

    def rows(spec):
        return [{"t_s": "%.3f" % t, "void": vd, "true_bpm": "%.6f" % tb if vd == "0" else "", "ref_tempo": "%.6f" % rt if vd == "0" else ""}
                for t, vd, tb, rt in spec]

    def series(arrive, void_at=None, skip=None):
        out = []
        t = 209.9
        while t < 211.0:
            if skip and skip[0] < t < skip[1]:
                t += SAMPLE_PERIOD_S
                continue
            vd = "1" if void_at and abs(t - void_at) < 1e-6 else "0"
            out.append((t, vd, 128.0 if t < 210.0 - 1e-9 else 130.56, 128.0 if t < arrive - 1e-9 else 130.56))
            t = round(t + SAMPLE_PERIOD_S, 3)
        return rows(out)

    r = reach_check(series(210.04), blind_intervals(series(210.04), [], 0.0))
    expect("reach within 300 ms passes", (r["status"], round(r["reach_ms"])), ("PASS", 40))
    sr = series(210.5)
    expect("reach after 500 ms with no stall recorded fails", reach_check(sr, blind_intervals(sr, [(100.4, 0.4)], 0.0))["status"], "FAIL")
    sr = series(210.5, void_at=210.1)
    expect("reach after 500 ms with a void reference sample inside the window is VOID",
           reach_check(sr, blind_intervals(sr, [], 0.0))["status"], "VOID")
    sr = series(210.5, skip=(210.05, 210.25))
    expect("reach after 500 ms with the measurer asleep inside the window is VOID", reach_check(sr, blind_intervals(sr, [], 0.0))["status"], "VOID")
    sr = series(210.5)
    expect("reach after 500 ms with a stand-in stall inside the window is VOID (time lines aligned)",
           reach_check(sr, blind_intervals(sr, [(210.30, 0.40)], -0.05))["status"], "VOID")
    expect("a stand-in stall outside the window does not void it", reach_check(sr, blind_intervals(sr, [(209.50, 0.20)], 0.0))["status"], "FAIL")

    # The attempt that started this: results/20261115-182303-2 failed the shown-tempo check in a stalled run and was
    # reported VOID. Judged again it must FAIL; its two siblings passed every check while disturbed: VOID.
    args = argparse.Namespace(seek=120.0)
    for name, want in (("20261115-182303-2", "FAIL"), ("20261115-182303-1", "VOID"), ("20261115-182303-3", "VOID")):
        d = LAB / "results" / name
        if not (d / "measure.csv").is_file():
            print("SKIP stored attempt %s is not here" % name)
            continue
        jr = judge(d, args)
        expect("stored attempt %s" % name, jr["verdict"], want)
        if name.endswith("-2"):
            expect("its failed check", [b["check"] for b in jr["bounds"] if b["status"] == "FAIL"], ["shown tempo changes at most twice from 100 s"])
    expect("a force 1 s after a peer joined is explained", force_check([0.19, 1.19, 120.1], 120.0, [0.561])[0], "PASS")
    expect("an extra force with no session change fails", force_check([0.19, 1.19, 120.1], 120.0, [])[0], "FAIL")
    expect("an extra force 3 s after the session change fails", force_check([0.19, 3.6, 120.1], 120.0, [0.561])[0], "FAIL")
    expect("one session change explains one force only", force_check([0.19, 1.19, 2.2, 120.1], 120.0, [0.561])[0], "FAIL")
    expect("no force at the seek fails", force_check([0.19], 120.0, [])[0], "FAIL")
    d = LAB / "results" / "20261115-193302-1"
    if (d / "measure.csv").is_file():
        jr = judge(d, args)
        expect("stored attempt 20261115-193302-1 (peer joined after the first lock) re-judged", jr["verdict"], "VOID")
    with tempfile.TemporaryDirectory() as tmp:
        expect("an attempt that did not run is FAIL, not VOID", judge(Path(tmp), args)["verdict"], "FAIL")
    print("%s verdict self-test: %d failed" % ("FAIL" if bad else "PASS", bad))
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=int, default=300)
    ap.add_argument("--attempts", type=int, default=3)
    ap.add_argument("--drift", type=float, default=40.0)
    ap.add_argument("--seek", type=float, default=120.0)
    ap.add_argument("--tempo-at", type=float, default=210.0)
    ap.add_argument("--tempo-pct", type=float, default=2.0)
    ap.add_argument("--obs-gaps", action="store_true", help="inject observation gaps of 0.3-1.0 s every 5-40 s in the stand-in")
    ap.add_argument("--self-test", action="store_true", help="check the verdict rules and exit")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    stamp = time.strftime("%Y%m%d-%H%M%S")
    attempts = []
    for n in range(1, a.attempts + 1):
        out = LAB / "results" / f"{stamp}-{n}"
        out.mkdir(parents=True, exist_ok=True)
        cmd = ["wsl.exe", "-d", "Ubuntu", "-u", "root", "--cd", linux(LAB), "--exec", "bash", "run_lab.sh", linux(out),
               str(a.seconds), str(a.drift), str(a.seek), str(a.tempo_at), str(a.tempo_pct), "1" if a.obs_gaps else "0"]
        print("attempt", n, "->", out, flush=True)
        subprocess.run(cmd, check=False)
        r = judge(out, a)
        (out / "verdict.json").write_text(json.dumps(r, indent=2) + "\n")
        print_attempt(r)
        attempts.append({"n": n, "dir": out.name, "verdict": r["verdict"], "bounds": r["bounds"], "disturbed": r["disturbed"]})
        if r["verdict"] != "VOID":
            break
    verdict, code, lines = run_verdict(attempts)
    print("attempts:", " ".join("%d:%s" % (t["n"], t["verdict"]) for t in attempts))
    for line in lines:
        print(line)
    print("RUN", verdict, "exit", code)
    return code


if __name__ == "__main__":
    sys.exit(main())
