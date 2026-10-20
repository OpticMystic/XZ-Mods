"""Interface/theme settings: the native parser and the desktop editor's schema migrate old records the same way."""
from pathlib import Path
import argparse, json, subprocess, sys, tempfile
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent))
from builder import usb_settings


def python_side():
    schema = json.loads((ROOT / 'settings-schema.json').read_text())
    fields = {f['key']: f for f in schema['fields']}
    assert {'interface', 'classic_theme', 'modern_theme'} <= set(fields) and 'theme' not in fields
    assert [c['label'] for c in fields['interface']['choices']] == ['MODERN', 'CLASSIC']
    assert fields['classic_theme']['max'] == 23 and 'FLAGSHIP' not in [c['label'] for c in fields['classic_theme']['choices']]
    assert [c['label'] for c in fields['modern_theme']['choices']] == ['STANDARD']
    values = usb_settings.defaults(schema)
    assert values['interface'] == 1 and values['classic_theme'] == 0 and values['modern_theme'] == 0
    wire = usb_settings.serialize(values, schema)
    assert b'\ntheme=24\n' in wire and b'\ninterface=1\nclassic_theme=0\nmodern_theme=0\n' in wire and wire.endswith(b'modern_theme=0\nrekordbox_press=2\nrekordbox_shift=1\n')
    classic = {**values, 'interface': 0, 'classic_theme': 11}
    wire = usb_settings.serialize(classic, schema)
    assert b'\ntheme=11\n' in wire and usb_settings.parse(wire, schema) == classic
    lines = wire.splitlines(keepends=True)
    older = [n for n in schema['accepted_field_counts'] if n < 41]
    assert older and 41 in schema['accepted_field_counts']
    for count in older:
        for theme in range(25):
            old = b''.join(lines[:count + 1]).replace(b'\ntheme=11\n', b'\ntheme=%d\n' % theme)
            got = usb_settings.parse(old, schema)
            assert got['interface'] == (1 if theme == 24 else 0), (count, theme)
            assert got['classic_theme'] == (0 if theme == 24 else theme), (count, theme)
            assert usb_settings.parse(usb_settings.serialize(got, schema), schema) == got
    # Half a block is not a record.
    for cut in (39, 40):
        try:
            usb_settings.parse(b''.join(lines[:cut + 1]), schema)
        except ValueError:
            pass
        else:
            raise AssertionError('partial appearance block accepted')
    # An older mod version's schema still gets the combined theme it understands.
    assert usb_settings.legacy_values(classic, schema)['theme'] == 11
    assert usb_settings.legacy_values({**classic, 'interface': 1}, schema)['theme'] == 24


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True, type=Path)
    args = parser.parse_args()
    python_side()
    source = (ROOT / 'settings.c').read_text().split('static int paths(')[0]
    for header in ('<sys/stat.h>', '<unistd.h>', '<fcntl.h>', '<mntent.h>'):
        source = source.replace('#include ' + header, '')
    with tempfile.TemporaryDirectory(prefix='xz-interface-') as folder:
        out = Path(folder)
        native = out / 'settings-parser.c'
        native.write_text(source)
        exe = out / 'interface.exe'
        subprocess.run([str(args.zig.resolve()), 'cc', '-std=c11', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT), str(native), str(ROOT / 'ui/beat_jump.c'), str(ROOT / 'tests/test_interface_settings.c'),
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('PASS desktop schema: interface defaults to MODERN, FLAGSHIP is not a classic theme, every old record length x 25 themes migrates')


if __name__ == '__main__':
    main()
