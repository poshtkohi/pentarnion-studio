"""Build a licence-aware public installer while preserving validated host binaries.

Inputs are explicit and offline. The validated base release is read-only. New
documentation, licence notices, source and inventories receive a new identity;
historical acceptance is not relabelled as final-artifact acceptance.
"""
from pathlib import Path,PurePosixPath
import argparse,datetime,hashlib,json,os,re,shutil,stat,subprocess,sys,zipfile
sys.dont_write_bytecode=True
from build_public_manuals import build as build_manuals

PE={'.exe','.dll','.pyd','.mexw64'}
DOC_NAMES=('Pentarnion-Studio-Quick-Start','Pentarnion-Theory-and-Programming-Language-Fundamentals')
NOTICES='''PENTARNION STUDIO - PUBLIC WINDOWS RELEASE

Copyright 2026 Alireza Poshtkohi and Susanne Pumplün.
First-party Studio implementation, documentation, examples and reusable helpers
are licensed under the Apache License, Version 2.0. The complete licence and
attribution notice are in licenses/Pentarnion/LICENSE and NOTICE. The matching
complete public implementation/build source is included under source/ and is
available at https://github.com/poshtkohi/pentarnion-studio.

User-authored models, scripts, inputs, data and results keep their authors' terms.
Studio runtime/helper files copied into generated Python/MATLAB bundles remain
Apache-2.0. The retained GUI/CLI binary does not automatically add the newly
supplied runtime notices: use penta-export-with-notices.cmd or attach them with
tools/export-with-notices.py. See documentation/PUBLIC-RELEASE-GUIDE.txt.

Third-party software retains its original copyrights and licences. Full notices,
matching compiler/Qt and selected LGPL corresponding sources, build/configuration
patches and the pagmo source/object relinking companion are supplied in licenses/.
GCC and binutils are GPL tools; selected GCC runtime files carry the GCC Runtime
Library Exception. Qt is dynamically linked; replacing compatible Qt libraries
is not blocked. The supplied first-party licence does not replace these terms.
Read the qualified wheel runtime provenance: the GCC16 source is not claimed to
reproduce all runtimes statically incorporated into third-party Python wheels.

The TIFF4.7.2 runtime has JBIG disabled, retaining the other baseline codecs and
ABI. JBIG-compressed TIFF input is intentionally unsupported. Exact source,
configuration and bounded codec/import proofs accompany this build.

This public packaging retains the previously validated Windows scientific host,
native modules, SDK and toolchain bytes. New licence/docs/source/package metadata
and installer bytes have their own hashes and acceptance records. It does not
claim a fresh full scientific, Linux, Windows10 VM or desktop stress validation.
The installer is unsigned. Checksums identify bytes, not publisher authentication.

Installation is per user and offline. No global PATH, system fonts, services or
administrator privileges are required. Optional Copilot sends only on explicit
user action. Writable examples are seeded in Documents/Pentarnion Studio/Examples;
existing work is never overwritten. Updates refuse busy installations and back
up changed owned files. Uninstall removes the verified owned application tree,
shortcuts and registration while preserving external work, credentials/recovery.
'''
GUIDE='''PUBLIC RELEASE, LICENCE AND EXPORT GUIDE

This edition contains local HTML and editable Markdown manuals. The earlier
research-preview DOCX/PDF files are not included. Open the Quick Start HTML from
the Start menu. Formulae use readable Unicode text in this offline HTML edition.

FIRST-PARTY SOURCE AND LICENCE
Studio and its reusable first-party runtime/helpers are Apache-2.0. Full terms,
NOTICE and redistribution guidance are under licenses/Pentarnion/. The complete
source ZIP is under source/; its exact source revision and SHA256 are recorded
in provenance/public-source.json. Native bridge, compiler/export implementations,
tests and editable packaging sources are included. No MATLAB SDK is distributed.

EXPORTED PROGRAMS
The retained validated binary predates automatic attachment of runtime notices.
After a GUI export, attach companions explicitly from the application directory:
  penta-export-with-notices.cmd attach "C:\\your-work\\export-directory"
Or use the helper's export subcommand with --exporter and --target. See:
  penta-export-with-notices.cmd --help
The helper copies PENTARNION-RUNTIME-LICENSE.txt, PENTARNION-RUNTIME-NOTICE.txt
and PENTARNION-RUNTIME-README.txt without changing a scientific calculation or
overwriting different existing notices. It does not license your own scripts.
Retain other Python/MATLAB/toolchain/dependency notices where applicable.

VALIDATION BOUNDARY
The scientific executable/module/SDK byte identities are preserved from the
validated Windows release identified in provenance/base-release.json. Licence,
documentation, source, companion metadata and the installer itself are new.
The release validation receipt describes the actual additional checks. No
Windows10 VM, new Linux execution or native desktop stress test is implied.
'''

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda:stream.read(4*1024*1024),b''):h.update(data)
    return h.hexdigest()

def regular(path):
    info=path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:raise ValueError('Reparse input rejected: '+str(path))

def copy(source,target,expected=None):
    regular(source);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    if expected is not None and sha(target)!=expected:raise ValueError('Validated input identity changed: '+str(source))

def copy_tree(source,target,accept=lambda p:True):
    regular(source)
    for current,dirs,files in os.walk(source,followlinks=False):
        current=Path(current)
        for name in dirs:regular(current/name)
        for name in files:
            relative=(current/name).relative_to(source)
            if accept(relative):copy(current/name,target/relative)

def run(command,cwd,log):
    environment=dict(os.environ);environment['OMP_NUM_THREADS']='1';environment['OPENBLAS_NUM_THREADS']='1'
    result=subprocess.run(list(map(str,command)),cwd=cwd,env=environment,capture_output=True,timeout=1800,
        creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name=='nt' else 0)
    Path(log).write_bytes(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError('Command failed: '+str(command[0])+'\n'+(result.stdout+result.stderr).decode(errors='replace')[-5000:])

def source_archive(source,archive):
    with zipfile.ZipFile(archive) as z:
        names={entry.filename:entry for entry in z.infolist() if not entry.is_dir()}
        for name in names:
            path=PurePosixPath(name)
            if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name or path.parts[0]=='.git':raise ValueError('Invalid public source archive member')
            if not (source/name).is_file() or z.read(name)!=(source/name).read_bytes():raise ValueError('Public source archive differs from the reviewed tree: '+name)
        for name in ('LICENSE','NOTICE','CMakeLists.txt','src/algebra/pentarnion.cpp','packaging/repackage_public.py','packaging/build_public_manuals.py'):
            if name not in names or z.read(name)!=(source/name).read_bytes():raise ValueError('Source archive does not match reviewed source: '+name)
        if 'LICENSE-PENDING.md' in names:raise ValueError('Pending-licence marker in source archive')
        actual={p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file() and '.git' not in p.relative_to(source).parts}
        if actual!=set(names):raise ValueError('Use a clean extraction of the exact reviewed source ZIP; extra or missing source files are not accepted')
        return len(names)

def refresh_licence_metadata(payload):
    changed=[]
    roots=('compiler-and-qt-sources','shipped-lgpl-sources','corresponding-sources-closure-supplement-v3','jkqtmathtext-sources','wheel-runtime-provenance','libtiff-no-jbig','pagmo-relink-kit')
    for name in roots:
        folder=payload/'licenses'/name;readme=folder/'README.txt'
        if not readme.is_file():continue
        old=readme.read_text(encoding='utf-8');new=old
        new=re.sub(r'private\s+Pentarnion','first-party Pentarnion',new,flags=re.I)
        new=new.replace('private development layout','original development layout').replace('private Python launcher','app-local Python launcher')
        if name=='pagmo-relink-kit':
            start=new.index('This package supports');end=new.index('\nCONTENTS',start)
            new=new[:start]+'''This companion supplies the exact pagmo source/build inputs and compiled
application objects for the verified LGPL library replacement/relink route.
Complete first-party implementation is also included in the public source ZIP
under source/ and is licensed under Apache-2.0; see licenses/Pentarnion/.
Third-party notices and LGPL rights remain applicable. Historical rebuild/relink
validation below applies to the unchanged source, objects and library bytes.
'''+new[end:]
        new+='\nPUBLIC RELEASE NOTE\nComplete first-party Pentarnion implementation and editable build sources are\navailable under source/ in this installation and at\nhttps://github.com/poshtkohi/pentarnion-studio under Apache-2.0. This companion\nretains third-party source/notice rights; signed upstream archives are unchanged.\n'
        if new!=old:readme.write_text(new,encoding='utf-8',newline='\n');changed.append(readme.relative_to(payload).as_posix())
    kit=payload/'licenses/pagmo-relink-kit';terms=kit/'BINARY-OBJECT-RELINK-TERMS.txt'
    terms.write_text('''PENTARNION STUDIO - APPLICATION OBJECTS AND LIBRARY RELINKING

The first-party application objects and their complete corresponding readable
implementation are licensed under Apache-2.0; see licenses/Pentarnion/LICENSE
and NOTICE and the included complete source ZIP. You may use, copy, modify,
recombine and redistribute that first-party material under those terms.

The covered pagmo library and original third-party material retain their own
licences. Reverse engineering to debug modifications of LGPL-covered library
portions is permitted; this notice imposes no restriction on the applicable LGPL
replacement/relink rights. Apache-2.0 grants no product trademark licence beyond
its stated attribution terms. All material is supplied without warranty.
''',encoding='utf-8')
    changed.append(terms.relative_to(payload).as_posix())
    link_rule=kit/'build-recipes/original-link-rule.txt'
    if link_rule.is_file():
        original=link_rule.read_text(encoding='utf-8')
        portable=re.sub(r'[A-Za-z](?:\$)?:[/\\]+Users[/\\]+[^\r\n]*?[/\\]+code[/\\]+third-party(?=[/\\\s])','PENTA_DEPENDENCY_ROOT',original)
        if re.search(r'(?:[/\\]Users[/\\])',portable,re.I):raise ValueError('Unredacted personal path in the copied first-party link provenance')
        if portable!=original:
            link_rule.write_text(portable,encoding='utf-8',newline='\n');changed.append(link_rule.relative_to(payload).as_posix())
    manifest=kit/'kit-manifest.json';data=json.loads(manifest.read_text(encoding='utf-8-sig'))
    for item in data['files']:
        p=kit/item['file'];item['sha256']=sha(p);item['bytes']=p.stat().st_size
    data['public_release_note']='First-party terms updated to Apache-2.0 with complete public source; original source/object/library byte identities preserved.'
    manifest.write_text(json.dumps(data,indent=2),encoding='utf-8')
    lines=[sha(kit/item['file'])+'  '+item['file'] for item in data['files']]
    lines += [sha(kit/name)+'  '+name for name in ('ROLE-ALLOWLIST.json','kit-manifest.json')]
    (kit/'SHA256SUMS.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    for name in roots:
        index=payload/'licenses'/name/'SHA256.json'
        if index.exists():
            mapping=json.loads(index.read_text(encoding='utf-8'))
            for path in mapping:mapping[path]=sha(index.parent/path)
            index.write_text(json.dumps(mapping,indent=2),encoding='utf-8')
    return changed

def refresh_companion_provenance(payload):
    provenance=payload/'provenance'
    path=provenance/'pagmo-relink-kit-inputs.json';data=json.loads(path.read_text(encoding='utf-8'))
    for item in data['files']:item['sha256']=sha(payload/'licenses/pagmo-relink-kit'/item['path'])
    data['first_party_material']='Matching compiled relink objects; complete first-party implementation additionally supplied in source/ under Apache-2.0.'
    path.write_text(json.dumps(data,indent=2),encoding='utf-8')
    path=provenance/'vendor-materials.json';data=json.loads(path.read_text(encoding='utf-8'))
    for group in data:
        for item in group['files']:item['sha256']=sha(payload/'licenses'/group['role']/item['path'])
    path.write_text(json.dumps(data,indent=2),encoding='utf-8')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for option in ('base-release','source','source-archive','out','inno'):parser.add_argument('--'+option,type=Path,required=True)
    parser.add_argument('--source-revision',required=True);parser.add_argument('--stage-only',action='store_true');a=parser.parse_args()
    base=a.base_release.resolve();source=a.source.resolve();out=a.out.resolve();home=Path(__file__).resolve().parent
    if out.exists() or out==source or source in out.parents or out in source.parents or out==base or base in out.parents or out in base.parents:raise ValueError('Output must be a new directory outside the source checkout and immutable base release')
    if (source/'LICENSE-PENDING.md').exists() or not (source/'NOTICE').is_file():raise ValueError('Confirmed LICENSE and NOTICE are required')
    licence=(source/'LICENSE').read_bytes()
    if hashlib.sha256(licence.replace(b'\r\n',b'\n')).hexdigest()!='cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30':raise ValueError('Expected the complete official Apache-2.0 licence')
    members=source_archive(source,a.source_archive)
    accepted=json.loads((base/'validation-summary.json').read_text());audit=json.loads((base/'source-and-dependency-audit.json').read_text())
    if accepted['status']!='passed' or audit['status']!='passed':raise ValueError('A validated immutable base release is required')
    if sha(base/'release'/accepted['installer']['name'])!=accepted['installer']['sha256']:raise ValueError('Validated installer identity changed')
    out.mkdir(parents=True);payload=out/'payload';payload.mkdir();preserved=[];omitted=[]
    for record in audit['inventory']:
        name=record['path'];relative=PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name:raise ValueError('Invalid baseline inventory path')
        if name.startswith(('documentation/','share/')) or name in ('LICENSE-NOTICES.txt','RELEASE-NOTES.txt','provenance/document-manifest.json','provenance/example-source-identity.json'):
            omitted.append(name);continue
        copy(base/'payload'/name,payload/name,record['sha256'])
        if Path(name).suffix.lower() in PE:preserved.append({'path':name,'sha256':record['sha256']})
    print('COPIED_VALIDATED_PAYLOAD',len(preserved),'PE images',flush=True)
    source_files=('LICENSE','NOTICE','THIRD_PARTY_NOTICES.md','docs/REDISTRIBUTION.md')
    for name in source_files:copy(source/name,payload/'licenses/Pentarnion'/Path(name).name)
    copy(source/'LICENSE',payload/'LICENSE');copy(source/'NOTICE',payload/'NOTICE')
    copy_tree(source/'LICENSES',payload/'licenses/Pentarnion/upstream-notices')
    copy(source/'tools/export-with-notices.py',payload/'tools/export-with-notices.py')
    (payload/'penta-export-with-notices.cmd').write_text('@echo off\r\n"%~dp0penta-python.exe" "%~dp0tools\\export-with-notices.py" %*\r\nexit /b %errorlevel%\r\n',encoding='utf-8',newline='')
    copy(a.source_archive,payload/'source/Pentarnion-Studio-Source.zip')
    (payload/'source/README.txt').write_text('COMPLETE PUBLIC SOURCE\n\nSource revision: '+a.source_revision+'\nArchive SHA256: '+sha(a.source_archive)+'\n\nThis archive contains the complete readable first-party implementation, tests,\nexamples and editable build/packaging sources. Select PENTA_DISTRIBUTION_BUILD=ON\nfor the compiled-runtime distribution path used by the retained validated host.\nBuild inputs/toolchain can affect binary bytes; bit-identical reconstruction\nis not claimed. See docs/BUILDING.md and packaging/README.md after extraction.\nThird-party corresponding sources and relink materials are under licenses/.\n',encoding='utf-8')
    help_root=payload/'share/pentarnion-studio'
    for path in source.glob('*'):
        if path.is_file() and path.suffix.lower() in ('.txt','.md') and path.name not in ('PLAN.txt',):copy(path,help_root/path.name)
    for folder in ('specs','docs'):
        copy_tree(source/folder,help_root/folder,lambda p:p.suffix.lower() in ('.txt','.md'))
    copy_tree(source/'assets/branding',help_root/'assets/branding',lambda p:p.suffix.lower() in ('.png','.ico','.svg','.txt'))
    examples=[]
    with zipfile.ZipFile(a.source_archive) as archive:
        for entry in archive.infolist():
            path=PurePosixPath(entry.filename)
            if entry.is_dir() or not path.parts or path.parts[0]!='examples':continue
            data=archive.read(entry);target=help_root/Path(entry.filename);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            examples.append({'path':entry.filename,'sha256':hashlib.sha256(data).hexdigest()})
    counts=build_manuals(source/'documentation/source',payload/'documentation')
    (payload/'documentation/PUBLIC-RELEASE-GUIDE.txt').write_text(GUIDE,encoding='utf-8')
    doc_manifest=payload/'documentation/document-manifest.json'
    documents=json.loads(doc_manifest.read_text(encoding='utf-8'))
    documents['files']=[{'name':p.name,'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(doc_manifest.parent.iterdir()) if p.is_file() and p!=doc_manifest]
    doc_manifest.write_text(json.dumps(documents,indent=2),encoding='utf-8')
    changes=refresh_licence_metadata(payload)
    refresh_companion_provenance(payload)
    (out/'LICENSE-NOTICES.txt').write_text(NOTICES,encoding='utf-8');copy(out/'LICENSE-NOTICES.txt',payload/'LICENSE-NOTICES.txt')
    notes='PENTARNION STUDIO 0.2 - APACHE-2.0 PUBLIC WINDOWS RELEASE\n\n'+NOTICES+'\nThe public source revision is '+a.source_revision+'.\nLocal HTML/Markdown manuals replace the earlier research-preview DOCX/PDF.\nRead the new validation receipt for this installer identity.\n'
    (payload/'RELEASE-NOTES.txt').write_text(notes,encoding='utf-8')
    public_source={'source_revision':a.source_revision,'source_zip_sha256':sha(a.source_archive),'source_archive_members':members,'source_included':'source/Pentarnion-Studio-Source.zip','first_party_licence':'Apache-2.0','LICENSE_sha256':sha(source/'LICENSE'),'NOTICE_sha256':sha(source/'NOTICE')}
    provenance=payload/'provenance'
    (provenance/'public-source.json').write_text(json.dumps(public_source,indent=2),encoding='utf-8')
    (provenance/'base-release.json').write_text(json.dumps({'format':'Validated Windows binary provenance, not new installer acceptance','installer':accepted['installer'],'historical_installed_checks':accepted['total_actual_installed_checks'],'preserved_PE_files':preserved,'current_desktop_retested':False},indent=2),encoding='utf-8')
    (provenance/'example-source-identity.json').write_text(json.dumps({'source_zip_sha256':sha(a.source_archive),'files':examples},indent=2),encoding='utf-8')
    (provenance/'public-notice-updates.json').write_text(json.dumps({'first_party_notice_updates':changes,'signed_upstream_archives_modified':False,'scientific_binaries_modified':False},indent=2),encoding='utf-8')
    copy(payload/'documentation/document-manifest.json',provenance/'document-manifest.json')
    for name in ('installer.iss','update_managed_application.iss','uninstall_managed_application.iss','audit_payload.py','pe_imports.py','repackage_public.py','build_public_manuals.py'):
        copy(home/name,out/name)
    copy_tree(home/'assets',out/'assets')
    recipe=(out/'installer.iss').read_text(encoding='utf-8-sig')
    for name in DOC_NAMES:recipe=recipe.replace(name+'.pdf',name+'.html')
    (out/'installer.iss').write_text(recipe,encoding='utf-8')
    run([sys.executable,'-B',out/'audit_payload.py','--payload',payload,'--source',source,'--out',out/'source-and-dependency-audit.json'],out,out/'audit.log')
    new_audit=json.loads((out/'source-and-dependency-audit.json').read_text());actual={x['path']:x['sha256'] for x in new_audit['inventory']}
    if any(actual.get(x['path'])!=x['sha256'] for x in preserved):raise ValueError('Scientific/toolchain PE preservation failed')
    if any(x['path'].lower().endswith('libjbig-0.dll') or (x['path'].startswith('documentation/') and x['path'].lower().endswith(('.docx','.pdf'))) for x in new_audit['inventory']):raise ValueError('Forbidden old manual/JBIG file reintroduced')
    copy(out/'installation-manifest.json',payload/'installation-manifest.json')
    (out/'incoming-file-hashes.txt').write_text(''.join(p.relative_to(payload).as_posix().replace('/','\\')+'|'+sha(p)+'\n' for p in sorted(payload.rglob('*')) if p.is_file()),encoding='utf-8')
    receipt={'format':'Pentarnion public Windows repack','source':public_source,'preserved_PE_files':len(preserved),'old_files_replaced':len(omitted),'manual_counts':counts,'payload_files':new_audit['files'],'PE_imports':new_audit['pe_imports'],'audit':new_audit['status'],'scientific_host_rebuilt':False,'desktop_started':False,'status':'staged; final-artifact acceptance pending'}
    if not a.stage_only:
        run([a.inno.resolve(),'/Qp',out/'installer.iss'],out,out/'installer-build.log')
        installer=out/'release/Pentarnion Studio Installer.exe'
        receipt['installer']={'name':installer.name,'sha256':sha(installer),'bytes':installer.stat().st_size,'signed':False}
        (out/'SHA256SUMS.txt').write_text(receipt['installer']['sha256']+'  '+installer.name+'\n'+sha(a.source_archive)+'  Pentarnion-Studio-Source.zip\n',encoding='utf-8')
        receipt['status']='built; final-artifact acceptance pending'
    (out/'public-build-receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
