"""Build optional standalone Windows components from the public source checkout.

No downloads, system installs, uploads, MATLAB execution or desktop tests.
All generated files go into one explicitly owned output tree outside the checkout.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, zipfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('step',choices=('prepare','bridge','python','matlab','sdk'))
    p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--compiler',type=Path)
    p.add_argument('--python-dev',type=Path)
    p.add_argument('--matlab-sdk',type=Path)
    p.add_argument('--cython-wheel',type=Path)
    a=p.parse_args();home=Path(__file__).resolve().parent
    source=a.source_root.resolve(strict=True);out=a.out.resolve()
    if out==source or source in out.parents or out in source.parents:
        p.error('Output must be outside the public source checkout.')
    marker=out/'.pentarnion-component-build'
    def digest(file):return hashlib.sha256(file.read_bytes()).hexdigest()
    if a.step=='prepare':
        if out.exists():p.error('Prepare requires a new output directory.')
        if not (source/'src/export/python_runtime.inc').is_file():p.error('Not a complete Studio source root.')
        out.mkdir(parents=True)
        marker.write_text(json.dumps({'source':str(source),'format':1})+'\n',encoding='utf-8')
        shutil.copytree(home/'components',out/'components')
        print('Prepared editable component copy; nothing compiled or downloaded.')
        return
    if not marker.is_file() or json.loads(marker.read_text(encoding='utf-8')).get('source')!=str(source):
        p.error('Run prepare first with the same source root and output directory.')
    components=out/'components';env=dict(os.environ)
    env['PENTA_SOURCE_ROOT']=str(source)
    if a.step in ('bridge','python','matlab'):
        if a.compiler is None or not a.compiler.is_file():p.error('An explicit GCC executable is required.')
        compiler=a.compiler.resolve();env['PENTA_REBUILD_COMPILER']=str(compiler)
        env['PATH']=str(compiler.parent)+os.pathsep+os.environ.get('PATH','')
        for key in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS'):env[key]='1'
    def run(command):
        subprocess.run(list(map(str,command)),env=env,cwd=out,check=True,
                       creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name=='nt' else 0)
    if a.step=='bridge':
        run([sys.executable,home/'build_native_bridge.py','--bridge-source',components,
             '--compiler',compiler,'--reference-source',source,'--out',out/'native-bridge'])
    elif a.step=='python':
        if a.python_dev is None or not a.python_dev.is_file():p.error('Provide the CPython 3.12 developer python.exe.')
        python=a.python_dev.resolve()
        if not (python.parent/'Include/Python.h').is_file() or not (python.parent/'libs/python3.lib').is_file():
            p.error('Python developer headers and stable-ABI import library are required.')
        if a.cython_wheel is None or not a.cython_wheel.is_file():p.error('Provide the pinned Cython wheel; see README.md.')
        lock=json.loads((components/'export-python/cython-wheel.json').read_text(encoding='utf-8'))
        if digest(a.cython_wheel)!=lock['sha256']:p.error('Cython wheel SHA256 mismatch.')
        vendor=components/'export-python/vendor'
        if not vendor.exists():
            with zipfile.ZipFile(a.cython_wheel) as archive:
                for item in archive.infolist():
                    name=Path(item.filename)
                    if name.is_absolute() or name.drive or '..' in name.parts or '\\' in item.filename:p.error('Unsafe wheel path.')
                archive.extractall(vendor)
        run([python,components/'export-python/build_compiled_runtime.py'])
    elif a.step=='matlab':
        if a.matlab_sdk is None or not (a.matlab_sdk/'extern/include/mex.h').is_file():p.error('External MATLAB developer SDK is required.')
        env['PENTA_REBUILD_MATLAB_SDK']=str(a.matlab_sdk.resolve())
        run([sys.executable,components/'exports/matlab/build_mex.py'])
    elif a.step=='sdk':
        sdk=out/'runtime-sdk'
        if sdk.exists():p.error('SDK output already exists; never overwrite a previous SDK.')
        inputs={'bin/penta_native_bridge.dll':out/'native-bridge/bin/penta_native_bridge.dll',
                'lib/libpenta_native_bridge.dll.a':out/'native-bridge/lib/libpenta_native_bridge.dll.a',
                'python/penta_export_runtime.pyd':components/'export-python/bin/penta_export_runtime.pyd'}
        for family in ('base','dense','dual'):
            name='native_'+family+'.hpp';inputs['include/pentarnion/'+name]=components/family/'include/pentarnion'/name
        for name in ('penta_dual.m','penta_runtime.m','penta_runtime_bind.m','penta_runtime_context.m','penta_runtime_native.mexw64'):
            inputs['matlab/'+name]=components/'exports/matlab/sdk'/name
        if any(not file.is_file() for file in inputs.values()):p.error('Build every selected SDK component first.')
        for name,file in inputs.items():
            target=sdk/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,target)
        record={'format':'Pentarnion compiled distribution SDK','version':1,'declaration_only':True,
                'abi':'GCC16.2 UCRT64 long-double16 model-ABI1 bridge-ABI1',
                'files':{name:digest(sdk/name) for name in inputs},
                'verification':{'status':'rebuilt; acceptance checks are required before release'}}
        (sdk/'distribution-sdk.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('Completed:',a.step)


if __name__=='__main__':main()
