import hashlib,io,json,os,shutil,sys,tempfile,unittest,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
WORK=Path(tempfile.gettempdir())
sys.path.insert(0,str(ROOT))
from builder import quad_payload as q,mod_versions as mv,firmware

def enc(v):return (json.dumps(v,sort_keys=True)+'\n').encode()
def fixture(root):
 boot=b'#!/bin/sh\n'+mv.OTA_GATE+b'\ntrue\nfi\n'
 arm=bytearray(20);arm[:6]=b'\x7fELF\x01\x01';arm[18]=40
 files={n:(boot if n=='bootstrap.sh' else bytes(arm) if n.endswith('.so') else n.encode()) for n in q.REQUIRED if n!='runtime/source.zip'}
 sources={'owned/source.c':b'synthetic source','LICENSE':b'synthetic license'}
 archive=io.BytesIO()
 with zipfile.ZipFile(archive,'w') as z:
  for n,b in sources.items():z.writestr(n,b)
 files['licenses/nested/NOTICE']=b'owned notice'
 files['runtime/xz-updater']=b'synthetic updater'
 files['runtime/source.zip']=archive.getvalue();files['runtime/quad/contract.json']=b'{"synthetic":true}'
 payload={'autoexec.sh':'bootstrap.sh','tools/libxz-mods.so':'runtime/libxz-mods.so','tools/libxz-directfb-hook.so':'runtime/libxz-receiver.so','licenses/xz-quad-source.zip':'runtime/source.zip',**{'tools/auto-bridge/'+n:'runtime/quad/'+n for n in ('xz-auto-bridge','xz_hub_depth.ko','detach')}}
 components={name:{'resource':resource,'sha256':q.sha(files[resource]),'source_paths':['owned/source.c'],'license_paths':['LICENSE'],'evidence_refs':['synthetic-host-fixture']} for name,resource in [('runtime','runtime/libxz-mods.so'),('receiver','runtime/libxz-receiver.so'),('bridge','runtime/quad/xz-auto-bridge'),('module','runtime/quad/xz_hub_depth.ko'),('detach','runtime/quad/detach')]}
 profile={'schema':'xz-quad-package/2','firmware':'XDJ-XZ 1.26','distribution':'public-beta','profile':'experimental-quad','music_removal_supported':False,'source_commit':'2b49d1e01846fe7a44b2686382fb8c7ab5876ae6','application_sha256':q.sha(b'synthetic application'),'bootstrap_utf8':boot.decode(),'resources':{n:q.sha(b) for n,b in files.items()},'source_files':{n:q.sha(b) for n,b in sources.items()},'payload':payload,'components':components,'contract':{'path':'runtime/quad/contract.json','sha256':q.sha(files['runtime/quad/contract.json'])}}
 profile['runtime_manifest']={'ota_updater_sha256':q.sha(files['runtime/xz-updater']),'firmware':profile['firmware'],'source_commit':profile['source_commit'],**{field:q.sha(files[n]) for field,n in [('runtime_sha256','runtime/libxz-mods.so'),('receiver_sha256','runtime/libxz-receiver.so'),('bootstrap_sha256','bootstrap.sh'),('source_zip_sha256','runtime/source.zip')]}}
 raw=enc(profile);q.PROFILE_SHA256=q.sha(raw)
 manifest={'ota_updater_sha256':q.sha(files['runtime/xz-updater']),'firmware':profile['firmware'],'source_commit':profile['source_commit'],'quad_package_sha256':q.PROFILE_SHA256,**{field:q.sha(files[n]) for field,n in [('runtime_sha256','runtime/libxz-mods.so'),('receiver_sha256','runtime/libxz-receiver.so'),('bootstrap_sha256','bootstrap.sh'),('source_zip_sha256','runtime/source.zip')]}}
 files.update({'runtime/manifest.json':enc(manifest),'runtime/quad-package.json':raw})
 for n,b in files.items():p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
 return profile
class Tests(unittest.TestCase):
 def setUp(self):
  self.old_data=os.environ.get('XZ_BUILDER_DATA');self.old_pin=q.PROFILE_SHA256
  self.temp=tempfile.TemporaryDirectory(dir=WORK);self.root=Path(self.temp.name)/'resources';self.profile=fixture(self.root);os.environ['XZ_BUILDER_DATA']=str(Path(self.temp.name)/'data')
 def tearDown(self):
  self.temp.cleanup();q.PROFILE_SHA256=self.old_pin
  if self.old_data is None:os.environ.pop('XZ_BUILDER_DATA',None)
  else:os.environ['XZ_BUILDER_DATA']=self.old_data
 def fixed_bundle(self):
  p=self.root/'bootstrap.sh';boot=p.read_bytes().replace(mv.OTA_GATE,mv.PINNED_GATE);p.write_bytes(boot)
  profile=self.profile;profile['bootstrap_utf8']=boot.decode();profile['resources']['bootstrap.sh']=q.sha(boot)
  profile['runtime_manifest']['bootstrap_sha256']=q.sha(boot);profile['runtime_manifest']['ota_unavailable_reason']='Use USB setup for this build.'
  raw=enc(profile);q.PROFILE_SHA256=q.sha(raw);(self.root/'runtime/quad-package.json').write_bytes(raw)
  (self.root/'runtime/manifest.json').write_bytes(enc({**profile['runtime_manifest'],'quad_package_sha256':q.PROFILE_SHA256}))
 def test_verified_fixed_bundle_inventory_and_selection(self):
  self.fixed_bundle();state=mv.inventory(self.root);self.assertTrue(state['selected']['pinned'])
  entry=next(x for x in state['versions'] if x['id']==state['bundled_id']);self.assertFalse(entry['network_updates_supported'])
  class Job:
   def progress(self,*a):pass
   def check(self):pass
  with mv.selected_resources(self.root,{},Job()) as (view,version):
   self.assertTrue(version['pinned']);q.verified_files(view)
   (view/'mod-version.json').write_bytes(enc({**version,'pinned':False}))
   with self.assertRaises(ValueError):q.verified_files(view)
 def test_fixed_bundle_rejects_unpin_without_changing_choice(self):
  self.fixed_bundle();state=mv.inventory(self.root);path=mv._root()/'selection.json';before=path.read_bytes()
  with self.assertRaisesRegex(ValueError,'USB setup'):
   mv.choose(self.root,{'mod_version_id':state['selected']['id'],'pinned':False,'expected_revision':state['selected']['revision']})
  self.assertEqual(before,path.read_bytes())
 def test_exact_and_stage(self):
  profile,files=q.verified_files(self.root)
  tree=Path(self.temp.name)/'stage'
  out=firmware.stage_payload(tree,b'synthetic application',self.root/'runtime/libxz-mods.so',self.root/'runtime/libxz-receiver.so',self.root/'bootstrap.sh')
  self.assertTrue(set(profile['payload'])<=out.keys());self.assertEqual({p.relative_to(tree).as_posix() for p in tree.rglob('*') if p.is_file()},set(out))
  for dest,src in profile['payload'].items():self.assertEqual((tree/dest).read_bytes(),files[src])
 def test_tamper(self):
  p=self.root/'runtime/quad/detach';p.write_bytes(p.read_bytes()+b'x')
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_missing(self):
  (self.root/'runtime/quad/xz-auto-bridge').unlink()
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_wrong_application(self):
  with self.assertRaises(ValueError):q.payload_files(self.root,b'wrong',self.root/'bootstrap.sh')
 def test_wrong_runtime_path(self):
  p=Path(self.temp.name)/'other.so';p.write_bytes(b'wrong')
  with self.assertRaises(ValueError):firmware.stage_payload(Path(self.temp.name)/'stage',b'synthetic application',self.root/'runtime/libxz-mods.so',p,self.root/'bootstrap.sh')
 def test_wrong_contract(self):
  (self.root/'runtime/quad/contract.json').write_bytes(b'wrong')
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_wrong_anchor(self):
  with self.assertRaises(ValueError):q.verified_files(self.root,'0'*64)
 def test_extra_private(self):
  for name in ('runtime/music.wad','runtime/secret.key','runtime/firmware.upd','runtime/rbp'):
   p=self.root/name;p.write_bytes(b'forbidden')
   with self.assertRaises(ValueError):q.verified_files(self.root)
   p.unlink()
 def test_prohibited_names(self):
  for n in ('../x','C:/key','runtime/music.wad','runtime/firmware.upd','runtime/rbp.patched'):
   with self.assertRaises(ValueError):q.safe_name(n)
 def test_bootstrap_tamper(self):
  (self.root/'bootstrap.sh').write_bytes(b'#!/bin/sh\nchanged')
  with self.assertRaises((ValueError,FileNotFoundError)):q.verified_files(self.root)
 def test_selection_default_and_explicit(self):
  class Job:
   def progress(self,*a):pass
   def check(self):pass
  state=mv.inventory(self.root)
  for request in ({},{'mod_version_id':state['selected']['id'],'pin_mod_version':state['selected']['pinned']}):
   with mv.selected_resources(self.root,request,Job()) as (view,version):
    self.assertTrue(version['pinned']);q.verified_files(view)
    self.assertEqual((view/'licenses/nested/NOTICE').read_bytes(),b'owned notice')
    q.payload_files(view,b'synthetic application',view/'bootstrap.sh')
    (view/'bootstrap.sh').write_bytes((view/'bootstrap.sh').read_bytes()+b'# tamper\n')
    with self.assertRaises(ValueError):q.verified_files(view)
 def test_nonquad(self):
  shutil.rmtree(self.root/'runtime');(self.root/'bootstrap.sh').write_bytes(b'#!/bin/sh\ntrue\n')
  self.assertEqual(q.payload_files(self.root,b'any',self.root/'bootstrap.sh'),{})
 def test_unpinned_selection(self):
  class Job:
   def progress(self,*a):pass
   def check(self):pass
  state=mv.inventory(self.root)
  mv.choose(self.root,{'mod_version_id':state['selected']['id'],'pinned':False,'expected_revision':state['selected']['revision']})
  with mv.selected_resources(self.root,{},Job()) as (view,version):
   self.assertFalse(version['pinned']);q.verified_files(view)
 def test_profile_changes_require_new_anchor(self):
  p=self.root/'runtime/quad-package.json'
  v=json.loads(p.read_bytes());v['contract']['sha256']='0'*64;p.write_bytes(enc(v))
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_forged_pin_metadata(self):
  boot=self.root/'bootstrap.sh';original=boot.read_bytes();boot.write_bytes(original.replace(mv.OTA_GATE,mv.PINNED_GATE))
  (self.root/'mod-version.json').write_bytes(enc({'pinned':False,'base_bootstrap_sha256':q.sha(original),'bootstrap_sha256':q.sha(boot.read_bytes())}))
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_source_archive_tamper(self):
  p=self.root/'runtime/source.zip';p.write_bytes(p.read_bytes()+b'tamper')
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_normal_stage(self):
  shutil.rmtree(self.root/'runtime/quad');(self.root/'runtime/quad-package.json').unlink();(self.root/'runtime/manifest.json').unlink()
  out=firmware.stage_payload(Path(self.temp.name)/'normal',b'ordinary application',self.root/'runtime/libxz-mods.so',self.root/'runtime/libxz-receiver.so',self.root/'bootstrap.sh')
  self.assertFalse(any('auto-bridge' in n for n in out))
 def test_release_requirements_preserved(self):
  import ast
  tree=ast.parse((ROOT.parents[1]/'tools/build_release.py').read_bytes())
  function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='release_markdown')
  namespace={};exec(compile(ast.Module(body=[function],type_ignores=[]),'release-markdown','exec'),namespace)
  release={'features':['feature'],'fixes':['fix'],'changes':['change'],'requirements':['Physical acceptance remains pending','Do not remove music USB; key and stem limitations remain'],'other':'retained'}
  before=dict(release);text=namespace['release_markdown'](release)
  self.assertEqual(text,'## New features\n- feature\n\n## Bug fixes\n- fix\n\n## Other changes\n- change\n\n## Known limitations and setup\n- Physical acceptance remains pending\n- Do not remove music USB; key and stem limitations remain\n')
  self.assertEqual(release,before)
  self.assertEqual(namespace['release_markdown']({'fixes':['only']}),'## Bug fixes\n- only\n')
 def test_manifest_tamper(self):
  p=self.root/'runtime/manifest.json';manifest=json.loads(p.read_bytes());manifest['hardware_qualified']=True;p.write_bytes(enc(manifest))
  with self.assertRaises(ValueError):q.verified_files(self.root)
 def test_install(self):
  target=Path(self.temp.name)/'copy';q.install(self.root,target,q.PROFILE_SHA256);q.verified_files(target)
if __name__=='__main__':unittest.main(verbosity=2)
