"""Read-only MSI table/cabinet extraction; never installs or registers anything."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess,sys,json,shutil

msi=c.WinDLL('msi');H=w.UINT
def bind(name,types,rest=w.UINT):
    f=getattr(msi,name);f.argtypes=types;f.restype=rest;return f
open_db=bind('MsiOpenDatabaseW',[w.LPCWSTR,w.LPCWSTR,c.POINTER(H)])
open_view=bind('MsiDatabaseOpenViewW',[H,w.LPCWSTR,c.POINTER(H)])
execute=bind('MsiViewExecute',[H,H]);fetch=bind('MsiViewFetch',[H,c.POINTER(H)])
getstr=bind('MsiRecordGetStringW',[H,w.UINT,w.LPWSTR,c.POINTER(w.DWORD)])
readstream=bind('MsiRecordReadStream',[H,w.UINT,c.c_void_p,c.POINTER(w.DWORD)])
close=bind('MsiCloseHandle',[H])
def string(rec,index):
    n=w.DWORD(32768);buf=c.create_unicode_buffer(n.value);rc=getstr(rec,index,buf,c.byref(n))
    if rc:raise OSError(rc,'MSI string')
    return buf.value
def rows(db,query,stream=None):
    view=H();rc=open_view(db,query,c.byref(view))
    if rc:raise OSError(rc,query)
    try:
        rc=execute(view,0)
        if rc:raise OSError(rc,'MSI execute')
        while True:
            record=H();rc=fetch(view,c.byref(record))
            if rc==259:break
            if rc:raise OSError(rc,'MSI fetch')
            try:
                if stream:
                    stream(record)
                else:
                    yield [string(record,i) for i in range(1,query.count(',')+2)]
            finally:close(record)
    finally:close(view)
def longname(name):return name.split(':',1)[0].split('|')[-1]
def main():
    source=Path(sys.argv[1]).resolve();target=Path(sys.argv[2]).resolve();target.mkdir(parents=True,exist_ok=True)
    db=H();rc=open_db(str(source),None,c.byref(db))
    if rc:raise OSError(rc,'MSI open')
    try:
        cabinets=[row[0][1:] for row in rows(db,'SELECT `Cabinet` FROM `Media`') if row[0].startswith('#')]
        scratch=target/'_cabinet';scratch.mkdir(exist_ok=True)
        def extract(rec):
            name=string(rec,1)
            if name not in cabinets:return
            path=scratch/(name+'.cab')
            with path.open('wb') as out:
                while True:
                    buf=c.create_string_buffer(1024*1024);n=w.DWORD(len(buf));rc=readstream(rec,2,buf,c.byref(n))
                    if rc:raise OSError(rc,'MSI stream')
                    if not n.value:break
                    out.write(buf.raw[:n.value])
            subprocess.run(['C:/Windows/System32/expand.exe','-F:*',str(path),str(scratch)],check=True,stdout=subprocess.DEVNULL)
        list(rows(db,'SELECT `Name`,`Data` FROM `_Streams`',extract))
        directories={row[0]:(row[1],longname(row[2])) for row in rows(db,'SELECT `Directory`,`Directory_Parent`,`DefaultDir` FROM `Directory`')}
        components={row[0]:row[1] for row in rows(db,'SELECT `Component`,`Directory_` FROM `Component`')}
        def directory(key):
            if not key or key=='TARGETDIR':return target
            parent,name=directories[key]
            base=directory(parent)
            return base if name in ('.','SourceDir') else base/name
        copied=0
        for key,component,name in rows(db,'SELECT `File`,`Component_`,`FileName` FROM `File`'):
            original=scratch/key
            if not original.exists():raise FileNotFoundError(original)
            destination=directory(components[component])/longname(name);destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(original,destination);copied+=1
        (target/'extraction.json').write_text(json.dumps({'source':source.name,'files':copied,'method':'read-only MSI tables and expand.exe'},indent=2))
        print('Extracted',copied,'files to',target)
    finally:close(db)
if __name__=='__main__':main()
