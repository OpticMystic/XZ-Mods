import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock

from builder import engines, host

CATALOG = engines.registry()
TORCH = CATALOG['torch']
CUDA_PLAN = {'device': 'cuda', 'device_name': 'NVIDIA GeForce RTX 4070 Ti SUPER', 'vram_bytes': 16376 * 1024 * 1024,
             'torch_flavour': 'cu128', 'reason': None}


def smi(driver):
    return lambda: {'device_name': 'NVIDIA GeForce RTX 4070 Ti SUPER', 'vram_bytes': 16376 * 1024 * 1024,
                    'driver_version': driver, 'cuda_api_version': None}


class AcceleratorTests(unittest.TestCase):
    def detect(self, detect, system='win32', machine='AMD64'):
        return host.detect_accelerator(TORCH, detect=detect, system=system, machine=machine)

    def test_apple_silicon_uses_mps(self):
        plan = self.detect(lambda: self.fail('Mac probed for NVIDIA'), system='darwin', machine='arm64')
        self.assertEqual((plan['device'], plan['torch_flavour'], plan['reason']), ('mps', 'default', None))

    def test_intel_mac_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'Apple Silicon'):
            self.detect(lambda: None, system='darwin', machine='x86_64')

    def test_no_gpu_runs_on_cpu(self):
        plan = self.detect(lambda: None)
        self.assertEqual((plan['device'], plan['torch_flavour']), ('cpu', 'cpu'))
        self.assertTrue(plan['reason'])

    def test_current_driver_gets_cu128(self):
        plan = self.detect(smi('616.92'))
        self.assertEqual((plan['device'], plan['torch_flavour'], plan['reason']), ('cuda', 'cu128', None))
        self.assertEqual(plan['vram_bytes'], 16376 * 1024 * 1024)
        self.assertEqual(plan['device_name'], 'NVIDIA GeForce RTX 4070 Ti SUPER')

    def test_older_driver_gets_cu126(self):
        plan = self.detect(smi('565.90'))
        self.assertEqual((plan['device'], plan['torch_flavour']), ('cuda', 'cu126'))

    def test_too_old_driver_falls_back_to_cpu(self):
        plan = self.detect(smi('550.0'))
        self.assertEqual((plan['device'], plan['torch_flavour']), ('cpu', 'cpu'))
        self.assertIn('driver', plan['reason'])

    def test_driver_api_version_selects_flavour(self):
        plan = self.detect(lambda: {'device_name': 'GPU', 'vram_bytes': 8 * 1024 ** 3,
                                    'driver_version': None, 'cuda_api_version': 12080})
        self.assertEqual((plan['device'], plan['torch_flavour']), ('cuda', 'cu128'))


class NvidiaSmiTests(unittest.TestCase):
    def query(self, returncode, stdout):
        completed = types.SimpleNamespace(returncode=returncode, stdout=stdout)
        with mock.patch('builder.host.subprocess.run', return_value=completed) as run:
            found = host.query_nvidia_smi('nvidia-smi')
        self.assertEqual(run.call_args.args[0][0], 'nvidia-smi')
        return found

    def test_parses_name_memory_and_driver(self):
        found = self.query(0, 'NVIDIA GeForce RTX 4070 Ti SUPER, 16376, 616.92\n')
        self.assertEqual(found, {'device_name': 'NVIDIA GeForce RTX 4070 Ti SUPER', 'vram_bytes': 16376 * 1024 * 1024,
                                 'driver_version': '616.92', 'cuda_api_version': None})

    def test_failed_query_returns_none(self):
        self.assertIsNone(self.query(9, 'NVIDIA GeForce RTX 4070 Ti SUPER, 16376, 616.92\n'))

    def test_garbage_returns_none(self):
        self.assertIsNone(self.query(0, 'NVIDIA-SMI has failed because it could not communicate\n'))


class TorchInstallTests(unittest.TestCase):
    def test_cuda_flavour_uses_its_index(self):
        self.assertEqual(host.torch_install(TORCH, 'cu128'),
                         ['--index-url', 'https://download.pytorch.org/whl/cu128', 'torch==2.8.0', 'torchaudio==2.8.0'])

    def test_default_flavour_has_no_index(self):
        self.assertEqual(host.torch_install(TORCH, 'default'), ['torch==2.8.0', 'torchaudio==2.8.0'])


class PlanEngineSetupTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        environment = mock.patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.root / 'data')})
        environment.start()
        self.addCleanup(environment.stop)
        self.resources = self.root / 'resources'
        self.resources.mkdir()
        self.job = object()

    def plan(self, preset, installed):
        with mock.patch('builder.engines.detect_accelerator', return_value=dict(CUDA_PLAN)), \
                mock.patch('builder.engines.installed_torch_flavour', return_value=installed) as probe:
            return engines.plan_engine_setup({'preset': preset}, self.job, self.resources), probe

    def add_uv(self):
        host.executable(self.resources, 'uv').write_bytes(b'')

    def test_cpu_runtime_on_gpu_machine_is_recreated(self):
        self.add_uv()
        plan, probe = self.plan('balanced', 'cpu')
        weights = ['MelBandRoformer.ckpt', 'f7e0c4bc-ba3fe64a.th']
        code = sum(record['bytes'] for record in CATALOG['code']['msst']['files'].values())
        expected = (host.torch_download_bytes(TORCH, 'cu128') + CATALOG['runtime_packages_bytes']
                    + sum(CATALOG['models'][name]['bytes'] for name in weights) + code)
        self.assertTrue(plan['recreate_runtime'])
        self.assertEqual(plan['torch_installed_flavour'], 'cpu')
        self.assertEqual((plan['device'], plan['torch_flavour'], plan['preset']), ('cuda', 'cu128', 'balanced'))
        self.assertEqual(plan['models_to_download'], weights)
        self.assertEqual(plan['download_bytes_estimate'], expected)
        uv, python, job = probe.call_args.args
        self.assertEqual(uv, host.executable(self.resources, 'uv'))
        self.assertEqual(python, engines.python_path(engines.data_root() / 'engine-python'))
        self.assertIs(job, self.job)

    def test_cu128_win32_wheel_size_is_counted_on_windows(self):
        if os.name != 'nt':
            self.skipTest('win32 wheel size applies to Windows')
        self.assertEqual(host.torch_download_bytes(TORCH, 'cu128'), TORCH['flavours']['cu128']['download_bytes']['win32'])

    def test_matching_runtime_only_counts_missing_models(self):
        self.add_uv()
        plan, _ = self.plan('fast', 'cu128')
        self.assertFalse(plan['recreate_runtime'])
        self.assertEqual(plan['models_to_download'], ['955717e8-8726e21a.th'])
        self.assertEqual(plan['download_bytes_estimate'], CATALOG['models']['955717e8-8726e21a.th']['bytes'])

    def test_missing_uv_plans_a_fresh_install(self):
        plan, probe = self.plan('fast', 'cpu')
        probe.assert_not_called()
        self.assertIsNone(plan['torch_installed_flavour'])
        self.assertFalse(plan['recreate_runtime'])
        self.assertEqual(plan['download_bytes_estimate'],
                         host.torch_download_bytes(TORCH, 'cu128') + CATALOG['runtime_packages_bytes']
                         + CATALOG['models']['955717e8-8726e21a.th']['bytes'])

    def test_unknown_preset_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'preset'):
            self.plan('nope', 'cpu')


class FreezeJob:
    def __init__(self, output):
        self.output, self.commands = output, []

    def run(self, argv, **kwargs):
        self.commands.append(argv)
        return self.output


class InstalledFlavourTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.python = Path(temp.name) / 'python.exe'
        self.python.write_bytes(b'')

    def flavour(self, output, python=None):
        job = FreezeJob(output)
        return engines.installed_torch_flavour('uv', python or self.python, job), job

    def test_local_label_is_the_flavour(self):
        flavour, job = self.flavour('numpy==2.2.6\ntorch==2.8.0+cu128\n')
        self.assertEqual(flavour, 'cu128')
        self.assertEqual(job.commands, [['uv', 'pip', 'freeze', '--python', self.python]])

    def test_unlabelled_torch_is_default(self):
        self.assertEqual(self.flavour('torch==2.8.0\n')[0], 'default')

    def test_no_torch_is_none(self):
        self.assertIsNone(self.flavour('numpy==2.2.6\ntorchaudio==2.8.0+cu128\n')[0])

    def test_missing_python_is_none_without_running(self):
        flavour, job = self.flavour('torch==2.8.0\n', python=self.python.with_name('absent.exe'))
        self.assertIsNone(flavour)
        self.assertEqual(job.commands, [])


if __name__ == '__main__':
    unittest.main()
