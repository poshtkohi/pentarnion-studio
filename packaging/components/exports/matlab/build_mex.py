"""Isolated single-worker private MEX build, never installs or starts MATLAB."""
import subprocess,os,json,time,hashlib
from pathlib import Path
HERE=Path(__file__).resolve().parent
for script in ('transpile_runtime.py','generate_glue.py'):
    subprocess.run([os.sys.executable,str(HERE/script)],check=True)
sdk=Path(os.environ['PENTA_REBUILD_MATLAB_SDK'])
compiler=Path(os.environ['PENTA_REBUILD_COMPILER'])
output=HERE/'sdk/penta_runtime_native.mexw64'
sources=[HERE/'private'/name for name in ('mx_value.cpp','compiled_runtime.cpp','compiled_adapters.cpp','mex_entry.cpp')]
common=[str(compiler),'-std=c++20','-O0','-g0','-DMATLAB_MEX_FILE','-DMX_COMPAT_32',
     '-I'+str(sdk/'extern/include'),'-I'+str(HERE/'private')]
env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
for key in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS'):env[key]='1'
started=time.monotonic()
commands=[];exitcode=0;objects=[]
headers=b''.join(p.read_bytes() for p in sorted((HERE/'private').glob('*.hpp')))
with (HERE/'build-mex.txt').open('w',encoding='utf-8') as log:
    for source in sources:
        obj=source.with_suffix('.o');objects.append(obj)
        identity=hashlib.sha256(source.read_bytes()+headers+json.dumps(common).encode()).hexdigest()
        stamp=obj.with_suffix('.stamp')
        if obj.exists() and stamp.exists() and stamp.read_text()==identity:continue
        cmd=common+['-c',str(source),'-o',str(obj)];commands.append(cmd)
        completed=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS,timeout=180)
        if completed.returncode:exitcode=completed.returncode;break
        stamp.write_text(identity)
    if not exitcode:
        cmd=[str(compiler),'-shared','-static','-static-libgcc','-static-libstdc++',*map(str,objects),str(sdk/'extern/lib/win64/microsoft/libmex.lib'),str(sdk/'extern/lib/win64/microsoft/libmx.lib'),'-o',str(output)];commands.append(cmd)
        completed=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS,timeout=90);exitcode=completed.returncode
receipt={'exit_code':exitcode,'seconds':time.monotonic()-started,'commands':commands,'workers':1,'matlab_execution':False}
if exitcode==0:receipt.update(artifact=str(output),sha256=hashlib.sha256(output.read_bytes()).hexdigest(),bytes=output.stat().st_size)
(HERE/'build-mex.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps({key:value for key,value in receipt.items() if key!='commands'},indent=2));raise SystemExit(exitcode)
