"""Exercise the loader's actual shell worker against disposable proc fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
LOADER = ROOT / 'source/xdj-xz-toolkit/vendor/tools/xz_runtime/orchestrator.sh'
BASH = shutil.which('bash') if os.name != 'nt' else r'C:\Program Files\Git\bin\bash.exe'


class ColdplugTest(unittest.TestCase):
    def run_worker(self, mounts='', fds=('usb1', 'usb2'), on_sleep='', repeats=1,
                   missing_slot=None, blocked_slot=None):
        with tempfile.TemporaryDirectory(prefix='usb-coldplug-') as temp:
            root = Path(temp)
            proc, ram = root / 'proc', root / 'ram'
            (proc / '42/fd').mkdir(parents=True)
            ram.mkdir()
            (proc / '42/stat').write_text('42 (stock ui) S ' + '0 ' * 18 + '123 0\n')
            (proc / 'mounts').write_text(mounts)
            (proc / 'uptime').write_text('100.00 0\n')
            for number, slot in enumerate(fds):
                (proc / f'42/fd/{number}').write_text('/proc/udev_' + slot)
            for slot in ('usb1', 'usb2'):
                if slot != missing_slot:
                    node = proc / ('udev_' + slot)
                    node.mkdir() if slot == blocked_slot else node.touch()
            source = LOADER.read_text().split('# USB_COLDPLUG_BEGIN\n', 1)[1].split(
                '# USB_COLDPLUG_END', 1)[0]
            script = root / 'run.sh'
            script.write_text(source + '''
ls() {
    for fd in "$2/"*; do
        [ -f "$fd" ] && printf 'fd -> %s\n' "$(cat "$fd")"
    done
}
ticks=0
sleep() {
    ticks=$((ticks + 1))
    echo "$((100 + ticks)).00 0" > "$fixture/proc/uptime"
''' + on_sleep + '''
}
''' + '\n'.join('usb_coldplug_watch 42 "$fixture/proc" "$fixture/ram" 4 0'
                 for _ in range(repeats)), newline='\n')
            env = {**os.environ, 'fixture': root.as_posix()}
            result = subprocess.run([BASH, script.as_posix()], env=env, text=True,
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            notices = {slot: (proc / ('udev_' + slot)).read_text()
                       for slot in ('usb1', 'usb2') if (proc / ('udev_' + slot)).exists()}
            return result.stdout, notices

    def test_existing_boot_and_two_partitioned_sticks(self):
        mounts = ''.join(f'/dev/{dev} /media/{slot}/{dev} {fs} rw 0 0\n'
                         for slot, dev, fs in [('usb1', 'sda1', 'vfat'),
                                              ('usb1', 'sda2', 'vfat'),
                                              ('usb2', 'sdb1', 'vfat'),
                                              ('usb2', 'sdb2', 'fuseblk')])
        log, notices = self.run_worker(mounts + mounts, repeats=2)
        self.assertEqual(log.count('result=notified'), 4)
        self.assertEqual(notices['usb1'], 'mount /media/usb1/sda2')
        self.assertEqual(notices['usb2'], 'mount /media/usb2/sdb2')
        self.assertNotIn('action=add', log)

    def test_waits_for_listener_and_later_mounts(self):
        log, notices = self.run_worker('/dev/sda1 /media/usb1/sda1 vfat rw 0 0\n',
                                      fds=(), on_sleep='''
    if [ "$ticks" = 1 ]; then
        echo /proc/udev_usb1 > "$fixture/proc/42/fd/21"
    fi
    if [ "$ticks" = 2 ]; then
        echo /proc/udev_usb2 > "$fixture/proc/42/fd/22"
        echo '/dev/sdb1 /media/usb2/sdb1 vfat rw 0 0' >> "$fixture/proc/mounts"
    fi
''')
        self.assertIn('ready=[]', log)
        self.assertIn('dev=sda1 action=mount slot=usb1 result=notified mounted_after_ms=1000', log)
        self.assertIn('dev=sdb1 action=mount slot=usb2 result=notified mounted_after_ms=2000', log)
        self.assertEqual(notices['usb2'], 'mount /media/usb2/sdb1')

    def test_never_ready_is_bounded(self):
        log, notices = self.run_worker('/dev/sda1 /media/usb1/sda1 vfat rw 0 0\n', fds=())
        self.assertIn('watch-complete attempts=4', log)
        self.assertNotIn('result=notified', log)
        self.assertEqual(notices['usb1'], '')

    def test_process_exit_stops_worker(self):
        log, _ = self.run_worker(on_sleep='rm -f "$fixture/proc/42/stat"')
        self.assertIn('attempt=2 result=process-exited', log)
        self.assertNotIn('watch-complete', log)

    def test_pid_reuse_stops_worker(self):
        log, _ = self.run_worker(on_sleep='sed -i s/123/124/ "$fixture/proc/42/stat"')
        self.assertIn('attempt=2 result=process-exited', log)

    def test_missing_proc_node_is_not_created(self):
        log, notices = self.run_worker('/dev/sda1 /media/usb1/sda1 vfat rw 0 0\n',
                                      missing_slot='usb1')
        self.assertNotIn('usb1', notices)
        self.assertNotIn('result=notified', log)

    def test_failed_notification_retries(self):
        log, notices = self.run_worker('/dev/sda1 /media/usb1/sda1 vfat rw 0 0\n',
                                      blocked_slot='usb1', on_sleep='''
    if [ "$ticks" = 1 ]; then
        rmdir "$fixture/proc/udev_usb1"
        touch "$fixture/proc/udev_usb1"
    fi
''')
        self.assertEqual(log.count('result=write-failed'), 1)
        self.assertEqual(log.count('result=notified'), 1)
        self.assertEqual(notices['usb1'], 'mount /media/usb1/sda1')

    def test_whole_disk_and_invalid_mounts(self):
        log, notices = self.run_worker('''/dev/sda /media/usb1/sda vfat rw 0 0
/dev/mmcblk0p1 /media/usb2/mmcblk0p1 vfat rw 0 0
/dev/sdb1 /media/usb3/sdb1 vfat rw 0 0
/dev/sdb1 /media/usb2/other vfat rw 0 0
/dev/sdb1 /media/usb2/sdb1/../../root vfat rw 0 0
''')
        self.assertEqual(log.count('result=notified'), 1)
        self.assertEqual(notices['usb1'], 'mount /media/usb1/sda')
        self.assertEqual(notices['usb2'], '')

    def test_loader_arms_launch_and_existing_startup_paths(self):
        source = LOADER.read_text()
        launch = source.split('launch_runtime_rbp() {', 1)[1].split('stage_optional_assets()', 1)[0]
        self.assertIn('usb_coldplug_start "$RBP_LAUNCH_PID"', launch)
        self.assertIn('usb_coldplug_start "$CURRENT_PID"', source)
        self.assertIn('usb_coldplug_start "$DELAYED_PID"', source)
        subprocess.run([BASH, '-n', LOADER.as_posix()], check=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
