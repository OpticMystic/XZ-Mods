import random
import struct
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from builder import service, three_band
from builder.jobs import Job

TRACK=Path('PIONEER','USBANLZ','P001','0000A001','ANLZ0000.EXT')
CUES=b'cue points and memory hot cues'

def section(tag,size,entries):
    head=20 if tag==b'PWV6' else 24
    fields=struct.pack('>II',size,len(entries)//size)+(b'' if head==20 else b'\0\0\0\x01')
    return tag+struct.pack('>II',head,head+len(entries))+fields+entries

def other(tag,payload):
    return tag+struct.pack('>II',12,12+len(payload))+payload

def pmai(*sections):
    body=b''.join(sections)
    return b'PMAI'+struct.pack('>II',28,28+len(body))+bytes(16)+body

def ext(preview,detail,cues=CUES):
    return pmai(other(b'PPTH',b'\0\0\0\x08Contents/a.wav'),section(b'PWV4',6,preview),
        section(b'PWV5',2,detail),other(b'PCO2',cues))

def two_ex(preview,detail=None):
    return pmai(section(b'PWV6',3,preview),*([section(b'PWV7',3,detail)] if detail is not None else []))

def entry(**energy):
    return bytes(energy.get(band,0) for band in three_band.BAND_ORDER)

def noise(seed,length):
    rng=random.Random(seed);return bytes(rng.randrange(256) for _ in range(length))

def detail_colours(data):
    at,count=three_band.waves(data)[b'PWV5']
    return [(value>>13&7,value>>10&7,value>>7&7) for value in struct.unpack_from(f'>{count}H',data,at)]

def preview_entries(data):
    at,count=three_band.waves(data)[b'PWV4']
    return [data[at+index*6:at+index*6+6] for index in range(count)]

def sections(data):
    found={};offset=28
    while offset<len(data):
        total=struct.unpack_from('>I',data,offset+8)[0];found[data[offset:offset+4]]=data[offset:offset+total];offset+=total
    return found

def rekordbox_ext(seed=1,preview_count=40,detail_count=150):
    return ext(noise(seed,preview_count*6),noise(seed+1,detail_count*2))

def analysis(seed=3,preview_count=40,detail_count=150):
    return two_ex(noise(seed,preview_count*3),noise(seed+1,detail_count*3))

def usb_track(root,folder='0000A001',data=None,analysed=True):
    path=root/'PIONEER'/'USBANLZ'/'P001'/folder/'ANLZ0000.EXT'
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(rekordbox_ext() if data is None else data)
    if analysed:path.with_suffix('.2EX').write_bytes(analysis())
    return path


class AdaptExtTests(unittest.TestCase):
    def test_empty_band_sections_fail_before_recolouring(self):
        for bands in (two_ex(b''), two_ex(entry(low=200), b'')):
            with self.subTest(bands=bands):
                with self.assertRaisesRegex(ValueError, 'empty'):
                    three_band.adapt_ext(rekordbox_ext(), bands)

    def test_band_order_is_the_measured_one(self):
        # Measured against the audio by builder/tests/verify_three_band_order.py; change both together.
        self.assertEqual(three_band.BAND_ORDER,('low','mid','high'))

    def test_parser_honours_each_section_header_length(self):
        bands=two_ex(noise(1,40*3),noise(2,150*3));found=three_band.waves(bands)
        self.assertEqual(found[b'PWV6'],(28+20,40))
        self.assertEqual(found[b'PWV7'],(28+20+40*3+24,150))

    def test_byte_order_maps_each_band_to_its_colour(self):
        level=200
        for band,expected in (('low',three_band.band_color(level,0,0)),('mid',three_band.band_color(0,level,0)),
                ('high',three_band.band_color(0,0,level))):
            with self.subTest(band=band):
                column=entry(**{band:level})
                adapted=three_band.adapt_ext(rekordbox_ext(),two_ex(column*40,column*150))
                self.assertEqual(set(detail_colours(adapted)),{expected})
        colours={three_band.band_color(level,0,0),three_band.band_color(0,level,0),three_band.band_color(0,0,level)}
        self.assertEqual(len(colours),3,'single-band colours must be distinguishable for this test to prove the order')
        low=three_band.band_color(level,0,0);self.assertGreater(low[2],low[0],'low is drawn blue')
        self.assertEqual(three_band.band_color(0,0,level),(7,7,7),'high is drawn white')

    def test_preview_colour_follows_pwv6_band(self):
        adapted=three_band.adapt_ext(rekordbox_ext(),two_ex(entry(low=200)*40,bytes(150*3)))
        low=three_band.LAYERS['low'][0]
        for column in preview_entries(adapted):
            peak=max(column[3:]);self.assertEqual(tuple(column[3:]),tuple(min(peak,round(value*peak/max(low))) for value in low))

    def test_only_colour_bits_change(self):
        original=rekordbox_ext();adapted=three_band.adapt_ext(original,analysis())
        self.assertEqual(len(adapted),len(original))
        self.assertNotEqual(adapted,original)
        at,count=three_band.waves(original)[b'PWV5']
        for index in range(count):self.assertEqual(adapted[at+index*2+1]&0x7f,original[at+index*2+1]&0x7f)
        for before,after in zip(preview_entries(original),preview_entries(adapted)):
            self.assertEqual(after[:3],before[:3]);self.assertEqual(max(after[3:]),max(before[3:]))
        for tag in (b'PPTH',b'PCO2'):self.assertEqual(sections(adapted)[tag],sections(original)[tag])
        self.assertEqual(original[:28],adapted[:28])

    def test_silent_preview_column_keeps_its_colour(self):
        original=rekordbox_ext()
        adapted=three_band.adapt_ext(original,two_ex(bytes(40*3),noise(9,150*3)))
        self.assertEqual(preview_entries(adapted),preview_entries(original))

    def test_idempotent(self):
        for seed in range(5):
            original=rekordbox_ext(seed*10);bands=analysis(seed*10+5)
            once=three_band.adapt_ext(original,bands)
            self.assertEqual(three_band.adapt_ext(once,bands),once)

    def test_pwv6_drives_detail_without_pwv7(self):
        level=180
        adapted=three_band.adapt_ext(ext(noise(1,2*6),noise(2,4*2)),two_ex(entry(low=level)+entry(mid=level)))
        low,mid=three_band.band_color(level,0,0),three_band.band_color(0,level,0)
        self.assertEqual(detail_colours(adapted),[low,low,mid,mid])

    def test_rejects_unexpected_entry_size(self):
        bad=pmai(section(b'PWV4',6,noise(1,60)),section(b'PWV5',3,noise(2,30)))
        with self.assertRaisesRegex(ValueError,'PWV5 entry size 3'):three_band.adapt_ext(bad,analysis())
        with self.assertRaisesRegex(ValueError,'PWV6 entry size 2'):three_band.adapt_ext(rekordbox_ext(),pmai(section(b'PWV6',2,noise(1,80))))

    def test_rejects_truncated_and_inconsistent_files(self):
        original=rekordbox_ext()
        with self.assertRaisesRegex(ValueError,'Analysis file length is inconsistent'):three_band.waves(original[:-1])
        shortened=original[:8]+struct.pack('>I',len(original)-5)+original[12:-5]
        with self.assertRaisesRegex(ValueError,'PCO2.* section length is inconsistent'):three_band.waves(shortened)
        with self.assertRaisesRegex(ValueError,'header is truncated'):three_band.waves(pmai(b'PWV4'))
        with self.assertRaisesRegex(ValueError,'Not a rekordbox analysis file'):three_band.waves(b'RIFF'+bytes(40))
        with self.assertRaises(ValueError):three_band.adapt_ext(original,pmai(section(b'PWV7',3,noise(1,30))))


class UsbTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup);self.root=Path(temporary.name)

    def backup(self,path):
        return self.root/three_band.ORIGINALS/path.relative_to(self.root)

    def test_apply_keeps_one_original_and_restore_round_trips(self):
        track=usb_track(self.root);original=track.read_bytes()
        self.assertEqual(three_band.apply(self.root),{'adapted':1,'already_current':0,'skipped_no_2ex':0,'failed':[]})
        self.assertEqual(self.backup(track).read_bytes(),original)
        self.assertEqual(track.read_bytes(),three_band.adapt_ext(original,track.with_suffix('.2EX').read_bytes()))
        self.assertFalse(list(self.root.rglob('*.xzmods-new')))
        with patch.object(three_band,'_write',wraps=three_band._write) as write:
            self.assertEqual(three_band.apply(self.root)['already_current'],1)
        write.assert_not_called()
        self.assertEqual(three_band.restore(self.root),{'restored':1,'stale_originals_removed':0,'failed':[]})
        self.assertEqual(track.read_bytes(),original)
        self.assertFalse((self.root/'CDJMODS').exists())

    def test_reexport_with_same_geometry_keeps_the_original(self):
        track=usb_track(self.root);original=track.read_bytes()
        three_band.apply(self.root);track.write_bytes(original)
        with patch.object(three_band,'_write',wraps=three_band._write) as write:
            self.assertEqual(three_band.apply(self.root)['adapted'],1)
        self.assertEqual([call.args[0] for call in write.call_args_list],[track])
        self.assertEqual(self.backup(track).read_bytes(),original)

    def test_reexport_with_new_heights_refreshes_the_original(self):
        track=usb_track(self.root);three_band.apply(self.root)
        reexported=bytearray(rekordbox_ext());at,count=three_band.waves(reexported)[b'PWV5']
        for index in range(count):reexported[at+index*2+1]^=0x04
        track.write_bytes(bytes(reexported))
        self.assertEqual(three_band.apply(self.root)['adapted'],1)
        self.assertEqual(self.backup(track).read_bytes(),bytes(reexported))
        three_band.restore(self.root);self.assertEqual(track.read_bytes(),bytes(reexported))

    def test_restore_keeps_later_cue_edits(self):
        track=usb_track(self.root);original=track.read_bytes();three_band.apply(self.root)
        edited=CUES.replace(b'hot',b'HOT')
        track.write_bytes(track.read_bytes().replace(CUES,edited))
        self.assertEqual(three_band.restore(self.root)['restored'],1)
        self.assertEqual(track.read_bytes(),original.replace(CUES,edited))

    def test_restore_drops_original_after_reexport(self):
        track=usb_track(self.root);three_band.apply(self.root)
        reexported=rekordbox_ext(detail_count=151);track.write_bytes(reexported)
        self.assertEqual(three_band.restore(self.root),{'restored':0,'stale_originals_removed':1,'failed':[]})
        self.assertEqual(track.read_bytes(),reexported)
        self.assertFalse((self.root/'CDJMODS').exists())

    def test_restore_keeps_other_cdjmods_content(self):
        usb_track(self.root);three_band.apply(self.root)
        stems=self.root/'CDJMODS'/'overcue'/'index.json';stems.parent.mkdir(parents=True);stems.write_text('{}')
        three_band.restore(self.root)
        self.assertTrue(stems.is_file());self.assertFalse((self.root/three_band.ORIGINALS).exists())

    def test_track_without_3band_analysis_is_untouched(self):
        track=usb_track(self.root,analysed=False);original=track.read_bytes()
        self.assertEqual(three_band.apply(self.root)['skipped_no_2ex'],1)
        self.assertEqual(track.read_bytes(),original);self.assertFalse((self.root/'CDJMODS').exists())

    def test_malformed_track_fails_alone(self):
        good=usb_track(self.root);bad=usb_track(self.root,'0000B002',rekordbox_ext()[:-3])
        broken=bad.read_bytes()
        report=three_band.apply(self.root)
        self.assertEqual(report['adapted'],1)
        self.assertEqual([path for path,_ in report['failed']],['PIONEER/USBANLZ/P001/0000B002/ANLZ0000.EXT'])
        self.assertEqual(bad.read_bytes(),broken);self.assertFalse(self.backup(bad).exists())
        self.assertTrue(self.backup(good).exists())

    def test_requires_rekordbox_usb_root(self):
        with self.assertRaisesRegex(ValueError,'PIONEER folder'):three_band.apply(self.root)

    def test_reports_progress(self):
        usb_track(self.root)
        class Recorder(Job):
            def __init__(self):super().__init__();self.messages=[]
            def progress(self,stage,message):self.messages.append((stage,message))
        job=Recorder();three_band.apply(self.root,job)
        self.assertEqual(job.messages,[('waveforms','Updating waveform colours (0 of 1 tracks)')])


class DispatchTests(unittest.TestCase):
    def test_apply_and_restore_through_service(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);track=usb_track(root);original=track.read_bytes()
            result=service.dispatch({'method':'apply_three_band','volume':str(root)},Job())
            self.assertEqual(result['format'],'three-band-waveforms/1')
            self.assertEqual(result['usb_root'],str(root.absolute()))
            self.assertEqual((result['adapted'],result['failed'],result['failed_count']),(1,[],0))
            result=service.dispatch({'method':'restore_rgb_waveforms','volume':str(root)},Job())
            self.assertEqual((result['format'],result['restored'],result['failed_count']),('rgb-waveforms-restored/1',1,0))
            self.assertEqual(track.read_bytes(),original)

    def test_caps_failures_for_the_result_line(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            for index in range(60):usb_track(root,f'{index:08X}',b'not an analysis file')
            result=service.dispatch({'method':'apply_three_band','volume':str(root)},Job())
            self.assertEqual((len(result['failed']),result['failed_count']),(50,60))

    def test_rejects_a_file_as_volume(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'file';path.write_bytes(b'')
            with self.assertRaisesRegex(ValueError,'folder'):
                service.dispatch({'method':'apply_three_band','volume':str(path)},Job())


if __name__=='__main__':unittest.main()
