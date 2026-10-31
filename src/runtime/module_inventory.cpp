#include "module_inventory.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace penta::runtime {
namespace {
struct Description {const char* name;const char* query;};
constexpr std::array descriptions{
    Description{"numerics","penta_numeric_query_v1"},Description{"linalg","penta_linalg_query_v1"},Description{"roots","penta_roots_query_v1"},
    Description{"fit","penta_fit_query_v1"},Description{"opt","penta_opt_query_v1"},Description{"maps","penta_maps_query_v1"},
    Description{"pde","penta_pde_query_v1"},Description{"pde_experiments","penta_pde_experiment_query_v1"},Description{"fem","penta_fem_query_v1"},
    Description{"chaos","penta_chaos_query_v1"},Description{"discovery","penta_discovery_query_v1"},Description{"orbits","penta_orbits_query_v1"},
    Description{"bifurcation","penta_bifurcation_query_v1"},Description{"graph","penta_graph_query_v1"},Description{"graph_tools","penta_graph_tools_query_v1"},
    Description{"networks","penta_network_query_v1"},Description{"geometry","penta_geometry_query_v1"},Description{"sensitivity","penta_sensitivity_query_v1"},
    Description{"dae","penta_dae_query_v1"},Description{"mechanics","penta_mechanics_query_v1"},Description{"units","penta_units_query_v1"},
    Description{"nd","penta_nd_query_v1"},Description{"inference","penta_inference_query_v1"},Description{"evolution","penta_evolution_query_v1"},Description{"science","penta_science_query_v1"},Description{"petsc","penta_petsc_query_v1"},Description{"cas","penta_cas_query_v1"},Description{"vector_plot","penta_vector_plot_query_v1"}};
constexpr std::uint32_t expectedAbi=0x00010000u;
std::mutex mutex;
std::map<std::filesystem::path,void*> retained;
std::filesystem::path moduleDirectory(std::filesystem::path directory){
    if(!directory.empty()){if(!directory.is_absolute())throw std::runtime_error("Module directories must be absolute host-controlled paths.");return directory.lexically_normal();}
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!count||count>=buffer.size())throw std::runtime_error("Cannot determine the application module directory.");
    return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path()/"modules";
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());
    if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw std::runtime_error("Cannot determine the application module directory.");
    return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path()/"modules";
#endif
}
std::filesystem::path modulePath(const std::filesystem::path& directory,const Description& description){
#ifdef _WIN32
    return directory/(std::string("penta_")+description.name+".dll");
#else
    return directory/(std::string("libpenta_")+description.name+".so");
#endif
}
void release(void* handle){
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}
void* opened(const std::filesystem::path& path,bool load){
#ifdef _WIN32
    if(!load)return GetModuleHandleW(path.c_str());
    return LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    return dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL|(load?0:RTLD_NOLOAD));
#endif
}
std::uint32_t queryAbi(void* handle,const Description& description){
    using Query=const void*(*)(std::uint32_t);
#ifdef _WIN32
    const auto query=reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),description.query)));
#else
    const auto query=reinterpret_cast<Query>(dlsym(handle,description.query));
#endif
    if(!query)return 0;
    const auto* api=query(expectedAbi);if(!api)return 0;
    std::uint32_t prefix[2]{};std::memcpy(prefix,api,sizeof(prefix));return prefix[0]>=sizeof(prefix)?prefix[1]:0;
}
ModuleInfo inspect(const Description& description,const std::filesystem::path& directory){
    ModuleInfo item;item.name=description.name;item.path=modulePath(directory,description);std::error_code error;item.available=std::filesystem::is_regular_file(item.path,error);
    if(auto* handle=opened(item.path,false)){item.loaded=true;item.abi=queryAbi(handle,description);
#ifndef _WIN32
        release(handle);
#endif
    }
    item.status=item.loaded?(item.abi==expectedAbi?"loaded":"incompatible_abi"):item.available?"available":"missing";return item;
}
}
std::vector<ModuleInfo> moduleInventory(const std::filesystem::path& requested){const auto directory=moduleDirectory(requested);std::lock_guard lock(mutex);std::vector<ModuleInfo> result;for(const auto& description:descriptions)result.push_back(inspect(description,directory));return result;}
ModuleInfo loadModule(const std::string& name,const std::filesystem::path& requested){
    const auto found=std::find_if(descriptions.begin(),descriptions.end(),[&](const auto& item){return name==item.name;});if(found==descriptions.end())throw std::runtime_error("Unknown Studio module name: "+name);
    const auto directory=moduleDirectory(requested);std::lock_guard lock(mutex);auto item=inspect(*found,directory);
    if(item.loaded){if(item.abi!=expectedAbi)throw std::runtime_error("Loaded module has an incompatible catalogue ABI.");return item;}
    if(!item.available)throw std::runtime_error("The selected module is missing from the configured module directory.");
    auto* handle=opened(item.path,true);if(!handle)throw std::runtime_error("The module or one of its native dependencies could not be loaded.");
    if(queryAbi(handle,*found)!=expectedAbi){release(handle);throw std::runtime_error("The module does not expose the expected Studio ABI1 query.");}
    retained.emplace(item.path,handle);return inspect(*found,directory);
}
}
