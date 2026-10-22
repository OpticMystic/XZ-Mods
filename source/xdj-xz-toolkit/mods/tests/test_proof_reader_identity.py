"""Exercise both real proof-reader CLIs with fake ELF/transport boundaries only."""
import base64
from contextlib import contextmanager, redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


READERS = [load('read_deck_input_trace'), load('read_jog_display_proof')]


@contextmanager
def fake_cli(reader, directory, failure=None):
    binary = directory / 'injected-elf-input.bin'
    binary.write_bytes(b'Only the injected ELF parser reads this test input')
    original = binary.read_bytes()
    output = directory / 'result.json'
    blob = bytearray(reader.SIZE)
    if reader.__name__ == 'read_deck_input_trace':
        struct.pack_into('<8I', blob, 0, 1, reader.SIZE, 1, reader.CAPACITY, 0, 0, 0, 0)
        symbol = 'xz_mods_deck_input_trace_v1'
    else:
        struct.pack_into('<I', blob, 0, 1)
        symbol = 'xz_mods_jog_proof_v1'
    pages = bytes(blob) + bytes((-len(blob)) % 4096)
    state = {'checks': 0, 'closed': False, 'commands': []}

    class Table:
        def get_symbol_by_name(self, name):
            assert name == symbol
            return [{'st_size': reader.SIZE, 'st_value': 4096, 'st_info': {'type': 'STT_OBJECT'}}]

    class ELF:
        elfclass, little_endian = 32, True
        def __init__(self, handle):
            assert handle.read() == original
            if failure == 'local_replaced':
                binary.write_bytes(b'Replaced after the immutable parser input was captured')
        def __getitem__(self, key):
            assert key == 'e_machine'
            return 'EM_ARM'
        def get_section_by_name(self, name):
            assert name == '.dynsym'
            return Table()
        def iter_segments(self):
            yield {'p_type': 'PT_LOAD', 'p_offset': 0, 'p_vaddr': 0, 'p_memsz': 0x20000}

    class Diagnostic:
        def __init__(self, host, source):
            assert (host, source) == ('offline.invalid', 'offline')
        def command(self, command):
            assert command.endswith('; echo')
            state['commands'].append(command)
            command = command[:-6]
            if command == 'cat /sys/class/net/eth0/address':
                state['checks'] += 1
                return 'bad' if failure == 'mac' else 'c8:3d:fc:0c:a8:3a'
            if command == 'pidof rbp':
                return '778' if failure == 'pid' else '777'
            if command == 'md5sum /dev/shm/libxz-mods.so':
                digest = '0' * 32 if failure == 'hash' else hashlib.md5(original).hexdigest()
                return digest + '  /dev/shm/libxz-mods.so'
            changed = state['checks'] > 1
            if command == 'cat /proc/777/stat':
                start = '9001' if failure == 'start' and changed else '9000'
                return '777 (rbp worker) S ' + ' '.join(['0'] * 18 + [start])
            if command == 'cat /proc/777/maps':
                inode = 43 if failure == 'inode' and changed else 42
                dev = '00:02' if failure == 'device' and changed else '00:01'
                end = '10021000' if failure == 'extent' and changed else '10020000'
                perms = 'r--p' if failure == 'permissions' and changed else 'rw-p'
                second_inode = 44 if failure == 'other_segment' and changed else 42
                return f'10000000-{end} {perms} 00000000 {dev} {inode} /dev/shm/libxz-mods.so\n' + \
                       f'10030000-10031000 rw-p 00030000 00:01 {second_inode} /dev/shm/libxz-mods.so'
            if command.startswith('dd if=/proc/777/mem '):
                assert command == f'dd if=/proc/777/mem bs=4096 skip=65537 count={len(pages)//4096} 2>/dev/null | base64'
                return base64.b64encode(pages[:-1] if failure == 'short' else pages).decode('ascii')
            raise AssertionError('Unexpected diagnostic command: ' + command)
        def close(self):
            state['closed'] = True

    class Loader:
        def exec_module(self, module):module.Diagnostic = Diagnostic

    parser = types.ModuleType('elftools.elf.elffile');parser.ELFFile = ELF
    transport = types.ModuleType('fake_transport')
    argv = ['reader', '--binary', str(binary), '--host', 'offline.invalid', '--source', 'offline',
            '--pid', '777', '--monitor', str(directory/'not-imported.py'), '--output', str(output)]
    with patch.dict(sys.modules, {'elftools.elf.elffile': parser}), \
         patch.object(reader.importlib.util, 'spec_from_file_location', return_value=types.SimpleNamespace(loader=Loader())), \
         patch.object(reader.importlib.util, 'module_from_spec', return_value=transport), \
         patch.object(sys, 'argv', argv), redirect_stdout(io.StringIO()):
        yield output, state


class Identity(unittest.TestCase):
    def test_same_runtime_generation_accepts_and_closes(self):
        for reader in READERS:
            with self.subTest(reader=reader.__name__), tempfile.TemporaryDirectory() as folder, fake_cli(reader, Path(folder)) as (output, state):
                reader.main()
                report = json.loads(output.read_text())
                self.assertEqual(report['process_start_ticks'], 9000)
                self.assertTrue(state['closed'])
                self.assertFalse(report['controller_modified'])

    def test_reused_address_or_process_identity_is_refused(self):
        for reader in READERS:
            for failure in ['inode', 'device', 'extent', 'permissions', 'other_segment', 'start', 'mac', 'pid', 'hash', 'short']:
                with self.subTest(reader=reader.__name__, failure=failure), tempfile.TemporaryDirectory() as folder, fake_cli(reader, Path(folder), failure) as (output, state):
                    with self.assertRaises(ValueError):reader.main()
                    self.assertFalse(output.exists())
                    self.assertTrue(state['closed'])

    def test_local_parser_and_hash_use_the_same_captured_bytes(self):
        for reader in READERS:
            with self.subTest(reader=reader.__name__), tempfile.TemporaryDirectory() as folder, fake_cli(reader, Path(folder), 'local_replaced') as (output, state):
                reader.main()
                report = json.loads(output.read_text())
                self.assertEqual(report['runtime_sha256'], hashlib.sha256(b'Only the injected ELF parser reads this test input').hexdigest())
                self.assertTrue(state['closed'])

    def test_process_name_parentheses_and_invalid_stats(self):
        for reader in READERS:
            text = '777 (name (with) parens)) S ' + ' '.join(['0'] * 18 + ['9000'])
            self.assertEqual(reader.process_start(text, 777), 9000)
            for bad in ['', '778 (rbp) S', '777 (rbp) S']:
                with self.assertRaises(ValueError):reader.process_start(bad, 777)


if __name__ == '__main__':unittest.main()
