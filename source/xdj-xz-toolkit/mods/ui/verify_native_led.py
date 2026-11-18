"""Run LED record, feedback and complete callback regressions without hardware."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
common = [str(Path(args.zig).resolve()), 'cc', '-UNDEBUG', '-O1',
          '-std=c11', '-Wall', '-Wextra', '-Werror']
with tempfile.TemporaryDirectory(prefix='xz-led-packet-') as folder:
    for name, flags, sources in (
        ('records', ['-DXZ_LED_PORTABLE_TEST'], [root / 'native_led.c', root / 'test_native_led.c']),
        ('feedback', [], [root.parent / 'tests/test_sync_led_feedback.c']),
        # Run the actual callback with native functions stubbed. Keep the 32-bit pointer ABI.
        ('delivery', ['-target', 'x86-windows-gnu' if os.name == 'nt' else 'x86-linux-gnu'],
         [root.parent / 'tests/test_sync_led_delivery.c', root.parent / 'deck_select.c']),
    ):
        binary = Path(folder) / (name + '.exe')
        subprocess.run([*common, *flags, *map(str, sources), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
