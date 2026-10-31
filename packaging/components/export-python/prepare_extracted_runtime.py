"""Reproducibly extract private compile input; never stage this .py source."""
from pathlib import Path
import hashlib,json,re,os
HERE=Path(__file__).resolve().parent
SOURCE=Path(os.environ['PENTA_SOURCE_ROOT'])/'src/export'
names=('runtime','scientific','optimize','graph','pde','geometry','mechanics','advanced_ode','hybrid','reverse')
parts=[];inputs=[]
for name in names:
    path=SOURCE/f'python_{name}.inc';data=path.read_text(encoding='utf-8')
    match=re.fullmatch(r'R"(\w+)\((.*)\)\1"\s*',data,re.S)
    if not match:raise ValueError(path)
    parts.append(match[2]);inputs.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
text=''.join(parts)
old='elif 1 in a.shape: shape=(1,len(first)) if a.shape[0]==1 else (len(first),1)'
new='elif 1 in a.shape:\n            if a.shape[0]==1: shape=(1,len(first))\n            else: shape=(len(first),1)'
if text.count(old)!=1:raise ValueError('Expected exactly one known Cython tuple inference workaround')
text=text.replace(old,new)
output=HERE/'private/penta_export_runtime.py';output.parent.mkdir(exist_ok=True)
output.write_text(text,encoding='utf-8')
(HERE/'extracted-runtime-inputs.json').write_text(json.dumps({'inputs':inputs,'transformation':'Equivalent branch expansion for Cython tuple-length inference only','source_sha256':hashlib.sha256(output.read_bytes()).hexdigest()},indent=2),encoding='utf-8')
print('Private original Python target runtime extracted reproducibly')
