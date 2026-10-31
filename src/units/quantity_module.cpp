#include "quantity_module.hpp"
#include <algorithm>
#include <cstring>
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
namespace penta::units {
namespace {
struct Loaded{void* handle;const penta_units_api_v1* api;const penta_units_api_v2* extended=nullptr;};std::mutex mutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
 std::vector<wchar_t> value(32768);const auto n=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));if(!n||n>=value.size())throw Error(4,"Cannot locate SI unit module directory.");return std::filesystem::path(std::wstring(value.data(),n)).parent_path();
#else
 std::vector<char> value(32768);const auto n=readlink("/proc/self/exe",value.data(),value.size());if(n<=0||static_cast<std::size_t>(n)>=value.size())throw Error(4,"Cannot locate SI unit module directory.");return std::filesystem::path(std::string(value.data(),n)).parent_path();
#endif
}
void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Host{const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;runtime::ParallelSettings settings;runtime::ParallelStatistics statistics;std::exception_ptr failure;static int cancelled(void* raw)noexcept{const auto& self=*static_cast<Host*>(raw);return self.cancellation&&self.cancellation->load(std::memory_order_relaxed);}static int account(void* raw,uint64_t amount,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{if(self.charge)self.charge(amount);return 0;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return 1;}catch(...){self.failure=std::current_exception();return 1;}}static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* context,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{std::string text;const int status=runtime::ThreadBudget::instance().run(count,cost,task,context,self.settings,self.cancellation,self.statistics,text);if(status)message(error,capacity,text.c_str());return status;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return 4;}catch(...){self.failure=std::current_exception();return 4;}}penta_units_host_v1 descriptor(){return {sizeof(penta_units_host_v1),PENTA_UNITS_ABI_V1,this,cancelled,account,parallel};}};
uint64_t size(uint64_t rows,uint64_t columns){if(rows>8000000||columns>100000||(columns&&rows>8000000/columns))throw Error(7,"SI quantity dimensions exceed bounded storage.");return rows*columns;}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"SI module directory must be absolute.");
#ifdef _WIN32
 path_=directory/"penta_units.dll";
#else
 path_=directory/"libpenta_units.so";
#endif
}
const penta_units_api_v1& Module::api()const{std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
 const auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(4,"Cannot load optional SI quantity module or dependencies.");const auto query=reinterpret_cast<penta_units_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_units_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
 const auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(4,"Cannot load optional SI quantity module or dependencies.");const auto query=reinterpret_cast<penta_units_query_function_v1>(dlsym(handle,"penta_units_query_v1"));auto release=[&]{dlclose(handle);};
#endif
 const auto result=query?query(PENTA_UNITS_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_UNITS_ABI_V1||!result->parse||!result->execute){release();throw Error(5,"Incompatible optional SI quantity module ABI.");}return *modules.emplace(path_,Loaded{handle,result}).first->second.api;}
penta_si_unit_v1 Module::parse(const std::string& text)const{if(text.size()>512||text.find('\0')!=std::string::npos)throw Error(1,"SI expressions require at most512 bytes without embedded NUL.");penta_si_unit_v1 result{};char error[1024]{};const auto status=api().parse(text.c_str(),&result,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Native SI unit parsing failed.");return result;}
const penta_units_api_v2& Module::extendedApi()const{(void)api();std::lock_guard lock(mutex);auto& loaded=modules.at(path_);if(loaded.extended)return *loaded.extended;
#ifdef _WIN32
 const auto query=reinterpret_cast<penta_units_query_function_v2>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_units_query_v2")));
#else
 const auto query=reinterpret_cast<penta_units_query_function_v2>(dlsym(loaded.handle,"penta_units_query_v2"));
#endif
 const auto* result=query?query(PENTA_UNITS_ABI_V2):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_UNITS_ABI_V2||!result->parse||!result->derive||!result->execute)throw Error(5,"Incompatible optional rational/affine quantity ABI.");loaded.extended=result;return *result;}
penta_si_unit_v2 Module::parseExtended(const std::string& text)const{if(text.size()>512||text.find('\0')!=std::string::npos)throw Error(1,"Extended SI labels require at most512 bytes without NUL.");penta_si_unit_v2 result{};char error[1024]{};const auto status=extendedApi().parse(text.c_str(),&result,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Extended unit parsing failed.");return result;}
penta_si_unit_v2 Module::derive(penta_units_request_v2 request)const{request.struct_size=sizeof(request);penta_si_unit_v2 result{};char error[1024]{};const auto status=extendedApi().derive(&request,&result,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Extended unit derivation failed.");return result;}
ExtendedResult Module::execute(penta_units_request_v2 request,const runtime::ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{const auto na=size(request.a_rows,request.a_columns);size(request.b_rows,request.b_columns);const bool binary=request.operation!=PENTA_UNIT_CONVERT&&request.operation!=PENTA_UNIT_POWER;const auto rows=binary&&na==1?request.b_rows:request.a_rows,columns=binary&&na==1?request.b_columns:request.a_columns,count=size(rows,columns);ExtendedResult result;result.values.resize(count);penta_units_result_v2 out{};out.struct_size=sizeof(out);out.capacity=count;out.values=result.values.data();request.struct_size=sizeof(request);Host context{cancellation,charge,settings,{},{}};auto host=context.descriptor();char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=extendedApi().execute(&host,&request,&out,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Extended quantity arithmetic failed.");if(out.rows!=rows||out.columns!=columns)throw Error(5,"Invalid extended quantity output dimensions.");result.rows=out.rows;result.columns=out.columns;result.unit=out.unit;result.work=out.work_units;result.parallel=context.statistics;return result;}
Result Module::execute(penta_units_request_v1 request,const runtime::ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{const auto na=size(request.a_rows,request.a_columns);size(request.b_rows,request.b_columns);const bool binary=request.operation!=PENTA_UNIT_CONVERT&&request.operation!=PENTA_UNIT_POWER;const auto rows=binary&&na==1?request.b_rows:request.a_rows,columns=binary&&na==1?request.b_columns:request.a_columns,count=size(rows,columns);Result result;result.values.resize(count);penta_units_result_v1 out{};out.struct_size=sizeof(out);out.capacity=count;out.values=result.values.data();request.struct_size=sizeof(request);Host context{cancellation,charge,settings,{},{}};auto host=context.descriptor();char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().execute(&host,&request,&out,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Native SI quantity arithmetic failed.");if(out.rows!=rows||out.columns!=columns)throw Error(5,"Invalid native SI output dimensions.");result.rows=out.rows;result.columns=out.columns;result.unit=out.unit;result.work=out.work_units;result.parallel=context.statistics;return result;}
}
