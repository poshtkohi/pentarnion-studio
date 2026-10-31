"""Reproducible, offline, low-priority installer builder (Python stdlib + Pillow)."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,shutil,subprocess,sys
from stage_payload import sha,copy_file,tree

SCRIPTS=('installer.iss','uninstall_managed_application.iss','update_managed_application.iss','launcher.cpp','launcher.rc','stage_payload.py','build_release.py','audit_payload.py','pe_imports.py','generate_artwork.py','bootstrap_python.py','extract_msi.py','patch_toolchain_manifests.py','README.txt','RELEASE-NOTES.txt','WINDOWS-10-VM-CHECK.txt')
DOCS=('Pentarnion-Studio-Quick-Start.docx','Pentarnion-Studio-Quick-Start.pdf','Pentarnion-Theory-and-Programming-Language-Fundamentals.docx','Pentarnion-Theory-and-Programming-Language-Fundamentals.pdf')

def run(command,cwd,environment=None,log=None):
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name=='nt' else 0
    result=subprocess.run([str(x) for x in command],cwd=cwd,env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=flags)
    if log:Path(log).write_text(result.stdout,encoding='utf-8')
    if result.returncode:raise RuntimeError('Command failed: '+str(command[0])+'\n'+result.stdout[-12000:])
    return result.stdout

def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--source-archive',type=Path,required=True);p.add_argument('--bin',type=Path,required=True);p.add_argument('--runtime-bin',type=Path,required=True);p.add_argument('--runtime-overrides',type=Path);p.add_argument('--relink-kit',type=Path);p.add_argument('--vendor-materials',action='append',default=[],metavar='ROLE=DIRECTORY');p.add_argument('--sdk',type=Path,required=True);p.add_argument('--compiler',type=Path,required=True);p.add_argument('--dependencies',type=Path,required=True);p.add_argument('--documents',type=Path,required=True);p.add_argument('--python-runtime',type=Path,required=True);p.add_argument('--export-notices',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--inno',type=Path,required=True);a=p.parse_args()
    if not (a.source/'LICENSE').is_file() or (a.source/'LICENSE-PENDING.md').exists():
        raise RuntimeError('A confirmed first-party LICENSE is required before building a public installer.')
    if hashlib.sha256((a.source/'LICENSE').read_bytes().replace(b'\r\n',b'\n')).hexdigest()!='cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30':
        raise RuntimeError('The complete official Apache-2.0 licence is required for this public release.')
    home=Path(__file__).resolve().parent;out=a.out.resolve()
    if out.exists():raise RuntimeError('Release output must not already exist. Earlier releases are never overwritten.')
    out.mkdir(parents=True)
    (out/'build-inputs.json').write_text(json.dumps({'format':'Pentarnion exact local installer build invocation','python':sys.executable,'arguments':sys.argv[1:],'inputs':{key:str(value.resolve()) if isinstance(value,Path) else value for key,value in vars(a).items()},'offline_build':True},indent=2),encoding='utf-8')
    for name in SCRIPTS:copy_file(home/name,out/name)
    tree(home/'assets',out/'assets')
    # Compile small owned launchers first; syntax errors must not trigger a
    # repeated multi-gigabyte runtime/source-material staging pass.
    env=dict(os.environ)
    for name in tuple(env):
        if name.upper().startswith(('PENTA_','GCC_')) or name.upper() in ('CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','COMPILER_PATH','LIBRARY_PATH','CMAKE_PREFIX_PATH','PKG_CONFIG_PATH'):env.pop(name,None)
    env['PATH']=str(a.compiler/'bin')+';'+str(a.dependencies/'bin')+';'+os.environ['SystemRoot']+'\\System32'
    run([a.compiler/'bin/windres.exe',out/'launcher.rc',out/'launcher-res.o'],out,env,log=out/'resource-build.log')
    for mode in ('console','gui'):
        options=['-std=c++20','-O2','-static','-municode']
        if mode=='gui':options+=['-mwindows','-DPENTA_GUI_LAUNCHER']
        run([a.compiler/'bin/g++.exe',*options,out/'launcher.cpp',out/'launcher-res.o','-lshell32','-lole32','-luuid','-o',out/f'launcher-{mode}.exe'],out,env,log=out/f'launcher-{mode}-build.log')
    payload=out/'payload'
    stage_args=[sys.executable,home/'stage_payload.py','--source',a.source,'--source-archive',a.source_archive,'--bin',a.bin,'--runtime-bin',a.runtime_bin,'--sdk',a.sdk,'--compiler',a.compiler,'--build-tools',a.dependencies,'--documents',a.documents,'--python-runtime',a.python_runtime,'--out',payload]
    if a.runtime_overrides:stage_args+=['--runtime-overrides',a.runtime_overrides]
    run(stage_args,home,log=out/'stage.log')
    tree(a.export_notices,payload/'licenses/Cython')
    if a.relink_kit:
        kit=payload/'licenses/pagmo-relink-kit';tree(a.relink_kit,kit)
        identity=json.loads((kit/'kit-manifest.json').read_text(encoding='utf-8-sig'));roles=json.loads((kit/'ROLE-ALLOWLIST.json').read_text(encoding='utf-8-sig'))
        if identity['original_module_sha256']!=sha(payload/'bin/modules/penta_evolution.dll') or roles.get('no_first_party_engine_source') is not True:raise RuntimeError('Pagmo relink companion does not match the shipped module or source boundary.')
        expected={'kit-manifest.json','SHA256SUMS.txt','ROLE-ALLOWLIST.json'}
        for record in identity['files']:
            name=record['file'];relative=Path(name)
            if relative.is_absolute() or '..' in relative.parts:raise RuntimeError('Relink manifest escaped its root.')
            prefixes=roles['third_party_source_prefixes']+roles.get('third_party_binary_prefixes',[])+roles['binary_first_party_prefixes']+roles['build_and_evidence_prefixes']
            if name not in roles['exact_root_files'] and not any(name.startswith(prefix) for prefix in prefixes):raise RuntimeError('Unexpected relink source role: '+name)
            if any(name.startswith(prefix) for prefix in roles['binary_first_party_prefixes']) and relative.suffix.lower() not in ('.obj','.dll'):raise RuntimeError('Readable first-party implementation is forbidden in binary relink roles.')
            if any(name.startswith(prefix) for prefix in roles.get('third_party_binary_prefixes',[])) and relative.suffix.lower() not in ('.a','.lib','.dll'):raise RuntimeError('Unexpected readable input in third-party binary relink role.')
            if sha(kit/relative)!=record['sha256']:raise RuntimeError('Relink companion input changed: '+name)
            expected.add(name)
        if {file.relative_to(kit).as_posix() for file in kit.rglob('*') if file.is_file()}!=expected:raise RuntimeError('Unexpected files outside the frozen relink inventory.')
        kit_inputs=[{'path':item.relative_to(a.relink_kit).as_posix(),'sha256':sha(item)} for item in sorted(a.relink_kit.rglob('*')) if item.is_file()]
        (payload/'provenance/pagmo-relink-kit-inputs.json').write_text(json.dumps({'format':'Pentarnion LGPL third-party relinking materials','readable_sources':'third-party pagmo and its patches only','first_party_material':'compiled objects only','files':kit_inputs},indent=2),encoding='utf-8')
    toolchain_materials=payload/'licenses/toolchain-resource-patches';toolchain_materials.mkdir()
    copy_file(home/'patch_toolchain_manifests.py',toolchain_materials/'patch_toolchain_manifests.py')
    copy_file(payload/'toolchain/ucrt64/toolchain-manifest-patches.json',toolchain_materials/'toolchain-manifest-patches.json')
    (toolchain_materials/'README.txt').write_text('''COPIED COMPILER/BINUTILS PROCESS-MANIFEST CHANGES

The supplied GCC/binutils binaries were changed only in their Windows
RT_MANIFEST resources: UTF-8 process code page and longPathAware are enabled.
Compiler algorithms, machine code, global MSYS2 installation and system locale
are not modified. Original and patched file/manifest hashes are recorded in
toolchain-manifest-patches.json. Existing DPI/compatibility declarations remain.

After rebuilding the exact upstream packages and recipes supplied with this
distribution, copy the compiler prefix into a NEW OWNED staging directory.
Inside that directory create .pentarnion-owned-toolchain-staging containing:
PentarnionToolchainStaging-v1
Then on Windows with Python3.11+ run:
  python patch_toolchain_manifests.py --root OWNED_STAGING_DIRECTORY
The script refuses signed executables and requires the explicit owner marker.
It patches only copied compiler executables and writes a fresh hash receipt.
Never point it at a system installation or the global C:/msys64 directory.

This generic build-infrastructure source contains no Pentarnion scientific
engine, IDE or language/compiler-front-end implementation. It is supplied to
describe/reproduce the resource changes to the third-party compiler tools.
''',encoding='utf-8')
    vendor_materials=[]
    for item in a.vendor_materials:
        role,separator,folder=item.partition('=')
        if not separator or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,63}',role):raise RuntimeError('Vendor materials must be a safe ROLE=DIRECTORY pair.')
        original=Path(folder).resolve();destination=payload/'licenses'/role
        if destination.exists():raise RuntimeError('Vendor materials cannot replace existing installed notices: '+role)
        tree(original,destination)
        vendor_materials.append({'role':role,'readable_sources':'explicit third-party corresponding-source/configuration materials','files':[{'path':file.relative_to(original).as_posix(),'sha256':sha(file)} for file in sorted(original.rglob('*')) if file.is_file()]})
    (payload/'provenance/vendor-materials.json').write_text(json.dumps(vendor_materials,indent=2),encoding='utf-8')
    copy_file(a.source/'LICENSE',payload/'licenses/Pentarnion/LICENSE')
    copy_file(a.source/'NOTICE',payload/'licenses/Pentarnion/NOTICE')
    copy_file(home/'RELEASE-NOTES.txt',payload/'RELEASE-NOTES.txt')
    copy_file(out/'launcher-gui.exe',payload/'pentarnion-studio.exe')
    for name in ('penta-run.exe','penta-check.exe','penta-export.exe'):copy_file(out/'launcher-console.exe',payload/name)
    copy_file(out/'launcher-console.exe',payload/'penta-python.exe')
    from patch_toolchain_manifests import patch_tree
    marker=payload/'.pentarnion-owned-application-staging';marker.write_text('PentarnionApplicationStaging-v1\n',encoding='utf-8')
    owned_executables=['bin/'+name for name in ('pentarnion-studio.exe','penta-run.exe','penta-check.exe','penta-export.exe')]+['pentarnion-studio.exe','penta-run.exe','penta-check.exe','penta-export.exe','penta-python.exe']
    patch_tree(payload,owned_executables,marker.name,'PentarnionApplicationStaging-v1','provenance/application-manifest-patches.json');marker.unlink()
    authors='''PENTARNION STUDIO - AUTHORS AND CONTACT

Dr Alireza Poshtkohi
School of Physics, Engineering and Computer Science, University of Hertfordshire,
Hatfield, Hertfordshire, United Kingdom
a.poshtkohi@herts.ac.uk (correspondence)

Dr Susanne Pumplün
School of Mathematical Sciences, University of Nottingham,
Nottingham NG7 2RD, United Kingdom
Susanne.Pumpluen@nottingham.ac.uk

https://pentarnion.com
'''
    notices='''PENTARNION STUDIO - WINDOWS DISTRIBUTION NOTICES

This package contains compiled Pentarnion Studio and its native runtime.
The complete first-party implementation and build scripts are available from
https://github.com/poshtkohi/pentarnion-studio at this release's source revision.
The confirmed first-party licence is included under licenses/Pentarnion/LICENSE.
Model-specific generated code represents user programs. Consult the licence and
runtime/export terms before redistributing generated programs and libraries.

Third-party copyright and licence terms remain applicable. Component notices,
complete supplied licence texts and package provenance are under licenses and
provenance. GCC and GNU binutils are GPL software; GCC runtime libraries carry the
GCC Runtime Library Exception where stated in their original notices. Qt libraries
are dynamically linked and retain their applicable LGPL/GPL/commercial terms.
Replacement of compatible Qt DLLs is not blocked by this package. CMake, Ninja,
VTK, Eigen, SUNDIALS, Ceres, PETSc, MFEM, OpenBLAS, Boost, pagmo, NLopt, FFT and data
libraries retain the notices provided with their pinned dependency distributions.
Private CPython retains its PSF licence; Tcl/Tk retain their permissive notices.
NumPy, SciPy, Matplotlib and their wheels retain their installed dist-info licence
directories, including bundled fonts. Cython-generated private export code is
compiled to a native extension; its original notices are retained in licenses/Cython.

Source availability: https://packages.msys2.org and https://github.com/msys2/MINGW-packages
identify corresponding MSYS2 package recipes and upstream source URLs. Exact
project package versions, download URLs and hashes are recorded under provenance.
Upstream source locations include https://gcc.gnu.org, https://www.gnu.org/software/binutils,
https://code.qt.io, https://vtk.org, https://computing.llnl.gov/projects/sundials,
https://petsc.org and https://mfem.org. Consult installed original notices for
redistribution and source/relinking obligations before sharing modified bundles.
Actual compiler/Qt and shipped LGPL corresponding-source packages, matching
recipes/provenance, JKQT sources and the pagmo source/object-relink companion are
also supplied under licenses. Copied compiler process-manifest modifications
and their exact source/receipts are under licenses/toolchain-resource-patches.
The source/configuration/codec proof for TIFF4.7.2 with JBIG disabled is under
licenses/libtiff-no-jbig. Third-party source and compiled relink objects are
explicit roles. First-party source is separately available with this release.
The actual binary dependency closure must satisfy its retained licences; the
existence of this installer is not a blanket legal-compliance certification.

Install for the current user without elevation. No global PATH, system font,
service or file association is changed. Copilot remains optional and makes no
provider request without the user's Send action.

Keep user work outside the application directory. Writable example copies are
seeded under Documents/Pentarnion Studio/Examples; existing files are never
overwritten. Updates preserve external projects, data, results and checkpoints.
Changed owned application files are backed up with verified SHA-256 hashes before
replacement. A busy installation stops the update; running work is not closed.

Uninstall removes the complete verified owned installation (including internal
caches/settings), its shortcuts and installer registration. External workspaces,
scripts, documents, data, results, checkpoints, credentials and recovery backups
are preserved. Junctions and symbolic links are never followed for deletion.

The installer is not digitally signed. Windows SmartScreen may identify it as an
unknown publisher. The separately supplied SHA256SUMS.txt records this release's
identity; a checksum establishes identity, not publisher authentication.
'''
    for name,text in (('AUTHORS-AND-CONTACT.txt',authors),('LICENSE-NOTICES.txt',notices)):
        (out/name).write_text(text,encoding='utf-8');copy_file(out/name,payload/name)
    document_hashes=[{'name':name,'sha256':sha(payload/'documentation'/name)} for name in DOCS]
    frozen_documents=json.loads((a.documents/'document-manifest.json').read_text(encoding='utf-8-sig'))
    frozen_hashes={item.get('name',item.get('filename')):item['sha256'] for item in frozen_documents}
    if any(frozen_hashes.get(item['name'])!=item['sha256'] for item in document_hashes):raise RuntimeError('Frozen, visually reviewed documentation manifest mismatch.')
    (out/'document-manifest.json').write_text(json.dumps(document_hashes,indent=2),encoding='utf-8')
    copy_file(out/'document-manifest.json',payload/'provenance/document-manifest.json')
    run([sys.executable,home/'audit_payload.py','--payload',payload,'--source',a.source,'--out',out/'source-and-dependency-audit.json'],home,log=out/'audit.log')
    copy_file(out/'installation-manifest.json',payload/'installation-manifest.json')
    catalog=''.join(item.relative_to(payload).as_posix().replace('/','\\')+'|'+sha(item)+'\n' for item in sorted(payload.rglob('*')) if item.is_file())
    (out/'incoming-file-hashes.txt').write_text(catalog,encoding='utf-8')
    run([a.inno,'/Qp',out/'installer.iss'],out,env,log=out/'installer-build.log')
    installer=out/'release/Pentarnion Studio Installer.exe'
    identity={'format':'Pentarnion standalone Windows installer identity','version':'0.2','built_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'installer':{'name':installer.name,'sha256':sha(installer),'bytes':installer.stat().st_size,'signed':False},'studio_sha256':sha(payload/'bin/pentarnion-studio.exe'),'cli_sha256':sha(payload/'bin/penta-run.exe'),'documents':document_hashes,'source_and_dependency_audit_sha256':sha(out/'source-and-dependency-audit.json'),'runtime_sdk_manifest_sha256':sha(payload/'runtime-sdk/distribution-sdk.json'),'inno_compiler_sha256':sha(a.inno)}
    (out/'release-identity.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
    (out/'SHA256SUMS.txt').write_text(identity['installer']['sha256']+'  '+installer.name+'\n'+''.join(x['sha256']+'  '+x['name']+'\n' for x in document_hashes),encoding='utf-8')
    print(json.dumps({'installer':str(installer),'sha256':sha(installer),'status':'built; final installed acceptance still required'}))

if __name__=='__main__':main()
