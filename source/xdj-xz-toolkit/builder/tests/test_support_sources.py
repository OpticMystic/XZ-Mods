import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

from builder import support_sources as s, support_reports as reports
from builder.support_contract import CONTRACT, SOURCES, automatic_fix_eligible
from builder.support_validation import validate_report
from builder.jobs import Job

class Sources(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir=os.environ.get('XZ_SUPPORT_TEST_OUTPUT',tempfile.gettempdir()))
        self.base=Path(self.tmp.name);self.data=self.base/'app';self.data.mkdir();self.job=Job()
        self.env=patch.dict(os.environ,{'XZ_BUILDER_DATA':str(self.data),'XZ_BUILDER_RESOURCES':str(self.base/'resources')})
        self.env.start()
    def tearDown(self):
        self.env.stop();self.tmp.cleanup()
    def usb(self,name='usb',log='WAIT_LOOPBACK\n'):
        root=self.base/name;(root/'XZ_RUNTIME').mkdir(parents=True);(root/'XZ_RUNTIME/session.txt').write_text(log)
        return root
    def discover(self,*roots):return s.discover(self.job,[{'volume':str(r)} for r in roots])
    def draft(self,**request):
        result=reports.dispatch({'method':'support_prepare_v2','description':'It stopped working.',**request},self.job)
        saved=reports.load(result['report_id']);body=json.loads(saved['body']);validate_report(body)
        self.assertEqual(hashlib.sha256(saved['body'].encode()).hexdigest(),result['sha256'])
        return result,body
    def diary(self,root,epoch=None,canary=False):
        directory=root/'VJ.Tools/XZ-Mods-diary';directory.mkdir(parents=True,exist_ok=True)
        header={'type':'session','schema':'xz-mods-diary/1','session_id':'d'*32,'epoch':int(time.time())-120 if epoch is None else epoch,'monotonic_ms':0,
                'version':'0.5.0-preview.22.6','firmware':'1.26','settings_sha256':'c'*64,'slots':[1,2],'interface':'MODERN','deck_view':4}
        lines=[header,{'type':'event','sequence':1,'monotonic_ms':60000,'code':'WAIT_LOOPBACK'},
               {'type':'metrics','sequence':2,'monotonic_ms':120000,'cpu_percent':35,'free_ram_kib':10240,'audio_underruns':2,'sync_phase_error_ms':[0,None,-3,5],'usb_read_latency_us':1200}]
        if canary:lines += [{'type':'event','sequence':3,'monotonic_ms':121000,'code':'CANARY title /music/song.wav 192.168.1.2 serial-secret'}]
        (directory/'segment-00.jsonl').write_bytes(b''.join(s.encoded(v)+b'\n' for v in lines))
        return header

    def test_diary_v2_and_v1_are_both_read_and_private(self):
        root=self.usb();header=self.diary(root)
        directory=root/'VJ.Tools/XZ-Mods-diary'
        v2={**header,'schema':'xz-mods-diary/2','session_id':'e'*32,'segment_generation':1}
        metrics={'type':'metrics','sequence':10,'monotonic_ms':90000,'cpu_percent':None,'free_ram_kib':50000,
                 'audio_underrun_frames':[None,None,256,0],'sync_phase_error_ms':[None,None,-4,1],
                 'key_shift_max_us':[None,None,781,234],'usb_read_latency_us':None,'dropped_records':2}
        event={'type':'event','sequence':11,'monotonic_ms':110000,'component':'sync','state':'following',
               'reason':'clock_lost','notice':'none','deck':3,'slot':0,'detail_code':0}
        bad={**event,'sequence':12,'notice':'CANARY /music/title.wav SERIAL 192.168.1.2'}
        extra={**metrics,'sequence':13,'title':'CANARY'}
        short={**metrics,'sequence':14,'key_shift_max_us':[0]}
        lines=[v2,metrics,event,bad,extra,short]
        (directory/'segment-01.jsonl').write_bytes(b''.join(s.encoded(v)+b'\n' for v in lines)+b'{"partial":')
        found=self.discover(root)
        snapshot=s.collect({'scan_generation':found['scan_generation'],'category':'audio'},self.job)
        result=next(i for i in snapshot['items'] if i['source_id']=='usb_diary')
        raw=json.dumps(result);self.assertIn('WAIT_LOOPBACK',raw);self.assertIn('clock_lost',raw);self.assertIn('781',raw)
        self.assertNotIn('CANARY',raw);self.assertNotIn('partial',raw);self.assertNotIn('segment_generation',raw)
        self.assertIsNotNone(snapshot['diary_coverage']['usb_a']['start_utc'])
        _,body=self.draft(scan_generation=found['scan_generation'],category='audio',kind_hint='DEGRADATION')
        validate_report(body);self.assertNotIn(v2['session_id'],json.dumps(body))

    def test_diary_unknown_version_and_v2_fields_in_v1_are_refused(self):
        root=self.usb();header=self.diary(root);path=root/'VJ.Tools/XZ-Mods-diary/segment-00.jsonl'
        lines=path.read_bytes().splitlines();bad=json.loads(lines[1]);bad['notice']='other'
        path.write_bytes(s.encoded(header)+b'\n'+s.encoded(bad)+b'\n')
        result,_=s.diary({'root':str(root),'identity':s.identity(root),'slot':'usb_a'},s.Budget(self.job),s.window({}))
        self.assertNotIn('WAIT_LOOPBACK',json.dumps(result))
        header['schema']='xz-mods-diary/999';path.write_bytes(s.encoded(header)+b'\n')
        _,coverage=s.diary({'root':str(root),'identity':s.identity(root),'slot':'usb_a'},s.Budget(self.job),s.window({}))
        self.assertEqual(coverage['state'],'unavailable')

    def test_metadata_discovery_never_calls_full_inspection_and_bounds_sticks(self):
        roots=[self.usb(str(i)) for i in range(6)];plain=self.base/'plain';plain.mkdir()
        with patch('builder.managed_usb.inspect',side_effect=AssertionError('music inspection')),patch('builder.managed_usb.support_candidates',return_value=[{'volume':str(p)} for p in [plain,*roots]]):
            found=s.discover(self.job)
        self.assertEqual(len(found['sticks']),4);self.assertEqual(found['excess_sticks'],2)
        snapshot=s.collect({'scan_generation':found['scan_generation'],'category':'bridge'},self.job)
        payload=s.encoded(snapshot['items']);self.assertNotIn(str(self.base).encode(),payload)
        self.assertEqual({i['source_slot'] for i in snapshot['items'] if i['source_slot'].startswith('usb_')},{'usb_a','usb_b','usb_c','usb_d'})
    def test_removed_and_replaced_sticks_do_not_read_replacement(self):
        root=self.usb();found=self.discover(root);root.rename(self.base/'removed');self.usb(log='READY_CH3_CH4\n')
        snapshot=s.collect({'scan_generation':found['scan_generation'],'category':'bridge'},self.job)
        self.assertNotIn('READY_CH3_CH4',json.dumps(snapshot));self.assertIn('removed',json.dumps(snapshot))
    def test_linked_parent_and_source_refused(self):
        root=self.usb();outside=self.base/'outside';outside.write_text('READY_CH3_CH4')
        target=root/'XZ_RUNTIME/session.txt';target.unlink()
        try:target.symlink_to(outside)
        except OSError as exc:self.skipTest('Host cannot create symlinks: '+str(exc))
        with self.assertRaises(s.SourceError):s.Budget(self.job).read(target,131072,tail=True)
        with self.assertRaises((s.SourceError,OSError)):s.Budget(self.job).read(Path('//host/share/file'),100)
    def test_locked_parent_cannot_swap_or_changed_file_is_rejected(self):
        root=self.usb();path=root/'XZ_RUNTIME/session.txt'
        if os.name=='nt':
            with s.locked_file(path):
                with self.assertRaises(OSError):root.rename(self.base/'moved')
        else:
            with self.assertRaises(s.SourceError):
                with s.locked_file(path):path.write_text('changed')
    def test_window_unknown_clock_and_positive_canaries(self):
        now=int(time.time());bounds={'epoch':now,'known':True,'before':600,'after':300}
        raw=f'{now-50} WAIT_LOOPBACK\n{now-1000} READY_CH3_CH4\n100 WAIT_MOUNT\n'.encode()+b'PRIVATE /music/Song.wav 192.168.2.4 serial-token\n'
        facts,omitted=s.events(raw,'events',bounds)
        self.assertEqual(facts[0]['relative_seconds'],-50);self.assertEqual(facts[1]['window_match'],'unknown');self.assertEqual(omitted,2)
        facts,omitted=s.events(b'WAIT_LOOPBACK\n'*100,'events',bounds);self.assertEqual(len(facts),32);self.assertEqual(omitted,68)
        ambiguous=s.window({'incident':{'time':'2026-11-01T01:30:00','uncertain':True}});self.assertFalse(ambiguous['known'])
        self.assertNotIn('PRIVATE',json.dumps(facts))
    def test_coldplug_facts_strip_device_pid_and_unknown_suffix(self):
        bounds=s.window({})
        raw=b'USB_COLDPLUG: dev=PRIVATE-DEVICE action=mount slot=2 result=notified mounted_after_ms=1234 attempt=3\nUSB_COLDPLUG: pid=443 result=watch-complete attempts=4\nUSB_COLDPLUG: pid=443 attempt=2 result=process-exited TOKEN\n'
        facts,omitted=s.events(raw,'coldplug',bounds);self.assertEqual(len(facts),2);self.assertEqual(facts[0]['slot'],2);self.assertEqual(omitted,1)
        self.assertNotIn('PRIVATE',json.dumps(facts));self.assertNotIn('443',json.dumps(facts))
    def test_diary_on_music_only_stick_coverage_and_absence(self):
        music=self.base/'music';music.mkdir();header=self.diary(music,canary=True);loader=self.usb()
        found=self.discover(music,loader);self.assertEqual(len(found['sticks']),2)
        snapshot=s.collect({'scan_generation':found['scan_generation'],'category':'audio'},self.job)
        diaries=[i for i in snapshot['items'] if i['source_id']=='usb_diary'];self.assertEqual(len(diaries),2)
        self.assertEqual(diaries[0]['coverage']['duration_seconds'],60);self.assertEqual(diaries[1]['missing_reason'],'missing')
        self.assertEqual(snapshot['diary_coverage']['usb_b']['state'],'absent')
        self.assertIsNotNone(snapshot['diary_coverage']['usb_a']['start_utc'])
        result,body=self.draft(scan_generation=found['scan_generation'],category='audio',kind_hint='DEGRADATION')
        raw=json.dumps(body);self.assertNotIn('CANARY',raw);self.assertNotIn(header['session_id'],raw);self.assertNotIn(str(header['epoch']),raw);self.assertNotIn('start_utc',raw)
        self.assertIn('audio_underruns',raw);self.assertEqual(body['build']['runtime_version'],'unknown')
    def test_diary_invalid_clock_ring_bounds_and_unknown_fields(self):
        root=self.usb();self.diary(root,epoch=1)
        directory=root/'VJ.Tools/XZ-Mods-diary';(directory/'secret.txt').write_text('DO-NOT-READ')
        (directory/'segment-08.jsonl').write_text('DO-NOT-READ')
        result,_=s.diary({'root':str(root),'identity':s.identity(root),'slot':'usb_a'},s.Budget(self.job),s.window({}))
        self.assertEqual(result['coverage']['clock_confidence'],'unknown');self.assertEqual(result['coverage']['duration_seconds'],60)
        self.assertNotIn('DO-NOT-READ',json.dumps(result))
    def test_registry_settings_hash_and_provenance(self):
        root=self.usb();resources=self.base/'resources/runtime';resources.mkdir(parents=True)
        schema_path=Path(__file__).parents[2]/'mods/settings-schema.json';raw=schema_path.read_bytes();schema=json.loads(raw)
        (resources/'settings-schema.json').write_bytes(raw);(resources/'manifest.json').write_text(json.dumps({'settings_schema_sha256':hashlib.sha256(raw).hexdigest()}))
        from builder import usb_settings
        settings=root/'VJ.Tools/XZ-Mods.cfg';settings.parent.mkdir();settings.write_bytes(usb_settings.serialize(usb_settings.defaults(schema),schema))
        found=self.discover(root);snapshot=s.collect({'scan_generation':found['scan_generation'],'source_ids':['usb_settings']},self.job)
        self.assertIsNone(snapshot['items'][0]['missing_reason']);self.assertTrue(snapshot['items'][0]['facts'])
        self.assertTrue(all(f['clock_basis']=='current' for f in snapshot['items'][0]['facts']))
    def test_frozen_evidence_omission_edit_binding_and_idea_discards_scan(self):
        root=self.usb();found=self.discover(root);snapshot=s.collect({'scan_generation':found['scan_generation'],'category':'bridge'},self.job)
        omitted=next(i for i in snapshot['items'] if i['source_id']=='usb_session')['item_id']
        first,body=self.draft(scan_generation=found['scan_generation'],category='bridge',omit_item_ids=[omitted],reporter_hints={'topic':'GOT_WORSE'})
        self.assertNotIn(omitted,json.dumps(body));self.assertNotIn('WAIT_LOOPBACK',json.dumps(body))
        self.assertEqual(body['reporter_hints']['topic'],'GOT_WORSE')
        second=reports.dispatch({'method':'support_edit_v2','report_id':first['report_id'],'sha256':first['sha256'],'description':'New text'},self.job)
        self.assertNotEqual(first['report_id'],second['report_id']);self.assertFalse((reports.root()/(second['report_id']+'.review')).exists())
        with self.assertRaises(ValueError):reports.dispatch({'method':'support_edit_v2','report_id':first['report_id'],'sha256':'0'*64},self.job)
        _,idea=self.draft(scan_generation=found['scan_generation'],kind_hint='FEATURE');self.assertEqual(idea['private']['items'],[])
        _,idea=self.draft(scan_generation=found['scan_generation'],kind_hint='BUG',reporter_hints={'topic':'NEW_IDEA'});self.assertEqual(idea['private']['items'],[])
        self.assertFalse((reports.root()/(first['report_id']+'.review')).exists())
    def test_category_filters_cancel_budget_and_serialized_caps(self):
        root=self.usb(log='WAIT_LOOPBACK\n'*50000);found=self.discover(root)
        snapshot=s.collect({'scan_generation':found['scan_generation'],'category':'display','source_ids':['usb_session']},self.job)
        self.assertEqual(len(snapshot['items']),1);self.assertTrue(snapshot['items'][0]['truncated'])
        self.assertLessEqual(len(s.encoded(snapshot['items'][0])),32768)
        class Cancel:
            def check(self):raise s.Cancelled('Cancelled')
        found=self.discover(root);partial=s.collect({'scan_generation':found['scan_generation'],'source_ids':['usb_session']},Cancel())
        self.assertEqual(partial['unfinished_sources'],1);self.assertEqual(partial['items'][0]['missing_reason'],'cancelled')
        first,body=self.draft(kind_hint='FEATURE',description='"\\\n'*1000);self.assertLessEqual(first['bytes'],524288)
        self.assertGreater(first['bytes'],len(body['description'].encode()))
        with self.assertRaises(ValueError):self.draft(kind_hint='FEATURE',description='ðŸ˜€'*1025)
    def test_native_code_only_bridge_legacy_import_and_metadata_bounds(self):
        facts,omitted=s.bridge(b'WAIT_LOOPBACK\nREADY_CH3_CH4\n',s.window({}));self.assertEqual(len(facts),2)
        with self.assertRaises(s.SourceError):s.bridge(b'WAIT_LOOPBACK PRIVATE\n',s.window({}))
        clean,_,_=reports.sanitize('bridge.json','WAIT_LOOPBACK\nREADY_CH3_CH4\n');self.assertIn('READY_CH3_CH4',clean['content'])
        path=self.base/'metadata.json';path.write_bytes(b'x'*1024)
        with self.assertRaisesRegex(s.SourceError,'oversized'):s.Budget(self.job).read(path,1023)
    def test_backend_writer_records_only_fixed_events(self):
        s.diagnostic_event('loader','started');s.diagnostic_event('loader','failed')
        raw=(self.data/'app.log').read_bytes();facts,_=s.events(raw,'app_events',s.window({}))
        self.assertEqual([f['result'] for f in facts],['started','failed']);self.assertNotIn('session',json.dumps(facts));self.assertNotIn('epoch',json.dumps(facts))
    def collected(self,generation,**request):
        return reports.dispatch({'method':'support_collect','scan_generation':generation,**request},self.job)
    def digest(self,items):
        # The renderer's algorithm: JSON of [item_id, sha256] pairs for the included items, in order.
        pairs=[[i['item_id'],i['sha256']] for i in items]
        return hashlib.sha256(json.dumps(pairs,separators=(',',':')).encode()).hexdigest()
    def test_send_consent_binds_exact_bytes_and_displayed_selection(self):
        root=self.usb();self.diary(root);found=self.discover(root)
        summary=self.collected(found['scan_generation'],reporter_hints={'topic':'NOT_WORKING','problem_area':'FOUR_DECKS_LOOPBACK'})
        self.assertEqual(summary['category'],'bridge');self.assertEqual(summary['kind_hint'],'BUG')
        shown=[i for i in summary['items'] if i['eligible']];self.assertTrue(shown)
        omitted=next(i for i in shown if i['source_id']=='usb_session');kept=[i for i in shown if i is not omitted]
        request={'method':'support_prepare_v2','description':'Decks 3 and 4 went silent.','scan_generation':found['scan_generation'],
                 'reporter_hints':{'topic':'NOT_WORKING','problem_area':'FOUR_DECKS_LOOPBACK'},'omit_item_ids':[omitted['item_id']]}
        # A selection that differs from what the reporter saw is refused before anything is saved.
        before=set(reports.root().iterdir())
        with self.assertRaisesRegex(ValueError,'Included details changed'):
            reports.dispatch({**request,'selection_sha256':self.digest(shown),'consent':'private-report/v2'},self.job)
        self.assertEqual(set(reports.root().iterdir()),before)
        sent=reports.dispatch({**request,'selection_sha256':self.digest(kept),'consent':'private-report/v2'},self.job)
        self.assertEqual(sent['selection_sha256'],self.digest(kept));self.assertEqual(sent['consent'],'private-report/v2')
        saved=reports.load(sent['report_id']);raw=saved['body'].encode()
        record=json.loads((reports.root()/(sent['report_id']+'.consent')).read_text())
        self.assertEqual((record['sha256'],record['bytes'],record['report_id']),(hashlib.sha256(raw).hexdigest(),len(raw),sent['report_id']))
        self.assertEqual(record['selection_sha256'],self.digest(kept))
        body=json.loads(raw);self.assertEqual([i['item_id'] for i in body['private']['items']],[i['item_id'] for i in kept])
        self.assertNotIn(omitted['item_id'],saved['body'])
        for item in body['private']['items']:
            self.assertEqual(hashlib.sha256(s.encoded(item)).hexdigest(),next(i['sha256'] for i in kept if i['item_id']==item['item_id']))
        # Consent is to these bytes only: a wrong hash, size, consent string or changed draft is refused.
        for bad in ({'sha256':'0'*64},{'bytes':len(raw)+1},{'consent':'public-summary-private-logs/v1'},{'selection_sha256':'0'*64}):
            with self.assertRaises(ValueError):
                reports.dispatch({'method':'support_consent','report_id':sent['report_id'],'sha256':sent['sha256'],'bytes':sent['bytes'],
                                  'consent':'private-report/v2',**bad},self.job)
        # Viewing pages writes nothing; reopening a draft clears consent so Send must be pressed again.
        reports.dispatch({'method':'support_preview','report_id':sent['report_id'],'sha256':sent['sha256'],'page':0},self.job)
        self.assertFalse((reports.root()/(sent['report_id']+'.review')).exists())
        reports.dispatch({'method':'support_resume','report_id':sent['report_id'],'sha256':sent['sha256']},self.job)
        self.assertFalse((reports.root()/(sent['report_id']+'.consent')).exists())
        again=reports.dispatch({'method':'support_consent','report_id':sent['report_id'],'sha256':sent['sha256'],'bytes':sent['bytes'],'consent':'private-report/v2'},self.job)
        self.assertEqual(again['selection_sha256'],self.digest(kept))
        listing=reports.dispatch({'method':'support_list'},self.job)['reports']
        entry=next(e for e in listing if e['report_id']==sent['report_id'])
        self.assertEqual((entry['schema'],entry['kind_hint'],entry['topic'],entry['attempted']),('xz-support-report/2','BUG','NOT_WORKING',False))
        self.assertNotIn(saved['key'],json.dumps(listing))
    def test_live_usb_rescan_supersedes_and_lists_sticks_without_diary(self):
        root=self.usb();music=self.base/'music';music.mkdir();fixed=self.base/'fixed';fixed.mkdir()
        candidates=[{'volume':str(root),'removable':True},{'volume':str(music),'removable':True},{'volume':str(fixed),'removable':False}]
        with patch('builder.managed_usb.support_candidates',return_value=candidates):
            first=reports.dispatch({'method':'support_sources'},self.job)
            self.assertEqual([o['local_volume'] for o in first['other_sticks']],[str(music)])
            self.assertTrue((s.store_root()/(first['scan_generation']+'.json')).exists())
            second=reports.dispatch({'method':'support_sources','supersedes':first['scan_generation']},self.job)
        self.assertFalse((s.store_root()/(first['scan_generation']+'.json')).exists())
        with self.assertRaises((ValueError,OSError)):s._record(first['scan_generation'])
        summary=self.collected(second['scan_generation'],reporter_hints={'topic':'GOT_WORSE','degradation_timing':'DURING_SET_OVER_TIME'})
        self.assertEqual(summary['kind_hint'],'DEGRADATION');self.assertEqual(summary['diary_coverage']['usb_a']['state'],'absent')
        # A superseded ID that is not a scan record never deletes anything else.
        keep=reports.root()/'keep.draft';keep.write_text('x')
        s.discover(self.job,[],supersedes='../support-reports/keep');self.assertTrue(keep.exists())
    def test_stalled_collection_sends_without_evidence_and_records_missing_coverage(self):
        root=self.usb();found=self.discover(root)
        sent=reports.dispatch({'method':'support_prepare_v2','description':'The screen froze.','scan_generation':found['scan_generation'],'collect':False,
                               'selection_sha256':self.digest([]),'consent':'private-report/v2'},self.job)
        body=json.loads(reports.load(sent['report_id'])['body'])
        self.assertEqual(body['private']['items'],[]);self.assertEqual(body['coverage']['unfinished_sources'],1)
        self.assertNotEqual(body['coverage']['scan_generation'],found['scan_generation'])
        idea=reports.dispatch({'method':'support_prepare_v2','description':'Colour themes','reporter_hints':{'topic':'NEW_IDEA','idea_type':'APPEARANCE'},
                               'selection_sha256':self.digest([]),'consent':'private-report/v2'},self.job)
        body=json.loads(reports.load(idea['report_id'])['body'])
        self.assertEqual((body['kind_hint'],body['private']['items'],body['coverage']['unfinished_sources']),('FEATURE',[],0))
    def test_taxonomy_eligibility_all_pairs_and_generated_bytes(self):
        for kind in [None,*CONTRACT['kinds']]:
            for impact in [None,*CONTRACT['impacts']]:
                self.assertEqual(automatic_fix_eligible(kind,impact),kind in ('BUG','REGRESSION','DEGRADATION') and impact in ('ADDITIVE','RESTORES_EXPECTED'))

if __name__=='__main__':unittest.main()
