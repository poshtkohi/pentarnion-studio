"""Small source/notice-boundary tests. No GUI, numerical solve or network use."""
from pathlib import Path
import argparse,html.parser,importlib.util,json,re,shutil,subprocess,sys,tempfile,unittest
sys.dont_write_bytecode=True

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('public_manuals',HERE/'build_public_manuals.py')
manuals=importlib.util.module_from_spec(spec);spec.loader.exec_module(manuals)

class CodeText(html.parser.HTMLParser):
    def __init__(self):super().__init__(convert_charrefs=True);self.in_code=False;self.code=[]
    def handle_starttag(self,tag,attrs):
        if tag=='pre':self.in_code=True;self.code.append('')
    def handle_endtag(self,tag):
        if tag=='pre':self.in_code=False
    def handle_data(self,data):
        if self.in_code:self.code[-1]+=data

class PublicReleaseTests(unittest.TestCase):
    source=None
    work=None
    def test_manual_code_is_preserved(self):
        for name in ('quick-start','fundamentals'):
            text=(self.source/'documentation/source'/str(name+'.md')).read_text(encoding='utf-8')
            document,counts=manuals.render(text);parser=CodeText();parser.feed(document)
            original=re.findall(r'^```[^\n]*\n(.*?)\n```',text,re.M|re.S)
            self.assertTrue(original)
            for block in original:self.assertIn(block,parser.code)
            self.assertNotIn('<script',document.lower())
            self.assertEqual(counts['equations'],len(re.findall(r'^@equation ',text,re.M)))

    def audit_fixture(self,mode):
        if True:
            directory=tempfile.mkdtemp(prefix='penta-public-notice-fixture-',dir=self.work)
            root=Path(directory);payload=root/'payload';(payload/'runtime-sdk').mkdir(parents=True)
            (payload/'runtime-sdk/distribution-sdk.json').write_text(json.dumps({'files':{}}))
            (payload/'tools').mkdir();helper=self.source/'tools/export-with-notices.py'
            data=helper.read_bytes()+(b'\n# changed\n' if mode=='changed' else b'')
            (payload/'tools/export-with-notices.py').write_bytes(data)
            if mode=='extra':(payload/'tools/unapproved.py').write_text('print("unapproved")\n')
            process=subprocess.run([sys.executable,'-B',self.source/'packaging/audit_payload.py','--payload',payload,
                '--source',self.source,'--out',root/'audit.json'],capture_output=True,timeout=30)
            result=json.loads((root/'audit.json').read_text())
            return process.returncode,result
    def test_exact_licensed_notice_helper_is_allowed(self):
        code,result=self.audit_fixture('exact');self.assertEqual(code,0);self.assertFalse(result['failures'])
    def test_changed_notice_helper_is_rejected(self):
        code,result=self.audit_fixture('changed');self.assertNotEqual(code,0)
        self.assertTrue(any('identity mismatch' in x['reason'] for x in result['failures']))
    def test_unapproved_python_file_remains_rejected(self):
        code,result=self.audit_fixture('extra');self.assertNotEqual(code,0)
        self.assertTrue(any(x['file']=='tools/unapproved.py' for x in result['failures']))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--work',type=Path,required=True);a=parser.parse_args()
    PublicReleaseTests.source=a.source.resolve();PublicReleaseTests.work=a.work.resolve();PublicReleaseTests.work.mkdir(parents=True,exist_ok=True)
    unittest.main(argv=[sys.argv[0]],verbosity=2)
