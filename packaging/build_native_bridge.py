"""Build the private compiled bridge from an explicit, copied source tree.

This developer-only script never installs anything. It writes only a new output
directory, uses one compiler process at a time and never reuses old proof EXEs.
"""
from __future__ import annotations
import argparse,datetime,hashlib,json,os,re,shutil,subprocess,sys,time
from pathlib import Path


def sha(path:Path)->str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def raw_support(path:Path)->str:
    source=path.read_text(encoding='utf-8-sig')
    opening=re.search(r'R"([^\s()]*)\(',source)
    if opening:
        closing=')'+opening[1]+'"'
        position=source.find(closing,opening.end())
        if position<0:raise ValueError('Unterminated original native support: '+str(path))
        source=source[opening.end():position]
    # Independent original implementations live beside, rather than inside,
    # the generated implementation namespace when compiled in each parity EXE.
    return re.sub(r'\bpenta_generated\b','penta_reference',source)


def original_headers(framework:Path)->tuple[str,str,list[Path]]:
    source=framework/'src' if (framework/'src').is_dir() else framework
    inputs=[source/'ode/native_support.hpp']+[source/'native_functions'/name for name in
            ('callback_support.hpp','third_support.hpp','universal_support.hpp',
             'native_array_support.hpp','native_tensor_support.hpp')]
    for path in inputs:
        if not path.is_file():raise FileNotFoundError('Missing original reference source: '+str(path))
    prefix='#pragma once\n#include "pentarnion/native_base.hpp"\n#include <string>\n#include <vector>\n#include <type_traits>\n'
    parts=[raw_support(path) for path in inputs]
    return prefix+'\n'.join(parts[:3]),prefix+'\n'.join(parts),inputs


def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge-source',type=Path,required=True,
                        help='Copied private tree containing base/, dense/, dual/.')
    parser.add_argument('--compiler',type=Path,required=True,
                        help='Explicit pinned UCRT64 g++.exe path; GCC16.2 was validated.')
    parser.add_argument('--out',type=Path,required=True,help='New output directory; must not exist.')
    parser.add_argument('--reference-source',type=Path,
                        help='Optional original framework root (or src/) for fresh three-family parity.')
    args=parser.parse_args()
    bridge=args.bridge_source.expanduser().resolve(strict=True)
    compiler=args.compiler.expanduser().resolve(strict=True)
    output=args.out.expanduser().resolve()
    reference=args.reference_source.expanduser().resolve(strict=True) if args.reference_source else None
    if not bridge.is_dir() or not compiler.is_file():raise ValueError('Explicit source directory and compiler file are required.')
    if output.exists():raise ValueError('Output must be new and must not already exist: '+str(output))
    if bridge==output or bridge in output.parents or output in bridge.parents:
        raise ValueError('Output must be outside the private bridge source tree.')
    if reference and (reference==output or reference in output.parents or output in reference.parents):
        raise ValueError('Output must be outside the original reference source tree.')
    essential=[bridge/'base/include/pentarnion/native_base.hpp',
               bridge/'dense/include/pentarnion/native_dense.hpp',
               bridge/'dual/include/pentarnion/native_dual.hpp',
               bridge/'dual/private/native_base_with_dual.cpp',
               bridge/'dense/private/native_dense.cpp',
               bridge/'dense/private/universal_dynamic.inc']
    for path in essential:
        if not path.is_file():raise FileNotFoundError('Missing final private bridge input: '+str(path))
    sources={str(path):sha(path) for path in essential}
    proof_sources=[]
    if reference:
        for family,filename in [('base','base_parity.cpp'),('dense','dense_parity.cpp'),('dual','dual_parity.cpp')]:
            path=bridge/family/'tests'/filename
            if not path.is_file():raise FileNotFoundError('Missing private parity test: '+str(path))
            proof_sources.append(path)
        proof_sources.append(bridge/'dense/model_forwarding.cpp.inc')
        if not proof_sources[-1].is_file():raise FileNotFoundError(proof_sources[-1])
        base_reference,dense_reference,original_inputs=original_headers(reference)
        sources.update({str(path):sha(path) for path in proof_sources+original_inputs})
    output.mkdir(parents=True)
    for name in ('bin','lib','logs','temp'):(output/name).mkdir()
    flags=['-std=c++20','-O2','-g0','-Wall','-Wextra','-Werror']
    includes=['-I'+str(bridge/family/'include') for family in ('base','dense','dual')]
    environment=dict(os.environ)
    windows=Path(environment.get('SystemRoot',r'C:\Windows')).resolve()
    environment['PATH']=os.pathsep.join(map(str,[output/'bin',compiler.parent,windows/'System32',windows]))
    environment['TEMP']=environment['TMP']=str(output/'temp')
    for name in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMEXPR_NUM_THREADS'):
        environment[name]='1'
    receipt={'format':'Pentarnion portable clean private bridge build','started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'bridge_source':str(bridge),'compiler':str(compiler),'compiler_sha256':sha(compiler),
             'reference_source':str(reference) if reference else None,'output':str(output),
             'input_sha256':sources,'compiler_workers':1,'priority':'BelowNormal' if os.name=='nt' else 'normal',
             'desktop_tests':False,'reused_proof_executables':False,'steps':[],'artifacts':{},'status':'building'}
    def save():
        (output/'build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    def run(command,label,timeout=300):
        command=list(map(str,command));started=time.monotonic()
        process=subprocess.run(command,cwd=output,env=environment,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                               text=True,encoding='utf-8',errors='replace',timeout=timeout,
                               creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name=='nt' else 0)
        log=output/'logs'/(label+'.txt');log.write_text(process.stdout,encoding='utf-8')
        receipt['steps'].append({'name':label,'command':command,'seconds':round(time.monotonic()-started,3),
                                 'exit_code':process.returncode,'log':str(log)})
        save();print(f'{label}: exit={process.returncode}, {receipt["steps"][-1]["seconds"]} seconds',flush=True)
        if process.returncode:raise RuntimeError('Command failed; inspect '+str(log))
        return process.stdout
    started=time.monotonic();save()
    try:
        receipt['compiler_version']=run([compiler,'--version'],'compiler-version',30).splitlines()[0]
        library=output/'lib/libpenta_native_bridge.dll.a';dll=output/'bin/penta_native_bridge.dll'
        # These are the final dual/build_proof.py compilation flags. -Xlinker
        # passes the same import-library option while supporting commas in paths.
        run([compiler,*flags,'-shared','-fvisibility=hidden','-DPENTA_NATIVE_BRIDGE_BUILD',*includes,
             bridge/'dual/private/native_base_with_dual.cpp',bridge/'dense/private/native_dense.cpp',
             '-Xlinker','--out-implib','-Xlinker',library,'-o',dll],'compile-bridge')
        for path in (dll,library):receipt['artifacts'][path.relative_to(output).as_posix()]={'sha256':sha(path),'bytes':path.stat().st_size}
        if reference:
            proof=output/'private-proof';proof.mkdir()
            for family,filename in [('base','base_parity.cpp'),('dense','dense_parity.cpp'),('dual','dual_parity.cpp')]:
                target=proof/family/'tests';target.mkdir(parents=True)
                shutil.copyfile(bridge/family/'tests'/filename,target/filename)
                header='original_dense.hpp' if family=='dense' else 'original_reference.hpp'
                (target/header).write_text(dense_reference if family=='dense' else base_reference,encoding='utf-8')
                if family=='dense':shutil.copyfile(bridge/'dense/model_forwarding.cpp.inc',target.parent/'model_forwarding.cpp.inc')
                executable=output/'bin'/(family+'_parity.exe')
                run([compiler,*flags,'-Wno-misleading-indentation',*includes,target/filename,library,'-o',executable],
                    'compile-'+family+'-parity')
                text=run([executable],family+'-parity',60)
                found=re.search(r'(?m)^(\d+)[^\n]*\bchecks,\s*(\d+)\s+failures',text)
                if not found or int(found[2]):raise RuntimeError('Missing successful parity summary for '+family)
                receipt.setdefault('parity',{})[family]={'checks':int(found[1]),'failures':int(found[2])}
                receipt['artifacts'][executable.relative_to(output).as_posix()]={'sha256':sha(executable),'bytes':executable.stat().st_size}
        if any(sha(Path(path))!=expected for path,expected in sources.items()):
            raise RuntimeError('A read-only build input changed during the build.')
        receipt['status']='complete';receipt['seconds']=round(time.monotonic()-started,3);save()
        print(json.dumps({'status':receipt['status'],'output':str(output),'parity':receipt.get('parity',{}),
                          'receipt':str(output/'build-receipt.json')},indent=2),flush=True)
        return 0
    except Exception as failure:
        receipt['status']='failed';receipt['error']=str(failure);receipt['seconds']=round(time.monotonic()-started,3);save()
        print(str(failure),file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
