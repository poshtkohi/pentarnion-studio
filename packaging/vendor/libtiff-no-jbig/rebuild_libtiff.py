"""Portable build recipe for third-party LibTIFF4.7.2 without optional JBIG.

No Pentarnion implementation is present. Requires existing matching codec
development libraries and an explicit Windows UCRT64 GCC toolchain.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tarfile,time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dependency-prefix',type=Path,required=True)
    p.add_argument('--compiler-prefix',type=Path,required=True)
    p.add_argument('--cmake',type=Path,required=True)
    p.add_argument('--ninja',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();home=Path(__file__).resolve().parent
    prefix=a.dependency_prefix.resolve(strict=True);compiler=a.compiler_prefix.resolve(strict=True)
    cmake=a.cmake.resolve(strict=True);ninja=a.ninja.resolve(strict=True);out=a.out.resolve()
    if out.exists():raise ValueError('Use a NEW output directory')
    archive=home/'sources/tiff-4.7.2.tar.gz'
    if hashlib.sha256(archive.read_bytes()).hexdigest()!='672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a':raise ValueError('Pinned source hash mismatch')
    out.mkdir(parents=True);(out/'source').mkdir();(out/'logs').mkdir();(out/'temp').mkdir()
    with tarfile.open(archive,'r:gz') as tar:tar.extractall(out/'source',filter='data')
    source=out/'source/tiff-4.7.2';build=out/'build'
    original=(source/'libtiff/Makefile.am').read_text()
    old='libtiffinclude_HEADERS = \\\n\ttiff.h \\\n\ttiffio.h \\\n\ttiffvers.h'
    new='libtiffinclude_HEADERS = \\\n\ttif_config.h \\\n\ttif_dir.h \\\n\ttif_hash_set.h \\\n\ttiff.h \\\n\ttiffio.h \\\n\ttiffiop.h \\\n\ttiffvers.h'
    if old not in original:raise ValueError('Pinned header-install patch context mismatch')
    (source/'libtiff/Makefile.am').write_text(original.replace(old,new))
    env=os.environ.copy();env['PATH']=str(prefix/'bin')+';'+str(compiler/'bin')+r';C:\Windows\System32;C:\Windows'
    env['TEMP']=env['TMP']=str(out/'temp');env['OMP_NUM_THREADS']=env['OPENBLAS_NUM_THREADS']='1'
    options={'BUILD_SHARED_LIBS':'ON','tiff-static':'OFF','tiff-tools':'OFF','tiff-tests':'OFF','tiff-contrib':'OFF','tiff-docs':'OFF',
      'tiff-cxx':'ON','jbig':'OFF','jpeg':'ON','old-jpeg':'ON','zlib':'ON','libdeflate':'ON','lzma':'ON','zstd':'ON','lerc':'ON','webp':'ON',
      'ccitt':'ON','packbits':'ON','lzw':'ON','thunder':'ON','next':'ON','logluv':'ON','mdi':'ON','pixarlog':'ON','strip-chopping':'ON',
      'extrasample-as-alpha':'ON','check-ycbcr-subsampling':'ON','win32-io':'ON','defer-strile-load':'OFF','chunky-strip-read':'OFF'}
    receipt={'component':'libtiff4.7.2-no-JBIG','worker_processes':1,'steps':[]}
    def run(command,name):
        command=list(map(str,command));start=time.monotonic()
        with (out/'logs'/(name+'.txt')).open('w',encoding='utf-8') as log:
            r=subprocess.run(command,env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,
                             creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name=='nt' else 0,timeout=300)
        receipt['steps'].append({'name':name,'command':command,'exit_code':r.returncode,'seconds':time.monotonic()-start})
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
        if r.returncode:raise RuntimeError('See '+str(out/'logs'/(name+'.txt')))
    run([cmake,'-S',source,'-B',build,'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja.as_posix(),
      '-DCMAKE_C_COMPILER='+(compiler/'bin/gcc.exe').as_posix(),'-DCMAKE_CXX_COMPILER='+(compiler/'bin/g++.exe').as_posix(),
      '-DCMAKE_RC_COMPILER='+(compiler/'bin/windres.exe').as_posix(),'-DCMAKE_PREFIX_PATH='+prefix.as_posix(),'-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_C_FLAGS_RELEASE=-O2 -pipe -fno-strict-aliasing','-DCMAKE_CXX_FLAGS_RELEASE=-O2 -pipe -fno-strict-aliasing',
      *['-D'+key+'='+value for key,value in options.items()]],'configure')
    for name in ('tiffconf.h','tif_config.h'):shutil.copyfile(home/'config'/name,build/'libtiff'/name)
    run([cmake,'--build',build,'--parallel','1','--target','tiff'],'build')
    artifact=build/'libtiff/libtiff-6.dll'
    receipt['artifact']={'path':str(artifact),'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest()}
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8');print(json.dumps(receipt['artifact'],indent=2))
if __name__=='__main__':main()
