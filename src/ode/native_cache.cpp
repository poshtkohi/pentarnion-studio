#include "native_internal.hpp"
#include "ode/native_build_config.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>
#include <cstring>
#include <cwchar>
#include <system_error>
#include <array>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace penta::ode::detail {
namespace {
using Clock=std::chrono::steady_clock;
std::atomic_uint64_t sequence{0};
bool expired(Clock::time_point start,double seconds){return std::chrono::duration<double>(Clock::now()-start).count()>seconds;}
class CompilationTimer {
    const std::function<void(double)>& finished;
    const Clock::time_point start=Clock::now();
public:
    explicit CompilationTimer(const std::function<void(double)>&callback):finished(callback){}
    ~CompilationTimer() noexcept {if(finished)try{finished(std::chrono::duration<double>(Clock::now()-start).count());}catch(...){}}
};
#ifdef _WIN32
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE&&h!=nullptr)CloseHandle(h);}Handle()=default;explicit Handle(HANDLE value):h(value){}Handle(const Handle&)=delete;};
struct ProcessAttributes {
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    explicit ProcessAttributes(const std::array<HANDLE,2>& handles) {
        SIZE_T bytes=0;
        InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        if(!bytes)throw OdeError("O1002","Cannot size native compiler handle attributes.");
        storage.resize(bytes);
        auto* candidate=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(candidate,1,0,&bytes))
            throw OdeError("O1002","Cannot initialize native compiler handle attributes.");
        list=candidate;
        if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(handles.data()),sizeof(handles),nullptr,nullptr)) {
            DeleteProcThreadAttributeList(list);list=nullptr;
            throw OdeError("O1002","Cannot restrict native compiler handle inheritance.");
        }
    }
    ~ProcessAttributes(){if(list)DeleteProcThreadAttributeList(list);}
    ProcessAttributes(const ProcessAttributes&)=delete;
};
std::wstring quote(const std::wstring&s){std::wstring out=L"\"";std::size_t slashes=0;for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');return out+L'\"';}
std::wstring compilerEnvironment(const std::filesystem::path&compiler,const std::filesystem::path&cwd){
    wchar_t* inherited=GetEnvironmentStringsW();if(!inherited)throw OdeError("O1002","Cannot read native compiler process environment.");
    std::vector<std::wstring> entries;bool pathFound=false;
    const auto samePrefix=[](const std::wstring& entry,const wchar_t* prefix,int count){return entry.size()>=static_cast<std::size_t>(count)&&CompareStringOrdinal(entry.data(),count,prefix,count,TRUE)==CSTR_EQUAL;};
    const auto drive=cwd.root_name().wstring();
    const std::wstring drivePrefix=drive.empty()?std::wstring():L"="+drive+L"=";
    try{for(const wchar_t*at=inherited;*at;at+=std::wcslen(at)+1){
        const std::wstring entry(at);
        if(samePrefix(entry,L"PATH=",5)){entries.push_back(L"PATH="+compiler.parent_path().wstring()+L";"+entry.substr(5));pathFound=true;}
        else if(samePrefix(entry,L"PWD=",4))continue;
        else if(!drivePrefix.empty()&&samePrefix(entry,drivePrefix.c_str(),static_cast<int>(drivePrefix.size())))continue;
        else entries.push_back(entry);
    }}catch(...){FreeEnvironmentStringsW(inherited);throw;}
    FreeEnvironmentStringsW(inherited);
    if(!pathFound)entries.push_back(L"PATH="+compiler.parent_path().wstring());
    entries.push_back(L"PWD="+cwd.wstring());
    if(!drivePrefix.empty())entries.push_back(drivePrefix+cwd.wstring());
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;});
    std::wstring block;for(const auto& entry:entries){block+=entry;block+=L'\0';}block+=L'\0';return block;
}
std::string compileProcess(const std::filesystem::path&compiler,const std::vector<std::filesystem::path>&arguments,const std::filesystem::path&cwd,const NativeOptions&options){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&sa,0))throw OdeError("O1002","Cannot create native compiler output pipe.");
    Handle reader(read),writer(write);
    if(!SetHandleInformation(read,HANDLE_FLAG_INHERIT,0))
        throw OdeError("O1002","Cannot isolate native compiler output handles.");
    // A GUI application may have no stdin, or a console/input handle belonging
    // to its launcher. The compiler only needs EOF, never Studio's own handles.
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(input.h==INVALID_HANDLE_VALUE)throw OdeError("O1002","Cannot create native compiler input.");
    Handle job(CreateJobObjectW(nullptr,nullptr));if(!job.h)throw OdeError("O1002","Cannot create native compiler process job.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw OdeError("O1002","Cannot bound the native compiler job.");
    const std::array<HANDLE,2> inherited{write,input.h};
    ProcessAttributes attributes(inherited);
    STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);
    start.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW|STARTF_FORCEOFFFEEDBACK;
    start.StartupInfo.wShowWindow=SW_HIDE;
    start.StartupInfo.hStdOutput=write;start.StartupInfo.hStdError=write;
    start.StartupInfo.hStdInput=input.h;start.lpAttributeList=attributes.list;
    std::wstring command=quote(compiler.wstring());for(const auto&a:arguments)command+=L" "+quote(a.wstring());PROCESS_INFORMATION info{};
    auto environment=compilerEnvironment(compiler,cwd);
    if(!CreateProcessW(compiler.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT,
        environment.data(),cwd.c_str(),&start.StartupInfo,&info))
        throw OdeError("O1002","Cannot start the configured native C++ compiler.");
    Handle process(info.hProcess),thread(info.hThread);
    if(!AssignProcessToJobObject(job.h,process.h)){TerminateProcess(process.h,1);throw OdeError("O1002","Cannot attach the native compiler to its bounded process job.");}
    if(ResumeThread(thread.h)==static_cast<DWORD>(-1)) {
        TerminateJobObject(job.h,1);
        throw OdeError("O1002","Cannot resume the native compiler process.");
    }
    CloseHandle(writer.h);writer.h=INVALID_HANDLE_VALUE;
    std::string output;const auto began=Clock::now();
    try{for(;;){DWORD available=0;if(PeekNamedPipe(reader.h,nullptr,0,nullptr,&available,nullptr)&&available){char buffer[4096];DWORD count=0;if(ReadFile(reader.h,buffer,std::min<DWORD>(available,sizeof(buffer)),&count,nullptr)&&output.size()<65536)output.append(buffer,std::min<std::size_t>(count,65536-output.size()));}
        checkCancelled(options);if(expired(began,options.compileTimeoutSeconds))throw OdeError("O1003","Native ODE compilation timed out.");
        if(WaitForSingleObject(process.h,10)==WAIT_OBJECT_0)break;
    }}catch(...){TerminateJobObject(job.h,1);WaitForSingleObject(process.h,10000);throw;}
    for(;;){char buffer[4096];DWORD count=0,available=0;if(!PeekNamedPipe(reader.h,nullptr,0,nullptr,&available,nullptr)||!available||!ReadFile(reader.h,buffer,sizeof(buffer),&count,nullptr)||!count)break;if(output.size()<65536)output.append(buffer,std::min<std::size_t>(count,65536-output.size()));}
    DWORD status=1;GetExitCodeProcess(process.h,&status);if(status!=0){
#ifdef PENTA_DISTRIBUTION_BUILD
        {const auto encoded=cwd.generic_u8string();output+="\nNative compiler directory: "+std::string(encoded.begin(),encoded.end());}
        output+="\nNative compiler arguments:";
        for(const auto& argument:arguments){const auto encoded=argument.generic_u8string();output+=" ["+std::string(encoded.begin(),encoded.end())+"]";}
#endif
        throw OdeError("O1002","Native ODE compilation failed (exit code "+std::to_string(status)+"):\n"+output);
    }return output;
}
class Lock {
    Handle file;
public:
    Lock(const std::filesystem::path&p,const NativeOptions&o){const auto start=Clock::now();for(;;){file.h=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file.h!=INVALID_HANDLE_VALUE)break;const auto error=GetLastError();if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_LOCK_VIOLATION)throw OdeError("O1002","Cannot lock the native ODE cache.");checkCancelled(o);if(expired(start,o.compileTimeoutSeconds))throw OdeError("O1003","Timed out waiting for the native ODE cache lock.");std::this_thread::sleep_for(std::chrono::milliseconds(10));}}
};
void replace(const std::filesystem::path&from,const std::filesystem::path&to){if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw OdeError("O1002","Cannot atomically publish a native ODE cache artifact (Windows error "+std::to_string(GetLastError())+").");}
std::uint64_t processId(){return GetCurrentProcessId();}
#else
std::string compileProcess(const std::filesystem::path&compiler,const std::vector<std::filesystem::path>&arguments,const std::filesystem::path&cwd,const NativeOptions&options){
    int pipes[2];if(pipe(pipes))throw OdeError("O1002","Cannot create native compiler output pipe.");
    std::vector<std::string> text{compiler.string()};for(const auto&a:arguments)text.push_back(a.string());std::vector<char*>argv;for(auto&s:text)argv.push_back(s.data());argv.push_back(nullptr);
    std::vector<std::string>environment;bool pathFound=false;
    for(char**at=environ;*at;++at){std::string entry(*at);if(entry.starts_with("PATH=")){entry="PATH="+compiler.parent_path().string()+":"+entry.substr(5);pathFound=true;}environment.push_back(std::move(entry));}
    if(!pathFound)environment.push_back("PATH="+compiler.parent_path().string());
    std::vector<char*>envp;for(auto&entry:environment)envp.push_back(entry.data());envp.push_back(nullptr);
    const pid_t child=fork();if(child<0){close(pipes[0]);close(pipes[1]);throw OdeError("O1002","Cannot fork native compiler.");}
    if(child==0){setsid();close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);dup2(pipes[1],STDERR_FILENO);close(pipes[1]);if(chdir(cwd.c_str()))_exit(126);execve(compiler.c_str(),argv.data(),envp.data());_exit(127);}
    close(pipes[1]);fcntl(pipes[0],F_SETFL,fcntl(pipes[0],F_GETFL)|O_NONBLOCK);std::string output;const auto began=Clock::now();int status=0;
    auto drain=[&]{char b[4096];ssize_t n;while((n=read(pipes[0],b,sizeof(b)))>0)if(output.size()<65536)output.append(b,std::min<std::size_t>(n,65536-output.size()));};
    try{for(;;){drain();checkCancelled(options);if(expired(began,options.compileTimeoutSeconds))throw OdeError("O1003","Native ODE compilation timed out.");const auto waited=waitpid(child,&status,WNOHANG);if(waited==child)break;if(waited<0)throw OdeError("O1002","Cannot wait for native compiler.");std::this_thread::sleep_for(std::chrono::milliseconds(10));}}
    catch(...){kill(-child,SIGKILL);kill(child,SIGKILL);waitpid(child,&status,0);close(pipes[0]);throw;}
    drain();close(pipes[0]);if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)throw OdeError("O1002","Native ODE compilation failed:\n"+output);return output;
}
class Lock {int file=-1;public:Lock(const std::filesystem::path&p,const NativeOptions&o){file=open(p.c_str(),O_CREAT|O_RDWR,0600);if(file<0)throw OdeError("O1002","Cannot open native ODE cache lock.");const auto start=Clock::now();try{while(flock(file,LOCK_EX|LOCK_NB)){if(errno!=EWOULDBLOCK&&errno!=EAGAIN)throw OdeError("O1002","Cannot lock native ODE cache.");checkCancelled(o);if(expired(start,o.compileTimeoutSeconds))throw OdeError("O1003","Timed out waiting for native ODE cache lock.");std::this_thread::sleep_for(std::chrono::milliseconds(10));}}catch(...){close(file);file=-1;throw;}}~Lock(){if(file>=0){flock(file,LOCK_UN);close(file);}}};
void replace(const std::filesystem::path&from,const std::filesystem::path&to){std::error_code e;std::filesystem::rename(from,to,e);if(e)throw OdeError("O1002","Cannot atomically publish native ODE cache artifact: "+e.message());}
std::uint64_t processId(){return getpid();}
#endif
std::string uniqueSuffix(){return ".tmp-"+std::to_string(processId())+"-"+std::to_string(sequence.fetch_add(1));}
void writeAtomic(const std::filesystem::path&path,const std::string&text){auto temporary=path;temporary+=uniqueSuffix();try{std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file||!file.write(text.data(),static_cast<std::streamsize>(text.size()))||!file.flush())throw OdeError("O1002","Cannot write native ODE cache file.");file.close();replace(temporary,path);}catch(...){std::error_code e;std::filesystem::remove(temporary,e);throw;}}
std::string readSmall(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)return{};std::string value;char c;while(f.get(c)){if(value.size()>=4096)return{};value+=c;}return value;}
template<class T>T symbol(void*library,const char*name){
#ifdef _WIN32
    const auto address=GetProcAddress(static_cast<HMODULE>(library),name);
#else
    const auto address=dlsym(library,name);
#endif
    if(!address)throw OdeError("O1002","Native ODE binary is missing a required ABI symbol.");
    T value{};static_assert(sizeof(value)==sizeof(address));std::memcpy(&value,&address,sizeof(value));return value;
}
}
std::string runCompilerProcess(const std::filesystem::path& compiler,
    const std::vector<std::filesystem::path>& arguments,const std::filesystem::path& cwd,
    const NativeOptions& options) {
    return compileProcess(compiler,arguments,cwd,options);
}
void checkCancelled(const NativeOptions&o){if(o.cancellation&&o.cancellation->load(std::memory_order_relaxed))throw OdeError("O1004","Native ODE operation cancelled.");}
Model::~Model(){if(library){
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}}
std::unique_ptr<Model> compile(const Generated&generated,const NativeOptions&options){
    checkCancelled(options);
    if(options.cacheDirectory.empty())throw OdeError("O1002","Native ODE cache directory was not configured.");
    if(!std::isfinite(options.compileTimeoutSeconds)||options.compileTimeoutSeconds<=0||options.compileTimeoutSeconds>600)throw OdeError("O1001","Native compilation timeout must be in (0,600] seconds.");
    const auto environmentPath=[](const char* name){
#ifdef _WIN32
        std::wstring wide;while(*name)wide.push_back(static_cast<unsigned char>(*name++));
        if(const auto* value=_wgetenv(wide.c_str()))return std::filesystem::path(value);
#else
        if(const auto* value=std::getenv(name))return std::filesystem::path(value);
#endif
        return std::filesystem::path{};
    };
    auto compiler=environmentPath("PENTA_NATIVE_COMPILER");
    if(compiler.empty())compiler=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(PENTA_ODE_CXX_COMPILER)));
    if(!compiler.is_absolute())throw OdeError("O1002","Native ODE compiler must be configured with an absolute path.");
    const std::string compilerHash=fileHash(compiler);
    std::string identity="penta-native-ode-abi1;A_gamma_v1-normalized-v1;"+std::string(PENTA_ODE_TOOLCHAIN_ID)+";"+PENTA_ODE_BUILD_ID+";"+compilerHash+";"+std::to_string(sizeof(void*))+";"+std::to_string(sizeof(long double));
#ifdef PENTA_DISTRIBUTION_BUILD
    const auto sdk=environmentPath("PENTA_RUNTIME_SDK");
    if(sdk.empty()||!sdk.is_absolute())throw OdeError("O1002","The installed compiled runtime SDK is unavailable. Start Studio using its installed shortcut.");
    identity+=";distribution-sdk-v1;";
    for(const auto* name:{"include/pentarnion/native_base.hpp","include/pentarnion/native_dense.hpp","include/pentarnion/native_dual.hpp","lib/libpenta_native_bridge.dll.a","bin/penta_native_bridge.dll"})identity+=fileHash(sdk/name)+";";
#endif
#ifdef _WIN32
    identity+=";windows;shared;-std=c++20;-O2";const std::string extension=".dll";
#else
    identity+=";posix;shared;pic;-std=c++20;-O2";const std::string extension=".so";
#endif
    auto model=std::make_unique<Model>();model->key=sha256(identity+"\n"+generated.normalized+"\n"+generated.source);model->coordinates=generated.coordinates;model->parameters=generated.parameters;
    auto cacheDirectory=options.cacheDirectory;
    const auto installedCache=environmentPath("PENTA_NATIVE_CACHE");
    if(!installedCache.empty()){
        if(!installedCache.is_absolute())throw OdeError("O1002","The installed native cache path must be absolute.");
        cacheDirectory=installedCache/options.cacheDirectory.filename();
    }
    std::error_code e;const auto cacheRoot=std::filesystem::absolute(cacheDirectory,e).lexically_normal();if(e)throw OdeError("O1002","Cannot resolve native ODE cache directory.");
    std::filesystem::create_directories(cacheRoot,e);if(e)throw OdeError("O1002","Cannot create native ODE cache directory: "+e.message());
    Lock lock(cacheRoot/(model->key+".lock"),options);
    const auto directory=cacheRoot/model->key;std::filesystem::create_directories(directory,e);if(e)throw OdeError("O1002","Cannot create native ODE model cache directory.");
    const auto source=directory/"model.cpp",binary=directory/("model"+extension),manifest=directory/"manifest.txt";
    const std::string sourceHash=sha256(generated.source);
    const std::string prefix="PENTA_NATIVE_ODE_CACHE_1\n"+model->key+"\n"+sourceHash+"\n"+compilerHash+"\n";
    bool valid=false;
    try{const auto cached=readSmall(manifest);valid=cached.starts_with(prefix)&&cached.size()==prefix.size()+65&&cached.back()=='\n'&&fileHash(source)==sourceHash&&cached.substr(prefix.size(),64)==fileHash(binary);}catch(const OdeError&){valid=false;}
    if(!valid){
        checkCancelled(options);writeAtomic(source,generated.source);const auto temporary=directory/("model"+uniqueSuffix()+extension);
        std::vector<std::filesystem::path> args{"-std=c++20","-O2","-shared","-fvisibility=hidden"};
#ifdef PENTA_DISTRIBUTION_BUILD
        // The bundled toolchain uses UTF-8 manifests. Absolute normalized SDK
        // paths avoid the legacy MAX_PATH check on long cwd + ../ strings.
        const auto compilerSdk=sdk.lexically_normal();
        args.emplace_back("-I");args.emplace_back((compilerSdk/"include").generic_wstring());
#endif
#ifndef _WIN32
        args.emplace_back("-fPIC");
#endif
        // Only ASCII filenames reach the compiler driver; the native process
        // cwd carries the full Unicode cache path on Windows and POSIX.
        args.emplace_back(source.filename());args.emplace_back("-o");args.emplace_back(temporary.filename());
#ifdef PENTA_DISTRIBUTION_BUILD
        args.emplace_back((compilerSdk/"lib/libpenta_native_bridge.dll.a").generic_wstring());
#endif
        try{
            {CompilationTimer timer(options.compilationFinished);(void)runCompilerProcess(compiler,args,directory,options);}
            checkCancelled(options);const auto binaryHash=fileHash(temporary);replace(temporary,binary);writeAtomic(manifest,prefix+binaryHash+"\n");
        }
        catch(...){std::filesystem::remove(temporary,e);throw;}
    }else model->cacheHit=true;
    checkCancelled(options);
#ifdef _WIN32
    model->library=LoadLibraryExW(binary.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    model->library=dlopen(binary.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
    if(!model->library)throw OdeError("O1002","Cannot load compiled native ODE model; check the matching C++ runtime dependencies.");
    if(symbol<unsigned(*)()>(model->library,"penta_model_abi")()!=1||symbol<unsigned(*)()>(model->library,"penta_model_coordinates")()!=generated.coordinates||symbol<unsigned(*)()>(model->library,"penta_model_parameters")()!=generated.parameters)throw OdeError("O1002","Native ODE binary ABI/dimension mismatch.");
    model->evaluate=symbol<EvaluateFunction>(model->library,"penta_model_evaluate");
    if(options.cacheStatus)model->cacheHit=options.cacheStatus(model->key,model->cacheHit);
    return model;
}
} // namespace penta::ode::detail
