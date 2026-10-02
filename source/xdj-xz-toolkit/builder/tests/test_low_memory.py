"""Verify disk-backed DSP against the original numerical operations at seams and tails."""
from pathlib import Path
import tempfile
import unittest
import numpy as np
from scipy.signal import resample_poly
from builder import inference, overcue_roles, grouped_stems
from builder.tests.test_inference import write_wav
import soundfile as sf


class LowMemoryTests(unittest.TestCase):
    def test_disk_input_overlap_postprocess_is_identical(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pcm = np.random.default_rng(19).integers(-20000, 20000, (150013, 2), dtype=np.int16)
            write_wav(root/'input.wav', pcm)
            with inference.DiskBuffers(root) as work:
                mix = inference.read_source(root/'input.wav', work)
                self.assertIsInstance(mix, np.memmap)
                np.testing.assert_array_equal(mix, pcm.astype(np.float32)/32768)
                def model(chunk):return {'vocals':chunk*.3, 'drums':chunk*.2}
                targets = inference.overlap_inference(mix, model, ['vocals','drums'], 4096, 2048, work)
                self.assertTrue(all(isinstance(p, np.memmap) for p in targets.values()))
                disk = inference.postprocess(mix, targets['vocals'], targets['drums'], work)
                reference = inference.postprocess(mix, targets['vocals'], targets['drums'])
                for name in disk:
                    self.assertIsInstance(disk[name][0], np.memmap)
                    np.testing.assert_array_equal(disk[name][0], reference[name][0])
                    self.assertEqual(disk[name][1], reference[name][1])
            # Close handles even while views remain in scope; Windows cleanup must work.
            for path in root.glob('*.f32'):path.unlink()

    def test_role_resample_seams_and_short_tail_match_whole_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for count in (17, 301056-1, 301056+1, 602119):
                audio = np.random.default_rng(count).uniform(-.9,.9,(count,2)).astype(np.float32)
                part = overcue_roles.spill(audio, root/'resampled.f32')
                try:np.testing.assert_array_equal(part, resample_poly(audio,320,147,axis=0))
                finally:part._mmap.close()

    def test_grouped_resample_seams_mono_and_downsample(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for rate,channels in ((48000,1),(32000,2),(192000,2)):
                samples = np.random.default_rng(rate).uniform(-.8,.8,(200017,channels)).astype(np.float32)
                file = root/'input.wav';sf.write(file,samples,rate,subtype='FLOAT')
                mapped = grouped_stems.read_audio(file,root/'source.f32')
                from math import gcd
                factor = gcd(rate,44100)
                reference = resample_poly(samples,44100//factor,rate//factor,axis=0)
                if channels==1:reference=np.repeat(reference,2,axis=1)
                try:np.testing.assert_array_equal(mapped,reference)
                finally:mapped._mmap.close()


if __name__=='__main__':unittest.main()
