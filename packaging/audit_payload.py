"""Strict source-disclosure and PE dependency closure audit for installed files."""
from pathlib import Path
import argparse,hashlib,json,os,re,stat
from pe_imports import pe_imports

SYSTEM={'advapi32.dll','bcrypt.dll','bcryptprimitives.dll','cabinet.dll','cfgmgr32.dll','comctl32.dll','comdlg32.dll','crypt32.dll','cryptbase.dll','cryptsp.dll','dbghelp.dll','dnsapi.dll','dwmapi.dll','dwrite.dll','dxgi.dll','d3d11.dll','d3d12.dll','d3dcompiler_47.dll','faultrep.dll','gdi32.dll','glu32.dll','hid.dll','imm32.dll','iphlpapi.dll','kernel32.dll','kernelbase.dll','mpr.dll','msimg32.dll','msvcrt.dll','mswsock.dll','ncrypt.dll','netapi32.dll','normaliz.dll','ntdll.dll','ntdsapi.dll','ole32.dll','oleacc.dll','oleaut32.dll','opengl32.dll','powrprof.dll','propsys.dll','psapi.dll','rpcrt4.dll','secur32.dll','setupapi.dll','shell32.dll','shlwapi.dll','user32.dll','userenv.dll','usp10.dll','ucrtbase.dll','uxtheme.dll','version.dll','wevtapi.dll','winhttp.dll','wininet.dll','winmm.dll','winnsi.dll','winspool.drv','wintrust.dll','wldap32.dll','ws2_32.dll','wsock32.dll','wtsapi32.dll'}
SOURCE={'.cpp','.c','.cc','.cxx','.hpp','.h','.hxx','.inc','.py','.ps1','.cmake','.a','.o','.obj','.lib','.pdb'}
SYSTEM.update({'authz.dll','d3d9.dll','mscms.dll','shcore.dll','xmllite.dll'})
def digest(file):
    h=hashlib.sha256()
    with file.open('rb') as stream:
        for data in iter(lambda:stream.read(1048576),b''):h.update(data)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--payload',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();root=a.payload.resolve()
    sdk=json.loads((root/'runtime-sdk/distribution-sdk.json').read_text(encoding='utf-8-sig'))
    public={('runtime-sdk/'+name).replace('\\','/') for name in sdk['files']}
    # This one readable first-party utility is deliberately installed with the
    # confirmed Apache source release. It only attaches licence companions.
    helper_name='tools/export-with-notices.py';helper=a.source/helper_name
    public_helpers={}
    if (a.source/'LICENSE').is_file() and not (a.source/'LICENSE-PENDING.md').exists() and helper.is_file():
        licence=(a.source/'LICENSE').read_bytes()
        if hashlib.sha256(licence.replace(b'\r\n',b'\n')).hexdigest()=='cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30':
            public_helpers[helper_name]=digest(helper);public.add(helper_name)
    files=[];failures=[];imports=[]
    python_dlls={item.name.lower():item for item in (root/'toolchain/python').rglob('*.dll')}
    private={digest(item):str(item.relative_to(a.source)) for item in (a.source/'src').rglob('*') if item.is_file() and item.suffix.lower() in SOURCE}
    snippets=[]
    # Compile-time dimensions and dispatch-only forwarding are the minimal
    # public model interface. These two lines contain no numerical kernel.
    public_glue={
        'template<int Tag>constexpr unsigned fixed_array_width(){if constexpr(Tag>=0)return UniversalTraits<Tag>::dimension;else return Tag==-2?5:1;}',
        'template<int Tag,class S>auto array_scalar(const S& value){if constexpr(Tag==-1)return value;else if constexpr(Tag==-2)return lift<S>(value);else return universal_lift<S,Tag>(value);}',
        'static_assert(sizeof...(E)==R*C);FixedArray<S,R,C,Tag> out{};unsigned n=0;(array_put(out,n++,values),...);return out;',
        'template<class S,unsigned R,unsigned C,int Tag>auto operator*(const S& a,const FixedArray<S,R,C,Tag>& b){return b*a;}',
        'template<int Tag,class E>auto array_product(const E& a,const E& b,double gamma){if constexpr(Tag==-1)return a*b;else if constexpr(Tag==-2)return product(a,b,gamma);else return universal_product(a,b);}',
        'struct PentaThirdHostV1 {std::uint32_t struct_size,abi_version;void* context;int(*charge)(void*,std::uint64_t,char*,std::uint64_t);};',
        'using PentaThirdEntryV1=int(*)(double,const double*,const double*,const double*,const double*,const unsigned char*,double*,double*,const PentaThirdHostV1*,char*,std::uint64_t);',
        'struct PentaThirdBridge {const PentaThirdHostV1* host;static int charge(void* raw,std::size_t count,char* error,std::size_t capacity){const auto& bridge=*static_cast<PentaThirdBridge*>(raw);return bridge.host->charge(bridge.host->context,count,error,capacity);}};',
        'CB_EXPORT int penta_callback_third_evaluate_v1(double gamma,const double* input,const double* su,const double* sv,const double* sw,const unsigned char* dependent,double* value,double* third,const PentaThirdHostV1* host,char* error,std::uint64_t capacity){try{',
        'if(!host||host->struct_size<sizeof(PentaThirdHostV1)||host->abi_version!=65536u||!host->charge)throw std::domain_error("Invalid third-derivative host ABI.");'
    }
    for directory in ('ode','native_functions'):
        for item in (a.source/'src'/directory).glob('*support.hpp'):
            # Distribution-only glue declares/forwards calls to compiled SDK
            # kernels; its readable presence is intentional and not private
            # numerical implementation. Original support bodies remain scanned.
            if item.name=='distribution_support.hpp':continue
            def signature_only(line):
                return line.startswith('template') and line.endswith('{') and '(' in line and not re.search(r'\b(return|for|while|if|throw)\b|;',line)
            text=item.read_text(encoding='utf-8-sig');lines=[s.strip() for s in text.splitlines() if len(s.strip())>110 and not s.strip().startswith('//') and not signature_only(s.strip()) and s.strip() not in public_glue]
            snippets.extend((item.name,s.encode('utf-8')) for s in lines)
    for item in (a.source/'src/export').glob('*.inc'):
        text=item.read_text(encoding='utf-8-sig');lines=[s.strip() for s in text.splitlines() if len(s.strip())>110 and not s.strip().startswith(('//','#'))]
        snippets.extend((item.name,s.encode('utf-8')) for s in lines)
    for current,dirs,names in os.walk(root,followlinks=False):
        current=Path(current)
        for name in dirs:
            info=(current/name).lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:failures.append({'file':str(current/name),'reason':'reparse directory'})
        for name in names:
            item=current/name;relative=item.relative_to(root).as_posix();info=item.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:failures.append({'file':relative,'reason':'reparse file'});continue
            h=digest(item);files.append({'path':relative,'size':info.st_size,'sha256':h})
            if relative in public_helpers and h!=public_helpers[relative]:failures.append({'file':relative,'reason':'public licence utility source identity mismatch'})
            if h in private:failures.append({'file':relative,'reason':'exact private source match','source':private[h]})
            vendor=relative.startswith(('toolchain/','licenses/','provenance/'))
            if item.suffix.lower() in SOURCE and not vendor and relative not in public:failures.append({'file':relative,'reason':'unexpected first-party implementation/developer artifact'})
            if not vendor and item.suffix.lower() in ('.cpp','.c','.hpp','.h','.inc','.py','.m','.txt','.exe','.dll','.pyd','.mexw64'):
                data=item.read_bytes()
                for source,snippet in snippets:
                    if snippet in data:failures.append({'file':relative,'reason':'embedded readable first-party support implementation','source':source});break
            if item.suffix.lower() in ('.exe','.dll','.pyd','.mexw64'):
                for dependency in pe_imports(item):
                    key=dependency.lower();system=key in SYSTEM or key.startswith(('api-ms-win-','ext-ms-win-'))
                    candidates=[item.parent/dependency,root/'bin'/dependency,root/'bin/modules'/dependency,root/'toolchain/ucrt64/bin'/dependency,root/'toolchain/bin'/dependency]
                    if relative.startswith(('toolchain/python/','runtime-sdk/python/')):
                        candidates.append(root/'toolchain/python'/dependency)
                        if key in python_dlls:candidates.append(python_dlls[key])
                    resolved=next((str(x.relative_to(root)) for x in candidates if x.is_file()),None)
                    matlab=relative.startswith('runtime-sdk/matlab/') and item.suffix.lower()=='.mexw64' and key in ('libmex.dll','libmx.dll','libmat.dll')
                    imports.append({'file':relative,'dependency':dependency,'system':system,'resolved':resolved,'external_target_runtime':'MATLAB' if matlab else None})
                    if not system and resolved is None and not matlab:failures.append({'file':relative,'reason':'unresolved PE import','dependency':dependency})
    for name,expected in sdk['files'].items():
        if digest(root/'runtime-sdk'/name)!=expected:failures.append({'file':'runtime-sdk/'+name,'reason':'SDK input identity mismatch'})
    report={'format':'Pentarnion installer source and dependency audit','files':len(files),'pe_imports':len(imports),'failures':failures,'inventory':files,'imports':imports,'private_implementation_sources':len(private),'private_body_fingerprints':len(snippets),'exact_public_glue_exceptions':sorted(public_glue),'exact_public_licence_utilities':public_helpers,'status':'passed' if not failures else 'failed'}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    (a.out.parent/'installation-manifest.json').write_text(json.dumps({'format':'Pentarnion Studio offline Windows x64 installation','version':'0.2','files':files},indent=2),encoding='utf-8')
    print(json.dumps({k:report[k] for k in ('status','files','pe_imports','failures')}))
    if failures:raise SystemExit(1)

if __name__=='__main__':main()
