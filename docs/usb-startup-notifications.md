# USB startup notification recovery

## Cause and evidence

The missing Browse sources are mounted filesystems with missed notifications,
not missing kernel block devices. The owner supplied these live observations
from 169.254.177.67 on 2026-10-19. This change did not contact the deck.

- `/etc/udev/rules.d/12-usb-memory-auto-mount.rules` assigns slots from the
  physical port: `1-1*` and `2-1.2*` select usb1, `2-1.1*` selects usb2.
- Its add chain mounts each filesystem, runs `write_serialnum.sh`, runs
  `decrypt_autoexec.sh`, then writes `mount /media/<slot>/<kernel>` without
  a newline to `/proc/udev_<slot>`.
- The running UI holds open fds to `/proc/udev_usb1` and `/proc/udev_usb2`.
- `/media/usb1/sda1`, `/media/usb1/sda2`, `/media/usb2/sdb1`, and
  `/media/usb2/sdb2` already appear in `/proc/mounts`. The last uses fuseblk;
  the others use vfat. The reported udev daemon PID is 1417.

The loader's `launch_runtime_rbp` starts a new UI process. The live insertion
path kills the old process before calling it. The early-boot path also kills
and relaunches the process after 25 seconds when a display hook is staged.
Already-delivered proc notifications cannot populate the new process's state.
These restart paths explain the report and match the supplied live mount/fd
evidence. Exact kernel notification queue behavior has not been measured here.

The stock rules and decrypt script are not present in the supplied extracted
firmware tree, which contains only the UI binary. The udev ordering above is
owner-supplied live evidence, not a claimed local reading of those files.
The binary has `UsbMountManager::run` at 0x297ed8 and
`UsbStorageManager::notify_usb_mount` at 0x29d290. Those symbols alone do not
prove notification delivery or justify calling their private interfaces.

Because decrypt runs before the final notice, the loader can also hold up the
boot partition's notice. Whether udev serializes the second stick's RUN chain
is not established by the available source. Recovery therefore watches for
later mounts instead of assuming that all partitions are ready at launch.

## Recovery behavior

The worker is embedded in the canonical `vendor/tools/xz_runtime/orchestrator.sh`
so it travels inside `autoexec.sh` without an extra payload file. It starts
after each loader-owned launch, including trial rollback, and in the delayed
startup and already-applied paths.

The worker runs separately with stdin detached, output in
`/dev/shm/xz-usb-coldplug.log`, and working directory `/`. It does not wait in
the udev RUN chain. It checks the target PID's start time on every pass, then
looks for the slot's open proc fd before sending that slot a notice.
It takes at most 45 passes with one second between passes, including passes
after successful notices so later mounts can appear. This is an attempt bound,
not a hard deadline for kernel proc operations.

Each pass sorts and deduplicates `/proc/mounts`. Only `/dev/sd*` sources at the
matching `/media/usb1/<device>` or `/media/usb2/<device>` target qualify.
The slot comes from the existing mount path, never the disk letter. This
includes the boot stick, partitioned sticks, whole-disk filesystems, and any
filesystem type mounted through the stock path. A successful notice is
remembered in RAM per PID, process start time, slot, and partition. Concurrent
workers for the same process share a RAM directory lock. Failed writes retry.
Process exit or PID reuse stops the worker. Repeat scans or loader invocations
do not resend successful notices to that process.

The worker never invokes mount, unmount, udev, or a sysfs trigger. It writes only
RAM state/logs and the stock proc notification nodes. It leaves physical slot
assignment and filesystem ownership with the stock system. Existing loader
logging and OTA behavior are outside this change.

Example output:

```text
USB_COLDPLUG: dev=sda1 action=mount slot=usb1 result=notified mounted_after_ms=1000 attempt=2
```

`mounted_after_ms` is elapsed time when an existing mount was observed and its
notice was written. It is not mount duration or proof the UI consumed the
notice. Each pass logs fd readiness; the final line records completion.

## Verification limits

Run `python tools/test_usb_coldplug.py` for disposable host fixtures exercising
the actual shell worker. They cover two partitioned sticks, repeated mounts
and invocations, whole disks, delayed listener readiness, later mounts, write
failure and retry, missing proc nodes, invalid paths, process exit, PID reuse,
startup integration, and shell syntax. No native imports are added, so the
GLIBC ceiling is unchanged. No image, installer, USB, or controller deployment
was performed.

Hardware must establish proc-write acceptance, UI recognition, per-partition
handling, browse/load/eject behavior, benign handling of a concurrent natural
udev notice, and whether 45 passes cover the slowest boot. A successful write
alone proves none of those UI results. This worker cannot make an unmounted
filesystem appear and stops watching after its startup window. The separate
development deployment script does not use this loader and is unchanged.

## Manual probe

With the decks stopped, run the committed shell snippet through the diagnostic
client from PowerShell. Do not run it repeatedly during playback.

```powershell
$probe = Get-Content -Raw C:/Users/short/Github/XZ-Mods/tools/usb_notification_probe.sh
python E:/Claude/xz-flagship-20261018/dsh.py 169.254.177.67 $probe 30
```

The snippet prints mounts, waits up to ten passes for both stock notification
fds, sends one notice per mounted partition in sorted order, waits two seconds,
and prints mounts again. It runs without the new loader installed. It does not
trigger udev or create files. Mounts should remain unchanged; check that both
source slots become visible, then verify browse, load, and eject on the display.
Save the output and any UI observations. The probe has not been run here.
