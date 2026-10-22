# CLASSIC native SYNC publication regression

Run `python -B tests/sync_classic/run.py --arch host --build-dir /absolute/artifact/path` from `mods`.
Use `--arch arm` with an existing arm-linux-gnueabi-gcc and qemu-arm toolchain.
Host enables ASan/UBSan with no recovery. ARM uses the existing `/usr/arm-linux-gnueabi` sysroot.
Windows uses the existing Ubuntu WSL distribution. The script never installs dependencies.

The driver generates assertion-enabled test objects from production code at execution time.
It retains the exact frame scheduler through the ownership decision, owner scheduler through
queue admission, clock publication, the full LOAD wrapper and command-result bookkeeping,
complete clock cache/qualification functions, actual wave identity loop and interface
lifecycle decision. Only the final native LOAD call is replaced by a fixture result;
generation checks, firmware-code guard branches and loaded-state refusal all execute.
Render pixels, native firmware calls, worker
threads, filesystem media discovery and actual firmware memory are not executed.
The real sync_native/native_clock readers use the existing synthetic memory fixture. The real
global dispatcher, runtime, selector, player queue and completed PCM path are exercised.
Source hashing verifies the test did not edit the supplied source tree.

`--source /other/mods --mode positive --expect-red` applies the same regression to old source.
Behavioral RED must exit 1 with assertion diagnostics, not a compiler/process failure.
The independent completed PCM phase oracle uses the fake clock's specified phase and rate,
not the production SYNC lock bit. Fixture clock time and source identity are synthetic.
This proves offline behavior, not physical deck timing, native hardware acceptance or a live cause.

The original 463 checks remain in positive, negative, coherence and uncertainty modes.
refusal, fence, master and prefence retain the four R3 review regressions. drawing
checks ownership across CLASSIC, ineligible MODERN and stale MODERN frame input.
observation injects load-id, loading, token and media ABA while native execution
is in flight, for both accepted and refused results. completion checks that a later
refusal cannot resurrect an already completed fence. boundary distinguishes token
changes before execution from later changes, preserves settled state changes, and
checks fresh publication after refusal. It also changes the token during the
wrapper's firmware guard reads, before native execution. guards exercises the actual
LOAD refusal branches and saturating observation-counter exhaustion. Total: 853 checks.

The publisher keeps its 33 ms gate; cache consumers retain the 150 ms age bound.
The extra guarded LOAD-boundary reads run only on the native owner command path.
Tests do not start the production control worker or prove owner event cadence,
hardware latency, native asynchronous completion, USB detection or DAC phase.
Completely unobserved same-tuple reloads remain indistinguishable between polls.
