from pathlib import Path
import hashlib,json,os,subprocess,sys,time
base=Path(__file__).resolve().parent
import runpy
runpy.run_path(str(base/'prepare_extracted_runtime.py'),run_name='__main__')
sys.path.insert(0,str(base/'vendor'))
from Cython.Compiler.Main import CompilationOptions,compile as cython_compile,default_options

source=base/'private/penta_export_runtime.py'
cpp=base/'private/penta_export_runtime.cpp'
binary=base/'bin';binary.mkdir(exist_ok=True)
module=binary/'penta_export_runtime.pyd'
start=time.monotonic()
options=CompilationOptions(default_options,output_file=str(cpp),cplus=True,
    compiler_directives=dict(language_level=3,annotation_typing=False,infer_types=False,
                            binding=True,emit_code_comments=False,embedsignature=False))
result=cython_compile(str(source),options=options)
if result.num_errors:raise SystemExit('Cython runtime compilation errors: '+str(result.num_errors))
cython_seconds=round(time.monotonic()-start,3)
compiler=Path(os.environ['PENTA_REBUILD_COMPILER'])
python=Path(sys.prefix)
command=[str(compiler),'-std=c++20','-O2','-g0','-shared','-DPy_LIMITED_API=0x03080000',
         '-static-libgcc','-static-libstdc++','-I'+str(python/'Include'),str(cpp),
         str(python/'libs/python3.lib'),'-o',str(module)]
env=os.environ.copy();env['PATH']=str(compiler.parent)+r';C:\Windows\System32;C:\Windows'
env['OMP_NUM_THREADS']=env['OPENBLAS_NUM_THREADS']='1';env['TEMP']=env['TMP']=str(base/'private')
start=time.monotonic()
with (base/'compile-runtime.txt').open('w',encoding='utf-8') as output:
    process=subprocess.run(command,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=300,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
receipt=dict(cython_version=__import__('Cython').__version__,cython_seconds=cython_seconds,
    compile_seconds=round(time.monotonic()-start,3),exit_code=process.returncode,command=command,
    minimum_cpython='3.8',stable_abi=True,python_library='python3.dll',source_bytes=source.stat().st_size)
if process.returncode==0:receipt.update(module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),module_bytes=module.stat().st_size)
(base/'compiled-runtime-build.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,indent=2),flush=True)
if process.returncode:print('\n'.join((base/'compile-runtime.txt').read_text(encoding='utf-8').splitlines()[-55:]),flush=True)
raise SystemExit(process.returncode)
