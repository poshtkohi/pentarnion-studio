"""Patch ONLY an explicitly owned copied compiler prefix, never global MSYS2.

Windows 10 1903+ UTF-8 process code pages allow GCC's narrow internal child-tool
paths to retain Unicode. Existing resource/manifest settings are preserved.
"""
from pathlib import Path
import argparse,ctypes as c,hashlib,json,os,struct,xml.etree.ElementTree as ET
from ctypes import wintypes as w

ASM='urn:schemas-microsoft-com:asm.v1';APP='urn:schemas-microsoft-com:asm.v3';UTF8='http://schemas.microsoft.com/SMI/2019/WindowsSettings';LONG='http://schemas.microsoft.com/SMI/2016/WindowsSettings'
ET.register_namespace('',ASM);ET.register_namespace('asmv3',APP);ET.register_namespace('ws19',UTF8);ET.register_namespace('ws16',LONG)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def api():
    k=c.WinDLL('kernel32',use_last_error=True)
    k.LoadLibraryExW.argtypes=(w.LPCWSTR,w.HANDLE,w.DWORD);k.LoadLibraryExW.restype=w.HMODULE;k.FreeLibrary.argtypes=(w.HMODULE,)
    k.FindResourceExW.argtypes=(w.HMODULE,c.c_void_p,c.c_void_p,w.WORD);k.FindResourceExW.restype=w.HANDLE;k.SizeofResource.argtypes=(w.HMODULE,w.HANDLE);k.SizeofResource.restype=w.DWORD;k.LoadResource.argtypes=(w.HMODULE,w.HANDLE);k.LoadResource.restype=w.HANDLE;k.LockResource.argtypes=(w.HANDLE,);k.LockResource.restype=c.c_void_p
    k.BeginUpdateResourceW.argtypes=(w.LPCWSTR,w.BOOL);k.BeginUpdateResourceW.restype=w.HANDLE;k.UpdateResourceW.argtypes=(w.HANDLE,c.c_void_p,c.c_void_p,w.WORD,c.c_void_p,w.DWORD);k.UpdateResourceW.restype=w.BOOL;k.EndUpdateResourceW.argtypes=(w.HANDLE,w.BOOL);k.EndUpdateResourceW.restype=w.BOOL
    return k
def manifests(k,path):
    module=k.LoadLibraryExW(str(path),None,0x2|0x20)
    if not module:raise c.WinError(c.get_last_error())
    languages=[];callback_type=c.WINFUNCTYPE(w.BOOL,w.HMODULE,c.c_void_p,c.c_void_p,w.WORD,c.c_void_p)
    @callback_type
    def found(module,kind,name,language,unused):languages.append(language);return True
    k.EnumResourceLanguagesW.argtypes=(w.HMODULE,c.c_void_p,c.c_void_p,callback_type,c.c_void_p);k.EnumResourceLanguagesW.restype=w.BOOL
    try:
        k.EnumResourceLanguagesW(module,c.c_void_p(24),c.c_void_p(1),found,None)
        result={}
        for language in languages:
            resource=k.FindResourceExW(module,c.c_void_p(24),c.c_void_p(1),language)
            size=k.SizeofResource(module,resource);handle=k.LoadResource(module,resource);address=k.LockResource(handle)
            if not size or not address:raise c.WinError(c.get_last_error())
            result[language]=c.string_at(address,size)
        return result
    finally:k.FreeLibrary(module)
def updated(original,name):
    if original:
        text=original.rstrip(b'\x00');root=ET.fromstring(text)
    else:
        root=ET.Element('{'+ASM+'}assembly',{'manifestVersion':'1.0'})
        ET.SubElement(root,'{'+ASM+'}assemblyIdentity',{'name':'Pentarnion.Toolchain.'+name.replace('+','p').replace('.','_'),'version':'1.0.0.0','type':'win32','processorArchitecture':'amd64'})
        trust=ET.SubElement(root,'{'+APP+'}trustInfo');security=ET.SubElement(trust,'{'+APP+'}security');privileges=ET.SubElement(security,'{'+APP+'}requestedPrivileges');ET.SubElement(privileges,'{'+APP+'}requestedExecutionLevel',{'level':'asInvoker','uiAccess':'false'})
    application=root.find('{'+APP+'}application')
    if application is None:application=ET.SubElement(root,'{'+APP+'}application')
    settings=application.find('{'+APP+'}windowsSettings')
    if settings is None:settings=ET.SubElement(application,'{'+APP+'}windowsSettings')
    for namespace,tag,value in ((UTF8,'activeCodePage','UTF-8'),(LONG,'longPathAware','true')):
        element=application.find('.//{'+namespace+'}'+tag)
        if element is None:element=ET.SubElement(settings,'{'+namespace+'}'+tag)
        element.text=value
    return ET.tostring(root,encoding='utf-8',xml_declaration=True)
def patch_tree(root,selected=None,marker_name='.pentarnion-owned-toolchain-staging',marker_value='PentarnionToolchainStaging-v1',receipt_name='toolchain-manifest-patches.json'):
    root=root.resolve();marker=root/marker_name
    if not marker.is_file() or marker.read_text(encoding='utf-8').strip()!=marker_value:raise RuntimeError('Only an explicitly owned copied staging prefix may be patched.')
    k=api();receipt=[]
    files=list(root.rglob('*.exe')) if selected is None else [root/relative for relative in selected]
    for path in sorted(files):
        info=path.lstat()
        if path.is_symlink() or getattr(info,'st_file_attributes',0)&0x400 or not path.resolve().is_relative_to(root):raise RuntimeError('A copied toolchain executable is linked outside its owned root.')
        data=path.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24;directory=opt+(112 if struct.unpack_from('<H',data,opt)[0]==0x20b else 96)
        if any(struct.unpack_from('<II',data,directory+8*4)):raise RuntimeError('A signed executable may not be modified by the private manifest stager: '+str(path))
        before=digest(path);old=manifests(k,path);replacement={language:updated(data,path.name) for language,data in (old or {0:b''}).items()}
        handle=k.BeginUpdateResourceW(str(path),False)
        if not handle:raise c.WinError(c.get_last_error())
        done=False
        try:
            for language,data in replacement.items():
                buffer=c.create_string_buffer(data)
                if not k.UpdateResourceW(handle,c.c_void_p(24),c.c_void_p(1),language,buffer,len(data)):raise c.WinError(c.get_last_error())
            if not k.EndUpdateResourceW(handle,False):raise c.WinError(c.get_last_error())
            done=True
        finally:
            if not done:k.EndUpdateResourceW(handle,True)
        check=manifests(k,path)
        if any(ET.fromstring(data.rstrip(b'\x00')).find('.//{'+UTF8+'}activeCodePage').text!='UTF-8' for data in check.values()):raise RuntimeError('Copied toolchain manifest verification failed.')
        receipt.append({'path':path.relative_to(root).as_posix(),'original_sha256':before,'patched_sha256':digest(path),'manifest_languages':list(replacement),'original_manifests_sha256':{str(language):hashlib.sha256(data).hexdigest() for language,data in old.items()},'patched_manifests_sha256':{str(language):hashlib.sha256(data).hexdigest() for language,data in check.items()}})
    destination=root/receipt_name;destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps({'format':'Pentarnion owned copied executable Unicode process manifests','minimum_windows':'10.0.18362 (1903)','global_msys2_modified':False,'files':receipt},indent=2),encoding='utf-8')
    return receipt
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);a=p.parse_args();entries=patch_tree(a.root);print(json.dumps({'patched_executables':len(entries),'root':str(a.root),'receipt':'toolchain-manifest-patches.json'}))
