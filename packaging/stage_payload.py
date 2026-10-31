"""Stage an end-user installation, never the private development checkout.

Inputs are explicit. This script refuses existing output and all reparse inputs.
The compiler package is third-party source/runtime; the application SDK must be
the separately built declaration-only compiled distribution SDK.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, stat, zipfile
from pe_imports import pe_imports

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda:stream.read(1024*1024),b''): h.update(data)
    return h.hexdigest()

def regular(path):
    info=path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
        raise RuntimeError(f'Reparse input is not permitted: {path}')

def copy_file(source,target):
    regular(source)
    if not source.is_file(): raise RuntimeError(f'Missing regular file: {source}')
    target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,target)

def tree(source,target,accept=lambda p:True):
    regular(source)
    for current,dirs,files in os.walk(source,followlinks=False):
        current=Path(current)
        for name in dirs: regular(current/name)
        for name in files:
            original=current/name
            if accept(original.relative_to(source)):copy_file(original,target/original.relative_to(source))

def stage_gcc(gcc,target):
    """Preserve the compiler's relative prefix; no shell/system tools required."""
    for folder in ('lib/gcc','x86_64-w64-mingw32'):tree(gcc/folder,target/folder)
    tree(gcc/'include',target/'include',lambda p:len(p.parts)==1 or p.parts[0] in ('c++','sys','sec_api','psdk_inc','ddk','wrl','gdiplus'))
    # MinGW's Windows import libraries (kernel32, user32, etc), linker support
    # objects and GCC runtime import libraries live at this top-level prefix.
    package_names={'crt','winpthreads','windows-default-manifest','libwinpthread','gcc','gcc-libs','libstdc++','libgcc','libatomic','libgomp','libquadmath'}
    database=gcc.parent/'var/lib/pacman/local';selected_packages=[];top_level=set()
    for package in database.iterdir():
        if not package.is_dir() or not (package/'desc').is_file() or not (package/'files').is_file():continue
        fields=(package/'desc').read_text(encoding='utf-8').splitlines();name=fields[fields.index('%NAME%')+1]
        prefix='mingw-w64-ucrt-x86_64-'
        if not name.startswith(prefix) or name[len(prefix):] not in package_names:continue
        selected_packages.append(package.name)
        for entry in (package/'files').read_text(encoding='utf-8').splitlines():
            relative=Path(entry)
            if len(relative.parts)==3 and relative.parts[:2]==(gcc.name,'lib') and relative.suffix.lower() in ('.a','.o'):
                copy_file(gcc.parent/relative,target/'lib'/relative.name);top_level.add(relative.name)
    if not {'libkernel32.a','libuser32.a','default-manifest.o'}.issubset(top_level):raise RuntimeError('Incomplete exact MinGW CRT/import-library package metadata.')
    (target/'toolchain-package-inputs.json').write_text(json.dumps({'packages':sorted(selected_packages),'top_level_libraries':sorted(top_level)},indent=2),encoding='utf-8')
    tools=('g++.exe','gcc.exe','as.exe','ld.exe','ld.bfd.exe','ar.exe','ranlib.exe','nm.exe','objdump.exe','strip.exe','windres.exe','dlltool.exe')
    pending=[]
    for name in tools:copy_file(gcc/'bin'/name,target/'bin'/name);pending.append(gcc/'bin'/name)
    for name in ('libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll','libgomp-1.dll','libatomic-1.dll'):
        if (gcc/'bin'/name).is_file():copy_file(gcc/'bin'/name,target/'bin'/name);pending.append(gcc/'bin'/name)
    pending.extend((target/'lib/gcc').rglob('*.exe'));seen=set()
    while pending:
        item=pending.pop()
        for name in pe_imports(item):
            key=name.lower()
            if key in seen:continue
            seen.add(key);original=gcc/'bin'/name
            if original.is_file():copy_file(original,target/'bin'/name);pending.append(original)
    (target/'.pentarnion-owned-toolchain-staging').write_text('PentarnionToolchainStaging-v1\n',encoding='utf-8')
    from patch_toolchain_manifests import patch_tree
    patch_tree(target)

def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True)
    p.add_argument('--bin',type=Path,required=True);p.add_argument('--runtime-bin',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--compiler',type=Path,required=True);p.add_argument('--build-tools',type=Path,required=True);p.add_argument('--documents',type=Path,required=True)
    p.add_argument('--python-runtime',type=Path,required=True)
    p.add_argument('--runtime-overrides',type=Path)
    p.add_argument('--source-archive',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    source=a.source.resolve();binary=a.bin.resolve();sdk=a.sdk.resolve();out=a.out.resolve()
    if out.exists() or out==source or out in source.parents:raise RuntimeError('Output must be a new directory outside the source root.')
    manifest=json.loads((sdk/'distribution-sdk.json').read_text(encoding='utf-8-sig'))
    if manifest.get('format')!='Pentarnion compiled distribution SDK' or manifest.get('declaration_only') is not True:
        raise RuntimeError('A verified compiled declaration-only distribution SDK is required.')
    out.mkdir(parents=True)
    for name in ('pentarnion-studio.exe','penta-run.exe','penta-check.exe','penta-export.exe'):
        copy_file(binary/name,out/'bin'/name)
    for original in binary.glob('*.dll'):
        if original.name.lower()!='qt6test.dll':copy_file(original,out/'bin'/original.name)
    runtime=a.runtime_bin.resolve()
    for name in ('platforms','imageformats','iconengines','tls','licenses'):
        if (runtime/name).is_dir():tree(runtime/name,out/'bin'/name)
    tree(binary/'modules',out/'bin/modules',lambda p:p.suffix.lower()=='.dll')
    # Build trees need not have deployment DLLs. Resolve only their actual PE
    # imports from explicit authoritative vendor deployments; never borrow an
    # older first-party executable/module from the canonical working checkout.
    search_roots=[]
    if a.runtime_overrides:search_roots.append(('reviewed-runtime-override',a.runtime_overrides.resolve()))
    search_roots.extend([('deployed-runtime',runtime),('pinned-dependencies',a.build_tools.resolve()/'bin'),('compiler-runtime',a.compiler.resolve()/'bin')])
    pending=list((out/'bin').rglob('*.exe'))+list((out/'bin').rglob('*.dll'));scanned=set();runtime_inputs=[]
    while pending:
        item=pending.pop();key=str(item).lower()
        if key in scanned:continue
        scanned.add(key)
        for name in pe_imports(item):
            if (out/'bin'/name).is_file():continue
            found=next(((role,root/name) for role,root in search_roots if (root/name).is_file()),None)
            if found:
                role,original=found;copy_file(original,out/'bin'/name);pending.append(out/'bin'/name);runtime_inputs.append({'name':name,'source_role':role,'sha256':sha(original)})
    (out/'provenance').mkdir(parents=True,exist_ok=True)
    (out/'provenance/deployed-runtime-inputs.json').write_text(json.dumps(runtime_inputs,indent=2),encoding='utf-8')
    for relative,expected in manifest['files'].items():
        path=Path(relative)
        if path.is_absolute() or '..' in path.parts:raise RuntimeError('SDK manifest escaped its root.')
        if sha(sdk/path)!=expected:raise RuntimeError(f'SDK hash mismatch: {path}')
        copy_file(sdk/path,out/'runtime-sdk'/path)
    copy_file(sdk/'distribution-sdk.json',out/'runtime-sdk/distribution-sdk.json')
    copy_file(sdk/'bin/penta_native_bridge.dll',out/'bin/penta_native_bridge.dll')
    if not (a.python_runtime/'python-dependencies.lock.json').is_file():raise RuntimeError('Pinned private Python runtime is required.')
    tree(a.python_runtime,out/'toolchain/python',lambda p:'__pycache__' not in p.parts and p.suffix.lower()!='.pyc')
    # Preserve GCC's relative prefix layout so cc1plus, headers and linker work
    # without C:/msys64, a shell, a registry entry or developer PATH.
    gcc=a.compiler.resolve();target=out/'toolchain/ucrt64'
    stage_gcc(gcc,target)
    build_tools=a.build_tools.resolve()
    for name in ('cmake.exe','ninja.exe'):
        copy_file(build_tools/'bin'/name,out/'toolchain/bin'/name)
    tree(build_tools/'share/cmake',out/'toolchain/share/cmake')
    pending=list((out/'toolchain/bin').glob('*.exe'));seen=set()
    while pending:
        item=pending.pop()
        for name in pe_imports(item):
            if name.lower() in seen:continue
            seen.add(name.lower());original=build_tools/'bin'/name
            if original.is_file():copy_file(original,out/'toolchain/bin'/name);pending.append(original)
    # No IDE/compiler/numerical implementation files are copied as help content.
    help_root=out/'share/pentarnion-studio'
    for name in ('language.txt','FIGURE_GUIDE.txt','ODE_GUIDE.txt','MATRIX_GUIDE.txt','FOUNDATIONS_GUIDE.txt','NATIVE_ALGEBRAS_GUIDE.txt','RESEARCH_GUIDE.txt','PDE_GEOMETRY_GUIDE.txt','ADVANCED_NUMERICS_GUIDE.txt','FRONTIER_GUIDE.txt'):
        if (source/name).is_file():copy_file(source/name,help_root/name)
    tree(source/'specs',help_root/'specs',lambda p:p.suffix.lower()=='.txt')
    for name in ('STUDIO_GUI_GUIDE.txt','STUDIO_HELP_AND_EDITOR.txt','STUDIO_SNAPSHOT_AND_WORKERS.txt'):
        copy_file(source/'docs'/name,help_root/'docs'/name)
    # Public builds use the checked algebra contracts, not manuscript excerpts.
    for name in ('TESTED_PLATFORMS.md','PREPARATION_RESULTS.md','REDISTRIBUTION.md'):
        copy_file(source/'docs'/name,help_root/'docs'/name)
    tree(source/'assets/branding',help_root/'assets/branding',lambda p:p.suffix.lower() in ('.png','.svg','.ico','.txt'))
    allowed_examples={'.p','.pprj','.txt','.md','.csv','.tsv','.json','.xlsx','.pmesh','.dat'}
    # Read tracked examples from the frozen source archive, never mutable main
    # checkout examples or side effects from a validation run in staging.
    example_inventory=[]
    with zipfile.ZipFile(a.source_archive) as archive:
        for entry in archive.infolist():
            path=Path(entry.filename)
            if entry.is_dir() or not path.parts or path.parts[0]!='examples':continue
            if path.is_absolute() or '..' in path.parts:raise RuntimeError('Example archive path escaped the root.')
            if path.suffix.lower() not in allowed_examples or any(x.startswith('.') or x in ('build','generated','output','outputs','__pycache__') for x in path.parts):continue
            data=archive.read(entry);dest=help_root/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
            example_inventory.append({'path':path.as_posix(),'sha256':hashlib.sha256(data).hexdigest()})
    if not example_inventory:raise RuntimeError('No tracked examples found in source archive.')
    documents=('Pentarnion-Studio-Quick-Start.docx','Pentarnion-Studio-Quick-Start.pdf','Pentarnion-Theory-and-Programming-Language-Fundamentals.docx','Pentarnion-Theory-and-Programming-Language-Fundamentals.pdf')
    for name in documents:copy_file(a.documents/name,out/'documentation'/name)
    tree(source/'third-party/licenses',out/'licenses/project-dependencies')
    if (gcc/'share/licenses').is_dir():tree(gcc/'share/licenses',out/'licenses/toolchain')
    if (build_tools/'share/licenses').is_dir():tree(build_tools/'share/licenses',out/'licenses/dependencies')
    # Separately built SDKs do not all install notices under windows-deps.
    # The explicit dependency prefix is third-party/windows-deps/ucrt64.
    vendor_root=build_tools.parent.parent
    for name in ('idas-sdk','mfem-sdk','pagmo-sdk','petsc-sdk'):
        notices=vendor_root/name/'share/licenses'
        if notices.is_dir():tree(notices,out/'licenses/separate-sdks'/name)
    idas_notices=vendor_root/'idas-sdk/share/doc/idas'
    if idas_notices.is_dir():tree(idas_notices,out/'licenses/separate-sdks/idas',lambda p:p.name in ('LICENSE','NOTICE'))
    for name in ('JKQTPlotter','sundials'):
        if (build_tools/'share/doc'/name).is_dir():tree(build_tools/'share/doc'/name,out/'licenses'/name,lambda p:p.name.lower().startswith(('license','copying')) or 'license' in p.name.lower() or p.name.lower().endswith('readme.txt'))
    locks={}
    for item in (source/'third-party').glob('*lock.json'):
        copy_file(item,out/'provenance'/item.name);locks[item.name]=sha(item)
    (out/'provenance/third-party-inputs.json').write_text(json.dumps(locks,indent=2),encoding='utf-8')
    copy_file(a.python_runtime/'python-dependencies.lock.json',out/'provenance/python-dependencies.lock.json')
    (out/'provenance/example-source-identity.json').write_text(json.dumps({'format':'Pentarnion frozen example provenance','source_archive_sha256':sha(a.source_archive),'files':example_inventory},indent=2),encoding='utf-8')
    print(json.dumps({'payload':str(out),'sdk':manifest.get('version'),'status':'staged'}))

if __name__=='__main__':main()
