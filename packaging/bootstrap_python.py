"""Explicit developer-only bootstrap of a private Python export runtime.

No end-user downloads, global installation or package-manager registration.
The lock records exact official inputs; --lock replays pinned hashes/URLs.
"""
from pathlib import Path
import argparse,email,hashlib,json,os,shutil,subprocess,sys,urllib.request,zipfile

PYTHON_URL='https://www.python.org/ftp/python/3.14.10/python-3.14.10-embed-amd64.zip'
PYTHON_SHA='90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159'
TCLTK_URL='https://www.python.org/ftp/python/3.14.10/amd64/tcltk.msi'
TCLTK_SHA='9642593510b188aa35dcdf1995cbe2b3c3c07446a0ac1dd0c1d1958bf25526f5'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def fetch(url,target,expected=None):
    request=urllib.request.Request(url,headers={'User-Agent':'Pentarnion-Installer-Build/0.2'})
    with urllib.request.urlopen(request,timeout=90) as stream,target.open('wb') as output:
        while data:=stream.read(1024*1024):output.write(data)
    actual=sha(target)
    if expected and actual!=expected:raise RuntimeError('Download hash mismatch: '+target.name)
    return actual
def unpack_wheel(wheel,target):
    with zipfile.ZipFile(wheel) as archive:
        for member in archive.infolist():
            path=Path(member.filename)
            if path.is_absolute() or '..' in path.parts:raise RuntimeError('Wheel path escaped private Python tree.')
            if member.is_dir():continue
            if path.parts[0].endswith('.data'):
                if len(path.parts)<3 or path.parts[1] not in ('purelib','platlib'):continue
                path=Path(*path.parts[2:])
            dest=target/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(archive.read(member))
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--lock',type=Path);a=p.parse_args();out=a.out.resolve()
    if out.exists():raise RuntimeError('Python output must be a new directory.')
    out.mkdir(parents=True);downloads=out/'downloads';downloads.mkdir();runtime=out/'runtime';runtime.mkdir()
    lock=json.loads(a.lock.read_text()) if a.lock else {'format':'Pentarnion private Python export dependencies','python':{'version':'3.14.10','url':PYTHON_URL,'sha256':PYTHON_SHA},'tcltk':{'version':'3.14.10','url':TCLTK_URL,'sha256':TCLTK_SHA,'authenticode':'Valid Python Software Foundation (verified during initial bootstrap)'},'wheels':[]}
    embed=downloads/'python-3.14.10-embed-amd64.zip';fetch(lock['python']['url'],embed,lock['python']['sha256'])
    with zipfile.ZipFile(embed) as archive:archive.extractall(runtime)
    wheelhouse=downloads/'wheels';wheelhouse.mkdir()
    if a.lock:
        for entry in lock['wheels']:fetch(entry['url'],wheelhouse/entry['filename'],entry['sha256'])
    else:
        cmd=[sys.executable,'-m','pip','download','--disable-pip-version-check','--index-url','https://pypi.org/simple','--only-binary=:all:','--platform','win_amd64','--python-version','314','--implementation','cp','--abi','cp314','--abi','abi3','--dest',str(wheelhouse),'numpy','scipy','matplotlib']
        result=subprocess.run(cmd,capture_output=True,text=True,timeout=240,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        (out/'pip-download.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode:raise RuntimeError('Wheel download failed. '+result.stderr[-3000:])
        for wheel in sorted(wheelhouse.glob('*.whl')):
            with zipfile.ZipFile(wheel) as archive:
                meta=email.message_from_bytes(archive.read(next(name for name in archive.namelist() if name.endswith('.dist-info/METADATA'))))
                name,version=meta['Name'],meta['Version']
            with urllib.request.urlopen(f'https://pypi.org/pypi/{name}/{version}/json',timeout=60) as stream:metadata=json.load(stream)
            official=next(item for item in metadata['urls'] if item['filename']==wheel.name)
            if sha(wheel)!=official['digests']['sha256']:raise RuntimeError('Wheel hash differs from official PyPI metadata.')
            lock['wheels'].append({'name':name,'version':version,'filename':wheel.name,'url':official['url'],'sha256':sha(wheel)})
    site=runtime/'Lib/site-packages';site.mkdir(parents=True)
    for wheel in sorted(wheelhouse.glob('*.whl')):unpack_wheel(wheel,site)
    component=lock.setdefault('tcltk',{'version':'3.14.10','url':TCLTK_URL,'sha256':TCLTK_SHA})
    msi=downloads/'tcltk.msi';fetch(component['url'],msi,component['sha256'])
    extracted=out/'tcltk-extracted'
    result=subprocess.run([sys.executable,str(Path(__file__).with_name('extract_msi.py')),str(msi),str(extracted)],capture_output=True,text=True,timeout=90,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW)
    (out/'tcltk-extraction.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    if result.returncode:raise RuntimeError('Read-only Tcl/Tk component extraction failed.')
    for name in ('DLLs','tcl'):shutil.copytree(extracted/name,runtime/name)
    shutil.copytree(extracted/'Lib/tkinter',runtime/'Lib/tkinter')
    # Isolated embeddable Python ignores registry, user site-packages and host
    # PYTHONPATH. Third-party wheel .pth processing is required for DLL loaders.
    (runtime/'python314._pth').write_text('python314.zip\n.\nLib\nDLLs\nLib/site-packages\n../../runtime-sdk/python\nimport site\n',encoding='utf-8')
    (out/'python-dependencies.lock.json').write_text(json.dumps(lock,indent=2),encoding='utf-8')
    (runtime/'python-dependencies.lock.json').write_text(json.dumps(lock,indent=2),encoding='utf-8')
    print(json.dumps({'runtime':str(runtime),'python':lock['python']['version'],'wheels':[(x['name'],x['version']) for x in lock['wheels']]}),flush=True)
    env=dict(os.environ);env['PATH']=os.environ['SystemRoot']+'\\System32;'+os.environ['SystemRoot'];env['OMP_NUM_THREADS']=env['OPENBLAS_NUM_THREADS']='1';env['MPLBACKEND']='Agg';env['MPLCONFIGDIR']=str(out/'matplotlib-cache')
    for entry in (runtime/'tcl').iterdir():
        if (entry/'init.tcl').is_file():env['TCL_LIBRARY']=str(entry)
        if (entry/'tk.tcl').is_file():env['TK_LIBRARY']=str(entry)
    result=subprocess.run([str(runtime/'python.exe'),'-c','import sys,numpy,scipy,matplotlib,tkinter; from scipy import integrate,optimize,sparse; from mpl_toolkits.mplot3d.art3d import Poly3DCollection; print(sys.version); print(numpy.__version__,scipy.__version__,matplotlib.__version__); print("Tcl",tkinter.Tcl().eval("info patchlevel")); print(integrate.solve_ivp(lambda t,y:-y,[0,1],[1]).y[0,-1])'],env=env,capture_output=True,text=True,timeout=60,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW)
    (out/'python-import-proof.txt').write_text(result.stdout+result.stderr,encoding='utf-8');print(result.stdout,result.stderr,flush=True)
    if result.returncode:raise RuntimeError('Private Python import proof failed.')

if __name__=='__main__':main()
