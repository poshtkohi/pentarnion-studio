#include "geometry_module.hpp"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <cmath>
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
namespace penta::geometry {
namespace {
struct Loaded{void* handle;const penta_geometry_api_v1* api;};std::mutex mutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
    std::vector<wchar_t> data(32768);const auto n=GetModuleFileNameW(nullptr,data.data(),static_cast<DWORD>(data.size()));if(!n||n>=data.size())throw Error(1,"Cannot locate geometry module executable directory.");return std::filesystem::path(std::wstring(data.data(),n)).parent_path();
#else
    std::vector<char> data(32768);const auto n=readlink("/proc/self/exe",data.data(),data.size());if(n<=0||static_cast<std::size_t>(n)>=data.size())throw Error(1,"Cannot locate geometry module executable directory.");return std::filesystem::path(std::string(data.data(),n)).parent_path();
#endif
}
struct Host{const std::shared_ptr<std::atomic_bool>& flag;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;};
int cancelled(void* raw){auto& h=*static_cast<Host*>(raw);return h.flag&&h.flag->load(std::memory_order_relaxed);}
int charge(void* raw,uint64_t work,char* error,uint64_t capacity)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.charge)h.charge(work);return 0;}catch(const std::exception& e){h.failure=std::current_exception();if(error&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return 1;}catch(...){h.failure=std::current_exception();return 1;}}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"Geometry module directory must be absolute.");
#ifdef _WIN32
path_=directory/"penta_geometry.dll";
#else
path_=directory/"libpenta_geometry.so";
#endif
}
const penta_geometry_api_v1& Module::api()const{std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
    auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(1,"Cannot load optional penta_geometry or a dependency.");const auto query=reinterpret_cast<penta_geometry_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_geometry_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
    auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(1,"Cannot load optional penta_geometry or a dependency.");const auto query=reinterpret_cast<penta_geometry_query_function_v1>(dlsym(handle,"penta_geometry_query_v1"));auto release=[&]{dlclose(handle);};
#endif
    const auto* result=query?query(PENTA_GEOMETRY_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_GEOMETRY_ABI_V1||!result->point||!result->geodesic){release();throw Error(5,"Incompatible differential geometry module ABI.");}modules.emplace(path_,Loaded{handle,result});return *result;
}
penta_chart_point_v1 Module::point(const penta_chart_sample_v1& sample,double tolerance)const{penta_chart_point_v1 result{};result.struct_size=sizeof(result);char error[2048]{};const auto status=api().point(&sample,tolerance,&result,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Chart geometry failed.");return result;}
Path Module::geodesic(const penta_geodesic_request_v1& request,const std::shared_ptr<std::atomic_bool>& flag,const std::function<void(std::size_t)>& operationCharge)const{
    if(request.dimension<2||request.dimension>5||!std::isfinite(request.t0)||!std::isfinite(request.t1)||!std::isfinite(request.sample_step)||request.t1<request.t0||request.sample_step<=0)throw Error(1,"Invalid geodesic dimensions/time settings.");
    const double count=std::ceil((request.t1-request.t0)/request.sample_step)+2;if(!std::isfinite(count)||count>1000000||count*(request.dimension+10)>8000000)throw Error(1,"Geodesic retained-coordinate budget exceeded.");const auto samples=static_cast<std::size_t>(count);
    Path result;result.times.resize(samples);result.coordinates.resize(samples*2);result.velocities.resize(samples*2);result.positions.resize(samples*request.dimension);result.transport.resize(samples*2);result.speedSquared.resize(samples);result.transportNormSquared.resize(samples);auto& r=result.report;r.struct_size=sizeof(r);r.capacity=samples;r.times=result.times.data();r.coordinates=result.coordinates.data();r.velocity=result.velocities.data();r.positions=result.positions.data();r.transport=result.transport.data();r.speed_squared=result.speedSquared.data();r.transport_norm_squared=result.transportNormSquared.data();
    Host context{flag,operationCharge,{}};penta_geometry_host_v1 host{sizeof(host),PENTA_GEOMETRY_ABI_V1,&context,cancelled,charge};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().geodesic(&host,&request,&r,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(cancelled(&context)?3:status,error[0]?error:"Geodesic failed.");result.times.resize(r.samples);result.coordinates.resize(r.samples*2);result.velocities.resize(r.samples*2);result.positions.resize(r.samples*request.dimension);result.transport.resize(r.samples*2);result.speedSquared.resize(r.samples);result.transportNormSquared.resize(r.samples);return result;
}
}
