import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('package_release', Path(__file__).with_name('package_release.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PackagingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.original_root = module.ROOT
        module.ROOT = self.root
        (self.root/'VERSION').write_text('1.1.0-experimental.23\n')
        for name in ['README.md','CHANGELOG.md','THIRD_PARTY_NOTICES.md','docs/README.en.md','docs/KNOWN_ISSUES.md','docs/BUILDING.md','plugin/third_party/sdk/LICENSE']:
            path = self.root/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('Documentation fixture\n')
        self.dlls = self.root/'binaries'
        self.dlls.mkdir()
        # Synthetic bytes test archive policy only, not DLL loading/ABI validation.
        for name in ['d2rl-3dcam.dll','d2rl-renderdistance.dll']:
            (self.dlls/name).write_bytes(b'MZ test fixture 1.1.0-experimental.23')
        (self.dlls/'renderdistance-settings.ini').write_text('enabled=1')
        (self.dlls/'private.d2s').write_bytes(b'not for distribution')

    def tearDown(self):
        module.ROOT = self.original_root
        self.temp.cleanup()

    def test_two_editions_and_same_camera(self):
        reports = module.package(self.dlls,'all')
        cameras = []
        for report in reports:
            path = self.root/'dist'/report['archive']
            with zipfile.ZipFile(path) as z:
                cameras.append(z.read('d2rl-3dcam.dll'))
                self.assertEqual('d2rl-renderdistance.dll' in z.namelist(), report['edition']=='extended')
                self.assertFalse(any(name.endswith(('.ini','.d2s')) for name in z.namelist()))
                manifest = json.loads(z.read('manifest.json'))
                self.assertEqual(manifest['F12'],'camera only')
                self.assertFalse(manifest['distance_default_on_first_install'])
            self.assertTrue(path.with_suffix('.zip.sha256').exists())
        self.assertEqual(cameras[0],cameras[1])
        with self.assertRaises(FileExistsError):
            module.package(self.dlls,'all')

    def test_standard_does_not_require_distance(self):
        (self.dlls/'d2rl-renderdistance.dll').unlink()
        self.assertEqual(len(module.package(self.dlls,'standard')),1)

    def test_all_requires_both_before_writing(self):
        (self.dlls/'d2rl-renderdistance.dll').unlink()
        with self.assertRaises(FileNotFoundError):
            module.package(self.dlls,'all')
        self.assertFalse(list(self.root.rglob('*.zip')))

    def test_wrong_version_rejected(self):
        (self.dlls/'d2rl-renderdistance.dll').write_bytes(b'MZ old 1.0.0')
        with self.assertRaises(ValueError):
            module.package(self.dlls,'all')
        self.assertFalse(list(self.root.rglob('*.zip')))

if __name__=='__main__':
    unittest.main()
