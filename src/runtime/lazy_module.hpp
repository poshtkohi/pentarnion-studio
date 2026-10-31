#pragma once
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace penta::runtime::detail {
inline std::filesystem::path nativeModuleDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto n=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!n||n>=buffer.size())throw std::runtime_error("Cannot determine the native module directory.");
    return std::filesystem::path(std::wstring(buffer.data(),n)).parent_path()/"modules";
#else
    std::vector<char> buffer(32768);const auto n=readlink("/proc/self/exe",buffer.data(),buffer.size());
    if(n<=0||static_cast<std::size_t>(n)>=buffer.size())throw std::runtime_error("Cannot determine the native module directory.");
    return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(n))).parent_path()/"modules";
#endif
}
inline std::filesystem::path nativeModulePath(std::filesystem::path directory,const char* name){
    if(directory.empty())directory=nativeModuleDirectory();
    if(!directory.is_absolute())throw std::runtime_error("Native module directories must be absolute host-controlled paths.");
#ifdef _WIN32
    return (directory/(std::string(name)+".dll")).lexically_normal();
#else
    return (directory/(std::string("lib")+name+".so")).lexically_normal();
#endif
}
template<class Api> struct NativeModuleRegistry {
    struct Entry {void* handle;const Api* api;};
    static auto& entries(){static std::map<std::filesystem::path,Entry> value;return value;}
    static auto& mutex(){static std::mutex value;return value;}
    static bool loaded(const std::filesystem::path& path){std::lock_guard lock(mutex());return entries().contains(path);}
    static const Api& get(const std::filesystem::path& path,const char* symbol,uint32_t abi){
        std::lock_guard lock(mutex());if(const auto found=entries().find(path);found!=entries().end())return *found->second.api;
        using Query=const Api*(*)(uint32_t);
#ifdef _WIN32
        void* handle=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!handle)throw std::runtime_error("Cannot load optional native module (Windows error "+std::to_string(GetLastError())+").");
        const auto query=reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),symbol)));
        auto release=[&]{FreeLibrary(static_cast<HMODULE>(handle));};
#else
        void* handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle){const auto* e=dlerror();throw std::runtime_error(std::string("Cannot load optional native module: ")+(e?e:"unknown loader failure"));}
        const auto query=reinterpret_cast<Query>(dlsym(handle,symbol));auto release=[&]{dlclose(handle);};
#endif
        const auto* api=query?query(abi):nullptr;
        if(!api||api->struct_size<sizeof(Api)||api->abi_version!=abi){release();throw std::runtime_error("Native module query/API/ABI is incompatible.");}
        entries().emplace(path,Entry{handle,api});return *api;
    }
};
}
