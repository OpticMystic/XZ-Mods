import importlib.util
from pathlib import Path
import tempfile
import unittest
import numpy as np
import soundfile as sf
from builder import grouped_stems

class GroupedTests(unittest.TestCase):
    def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()
    def audio(self,name,value,frames=4410,rate=44100):
        path=self.root/name;sf.write(path,np.full((frames,2),value,dtype=np.float32),rate,subtype='PCM_24');return str(path)
    def test_multiple_files_sum_into_explicit_groups(self):
        mix=self.audio('mix.wav',.5);v1=self.audio('lead.wav',.1);v2=self.audio('backing.wav',.05);drums=self.audio('drums.flac',.2);harm=self.audio('keys.wav',.1)
        result=grouped_stems.combine({'mix':mix,'files':[{'path':v1,'group':'vocals'},{'path':v2,'group':'vocals'},{'path':drums,'group':'drums'},{'path':harm,'group':'harmonics'}]},self.root/'out')
        def value(role):return np.fromfile(self.root/'out/roles'/(role+'.s16le'),dtype='<i2').reshape(-1,2)[100:-100].mean()/32767/result['headroom_gain']
        self.assertAlmostEqual(value('vocal'),.15,places=3)
        self.assertAlmostEqual(value('drums'),.2,places=3) # not the .25 residual of mix - other groups
        self.assertAlmostEqual(value('instrumental'),.3,places=3)
        self.assertAlmostEqual(value('full-mix'),.5,places=3)
        self.assertEqual(len(result['roles']),7)
    def test_length_mismatch_requires_explicit_adjustment(self):
        request={'mix':self.audio('mix.wav',.3),'files':[{'path':self.audio('short.wav',.1,2205),'group':'drums'}]}
        with self.assertRaisesRegex(ValueError,'duration does not match'):grouped_stems.combine(request,self.root/'bad')
        result=grouped_stems.combine({**request,'fit_length':True},self.root/'good')
        drums=np.fromfile(self.root/'good/roles/drums.s16le',dtype='<i2')
        self.assertGreater(abs(drums[:1000]).mean(),100)
        self.assertEqual(int(abs(drums[-1000:]).max()),0)
        self.assertEqual(result['source_frames'],4410)
    def test_mono_and_sample_rate_are_converted(self):
        mix=self.audio('mix.wav',.3);path=self.root/'mono.wav';sf.write(path,np.full(4800,.1),48000)
        result=grouped_stems.combine({'mix':mix,'files':[{'path':str(path),'group':'harmonics'}]},self.root/'out')
        self.assertEqual(result['source_frames'],4410)

    @unittest.skipUnless('MP3' in sf.available_formats(),'libsndfile here has no MP3 support')
    def test_mp3_decodes_sample_exactly_across_blocks(self):
        from scipy.signal import resample_poly
        for rate in (44100,48000):
            t=np.arange(rate*3)/rate;tone=(.3*np.sin(2*np.pi*55*t)*(t>.025)).astype(np.float32)
            path=self.root/f'stem-{rate}.mp3';sf.write(path,np.stack([tone,tone],1),rate,format='MP3',subtype='MPEG_LAYER_III')
            whole,_=sf.read(path,dtype='float32',always_2d=True)
            expected=whole if rate==44100 else resample_poly(whole,147,160,axis=0)
            audio=grouped_stems.read_audio(str(path),self.root/f'stem-{rate}.f32')
            self.assertEqual(len(audio),len(expected))
            self.assertLess(float(np.max(np.abs(audio[2000:-2000]-expected[2000:-2000]))),1e-4)
            audio._mmap.close()
            self.assertEqual(sorted(p.name for p in self.root.glob(f'stem-{rate}.*')),[f'stem-{rate}.f32',f'stem-{rate}.mp3'])
    def role(self,folder,name,gain):return np.fromfile(self.root/folder/'roles'/(name+'.s16le'),dtype='<i2').reshape(-1,2)[100:-100].mean()/32767/gain
    def delayed(self,name,value,delay,frames=4410):
        path=self.root/name;audio=np.concatenate([np.zeros((delay,2)),np.full((frames,2),value)]).astype(np.float32)
        sf.write(path,audio,44100,subtype='PCM_24');return str(path)
    def test_alignment_shifts_and_scales_before_grouping(self):
        mix=self.audio('mix.wav',.5)
        files=[{'path':self.delayed('v.wav',.075,441),'group':'vocals'},{'path':self.delayed('d.wav',.1,441),'group':'drums'},
               {'path':self.delayed('h.wav',.075,441),'group':'harmonics'}]
        result=grouped_stems.combine({'mix':mix,'files':files,'alignment':{'offset_samples':441,'gain':2.0},'require':'aligned'},self.root/'out')
        self.assertAlmostEqual(self.role('out','vocal',result['headroom_gain']),.15,places=3)
        self.assertAlmostEqual(self.role('out','drums',result['headroom_gain']),.2,places=3)
        self.assertEqual(result['check']['verdict'],'aligned')
        self.assertEqual(result['check']['offset_samples'],441)
        self.assertLess(result['check']['residual_db'],-40)
    def test_require_refuses_stems_that_do_not_belong(self):
        rng=np.random.default_rng(5)
        mix=self.root/'mix.wav';sf.write(mix,rng.uniform(-.3,.3,(4410,2)),44100,subtype='PCM_16')
        other=self.root/'other.wav';sf.write(other,rng.uniform(-.3,.3,(4410,2)),44100,subtype='PCM_16')
        request={'mix':str(mix),'files':[{'path':str(other),'group':'drums'}],'alignment':{'offset_samples':0,'gain':1.0},'require':'weak'}
        with self.assertRaisesRegex(ValueError,'do not match this track'):grouped_stems.combine(request,self.root/'bad')
    def test_instrumental_waits_for_the_drum_split(self):
        mix=self.audio('mix.wav',.5);vocals=self.audio('vocals.wav',.2);inst=self.audio('inst.wav',.3)
        state=grouped_stems.combine({'mix':mix,'files':[{'path':vocals,'group':'vocals'},{'path':inst,'group':'instrumental'}]},self.root/'out')
        self.assertTrue(state['needs_drum_split'])
        with self.assertRaisesRegex(ValueError,'needs drum separation'):grouped_stems.render_groups(self.root/'out')
        stored,_=sf.read(self.root/'out/instrumental.wav',dtype='float32')
        split={}
        for name,share in (('drums',.4),('harmonics',.6)):
            path=self.root/(name+'-split.wav');sf.write(path,stored*share,44100,subtype='PCM_16')
            split[name]={'path':str(path),'gain':32768/32767}
        result=grouped_stems.render_groups(self.root/'out',split)
        self.assertAlmostEqual(self.role('out','drums',result['headroom_gain']),.12,places=3)
        self.assertAlmostEqual(self.role('out','harmonics',result['headroom_gain']),.18,places=3)
        self.assertAlmostEqual(self.role('out','instrumental',result['headroom_gain']),.3,places=3)
        self.assertAlmostEqual(self.role('out','vocal',result['headroom_gain']),.2,places=3)
    def test_instrumental_cannot_join_separate_parts(self):
        request={'mix':self.audio('mix.wav',.5),'files':[{'path':self.audio('i.wav',.3),'group':'instrumental'},{'path':self.audio('d.wav',.1),'group':'drums'}]}
        with self.assertRaisesRegex(ValueError,'already contains drums'):grouped_stems.combine(request,self.root/'out')
    def test_plan_reports_lengths_and_alignment(self):
        mix=self.root/'mix.wav';rng=np.random.default_rng(9);audio=rng.uniform(-.3,.3,(44100,2)).astype(np.float32)
        sf.write(mix,audio,44100,subtype='FLOAT')
        late=self.root/'late.wav';sf.write(late,np.concatenate([np.zeros((300,2),np.float32),audio*.5]),44100,subtype='FLOAT')
        result=grouped_stems.plan({'mix':str(mix),'files':[str(late)]},self.root/'plan')
        self.assertEqual(result['files'][0]['frames'],44400)
        self.assertEqual(result['alignment']['offset_samples'],300)
        self.assertAlmostEqual(result['alignment']['gain'],2.0,places=4)
        self.assertEqual(result['alignment']['verdict'],'aligned')
        self.assertEqual(sorted(path.name for path in (self.root/'plan').iterdir()),['result.json'])

if __name__=='__main__':unittest.main()
