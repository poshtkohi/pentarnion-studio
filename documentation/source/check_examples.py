"""Syntax/type-check every complete .p listing; run selected tiny examples only."""
from pathlib import Path
import re,subprocess,json,os,argparse
root=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--checker',type=Path,default=root.parent/'frontier-build/bin/penta-check.exe',help='Path to the matching penta-check executable')
checker=parser.parse_args().checker.resolve()
(root/'verified-examples').mkdir(exist_ok=True)
(root/'qa').mkdir(exist_ok=True)
if not checker.is_file():parser.error('penta-check was not found; supply --checker with the matching release executable')
env=dict(os.environ,OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',PENTA_THREADS='1')
results=[]
for source in ('quick-start.md','fundamentals.md'):
    for index,match in enumerate(re.finditer(r'```p\n(.*?)\n```', (root/source).read_text(encoding='utf-8'),re.S),1):
        code=match[1]+'\n';path=root/'verified-examples'/f'{Path(source).stem}-listing-{index:02d}.p';path.write_text(code,encoding='utf-8')
        # Persistence excerpts intentionally refer to the previous workflow's p.
        if source=='quick-start.md' and code.startswith('save('):path.write_text('pentarnion p=i+j;\n'+code,encoding='utf-8')
        p=subprocess.run([str(checker),str(path)],capture_output=True,text=True,env=env,timeout=20)
        results.append({'source':source,'listing':index,'exit':p.returncode,'output':p.stdout+p.stderr})
(root/'qa/listing-checks.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
for result in results:print(result['source'],result['listing'],result['exit'],result['output'][:700] if result['exit'] else '')
raise SystemExit(any(x['exit'] for x in results))
