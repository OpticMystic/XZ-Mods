import unittest,subprocess,json,os,sys,tempfile,hashlib,importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.dont_write_bytecode=True
from builder import support_reports as s
class Reports(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(dir=os.environ.get('XZ_SUPPORT_TEST_OUTPUT',tempfile.gettempdir()));self.data=Path(self.tmp.name);self.env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1','XZ_BUILDER_DATA':str(self.data)}
 def tearDown(self):self.tmp.cleanup()
 def call(self,r,env=None):
  p=subprocess.run([sys.executable,'-B','-m','builder.entry'],cwd=str(ROOT.parent),input=json.dumps(r).encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env or self.env,timeout=10)
  self.assertEqual(p.returncode,0,p.stderr);self.assertLess(len(p.stdout),65536);return json.loads(p.stdout)
 def prepare(self,**fields):
  r=self.call({'method':'support_prepare','title':'Four decks unavailable','description':'Steps approved by reporter','include_private':True,**fields});self.assertTrue(r['ok'],r);return r['result']
 def body(self,d):return json.loads((self.data/'support-reports'/(d['report_id']+'.draft')).read_text())
 def test_service_privacy_and_exact_hash(self):
  (self.data/'app.log').write_text('state=ACTIVE\nCookie: canary-secret\nC:\\Users\\NamedPerson\\song.wav\n192.168.1.9 serial=canary\nreason=WAIT_MOUNT\n')
  d=self.prepare();v=self.body(d);self.assertEqual(hashlib.sha256(v['body'].encode()).hexdigest(),d['sha256']);self.assertNotIn(v['key'],v['body']);self.assertNotIn('canary',v['body']);self.assertNotIn('NamedPerson',v['body']);self.assertIn('ACTIVE',v['body']);self.assertEqual(json.loads(v['body'])['redactions']['omitted_lines'],4)
  p=self.call({'method':'support_preview',**d,'page':0});self.assertTrue(p['ok']);self.assertEqual(p['result']['text'],v['body']);self.assertLessEqual(len(p['result']['text'].encode()),16384)
 def test_edit_does_not_recollect_or_reuse_identity(self):
  (self.data/'app.log').write_text('state=ACTIVE');d=self.prepare();(self.data/'app.log').write_text('state=RETIRED');e=self.call({'method':'support_edit','report_id':d['report_id'],'title':'Edited','include_private':True})['result'];self.assertNotEqual(d['report_id'],e['report_id']);self.assertIn('ACTIVE',self.body(e)['body']);self.assertNotIn('RETIRED',self.body(e)['body']);self.assertFalse((self.data/'support-reports'/(e['report_id']+'.review')).exists())
 def test_stale_invalid_and_oversize_ipc(self):
  d=self.prepare();r=self.call({'method':'support_preview',**d,'sha256':'0'*64,'page':0});self.assertFalse(r['ok']);self.assertIn('Stale',r['error'])
  for r in ({'method':'support_preview','report_id':'../owner-key.dpapi'}, {'method':'support_prepare','title':'x'*161},{'method':'support_prepare','title':'valid','description':'x'*70000}):self.assertFalse(self.call(r)['ok'])
 def test_cancellation_writes_nothing(self):
  cancel=self.data/'cancel';cancel.write_text('cancel');e={**self.env,'XZ_BUILDER_CANCEL_FILE':str(cancel)};r=self.call({'method':'support_prepare','title':'Cancelled'},e);self.assertTrue(r['cancelled']);self.assertFalse((self.data/'support-reports').exists())
 def test_truncation_missing_contact_and_decline(self):
  (self.data/'app.log').write_text('secret=CANARY\n'*15000+'state=ACTIVE\n');d=self.prepare(contact_opt_in=True,contact='private@example.invalid');body=json.loads(self.body(d)['body']);self.assertTrue(body['private']['items'][0]['truncated']);self.assertTrue(body['private']['items'][1]['missing']);self.assertEqual(body['private']['contact'],'private@example.invalid');self.assertNotIn('CANARY',self.body(d)['body'])
  d=self.prepare(include_private=False,contact_opt_in=True,contact='private@example.invalid');body=json.loads(self.body(d)['body']);self.assertEqual(body['private'],{'contact':None,'items':[]})
 def test_no_fallback_or_path_escape(self):
  e={**self.env};e.pop('XZ_BUILDER_DATA');self.assertFalse(self.call({'method':'support_prepare','title':'No host root','include_private':False},e)['ok'])
  self.assertFalse(self.call({'method':'support_preview','report_id':'../bad','sha256':'0'*64})['ok'])
 def test_large_preview_pages(self):
  (self.data/'app.log').write_text('state=ACTIVE\n'*10000);d=self.prepare();parts=[]
  for page in range(d['pages']):
   r=self.call({'method':'support_preview',**d,'page':page});self.assertTrue(r['ok'],r);parts.append(r['result']['text']);self.assertLessEqual(len(parts[-1].encode()),16384)
  self.assertEqual(''.join(parts),self.body(d)['body']);review=json.loads((self.data/'support-reports'/(d['report_id']+'.review')).read_text());self.assertTrue(review['complete'])
 def test_actual_boot_session_and_explicit_native_export(self):
  usb=self.data/'usb';(usb/'XZ_RUNTIME').mkdir(parents=True)
  (usb/'XZ_RUNTIME/session.txt').write_text('[2026-10-25 01:02:03] VJ_NETWORK_READY: address=192.168.9.4 serial=CANARY\n[2026-10-25 01:02:04] FAILED: C:/Users/NamedPerson/private.wav\nprivate token CANARY\n')
  export=usb/'VJTOOLS/support-reports';export.mkdir(parents=True);ident='a'*32
  native={'schema':'xz-support-report/1','report_id':ident,'origin':'controller','private':{'contact':'CANARY-contact','items':[
   {'label':'bridge.json','content':json.dumps({'schema':'xz-bridge-status/1','state':'waiting','reason':'WAIT_MUSIC_MOUNT','terminal_reason':'TERMINATED_MUSIC_IDENTITY','attempt':2,'retryable':True,'route_ready':False,'serial':'CANARY'}),'missing':False,'truncated':False},
   {'label':'bridge.log','content':'1700000000 WAIT_MUSIC_MOUNT\n1700000001 READY_CH3_CH4\n1700000002 TERMINATED_SIGNAL\nowner_key=CANARY','missing':False,'truncated':False}]}}
  (export/('xz-support-report-'+ident+'.json')).write_text(json.dumps(native))
  (export/'draft.meta').write_text('receipt_key=CANARY')
  d=self.prepare(volume=str(usb));v=self.body(d);body=json.loads(v['body']);items={x['label']:x for x in body['private']['items']}
  self.assertIn('VJ_NETWORK_READY',items['bootstrap.log']['content']);self.assertIn('FAILED',items['bootstrap.log']['content'])
  for code in ('WAIT_MUSIC_MOUNT','READY_CH3_CH4','TERMINATED_SIGNAL'):self.assertIn(code,items['bridge.log']['content'])
  self.assertEqual(json.loads(items['bridge.json']['content'])['state'],'waiting');self.assertNotIn('CANARY',v['body']);self.assertNotIn('192.168',v['body']);self.assertNotIn('NamedPerson',v['body']);self.assertGreater(body['redactions']['omitted_lines'],0)
  # Manual edits reuse sanitized evidence and build facts, even if sources disappear.
  (export/('xz-support-report-'+ident+'.json')).unlink();(usb/'XZ_RUNTIME/session.txt').write_text('reason=FAILED')
  e=self.call({'method':'support_edit','report_id':d['report_id'],'title':'Edited','include_private':True})['result'];edited=json.loads(self.body(e)['body']);self.assertEqual(edited['private']['items'],body['private']['items']);self.assertEqual(edited['build'],body['build'])
 def test_native_export_refuses_identity_size_and_duplicate_labels(self):
  usb=self.data/'usb';directory=usb/'VJTOOLS/support-reports';directory.mkdir(parents=True);ident='a'*32;path=directory/('xz-support-report-'+ident+'.json')
  item={'label':'bridge.log','content':'reason=READY_CH3_CH4','missing':False,'truncated':False}
  for value in ({'schema':'xz-support-report/1','origin':'controller','report_id':'b'*32,'private':{'items':[item]}},{'schema':'xz-support-report/1','origin':'controller','report_id':ident,'private':{'items':[item,item]}}):
   path.write_text(json.dumps(value));body=json.loads(self.body(self.prepare(volume=str(usb)))['body']);self.assertNotIn('READY_CH3_CH4',json.dumps(body));self.assertGreater(body['redactions']['omitted_lines'],0)
  path.write_bytes(b'x'*524289);body=json.loads(self.body(self.prepare(volume=str(usb)))['body']);self.assertNotIn('bridge.log',[i['label'] for i in body['private']['items']])
 def test_trusted_resource_metadata_and_hash_pin(self):
  resources=self.data/'resources';runtime=resources/'runtime';runtime.mkdir(parents=True)
  package={'components':{'bridge':{'sha256':'b'*64},'module':{'sha256':'c'*64}}};raw=json.dumps(package).encode();(runtime/'quad-package.json').write_bytes(raw)
  manifest={'app_version':'0.5.0-preview.77','runtime_sha256':'a'*64,'quad_package_sha256':hashlib.sha256(raw).hexdigest()};(runtime/'manifest.json').write_text(json.dumps(manifest))
  self.env['XZ_BUILDER_RESOURCES']=str(resources);d=self.prepare();body=json.loads(self.body(d)['body']);self.assertEqual(body['build'],{'app_version':'0.5.0-preview.77','runtime_sha256':'a'*64,'bridge_sha256':'b'*64,'module_sha256':'c'*64})
  (runtime/'quad-package.json').write_text(json.dumps({'components':{'bridge':{'sha256':'d'*64}}}));body=json.loads(self.body(self.prepare())['body']);self.assertIsNone(body['build']['bridge_sha256']);self.assertIsNone(body['build']['module_sha256'])
 def test_sanitized_event_expansion_stays_within_item_cap(self):
  (self.data/'app.log').write_text('FAILED\n'*20000);body=json.loads(self.body(self.prepare())['body']);item=next(i for i in body['private']['items'] if i['label']=='app.log');self.assertLessEqual(len(item['content'].encode()),131072);self.assertTrue(item['truncated']);self.assertGreater(body['redactions']['omitted_lines'],0)
 def test_unexported_ram_and_obsolete_paths_are_not_collected(self):
  usb=self.data/'usb';(usb/'VJ.Tools').mkdir(parents=True);(usb/'VJ.Tools/bridge.log').write_text('reason=READY_CH3_CH4')
  body=json.loads(self.body(self.prepare(volume=str(usb)))['body']);self.assertNotIn('READY_CH3_CH4',json.dumps(body));self.assertNotIn('bridge.log',[i['label'] for i in body['private']['items']])
 def test_saved_reports_recover_quota_without_losing_reviewed_bytes(self):
  drafts=[self.prepare(title='Saved report '+str(i),include_private=False) for i in range(32)]
  self.assertFalse(self.call({'method':'support_prepare','title':'Overflow','include_private':False})['ok'])
  listing=self.call({'method':'support_list'})['result'];self.assertEqual(len(listing['reports']),32)
  first=drafts[0];before=self.body(first);entry=next(x for x in listing['reports'] if x['report_id']==first['report_id'])
  self.assertNotIn(before['key'],json.dumps(listing))
  self.call({'method':'support_preview',**first,'page':0})
  marker=self.data/'support-reports'/(first['report_id']+'.review');self.assertTrue(marker.exists())
  resumed=self.call({'method':'support_resume','report_id':first['report_id'],'sha256':first['sha256']})
  self.assertTrue(resumed['ok']);self.assertEqual(resumed['result']['sha256'],first['sha256']);self.assertEqual(resumed['result']['fields']['title'],'Saved report 0')
  self.assertFalse(marker.exists());self.assertEqual(self.body(first),before)
  stale=self.call({'method':'support_remove','report_id':entry['report_id'],'stored_revision':'0'*64});self.assertFalse(stale['ok']);self.assertEqual(self.body(first),before)
  removed=self.call({'method':'support_remove','report_id':entry['report_id'],'stored_revision':entry['stored_revision']});self.assertTrue(removed['ok'])
  self.prepare(title='Recovered after discard',include_private=False)
 def test_saved_reports_remove_damaged_copy_and_reject_escape(self):
  d=self.prepare(include_private=False);path=self.data/'support-reports'/(d['report_id']+'.draft');path.write_text('Damaged local JSON')
  (path.parent/'owner-key.dpapi').write_text('OWNER_CANARY');(path.parent/'not-a-report.draft').write_text('UNKNOWN_CANARY')
  listing=self.call({'method':'support_list'})['result'];self.assertEqual(len(listing['reports']),1);entry=listing['reports'][0]
  self.assertFalse(entry['readable']);self.assertNotIn('CANARY',json.dumps(listing))
  self.assertFalse(self.call({'method':'support_remove','report_id':'../owner-key.dpapi','stored_revision':entry['stored_revision']})['ok'])
  self.assertTrue(self.call({'method':'support_remove','report_id':entry['report_id'],'stored_revision':entry['stored_revision']})['ok'])
  self.assertEqual((path.parent/'owner-key.dpapi').read_text(),'OWNER_CANARY');self.assertTrue((path.parent/'not-a-report.draft').exists())
class NativeSupport(unittest.TestCase):
 @unittest.skipUnless(os.environ.get('XZ_SUPPORT_RUST_TESTS')=='1','Opt in to isolated native module tests')
 def test_native_module(self):
  # Compile the real module, not a rewritten proxy. Tauri application build.rs,
  # generated schemas, UI, shared release output and hosted endpoints are unused.
  app=ROOT.parents[2];manifest=app/'src-tauri/Cargo.toml';native=app/'src-tauri/src/support.rs'
  lines=manifest.read_text().splitlines();selected=[];inside=False
  for line in lines:
   if line=='[dependencies]':inside=True;continue
   if line.startswith('['):inside=False
   if inside and line.split('=',1)[0].strip() in {'tauri','tauri-plugin-dialog','serde_json','sha2','reqwest','rustls','tokio'}:selected.append(line)
  with tempfile.TemporaryDirectory(dir=os.environ.get('XZ_SUPPORT_TEST_OUTPUT',tempfile.gettempdir()),prefix='native-support-') as work:
   work=Path(work);(work/'Cargo.toml').write_text('[package]\nname="xz-support-focused-check"\nversion="0.0.0"\nedition="2021"\n[workspace]\n[lib]\npath="lib.rs"\n[dependencies]\n'+'\n'.join(selected)+'\n[profile.dev]\ndebug=0\nincremental=false\n')
   (work/'lib.rs').write_text('#[path='+json.dumps(native.as_posix())+']\nmod support;\n')
   (work/'Cargo.lock').write_bytes((app/'src-tauri/Cargo.lock').read_bytes())
   target=work/'target'
   p=subprocess.run(['cargo','test','--offline','--manifest-path',str(work/'Cargo.toml'),'--target-dir',str(target),'--lib'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=600)
   print(p.stdout.decode('utf-8','replace')[-8000:]);self.assertEqual(p.returncode,0)
if __name__=='__main__':unittest.main(verbosity=2)
