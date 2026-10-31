#include "constrained_mechanics.hpp"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <cmath>
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
namespace penta::mechanics {
namespace {
struct Loaded{void* handle;const penta_mechanics_constraint_api_v1* api;};std::mutex mutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
 std::vector<wchar_t> value(32768);const auto n=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));if(!n||n>=value.size())throw Error(4,"Cannot locate mechanics module directory.");return std::filesystem::path(std::wstring(value.data(),n)).parent_path();
#else
 std::vector<char> value(32768);const auto n=readlink("/proc/self/exe",value.data(),value.size());if(n<=0||static_cast<std::size_t>(n)>=value.size())throw Error(4,"Cannot locate mechanics module directory.");return std::filesystem::path(std::string(value.data(),n)).parent_path();
#endif
}
struct Host {
 const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;
 static int cancelled(void* raw)noexcept{const auto& self=*static_cast<Host*>(raw);return self.cancellation&&self.cancellation->load(std::memory_order_relaxed);}
 static int account(void* raw,uint64_t work,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{if(self.charge)self.charge(work);return 0;}catch(const std::exception& e){self.failure=std::current_exception();if(error&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return 1;}catch(...){self.failure=std::current_exception();return 1;}}
 penta_mechanics_host_v1 descriptor(){return {sizeof(penta_mechanics_host_v1),PENTA_MECHANICS_CONSTRAINT_ABI_V1,this,cancelled,account};}
};
}
ConstrainedModule::ConstrainedModule(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"Mechanics module directory must be absolute.");
#ifdef _WIN32
 path_=directory/"penta_mechanics.dll";
#else
 path_=directory/"libpenta_mechanics.so";
#endif
}
const penta_mechanics_constraint_api_v1& ConstrainedModule::api()const{
 std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
 const auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(4,"Cannot load optional mechanics module or dependencies.");const auto query=reinterpret_cast<penta_mechanics_constraint_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_mechanics_constraint_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
 const auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(4,"Cannot load optional mechanics module or dependencies.");const auto query=reinterpret_cast<penta_mechanics_constraint_query_function_v1>(dlsym(handle,"penta_mechanics_constraint_query_v1"));auto release=[&]{dlclose(handle);};
#endif
 const auto result=query?query(PENTA_MECHANICS_CONSTRAINT_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_MECHANICS_CONSTRAINT_ABI_V1||!result->solve){release();throw Error(5,"Mechanics module lacks the optional holonomic constraint capability.");}return *modules.emplace(path_,Loaded{handle,result}).first->second.api;
}
ConstrainedResult ConstrainedModule::solve(penta_holonomic_request_v1 request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
 if(request.dimension<1||request.dimension>16||request.constraints<1||request.constraints>request.dimension||!std::isfinite(request.t0)||!std::isfinite(request.t1)||request.t1<request.t0||!std::isfinite(request.sample_step)||request.sample_step<=0)throw Error(1,"Invalid holonomic coordinate/time dimensions.");
 const double estimate=std::ceil((request.t1-request.t0)/request.sample_step)+2;const auto width=3*request.dimension+5*request.constraints+8;if(!std::isfinite(estimate)||estimate>100000||estimate*width>8000000)throw Error(7,"Holonomic retained histories exceed bounded coordinate storage.");const auto capacity=static_cast<uint64_t>(estimate);ConstrainedResult result;auto& out=result.report;out.struct_size=sizeof(out);out.capacity=capacity;
 auto buffer=[&](std::vector<double>& values,double*& pointer,std::size_t components){values.resize(capacity*components);pointer=values.data();};buffer(result.times,out.times,1);buffer(result.q,out.q,request.dimension);buffer(result.v,out.v,request.dimension);buffer(result.p,out.p,request.dimension);buffer(result.energy,out.energy,1);buffer(result.kinetic,out.kinetic,1);buffer(result.potential,out.potential,1);buffer(result.g,out.g,request.constraints);buffer(result.gv,out.gv,request.constraints);buffer(result.lambda,out.lambda,request.constraints);buffer(result.positionMultipliers,out.position_multipliers,request.constraints);buffer(result.velocityMultipliers,out.velocity_multipliers,request.constraints);buffer(result.positionError,out.position_error,1);buffer(result.velocityError,out.velocity_error,1);buffer(result.positionIterations,out.position_iterations,1);buffer(result.correction,out.correction,1);request.struct_size=sizeof(request);Host context{cancellation,charge,{}};auto host=context.descriptor();char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().solve(&host,&request,&out,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Native holonomic mechanics failed.");if(out.samples>capacity)throw Error(5,"Invalid native holonomic output count.");
 auto trim=[&](std::vector<double>& values,double*& pointer,std::size_t components){values.resize(out.samples*components);pointer=nullptr;};trim(result.times,out.times,1);trim(result.q,out.q,request.dimension);trim(result.v,out.v,request.dimension);trim(result.p,out.p,request.dimension);trim(result.energy,out.energy,1);trim(result.kinetic,out.kinetic,1);trim(result.potential,out.potential,1);trim(result.g,out.g,request.constraints);trim(result.gv,out.gv,request.constraints);trim(result.lambda,out.lambda,request.constraints);trim(result.positionMultipliers,out.position_multipliers,request.constraints);trim(result.velocityMultipliers,out.velocity_multipliers,request.constraints);trim(result.positionError,out.position_error,1);trim(result.velocityError,out.velocity_error,1);trim(result.positionIterations,out.position_iterations,1);trim(result.correction,out.correction,1);return result;
}
}
