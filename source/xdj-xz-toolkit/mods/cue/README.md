# XZ gate and smart cue port

`cue.c` implements independent per-deck gate sessions and smart-cue following. Both settings start off. It passes stock pad presses/releases through, updates smart CUE immediately after a successful stock press recalls a pre-existing cue, waits until stock release bookkeeping completes before gate return, ignores newly recorded/deleted/moved cue slots, supports PLAY latching, and clears sessions when settings or pad mode change. Native adapter is ARM32-only; it does not install hooks or modify device files.

Integration: create `xz_cue_native` with zero initialization, validate the exact executable and all target function bytes against `../abi/`, then set its `verified` field. Initialize the cue engine using `xz_cue_native_api`. At the verified physical-key vtable wrapper call decode, then before; only a return of1 consumes PLAY. Otherwise invoke stock and then `xz_cue_after(c, event, stock_result)`. Do not unload the module while its hook is installed. Serialize settings with the physical-key thread.

The native methods use firmware-specific addresses. `getCueTime` requires a hidden structure-return pointer and writes0x338 bytes; it is not a scalar getter. IN milliseconds are at0x318; INVALID_TIME is-1. `getPlayingTime`/`setPlayingTime` use milliseconds, with native ceil(samples/44.1)/floor(ms*44.1) rounding. Smart Cue uses `getCommonCueInfoFromCueInfo` at0x63a00 to convert the updated main-cue metadata into native sample units, then `Player::setMemoryCuePosition` at0x63adc with kind0. It resolves the selected player through the guarded two-player vector at the engine singleton0x10e9688 and refuses unloaded or changing readers. Loop OUT/flag are cleared. `pause` receives the same1,1,0 boolean tuple as the physical PLAY playing branch.

Do not use `DjEngineIF::setMemoryCueTime` for Smart Cue. Its player implementation cancels playback reservations and can end slip or audible-pause state. On the controller, a Hot Cue stopped on release with Gate Cue off and Smart Cue on; turning only Smart Cue off restored continuous playback. The emulator regression reproduced the unwanted `cancelReservation` call and changed player fields before the fix. `verify_follow_transport.py` compiles the actual cue adapter and runs the real firmware cue setter instructions against fixture readers. It covers both decks, sample conversion, slip flags and invalid/changing readers. Reader callbacks are emulated, so this does not replace controller acceptance.

```powershell
python mods/cue/verify_follow_transport.py --rbp <private-1.26-rbp> --zig <zig.exe> --evidence <new-report.json>
```

The offline regression needs `pyelftools` and `unicorn==2.1.4`. It never connects to the XZ or packages firmware bytes.

Proof: host test exercises disabled stock flow, smart-memory update while a pad remains held, overlapping release order, exact post-stock release ordering, latch, delete versus recall, newly recorded cue, settings disabled during a hold independent decks and native setter/pause/seek failures. Each deck records the latest native operation error in `last_error`; failed memory setters are not retried on release, and a failed pause prevents seeking. Native adapter compiles with Zig for ARMv7 cortex_a9 gnueabi. These are source/build checks only. Vtable reachability, native return values, exact sample positioning, smart-cue reservation/slip side effects and short-hold transport ordering still need device acceptance; `verified` must not be interpreted as that acceptance.

```powershell
.tmp/cdj-zig/ziglang/zig.exe cc -std=c11 -Wall -Wextra -Werror packages/xdj-xz-toolkit/mods/cue/cue.c packages/xdj-xz-toolkit/mods/cue/test_cue.c -o .tmp/xz-cue-abi/test_cue.exe
.tmp/xz-cue-abi/test_cue.exe
.tmp/cdj-zig/ziglang/zig.exe cc -target arm-linux-gnueabi -mcpu=cortex_a9 -std=c11 -Wall -Wextra -Werror -c packages/xdj-xz-toolkit/mods/cue/native.c -o .tmp/xz-cue-abi/native.o
```

Run the supported acceptance command with assertions explicitly enabled:

```powershell
python packages/xdj-xz-toolkit/mods/cue/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe
```

This first checks that compiling with `-DNDEBUG` fails at the acceptance guard, then builds and runs the optimized suite with `-UNDEBUG`. A build that silently removes assertions cannot print a passing result.
