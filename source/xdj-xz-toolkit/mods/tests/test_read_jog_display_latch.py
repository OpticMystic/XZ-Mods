"""Offline decoder and real CLI with injected ELF/transport. No live calls."""
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
spec = importlib.util.spec_from_file_location('latch_reader', ROOT/'read_jog_display_latch.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def blob():
    value = bytearray(reader.SIZE)
    struct.pack_into('<6I', value, 0, *reader.HEADER)
    return value


def event(value, slot=6, side=1, **changes):
    fields = dict.fromkeys(reader.FIELDS, 0)
    fields.update(seq=2, valid=1, context=3, session=8, control=14, route_epoch=3,
                  at_ms_lo=9000, source_present=1, source_session=8, source_route_epoch=3,
                  source_timeline_route=3, source_load_revision=7, publication_seq=1,
                  snapshot_reason=6, select_result=0, step_result=0, phase_before=2, phase=2, desired=4,
                  encoded_frame=4, encoded_packet=4, packet_command=0x21,
                  candidate_changed=1, final_copied=1, read_result=1)
    fields.update(changes)
    struct.pack_into('<33I', value, 4*(6+side*520+25+slot*33), *(fields[n] for n in reader.FIELDS))


def record(report, slot=6, side=1):
    return report['sides'][side]['slots'][slot]


@contextmanager
def fake_cli(directory, failure=None):
    binary = directory/'own-code-mock-elf'
    binary.write_bytes(b'Own-code ELF metadata fixture; no instructions parsed')
    original = binary.read_bytes()
    output = directory/'result.json'
    value = blob();event(value)
    offset = 8191 if failure == 'three_pages' else 4096
    prefix = offset % 4096
    count = (prefix + reader.SIZE + 4095) // 4096
    pages = bytes(prefix)+bytes(value)+bytes(count*4096-prefix-len(value))
    rows = [['10000000-10020000', 'rw-p', '00000000', '00:01', '42', reader.MODULE],
            ['10030000-10031000', 'rw-p', '00030000', '00:01', '42', reader.MODULE]]
    identity = dict(runtime_sha256=hashlib.sha256(original).hexdigest(), pid=777,
                    process_start_ticks=9000, controller_mac='c8:3d:fc:0c:a8:3a',
                    mapping_identity=[rows, rows[:1]])
    if failure == 'expected_hash':identity['runtime_sha256']='0'*64
    if failure == 'expected_pid':identity['pid']=778
    if failure == 'expected_start':identity['process_start_ticks']=9001
    if failure == 'expected_mac':identity['controller_mac']='c8:3d:fc:0c:a8:3b'
    if failure == 'expected_mapping':identity['mapping_identity'][0][0][4]='999'
    expected = directory/'expected.json';expected.write_text(json.dumps(identity))
    state = {'checks':0, 'closed':False, 'commands':[]}

    class Table:
        def get_symbol_by_name(self, name):
            assert name == reader.SYMBOL
            symbol = dict(st_size=reader.SIZE, st_value=offset, st_info=dict(type='STT_OBJECT',bind='STB_GLOBAL'),
                          st_other=dict(visibility='STV_DEFAULT'),st_shndx=1)
            if failure == 'symbol_size':symbol['st_size']+=4
            if failure == 'symbol_type':symbol['st_info']['type']='STT_FUNC'
            if failure == 'symbol_bounds':symbol['st_value']=0xffffffff
            if failure == 'symbol_local':symbol['st_info']['bind']='STB_LOCAL'
            if failure == 'symbol_hidden':symbol['st_other']['visibility']='STV_HIDDEN'
            if failure == 'symbol_undefined':symbol['st_shndx']='SHN_UNDEF'
            return [symbol, symbol] if failure == 'symbol_duplicate' else [symbol]

    class ELF:
        elfclass, little_endian = 32, True
        def __init__(self, handle):
            assert handle.read() == original
            if failure == 'local_replaced':binary.write_bytes(b'Changed after captured parser input')
        def __getitem__(self, key):
            if key == 'e_type':return 'ET_EXEC' if failure == 'executable' else 'ET_DYN'
            assert key == 'e_machine'
            return 'EM_X86_64' if failure == 'architecture' else 'EM_ARM'
        def get_section_by_name(self, name):
            assert name == '.dynsym';return Table()
        def iter_segments(self):
            if failure == 'no_load':return
            yield dict(p_type='PT_LOAD',p_vaddr=4 if failure=='load_origin' else 0,
                       p_memsz=0x20000 if failure != 'segment' else 8,p_flags=0 if failure=='load_unreadable' else 6)

    class Diagnostic:
        def __init__(self, host, source):
            assert (host,source)==('offline.invalid','offline')
        def command(self, command):
            assert command.endswith('; echo');state['commands'].append(command);command=command[:-6]
            if command=='cat /sys/class/net/eth0/address':
                state['checks']+=1
                return 'bad' if failure=='mac' or failure=='mac_after' and state['checks']>1 else 'c8:3d:fc:0c:a8:3a'
            changed=state['checks']>1
            if command=='pidof rbp':return '778' if failure=='pid' or failure=='pid_after' and changed else '777'
            if command=='sha256sum '+reader.MODULE:
                digest='0'*64 if failure=='hash' or failure=='hash_after' and changed else hashlib.sha256(original).hexdigest()
                return digest+'  '+reader.MODULE
            if command in ('cat /proc/777/stat','cat /proc/778/stat'):
                start='9001' if failure=='start' and changed else '9000'
                return '777 (rbp (worker)) S '+' '.join(['0']*18+[start])
            if command=='cat /proc/777/maps':
                actual=[row[:] for row in rows]
                actual[0][4]='42';actual[1][4]='42'
                if changed:
                    if failure=='inode':actual[0][4]='43'
                    if failure=='other_segment':actual[1][4]='43'
                    if failure=='device':actual[0][3]='00:02'
                    if failure=='extent':actual[0][0]='10000000-10021000'
                    if failure=='permissions':actual[0][1]='r--p'
                if failure=='unreadable':actual[0][1]='-w-p'
                if failure=='gap':actual[0][0]='10000000-10001004'
                if failure=='oversize_maps':return ' '* (2*1024*1024+1)
                return '\n'.join(' '.join(row) for row in actual)
            if command.startswith('dd if=/proc/777/mem '):
                assert command==f'dd if=/proc/777/mem bs=4096 skip=65537 count={count} 2>/dev/null | base64'
                if failure=='short':return base64.b64encode(pages[:-1]).decode()
                if failure=='oversize_read':return 'A'*24577
                if failure=='invalid_base64':return '!'+base64.b64encode(pages).decode()[1:]
                if failure=='version':
                    bad=bytearray(pages);struct.pack_into('<I',bad,0,9);return base64.b64encode(bad).decode()
                return base64.b64encode(pages).decode()
            raise AssertionError('Unexpected offline command: '+command)
        def close(self):state['closed']=True

    class Loader:
        def exec_module(self, module):module.Diagnostic=Diagnostic

    parser=types.ModuleType('elftools.elf.elffile');parser.ELFFile=ELF
    transport=types.ModuleType('offline_transport')
    argv=['reader','--binary',str(binary),'--identity',str(expected),'--host','offline.invalid',
          '--source','offline','--monitor',str(directory/'not-imported.py'),'--output',str(output)]
    with patch.dict(sys.modules,{'elftools.elf.elffile':parser}), \
         patch.object(reader.importlib.util,'spec_from_file_location',return_value=types.SimpleNamespace(loader=Loader())), \
         patch.object(reader.importlib.util,'module_from_spec',return_value=transport), \
         patch.object(sys,'argv',argv),redirect_stdout(io.StringIO()):
        yield output,state


class Decode(unittest.TestCase):
    def test_historical_only_and_independent_counters(self):
        a=blob();event(a);b=bytearray(a)
        struct.pack_into('<I',b,4*(6+520),99)
        result=reader.decode(a,b)
        self.assertFalse(result['atomic_snapshot']);self.assertFalse(result['latest_frame'])
        self.assertEqual(result['sides'][1]['counters']['callbacks'],99)
        self.assertEqual(record(result)['record']['evidence'],'HISTORICAL')
        self.assertEqual(record(result)['record']['at_monotonic_ms'],9000)

    def test_changed_or_odd_payload_withheld(self):
        a=blob();event(a)
        for changes in [dict(seq=4),dict(seq=3),dict(session=9),dict(at_ms_lo=9001),dict(encoded_frame=2)]:
            b=blob();event(b,**changes)
            self.assertNotIn('record',record(reader.decode(a,b)))
        b=blob();event(b,seq=3)
        self.assertNotIn('record',record(reader.decode(b,b)))

    def test_invalid_records_fail_closed(self):
        for changes in [dict(valid=2),dict(context=0),dict(source_session=7),dict(source_route_epoch=4),
                        dict(source_timeline_route=4),dict(publication_seq=0),dict(candidate_changed=2),
                        dict(snapshot_reason=99),dict(select_result=99),dict(step_result=99),dict(phase=99),
                        dict(read_result=3),dict(desired=3),dict(final_copied=2),dict(phase_before=99)]:
            b=blob();event(b,**changes)
            self.assertNotIn('record',record(reader.decode(b,b)))

    def test_slot_bins_and_absent_source(self):
        b=blob();event(b,slot=7,source_present=0,candidate_changed=0)
        row=record(reader.decode(b,b),7)['record']
        self.assertEqual(row['final_copied'],1);self.assertEqual(row['candidate_changed'],0)
        self.assertNotIn('source_session',row)
        for slot in (1,10,13):
            b=blob();event(b,slot=slot)
            self.assertNotIn('record',record(reader.decode(b,b),slot))

    def test_normal_info_bin_and_old_context_are_explicitly_historical(self):
        b=blob();event(b,slot=14)
        struct.pack_into('<4I',b,4*(6+520),100,0,99,500)
        row=record(reader.decode(b,b),14)['record']
        self.assertEqual(row['session'],8);self.assertEqual(row['context'],3)
        self.assertEqual(row['evidence'],'HISTORICAL');self.assertFalse(row['current_context_claim'])
        self.assertFalse(row['latest_frame']);self.assertEqual(row['copy_outcome'],'ACTUAL_MEMCPY')
        for changes in [dict(phase_before=1),dict(phase=1),dict(packet_command=0x2d),
                        dict(snapshot_reason=2),dict(source_present=0),dict(step_result=4),
                        dict(encoded_packet=2),dict(encoded_frame=2),dict(final_copied=0)]:
            b=blob();event(b,slot=14,**changes)
            self.assertNotIn('record',record(reader.decode(b,b),14))

    def test_old_abi_is_refused(self):
        with self.assertRaises(ValueError):reader.decode(bytes(4064),bytes(4064))
        b=blob();struct.pack_into('<6I',b,0,1,4064,2,15,128,25)
        with self.assertRaises(ValueError):reader.decode(b,b)

    def test_fixed_metadata_and_bounded_sizes(self):
        for index in range(6):
            b=blob();struct.pack_into('<I',b,index*4,9999)
            with self.assertRaises(ValueError):reader.decode(b,b)
        for n in (0,reader.SIZE-1,reader.SIZE+1):
            with self.assertRaises(ValueError):reader.decode(bytes(n),bytes(n))

    def test_mapping_gap_and_overflow_rejected(self):
        rows=[['10000000-10001000','rw-p','00000000','00:01','42',reader.MODULE],
              ['10002000-10003000','rw-p','00002000','00:01','42',reader.MODULE]]
        with self.assertRaises(ValueError):reader.mapping_identity(rows,0x10000ff0,reader.SIZE)
        with self.assertRaises(ValueError):reader.mapping_identity(rows,0xffffffff,reader.SIZE)


class Identity(unittest.TestCase):
    def test_non_runtime_metadata_is_rejected_before_transport(self):
        for failure in ['symbol_local','symbol_hidden','symbol_undefined','executable',
                        'no_load','load_origin','load_unreadable']:
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as d,fake_cli(Path(d),failure) as (output,state):
                with self.assertRaises(ValueError):reader.main()
                self.assertFalse(output.exists())
                self.assertEqual(state['commands'],[])

    def test_exact_supplied_identity_accepts(self):
        with tempfile.TemporaryDirectory() as d,fake_cli(Path(d)) as (output,state):
            reader.main();result=json.loads(output.read_text())
            self.assertEqual(result['process_start_ticks'],9000);self.assertTrue(state['closed'])
            self.assertFalse(result['controller_modified']);self.assertEqual(result['evidence'],'HISTORICAL')

    def test_unaligned_object_has_explicit_three_page_bound(self):
        self.assertEqual(reader.MAX_PAGES,3)
        with tempfile.TemporaryDirectory() as d,fake_cli(Path(d),'three_pages') as (output,state):
            reader.main();self.assertTrue(output.exists());self.assertTrue(state['closed'])
            self.assertTrue(any('count=3' in command for command in state['commands']))

    def test_identity_bounds_and_version_fail_closed(self):
        for failure in ['expected_hash','expected_pid','expected_start','expected_mac','expected_mapping',
                        'mac','mac_after','pid','pid_after','hash','hash_after','start','inode','device','extent','permissions',
                        'other_segment','unreadable','gap','short','oversize_maps','oversize_read','invalid_base64',
                        'symbol_size','symbol_type','symbol_bounds','symbol_duplicate','architecture','segment','version']:
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as d,fake_cli(Path(d),failure) as (output,state):
                with self.assertRaises(ValueError):reader.main()
                self.assertFalse(output.exists())
                if state['commands']:self.assertTrue(state['closed'])

    def test_local_parser_and_hash_share_immutable_input(self):
        with tempfile.TemporaryDirectory() as d,fake_cli(Path(d),'local_replaced') as (output,state):
            reader.main();self.assertTrue(state['closed'])
            self.assertEqual(json.loads(output.read_text())['runtime_sha256'],
                             hashlib.sha256(b'Own-code ELF metadata fixture; no instructions parsed').hexdigest())


if __name__=='__main__':unittest.main()
