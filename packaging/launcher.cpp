#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs=std::filesystem;
std::wstring winQuote(const std::wstring& text){
    std::wstring out=L"\"";unsigned slashes=0;
    for(wchar_t ch:text){if(ch==L'\\'){++slashes;continue;}if(ch==L'\"'){out.append(2*slashes+1,L'\\');out+=ch;}else{out.append(slashes,L'\\');out+=ch;}slashes=0;}
    out.append(2*slashes,L'\\');return out+L'\"';
}
fs::path modulePath(){std::vector<wchar_t> b(32768);DWORD n=GetModuleFileNameW(nullptr,b.data(),DWORD(b.size()));if(!n||n>=b.size())throw std::runtime_error("Application path unavailable.");return fs::path(std::wstring(b.data(),n));}
void variable(const wchar_t* name,const fs::path& value){if(!SetEnvironmentVariableW(name,value.c_str()))throw std::runtime_error("Cannot configure the private application environment.");}
int run(){
    const auto launcher=modulePath(),root=launcher.parent_path(),invocationDirectory=fs::current_path();
    const bool python=CompareStringOrdinal(launcher.filename().c_str(),-1,L"penta-python.exe",-1,TRUE)==CSTR_EQUAL;
    const auto real=python?root/L"toolchain/python/python.exe":root/L"bin"/launcher.filename();
    if(!fs::is_regular_file(real))throw std::runtime_error("Installed application executable is missing.");
    variable(L"PENTA_INSTALL_ROOT",root);variable(L"PENTA_NATIVE_COMPILER",root/L"toolchain/ucrt64/bin/g++.exe");
    variable(L"PENTA_RUNTIME_SDK",root/L"runtime-sdk");variable(L"PENTA_INSTALL_SETTINGS",root/L"settings/Workbench.ini");
    const auto cache=root/L"cache";fs::create_directories(cache);variable(L"PENTA_NATIVE_CACHE",cache/L"native");
    if(python){
        variable(L"PYTHONPYCACHEPREFIX",cache/L"python");variable(L"MPLCONFIGDIR",cache/L"matplotlib");
        const auto tcl=root/L"toolchain/python/tcl";
        if(fs::is_directory(tcl))for(const auto& entry:fs::directory_iterator(tcl)){
            if(fs::is_regular_file(entry.path()/L"init.tcl"))variable(L"TCL_LIBRARY",entry.path());
            if(fs::is_regular_file(entry.path()/L"tk.tcl"))variable(L"TK_LIBRARY",entry.path());
        }
        if(!GetEnvironmentVariableW(L"MPLBACKEND",nullptr,0)&&fs::is_directory(tcl))SetEnvironmentVariableW(L"MPLBACKEND",L"TkAgg");
    }
    std::wstring path=(root/L"bin").wstring()+L";"+(root/L"bin/modules").wstring()+L";"+(root/L"toolchain/ucrt64/bin").wstring();
    wchar_t windows[32768];const UINT length=GetWindowsDirectoryW(windows,32768);if(!length||length>=32768)throw std::runtime_error("Windows directory unavailable.");
    path+=L";"+(root/L"toolchain/bin").wstring()+L";"+(fs::path(windows)/L"System32").wstring()+L";"+windows;
    if(!SetEnvironmentVariableW(L"PATH",path.c_str()))throw std::runtime_error("Cannot configure private compiler search path.");
    variable(L"QT_PLUGIN_PATH",root/L"bin");variable(L"QT_QPA_PLATFORM_PLUGIN_PATH",root/L"bin/platforms");
    // Use an installer-recorded workspace, not a developer path or the app tree.
    std::wstring workspace;
    const auto record=root/L"workspace-path.txt";
    HANDLE file=CreateFileW(record.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD size=GetFileSize(file,nullptr);if(size>0&&size<65536){std::string bytes(size,'\0');DWORD read=0;if(ReadFile(file,bytes.data(),size,&read,nullptr)){const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(read),nullptr,0);if(count>0){workspace.resize(count);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(read),workspace.data(),count);while(!workspace.empty()&&(workspace.back()==L'\r'||workspace.back()==L'\n'))workspace.pop_back();}}}CloseHandle(file);}
    if(workspace.empty()){PWSTR documents=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,KF_FLAG_DEFAULT,nullptr,&documents))){workspace=(fs::path(documents)/L"Pentarnion Studio").wstring();CoTaskMemFree(documents);}}
    if(!workspace.empty()){
        variable(L"PENTA_DEFAULT_PROJECT",fs::path(workspace)/L"Examples/calculations/project.pprj");
        variable(L"PENTA_RECOVERY_ROOT",fs::path(workspace)/L"Recovery");
    }
    int count=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)throw std::runtime_error("Command line unavailable.");
    std::wstring command=winQuote(real.wstring());
    // Embeddable Python's _pth imports site explicitly. Also use -I so a user
    // site-packages directory can never enter this private runtime; explicit
    // -X keeps bytecode under the owned app even when Python ignores env vars.
    if(python)command+=L" -I -X "+winQuote(L"pycache_prefix="+(cache/L"python").wstring());
    for(int i=1;i<count;++i){
        std::wstring argument=args[i];
#ifdef PENTA_GUI_LAUNCHER
        // Shortcuts start in the workspace; explicit relative project arguments
        // still refer to the caller's directory, before that cwd is changed.
        if((argument==L"--project"||argument==L"-p")&&i+1<count){command+=L" "+winQuote(argument);argument=args[++i];if(!argument.empty())argument=fs::absolute(fs::path(argument)).wstring();}
        else if(argument.starts_with(L"--project=")){const auto value=argument.substr(10);if(!value.empty())argument=L"--project="+fs::absolute(fs::path(value)).wstring();}
#endif
        command+=L" "+winQuote(argument);
    }
    LocalFree(args);
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
#ifdef PENTA_GUI_LAUNCHER
    const fs::path cwd=workspace.empty()?root:fs::path(workspace);fs::create_directories(cwd);
#else
    const fs::path cwd=invocationDirectory;
#endif
    HANDLE active=CreateMutexW(nullptr,FALSE,L"PentarnionStudioManagedInstaller-v1");if(!active)throw std::runtime_error("Cannot register the active application for safe updates.");
    if(!CreateProcessW(real.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&startup,&process)){CloseHandle(active);throw std::runtime_error("The installed application could not start. Please reinstall and check the installed component notices.");}
    CloseHandle(process.hThread);
    // Keep this invisible launcher alive with the child. The named mutex makes
    // install/update/uninstall refuse while any installed calculation runs.
    WaitForSingleObject(process.hProcess,INFINITE);DWORD status=1;GetExitCodeProcess(process.hProcess,&status);CloseHandle(process.hProcess);CloseHandle(active);return int(status);
}
int wmain(){try{return run();}catch(const std::exception& error){std::string text=error.what();std::wstring wide(text.begin(),text.end());MessageBoxW(nullptr,wide.c_str(),L"Pentarnion Studio",MB_OK|MB_ICONERROR);return 2;}}
