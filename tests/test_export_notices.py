"""Licence companion checks; runtime/model bytes must remain unchanged."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
SPEC=importlib.util.spec_from_file_location('export_notices',Path(__file__).resolve().parents[1]/'tools/export-with-notices.py')
HELPER=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class ExportNoticeTests(unittest.TestCase):
    def fixture(self, root, runtime='penta_export_runtime.py'):
        bundle=root/'bundle';bundle.mkdir()
        (bundle/runtime).write_bytes(b'original runtime bytes\n')
        (bundle/'exported.py').write_bytes(b'user model statements\n')
        licence=root/'licence';licence.mkdir()
        (licence/'LICENSE').write_bytes((Path(__file__).resolve().parents[1]/'LICENSE').read_bytes())
        (licence/'NOTICE').write_text('First-party attribution\n')
        return bundle,licence

    def test_preserves_program_and_copies_exact_licence(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle,licence=self.fixture(Path(temporary))
            self.assertEqual(len(HELPER.attach(bundle,licence)),3)
            self.assertEqual((bundle/'penta_export_runtime.py').read_bytes(),b'original runtime bytes\n')
            self.assertEqual((bundle/'exported.py').read_bytes(),b'user model statements\n')
            self.assertEqual((bundle/'PENTARNION-RUNTIME-LICENSE.txt').read_bytes(),(licence/'LICENSE').read_bytes())
            self.assertEqual(HELPER.attach(bundle,licence),[])

    def test_compiled_matlab_bundle_is_supported(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle,licence=self.fixture(Path(temporary),'penta_runtime_native.mexw64')
            self.assertEqual(len(HELPER.attach(bundle,licence)),3)
            self.assertEqual((bundle/'PENTARNION-RUNTIME-NOTICE.txt').read_bytes(),(licence/'NOTICE').read_bytes())

    def test_different_notice_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle,licence=self.fixture(Path(temporary))
            target=bundle/'PENTARNION-RUNTIME-NOTICE.txt';target.write_bytes(b'existing attribution')
            with self.assertRaises(ValueError):HELPER.attach(bundle,licence)
            self.assertEqual(target.read_bytes(),b'existing attribution')
            self.assertFalse((bundle/'PENTARNION-RUNTIME-LICENSE.txt').exists())

    def test_pending_licence_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle,licence=self.fixture(Path(temporary))
            (licence/'LICENSE-PENDING.md').write_text('pending fixture')
            with self.assertRaises(ValueError):HELPER.attach(bundle,licence)
            self.assertFalse((bundle/'PENTARNION-RUNTIME-LICENSE.txt').exists())

    def test_truncated_licence_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle,licence=self.fixture(Path(temporary))
            (licence/'LICENSE').write_text('Apache License\nVersion 2.0\n')
            with self.assertRaises(ValueError):HELPER.attach(bundle,licence)
            self.assertFalse((bundle/'PENTARNION-RUNTIME-LICENSE.txt').exists())


if __name__=='__main__':unittest.main()
