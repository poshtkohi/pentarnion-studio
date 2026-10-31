"""Security boundary checks for the public-source audit; stdlib only."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

SPEC = importlib.util.spec_from_file_location('source_audit', Path(__file__).resolve().parents[1]/'tools/audit-public-source.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class SourceAuditTests(unittest.TestCase):
    def test_clean_text_is_inventoried(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'hello.cpp').write_text('int main() { return 0; }')
            result=AUDIT.audit(root)
            self.assertTrue(result['passed'])
            self.assertEqual(result['file_count'],1)
            self.assertEqual(result['files'][0]['path'],'hello.cpp')

    def test_secret_is_reported_by_location_without_value(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);secret='gh'+'p_'+'a'*36
            (root/'config.txt').write_text('header\n'+secret+'\n')
            result=AUDIT.audit(root)
            self.assertFalse(result['passed'])
            self.assertEqual(result['findings'][0],{'path':'config.txt','reason':'github-token','line':2})
            self.assertNotIn(secret,str(result))

    def test_binary_magic_and_private_paths_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'innocent.txt').write_bytes(b'MZ'+bytes(32))
            (root/'build').mkdir();(root/'build/cache.txt').write_text('cache')
            reasons={item['reason'] for item in AUDIT.audit(root)['findings']}
            self.assertIn('executable-magic',reasons)
            self.assertIn('private-or-generated-directory',reasons)

    def test_release_requires_confirmed_licence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'LICENSE-PENDING.md').write_text('pending')
            self.assertFalse(AUDIT.audit(root,release=True)['passed'])
            (root/'LICENSE-PENDING.md').unlink();(root/'LICENSE').write_text('test fixture only')
            self.assertTrue(AUDIT.audit(root,release=True)['passed'])

    def test_plain_text_copying_lib_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'LICENSES').mkdir()
            (root/'LICENSES/COPYING.LIB').write_text('GNU LESSER GENERAL PUBLIC LICENSE')
            self.assertTrue(AUDIT.audit(root)['passed'])
            (root/'LICENSES/COPYING.LIB').write_bytes(b'MZ'+bytes(16))
            self.assertFalse(AUDIT.audit(root)['passed'])


if __name__=='__main__':unittest.main()
