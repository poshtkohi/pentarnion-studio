#include "graph_tools.hpp"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace penta::graph_tools {
namespace {
struct Loaded {void* handle=nullptr;const penta_graph_tools_api_v1* api=nullptr;};
std::mutex moduleMutex;
std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> value(32768);const auto count=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    if(!count||count>=value.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate graph module executable directory.");
    return std::filesystem::path(std::wstring(value.data(),count)).parent_path();
#else
    std::vector<char> value(32768);const auto count=readlink("/proc/self/exe",value.data(),value.size());
    if(count<=0||static_cast<std::size_t>(count)>=value.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate graph module executable directory.");
    return std::filesystem::path(std::string(value.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
struct Host {
    const std::shared_ptr<std::atomic_bool>* cancellation;
    const std::function<void(std::size_t)>* charge;
    std::exception_ptr failure;
    static int cancelled(void* raw) noexcept {
        const auto& self=*static_cast<Host*>(raw);
        return *self.cancellation&&(*self.cancellation)->load(std::memory_order_relaxed);
    }
    static int account(void* raw,uint64_t amount,char* error,uint64_t capacity) noexcept {
        auto& self=*static_cast<Host*>(raw);
        try {if(*self.charge)(*self.charge)(static_cast<std::size_t>(amount));return 0;}
        catch(const std::exception& exception) {
            self.failure=std::current_exception();
            if(error&&capacity){const auto count=std::min<std::size_t>(capacity-1,std::strlen(exception.what()));std::memcpy(error,exception.what(),count);error[count]=0;}
            return 1;
        } catch(...) {self.failure=std::current_exception();return 1;}
    }
};
}

Module::Module(std::filesystem::path directory) {
    if(directory.empty())directory=executableDirectory()/"modules";
    if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"Graph module directory must be absolute.");
#ifdef _WIN32
    path_=directory/"penta_graph_tools.dll";
#else
    path_=directory/"libpenta_graph_tools.so";
#endif
}
const penta_graph_tools_api_v1& Module::api() const {
    std::lock_guard lock(moduleMutex);
    if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
    Loaded loaded;
#ifdef _WIN32
    loaded.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_graph_tools module or its dependencies.");
    const auto query=reinterpret_cast<penta_graph_tools_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_graph_tools_query_v1")));
    auto release=[&]{FreeLibrary(static_cast<HMODULE>(loaded.handle));};
#else
    loaded.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_graph_tools module or its dependencies.");
    const auto query=reinterpret_cast<penta_graph_tools_query_function_v1>(dlsym(loaded.handle,"penta_graph_tools_query_v1"));
    auto release=[&]{dlclose(loaded.handle);};
#endif
    loaded.api=query?query(PENTA_GRAPH_TOOLS_ABI_V1):nullptr;
    if(!loaded.api||loaded.api->struct_size<sizeof(penta_graph_tools_api_v1)||loaded.api->abi_version!=PENTA_GRAPH_TOOLS_ABI_V1||!loaded.api->execute){release();throw Error(PENTA_NUMERIC_VERSION,"Incompatible graph module ABI.");}
    return *modules.emplace(path_,loaded).first->second.api;
}
bool Module::isLoaded() noexcept {std::lock_guard lock(moduleMutex);return !modules.empty();}
Result Module::execute(penta_graph_tools_request_v1 request,std::size_t entries,std::size_t pointers,std::size_t endpoints,
        const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
    if(entries>1100000||pointers>100001||endpoints>1000000)throw Error(PENTA_NUMERIC_ARGUMENT,"Graph tools native output budget exceeded.");
    Result result;result.indices.resize(entries);result.values.resize(entries);result.pointers.resize(pointers);result.endpoints.resize(endpoints);
    auto& r=result.report;r.struct_size=sizeof(r);r.row_indices=result.indices.data();r.values=result.values.data();r.column_pointers=result.pointers.data();r.endpoints=result.endpoints.data();r.entry_capacity=entries;r.pointer_capacity=pointers;r.endpoint_capacity=endpoints;
    request.struct_size=sizeof(request);Host context{&cancellation,&charge,{}};const penta_graph_host_v1 host{sizeof(host),PENTA_GRAPH_TOOLS_ABI_V1,&context,Host::cancelled,Host::account};char error[1024]{};int status=0;
    runtime::ThreadBudget::instance().withReservation([&]{status=api().execute(&host,&request,&r,error,sizeof(error));});
    if(context.failure)std::rethrow_exception(context.failure);
    if(status)throw Error(status,error[0]?error:"Native graph tools failed.");
    if(r.entries>entries||(r.edges>endpoints/2&&request.operation==PENTA_GRAPH_TOOLS_GENERATE)||(r.columns+1>pointers&&request.operation==PENTA_GRAPH_TOOLS_SPARSE))throw Error(PENTA_NUMERIC_VERSION,"Graph tools returned an invalid output count.");
    if(request.operation==PENTA_GRAPH_TOOLS_SPARSE){result.indices.resize(r.entries);result.values.resize(r.entries);result.pointers.resize(r.columns+1);result.endpoints.clear();}
    else{result.indices.clear();result.values.clear();result.pointers.clear();result.endpoints.resize(2*r.edges);}
    r.row_indices=nullptr;r.values=nullptr;r.column_pointers=nullptr;r.endpoints=nullptr;return result;
}
}
