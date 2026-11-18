"""Breaks each rule of link_policy.c in a scratch copy and checks that tests/test_link_policy.c then fails.

Run from Windows with WSL Ubuntu present:
    python tests/mutate_link_policy.py --out <scratch dir>
The unchanged policy must pass first. Every mutation must make the simulation exit non-zero; the first FAIL
line it prints is shown, so the report can say which check caught it.
"""
import argparse
import subprocess
import sys
from pathlib import Path

MODS = Path(__file__).resolve().parents[1]

# Rule 4 holds an interval in which the master did not move by its own tempo and restarts the step count. It also
# holds a paused or scratched master (its phase stands still or jumps), so the mutations of those rules switch it off
# too, to test their own rule.
RULE4_HOLD = ("  p->steps = 0;\n  return hold(p, XZ_LINK_HOLD_NOT_RUNNING);", "  (void)0;")

MUTATIONS = [
    # The clock ratio is now measured from the master's own phase (rule 1), not integrated from the session error:
    # the old "integral trim removed" mutation becomes "clock ratio removed".
    ("clock ratio removed", "p->ratio = p->fit.seconds >= RATIO_MIN_S ? p->fit.ratio : 0;", "p->ratio = 0;"),
    ("step needs only one observation", ["if (++p->steps >= XZ_LINK_STEP_COUNT &&", RULE4_HOLD[0]],
     ["if (++p->steps >= 1 &&", RULE4_HOLD[1]]),
    ("step threshold 0.3 ms instead of 10 ms", "if (fabs(e_us) > XZ_LINK_STEP_US) {", "if (fabs(e_us) > 300.0) {"),
    ("no one-force-per-second limit", "(!p->force_commands || now - p->last_force_us >= XZ_LINK_FORCE_GAP_US)", "1"),
    ("stale observations used", "|| age >= (int64_t)XZ_LINK_FRESH_US", "|| 0"),
    ("uncertain observations used", "if (m->uncertainty_us >= XZ_LINK_UNCERTAIN_US) {", "if (0) {"),
    ("paused master chased", ["if (!m->playing) {", RULE4_HOLD[0]], ["if (0) {", RULE4_HOLD[1]]),
    ("scratching chased", ["if (m->manual) {", RULE4_HOLD[0]], ["if (0) {", RULE4_HOLD[1]]),
    ("tempo changes only on the slow trim path",
     "if (off > FAR || (off > (p->shown ? NEAR_HELD : NEAR) && (moved || ramp || now - p->tempo_sent_us >= TRIM_GAP_US))) {",
     "if ((moved || ramp || 1) && off > (p->shown ? NEAR_HELD : NEAR) && now - p->tempo_sent_us >= TRIM_GAP_US) {"),
    ("proportional pull 30 times stronger", "#define KP 0.1", "#define KP 3.0"),
    ("shown-tempo hold removed", "  if (held) desired = hold_shown(p, m->bpm, target, desired, ef, now);", "  (void)held;"),
    ("no wait for the acknowledgement", "else if (now - p->sent_us < XZ_LINK_ACK_US) return hold(p, XZ_LINK_HOLD_PENDING);",
     "else if (0) return hold(p, XZ_LINK_HOLD_PENDING);"),
    # Rule 1: the ratio belongs to a clock.
    ("rule 1: ratio relearnt after a master change, pause or loop on this unit", " p->slope = 0;\n}",
     " p->slope = 0;\n memset(&p->fit, 0, sizeof p->fit);\n}"),
    ("rule 1: a seek or jump is fitted as clock drift",
     "if (fabs(moved_us - p->fit.ratio * dt * 1e6) <= RATIO_JUMP_US) inc_us = moved_us;", "inc_us = moved_us;"),
    ("rule 1: an external master keeps this unit's ratio",
     "if (external && (p->clock != XZ_LINK_CLOCK_EXTERNAL || p->clock_epoch != m->epoch)) {", "if (0) {"),
    # Rule 2: small phase errors are absorbed slowly once the ratio is learnt.
    ("rule 2: the learning pull stays on after learning",
     "double pull = learnt && !p->released && !p->recovering ? absorb(ef) : clamp(KP * ef / 1e6, XZ_LINK_PULL_MAX);",
     "double pull = 0 && learnt && !p->released && !p->recovering ? absorb(ef) : clamp(KP * ef / 1e6, XZ_LINK_PULL_MAX);"),
    # Rule 3: the shown value changes only for more than 1 ms of phase, and stays 60 s after such a change.
    ("rule 3: the shown value changes without the 1 ms phase cost", "if (dir && fabs(ef_us) > CROSS_US &&", "if (dir && 1 &&"),
    ("rule 3: no 60 s stay after a change", "int dwelt = !p->crossed || now - p->crossed_us >= DWELL_US;", "int dwelt = 1;"),
    # Rule 4: a master that does not move by its own tempo is not chased (the unit's force every second, r29).
    ("rule 4 removed: a master that stands still is chased",
     "  jumped = fabs(run_us) > RUN_TOL_US;", "  jumped = 0;\n  run_us = 0;"),
    ("rule 4: a stand-still interval is not held", RULE4_HOLD[0], RULE4_HOLD[1]),
    ("rule 4: a creeping master is forced", "fabs(crept) <= RUN_TOL_US &&", "1 &&"),
    ("rule 4: a step is forced before the master has moved again",
     "  p->steps = 0;\n  return hold(p, XZ_LINK_HOLD_NOT_RUNNING);", "  return hold(p, XZ_LINK_HOLD_NOT_RUNNING);"),
]


def linux(path: Path) -> str:
    s = str(path.resolve()).replace("\\", "/")
    return "/mnt/" + s[0].lower() + s[2:]


def wsl(cmd: str) -> subprocess.CompletedProcess:
    return subprocess.run(["wsl", "-d", "Ubuntu", "--exec", "bash", "-c", cmd], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)


def build_and_run(policy: Path, out: Path, name: str) -> subprocess.CompletedProcess:
    """A mutant that does not compile proves nothing, so a build failure stops the run."""
    binary = out / f"sim-{name}"
    built = wsl(f"gcc -std=c11 -O2 -Wall -Wextra -Werror -I '{linux(MODS)}' '{linux(MODS / 'tests/test_link_policy.c')}' "
                f"'{linux(policy)}' '{linux(MODS / 'link_clock.c')}' -lm -o '{linux(binary)}'")
    if built.returncode != 0:
        print(built.stdout[-3000:])
        raise SystemExit(f"ERROR {name} does not compile")
    return wsl(f"'{linux(binary)}'")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    source = (MODS / "link_policy.c").read_text()
    baseline = build_and_run(MODS / "link_policy.c", a.out, "baseline")
    if baseline.returncode != 0:
        print(baseline.stdout[-2000:])
        print("FAIL the unchanged policy does not pass")
        return 1
    print("PASS unchanged policy")
    survivors = 0
    for i, (name, old, new) in enumerate(MUTATIONS):
        olds, news = ([old], [new]) if isinstance(old, str) else (old, new)
        text = source
        for o, n in zip(olds, news):
            if text.count(o) != 1:
                print(f"ERROR mutation '{name}': pattern occurs {text.count(o)} times")
                return 2
            text = text.replace(o, n)
        mutant = a.out / f"link_policy_m{i}.c"
        mutant.write_text(text)
        r = build_and_run(mutant, a.out, f"m{i}")
        fails = [line for line in r.stdout.splitlines() if line.startswith("FAIL ")]
        if r.returncode == 0:
            survivors += 1
            print(f"SURVIVED {name}")
        else:
            distinct = []
            for line in fails:
                text = line.split(": ", 1)[-1]
                if not text.startswith("link policy simulation") and text not in distinct:
                    distinct.append(text)
            shown = "; ".join(distinct[:3]) if distinct else r.stdout.strip().splitlines()[-1]
            print(f"CAUGHT   {name}: {len(fails) - 1} failed checks; first: {shown}")
    print(f"{len(MUTATIONS) - survivors} of {len(MUTATIONS)} mutations caught")
    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
