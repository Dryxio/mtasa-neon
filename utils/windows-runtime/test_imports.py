import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('audit-imports.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def fixture(path, name, machine=0x14c, delayed=False):
    """One mapped section with a real import descriptor and terminator."""
    data = bytearray(2048)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    is64 = machine == 0x8664
    optional_size = 240 if is64 else 224
    struct.pack_into('<HH', data, 0x84, machine, 1)
    struct.pack_into('<H', data, 0x94, optional_size)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b if is64 else 0x10b)
    directory = optional + (112 if is64 else 96)
    struct.pack_into('<I', data, directory - 4, 16)
    size = 32 if delayed else 20
    struct.pack_into('<II', data, directory + (13 if delayed else 1) * 8, 0x1000, size * 2)
    struct.pack_into('<IIII', data, optional + optional_size + 8, 1024, 0x1000, 1024, 512)
    if delayed:
        struct.pack_into('<II', data, 512, 1, 0x1100)
    else:
        struct.pack_into('<I', data, 512 + 12, 0x1100)
    data[768:768 + len(name) + 1] = name.encode() + b'\0'
    path.write_bytes(data)


class ImportsTest(unittest.TestCase):
    def test_pe32_pe64_and_delayed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.dll'
            for machine in (0x14c, 0x8664):
                for delayed in (False, True):
                    fixture(path, 'MSVCR100.dll', machine, delayed)
                    self.assertEqual(audit.imports(path), (machine, ['msvcr100.dll']))

    def test_missing_dependency_and_wrong_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture(root / 'consumer.dll', 'vendor.dll')
            fixture(root / 'vendor.dll', 'KERNEL32.dll', machine=0x8664)
            policy = {'loaderAliases': {}, 'windows10Libraries': ['kernel32.dll'], 'existingPrerequisites': {}}
            result = audit.audit(root, policy)
            self.assertEqual(result['findings'][0]['dependency'], 'vendor.dll')
            fixture(root / 'vendor.dll', 'KERNEL32.dll')
            self.assertEqual(audit.audit(root, policy)['findings'], [])

    def test_vc2010_requires_exact_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture(root / 'plugin.dll', 'MSVCR100.dll')
            policy = json.loads(Path(__file__).with_name('import-policy.json').read_text())
            self.assertEqual(audit.audit(root, policy)['findings'][0]['severity'], 'error')
            fake = root / 'fake.bin'
            fake.write_bytes(b'not the approved Microsoft redistributable')
            self.assertFalse(audit.audit(root, policy, fake)['vc2010Verified'])

    def test_rejects_malformed_executables(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'broken.dll'
            path.write_bytes(b'not a PE')
            with self.assertRaises(ValueError):
                audit.imports(path)


if __name__ == '__main__':
    unittest.main()
