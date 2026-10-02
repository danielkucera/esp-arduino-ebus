"""Regression checks for the SDK/firmware gate used by the WSS workflow."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/check_wss_build.py'
spec = importlib.util.spec_from_file_location('wss_build_check', SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class BuildCheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / '.pio/build/esp32-c3-internal'
        (self.build / 'config').mkdir(parents=True)
        self.header = self.build / 'config/sdkconfig.h'
        self.settings = {key: '1' for key in checker.REQUIRED}
        self.write_header()
        (self.root / 'min_spiffs.csv').write_text(
            'app0,app,ota_0,0x10000,0x1E0000,\n'
            'app1,app,ota_1,0x1F0000,0x1E0000,\n')
        self.firmware = self.build / 'firmware.bin'
        self.firmware.write_bytes(b'\xe9wss-r2' + bytes(1017))
        env = patch.dict(os.environ, {'GITHUB_SHA': 'test-source'})
        env.start()
        self.addCleanup(env.stop)

    def write_header(self):
        self.header.write_text(''.join(f'#define {k} {v}\n' for k, v in self.settings.items()))

    def test_valid_image_reports_exact_build(self):
        report = checker.validate(self.root)
        self.assertEqual(report['patch'], 'wss-r2')
        self.assertEqual(report['source_commit'], 'test-source')
        self.assertEqual(report['firmware_bytes'], 1024)
        self.assertFalse(report['hardware_tested'])

    def test_every_required_flag_is_enforced(self):
        for key in checker.REQUIRED:
            with self.subTest(key=key):
                self.settings[key] = '0'
                self.write_header()
                with self.assertRaisesRegex(ValueError, key):
                    checker.validate(self.root)
                self.settings[key] = '1'

    def test_every_forbidden_flag_is_enforced(self):
        for key in checker.FORBIDDEN:
            with self.subTest(key=key):
                self.settings[key] = '1'
                self.write_header()
                with self.assertRaisesRegex(ValueError, key):
                    checker.validate(self.root)
                del self.settings[key]

    def test_missing_sdk_header(self):
        self.header.unlink()
        with self.assertRaisesRegex(ValueError, 'configuration is missing'):
            checker.validate(self.root)

    def test_rejects_old_patch_marker(self):
        self.firmware.write_bytes(b'\xe9wss-r1' + bytes(1017))
        with self.assertRaisesRegex(ValueError, 'marker'):
            checker.validate(self.root)

    def test_rejects_invalid_image_magic(self):
        self.firmware.write_bytes(b'\x00wss-r2' + bytes(1017))
        with self.assertRaisesRegex(ValueError, 'ESP application image'):
            checker.validate(self.root)

    def test_rejects_truncated_image(self):
        self.firmware.write_bytes(b'\xe9wss-r2')
        with self.assertRaisesRegex(ValueError, 'truncated'):
            checker.validate(self.root)

    def test_rejects_image_larger_than_ota_slot(self):
        self.firmware.write_bytes(b'\xe9wss-r2' + bytes(0x1E0000))
        with self.assertRaisesRegex(ValueError, 'exceeds OTA slot'):
            checker.validate(self.root)

    def test_rejects_unknown_partition_layout(self):
        (self.root / 'min_spiffs.csv').write_text('app0,app,ota_0,0x10000,0x1E0000,\n')
        with self.assertRaisesRegex(ValueError, 'two OTA'):
            checker.validate(self.root)


if __name__ == '__main__':
    unittest.main()
