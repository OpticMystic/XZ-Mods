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

if __name__=='__main__':unittest.main()
