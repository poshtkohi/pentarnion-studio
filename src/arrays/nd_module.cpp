#include "nd_module.hpp"
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
namespace penta::arrays {
namespace {
struct Loaded {void* handle;const penta_nd_api_v1* api;};std::mutex moduleMutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executableDirectory(){
#ifdef _WIN32
 std::vector<wchar_t> text(32768);const auto n=GetModuleFileNameW(nullptr,text.data(),static_cast<DWORD>(text.size()));if(!n||n>=text.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate N-D module directory.");return std::filesystem::path(std::wstring(text.data(),n)).parent_path();
#else
 std::vector<char> text(32768);const auto n=readlink("/proc/self/exe",text.data(),text.size());if(n<=0||static_cast<std::size_t>(n)>=text.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate N-D module directory.");return std::filesystem::path(std::string(text.data(),n)).parent_path();
#endif
}
void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Host {
 const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;const MapCallback& callback;runtime::ParallelSettings settings;runtime::ParallelStatistics statistics;std::mutex mutex;std::exception_ptr failure;
 void capture()noexcept{std::lock_guard lock(mutex);if(!failure)failure=std::current_exception();}
 static int cancelled(void* raw)noexcept{const auto& h=*static_cast<Host*>(raw);return h.cancellation&&h.cancellation->load(std::memory_order_relaxed);}
 static int account(void* raw,uint64_t amount,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.charge)h.charge(static_cast<std::size_t>(amount));return 0;}catch(const std::exception& e){h.capture();message(error,cap,e.what());return 1;}catch(...){h.capture();return 1;}}
 static int parallel(void* raw,uint64_t count,uint64_t cost,uint64_t limit,penta_numeric_range_task_v1 task,void* taskContext,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{auto settings=h.settings;if(limit)settings.threads=std::min<std::size_t>(runtime::ThreadBudget::effectiveThreads(settings.threads),static_cast<std::size_t>(limit));std::string text;const auto status=runtime::ThreadBudget::instance().run(static_cast<std::size_t>(count),static_cast<std::size_t>(cost),task,taskContext,settings,h.cancellation,h.statistics,text);if(status)message(error,cap,text.c_str());return status;}catch(const std::exception& e){h.capture();message(error,cap,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){h.capture();return PENTA_NUMERIC_INTERNAL;}}
 static int map(void* raw,const double* input,uint32_t inputWidth,double* output,uint32_t outputWidth,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{if(!h.callback)throw Error(PENTA_NUMERIC_ARGUMENT,"Missing prepared native N-D callback.");h.callback(input,inputWidth,output,outputWidth);return 0;}catch(const std::exception& e){h.capture();message(error,cap,e.what());return PENTA_NUMERIC_DOMAIN;}catch(...){h.capture();return PENTA_NUMERIC_INTERNAL;}}
};
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"N-D module directory must be absolute.");
#ifdef _WIN32
 path_=directory/"penta_nd.dll";
#else
 path_=directory/"libpenta_nd.so";
#endif
}
const penta_nd_api_v1& Module::api()const{
 std::lock_guard lock(moduleMutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
 const auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_nd module or its dependencies.");const auto query=reinterpret_cast<penta_nd_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_nd_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
 const auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_nd module or its dependencies.");const auto query=reinterpret_cast<penta_nd_query_function_v1>(dlsym(handle,"penta_nd_query_v1"));auto release=[&]{dlclose(handle);};
#endif
 const auto api=query?query(PENTA_ND_ABI_V1):nullptr;if(!api||api->struct_size<sizeof(*api)||api->abi_version!=PENTA_ND_ABI_V1||!api->execute){release();throw Error(PENTA_NUMERIC_VERSION,"Incompatible N-D module ABI.");}return *modules.emplace(path_,Loaded{handle,api}).first->second.api;
}
Result Module::execute(penta_nd_request_v1 request,std::size_t capacity,const runtime::ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge,const MapCallback& map)const{
 if(capacity>8000000)throw Error(PENTA_NUMERIC_ARGUMENT,"N-D output exceeds eight-million coordinates.");
 Result result;result.values.resize(capacity);result.report.struct_size=sizeof(result.report);request.struct_size=sizeof(request);request.output=result.values.data();request.output_capacity=capacity;
 Host context{cancellation,charge,map,settings,{},{},{}};const penta_nd_host_v1 host{sizeof(host),PENTA_ND_ABI_V1,&context,Host::cancelled,Host::account,Host::parallel,Host::map};char error[1024]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().execute(&host,&request,&result.report,error,sizeof(error));});
 if(context.failure)std::rethrow_exception(context.failure);
 if(status)throw Error(status,error[0]?error:"N-D native operation failed.");
 if(result.report.output_count>result.values.size())throw Error(PENTA_NUMERIC_VERSION,"Invalid N-D module output count.");
 result.values.resize(static_cast<std::size_t>(result.report.output_count));result.parallel=context.statistics;return result;
}
}
